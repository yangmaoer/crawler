/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: flt_comm.c
 ** 版本号: 1.0
 ** 描  述: 网页过滤
 **         负责过滤HTML网页
 ** 作  者: # Qifeng.zou # 2014.09.04 #
 ******************************************************************************/
#include "log.h"
#include "str.h"
#include "lock.h"
#include "hash.h"
#include "common.h"
#include "syscall.h"
#include "filter.h"
#include "flt_man.h"
#include "flt_sched.h"
#include "flt_worker.h"

#define FLT_PROC_LOCK_PATH "../temp/filter/filter.lck"

static int flt_worker_pool_creat(flt_cntx_t *ctx);

/******************************************************************************
 **函数名称: flt_getopt 
 **功    能: 解析输入参数
 **输入参数: 
 **     argc: 参数个数
 **     argv: 参数列表
 **输出参数:
 **     opt: 参数选项
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 解析输入参数
 **     2. 验证输入参数
 **注意事项: 
 **     c: 配置文件路径
 **     h: 帮助手册
 **作    者: # Qifeng.zou # 2015.03.11 #
 ******************************************************************************/
int flt_getopt(int argc, char **argv, flt_opt_t *opt)
{
    int ch;

    /* 1. 解析输入参数 */
    while (-1 != (ch = getopt(argc, argv, "c:hd")))
    {
        switch (ch)
        {
            case 'c':   /* 指定配置文件 */
            {
                snprintf(opt->conf_path, sizeof(opt->conf_path), "%s", optarg);
                break;
            }
            case 'd':
            {
                opt->isdaemon = true;
                break;
            }
            case 'h':   /* 显示帮助信息 */
            default:
            {
                return FLT_SHOW_HELP;
            }
        }
    }

    optarg = NULL;
    optind = 1;

    /* 2. 验证输入参数 */
    if (!strlen(opt->conf_path))
    {
        snprintf(opt->conf_path, sizeof(opt->conf_path), "%s", FLT_DEF_CONF_PATH);
    }

    return FLT_OK;
}

/******************************************************************************
 **函数名称: flt_usage
 **功    能: 显示启动参数帮助信息
 **输入参数:
 **     name: 程序名
 **输出参数: NULL
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.03.11 #
 ******************************************************************************/
int flt_usage(const char *exec)
{
    printf("\nUsage: %s [-h] [-d] -c <config file> [-l log_level]\n", exec);
    printf("\t-h\tShow help\n"
           "\t-c\tConfiguration path\n\n");
    return FLT_OK;
}

/******************************************************************************
 **函数名称: flt_init_log
 **功    能: 初始化日志模块
 **输入参数:
 **     fname: 日志文件名
 **输出参数: NONE
 **返    回: 日志对象
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.21 #
 ******************************************************************************/
static log_cycle_t *flt_init_log(char *fname)
{
    log_cycle_t *log;
    char path[FILE_NAME_MAX_LEN];

    /* 1. 初始化系统日志 */
    syslog_get_path(path, sizeof(path), basename(fname));

    if (syslog_init(LOG_LEVEL_ERROR, path))
    {
        fprintf(stderr, "Init syslog failed!");
        return NULL;
    }

    /* 2. 初始化业务日志 */
    log_get_path(path, sizeof(path), basename(fname));

    log = log_init(LOG_LEVEL_ERROR, path);
    if (NULL == log)
    {
        sys_error("Initialize log failed!");
        syslog_destroy();
        return NULL;
    }

    return log;
}

/******************************************************************************
 **函数名称: flt_init
 **功    能: 初始化模块
 **输入参数: 
 **     pname: 进程名
 **     path: 配置路径
 **输出参数:
 **返    回: 全局对象
 **实现描述: 
 **     创建各对象(表,队列, 内存池, 线程池等)
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.03.11 #
 ******************************************************************************/
flt_cntx_t *flt_init(char *pname, const char *path)
{
    void *addr;
    flt_cntx_t *ctx;
    flt_conf_t *conf;
    log_cycle_t *log;

    /* > 初始化日志模块 */
    log = flt_init_log(pname);
    if (NULL == log)
    {
        fprintf(stderr, "Initialize log failed!\n");
        return NULL;
    }

    /* > 申请对象空间 */
    ctx = (flt_cntx_t *)calloc(1, sizeof(flt_cntx_t));
    if (NULL == ctx)
    {
        log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    ctx->log = log;

    do
    {
        /* > 创建内存池 */
        addr = (void *)calloc(1, FLT_SLAB_SIZE);
        if (NULL == addr)
        {
            log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
            free(ctx);
            return NULL;
        }

        ctx->slab = slab_init(addr, FLT_SLAB_SIZE);
        if (NULL == ctx->slab)
        {
            log_error(log, "Init slab failed!");
            break;
        }

        /* > 加载配置信息 */
        conf = flt_conf_load(path, log);
        if (NULL == conf)
        {
            log_error(log, "Initialize log failed!");
            break;
        }

        ctx->conf = conf;
        log_set_level(log, conf->log.level);
        syslog_set_level(conf->log.syslevel);

        /* > 连接Redis集群 */
        ctx->redis = redis_clst_init(conf->redis.conf, conf->redis.num);
        if (NULL == ctx->redis)
        {
            log_error(ctx->log, "Initialize redis context failed!");
            break;
        }

        /* > 创建工作队列 */
        ctx->taskq = queue_creat(10000, sizeof(flt_task_t));
        if (NULL == ctx->taskq)
        {
            log_error(ctx->log, "Create queue failed!");
            break;
        }

        /* > 创建工作队列 */
        ctx->crwlq = queue_creat(10000, sizeof(flt_crwl_t));
        if (NULL == ctx->crwlq)
        {
            log_error(ctx->log, "Create queue failed!");
            break;
        }

        /* > 新建域名IP映射表 */
        ctx->domain_ip_map = hash_tab_creat(
                ctx->slab,
                FLT_DOMAIN_IP_MAP_HASH_MOD,
                hash_time33_ex,
                (avl_cmp_cb_t)flt_domain_ip_map_cmp_cb);
        if (NULL == ctx->domain_ip_map)
        {
            log_error(log, "Initialize hash table failed!");
            break;
        }

        /* > 新建域名黑名单表 */
        ctx->domain_blacklist = hash_tab_creat(
                ctx->slab,
                FLT_DOMAIN_BLACKLIST_HASH_MOD,
                hash_time33_ex,
                (avl_cmp_cb_t)flt_domain_blacklist_cmp_cb);
        if (NULL == ctx->domain_blacklist)
        {
            log_error(log, "Initialize hash table failed!");
            break;
        }

        /* > 创建工作线程池 */
        if (flt_worker_pool_creat(ctx))
        {
            log_error(log, "Initialize thread pool failed!");
            break;
        }

        return ctx;
    } while(0);

    /* > 释放内存空间 */
    if (addr) { free(addr); }
    if (ctx->redis) { redis_clst_destroy(ctx->redis); }
    if (ctx->taskq) { queue_destroy(ctx->taskq); }    
    free(ctx);
    return NULL;
}

/******************************************************************************
 **函数名称: flt_destroy
 **功    能: 销毁Filter对象
 **输入参数: 
 **     filter: Filter对象
 **输出参数:
 **返    回: VOID
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.04 #
 ******************************************************************************/
void flt_destroy(flt_cntx_t *ctx)
{
    if (ctx->log)
    {
        log_destroy(&ctx->log);
        ctx->log = NULL;
    }

    syslog_destroy();

    if (ctx->redis)
    {
        redis_clst_destroy(ctx->redis);
        ctx->redis = NULL;
    }

    if (ctx->conf)
    {
        flt_conf_destroy(ctx->conf);
        ctx->conf = NULL;
    }

    free(ctx);
}

/******************************************************************************
 **函数名称: flt_startup
 **功    能: 启动过滤服务
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     设置线程回调
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.09.04 #
 ******************************************************************************/
int flt_startup(flt_cntx_t *ctx)
{
    int idx;
    const flt_conf_t *conf = ctx->conf;

    /* > 获取运行时间 */
    ctx->run_tm = time(NULL);

    /* > 启动Push线程 */
    if (thread_creat(&ctx->sched_tid, flt_push_routine, ctx))
    {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FLT_ERR;
    }

    /* > 设置Worker线程回调 */
    for (idx=0; idx<conf->worker.num; ++idx)
    {
        thread_pool_add_worker(ctx->worker_pool, flt_worker_routine, ctx);
    }
    
    /* > 启动Sched线程 */
    if (thread_creat(&ctx->sched_tid, flt_sched_routine, ctx))
    {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FLT_ERR;
    }

    /* > 启动Manager线程 */
    if (thread_creat(&ctx->sched_tid, flt_manager_routine, ctx))
    {
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FLT_ERR;
    }

    /* > 推送SEED至CRWL队列 */
    if (flt_push_seed_to_crwlq(ctx))
    {
        log_error(ctx->log, "Push seed to redis taskq failed!");
        return FLT_ERR;
    }

    return FLT_OK;
}

/******************************************************************************
 **函数名称: flt_worker_pool_creat
 **功    能: 初始化工作线程池
 **输入参数: 
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.11 #
 ******************************************************************************/
static int flt_worker_pool_creat(flt_cntx_t *ctx)
{
    int idx, num;
    thread_pool_option_t option;
    flt_worker_conf_t *conf = &ctx->conf->worker;

    memset(&option, 0, sizeof(option));

    /* 1. 创建Worker线程池 */
    option.pool = (void *)ctx->slab;
    option.alloc = (mem_alloc_cb_t)slab_alloc;
    option.dealloc = (mem_dealloc_cb_t)slab_dealloc;

    ctx->worker_pool = thread_pool_init(conf->num, &option, NULL);
    if (NULL == ctx->worker_pool)
    {
        log_error(ctx->log, "Initialize thread pool failed!");
        return FLT_ERR;
    }

    /* 2. 新建Worker对象 */
    ctx->worker = (flt_worker_t *)calloc(conf->num, sizeof(flt_worker_t));
    if (NULL == ctx->worker)
    {
        thread_pool_destroy(ctx->worker_pool);
        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FLT_ERR;
    }

    /* 3. 依次初始化Worker对象 */
    num = 0;
    for (idx=0; idx<conf->num; ++idx, ++num)
    {
        if (flt_worker_init(ctx, ctx->worker+idx, idx))
        {
            log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
            goto FLT_PROC_ERR;
        }
    }

    return FLT_OK; /* 成功 */

FLT_PROC_ERR:
    /* 4. 释放Worker对象 */
    for (idx=0; idx<num; ++idx)
    {
        flt_worker_destroy(ctx, ctx->worker+idx);
    }

    free(ctx->worker);
    thread_pool_destroy(ctx->worker_pool);

    return FLT_ERR;
}

/******************************************************************************
 **函数名称: flt_proc_lock
 **功    能: 爬虫进程锁(防止同时启动两个服务进程)
 **输入参数: NONE
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.21 #
 ******************************************************************************/
int flt_proc_lock(void)
{
    int fd;
    char path[FILE_PATH_MAX_LEN];

    /* 1. 获取路径 */
    snprintf(path, sizeof(path), "%s", FLT_PROC_LOCK_PATH);

    Mkdir2(path, DIR_MODE);

    /* 2. 打开文件 */
    fd = Open(path, OPEN_FLAGS, OPEN_MODE);
    if(fd < 0)
    {
        return FLT_ERR;
    }

    /* 3. 尝试加锁 */
    if(proc_try_wrlock(fd) < 0)
    {
        Close(fd);
        return FLT_ERR;
    }

    return FLT_OK;
}

/******************************************************************************
 **函数名称: flt_get_domain_ip_map
 **功    能: 获取域名IP映射
 **输入参数:
 **     ctx: 全局信息
 **     host: 域名
 **输出参数:
 **     map: 域名IP映射
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 从域名IP映射表中查询
 **     2. 通过DNS服务器查询
 **注意事项: 
 **     如果域名不存在, getaddrinfo()将阻塞30s左右的时间!
 **作    者: # Qifeng.zou # 2014.10.21 #
 ******************************************************************************/
int flt_get_domain_ip_map(flt_cntx_t *ctx, char *host, flt_domain_ip_map_t *map)
{
    int ret;
    flt_domain_ip_map_t *new;
    flt_domain_blacklist_t blacklist;
    flt_domain_blacklist_t *new_blacklist;
    struct sockaddr_in *sockaddr;
    struct addrinfo *addrinfo, *curr;
    struct addrinfo hints;

    /* 1. 从域名IP映射表中查找 */
    if (!hash_tab_query(ctx->domain_ip_map,
            host, strlen(host), map, sizeof(flt_domain_ip_map_t)))
    {
        log_trace(ctx->log, "Found domain ip map in talbe! %s", host);
        return FLT_OK; /* 成功 */
    }

    /* 2. 从域名黑名单中查找 */
    if (!hash_tab_query(ctx->domain_blacklist,
            host, strlen(host), &blacklist, sizeof(blacklist)))
    {
        log_info(ctx->log, "Host [%s] in blacklist!", host);
        return FLT_ERR; /* 在黑名单中 */
    }

    /* 2. 通过DNS服务器查询 */
    memset(&hints, 0, sizeof(hints));

    hints.ai_socktype = SOCK_STREAM;

    if (0 != getaddrinfo(host, NULL, &hints, &addrinfo))
    {
        log_error(ctx->log, "Get address info failed! host:%s", host);

        /* 插入域名黑名单中 */
        new_blacklist = slab_alloc(ctx->slab, sizeof(flt_domain_blacklist_t));
        if (NULL == new_blacklist)
        {
            return FLT_ERR;
        }

        snprintf(new_blacklist->host, sizeof(new_blacklist->host), "%s", host);
        new_blacklist->access_tm = time(NULL);

        if (hash_tab_insert(ctx->domain_blacklist, host, strlen(host), new_blacklist))
        {
            slab_dealloc(ctx->slab, new_blacklist);
        }

        return FLT_ERR;
    }

    /* 3. 申请新的内存空间(此处不释放空间) */
    new = (flt_domain_ip_map_t *)slab_alloc(ctx->slab, sizeof(flt_domain_ip_map_t));
    if (NULL == new)
    {
        freeaddrinfo(addrinfo);

        log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return FLT_ERR;
    }

    snprintf(new->host, sizeof(new->host), "%s", host);
    new->ip_num = 0;

    curr = addrinfo;
    while ((NULL != curr) && (new->ip_num < FLT_IP_MAX_NUM))
    {
        sockaddr = (struct sockaddr_in *)curr->ai_addr;
        if (0 == sockaddr->sin_addr.s_addr)
        {
            curr = curr->ai_next;
            continue;
        }

        new->ip[new->ip_num].family = curr->ai_family;
        inet_ntop(curr->ai_family,
                &sockaddr->sin_addr.s_addr,
                new->ip[new->ip_num].ip,
                sizeof(new->ip[new->ip_num].ip));
        ++new->ip_num;

        curr = curr->ai_next;
    }

    freeaddrinfo(addrinfo);

    /* 4. 插入域名IP映射表 */
    ret = hash_tab_insert(ctx->domain_ip_map, host, strlen(host), new);
    if (0 != ret)
    {
        if (AVL_NODE_EXIST == ret)
        {
            memcpy(map, new, sizeof(flt_domain_ip_map_t));
            slab_dealloc(ctx->slab, new);
            log_debug(ctx->log, "Domain is exist! host:[%s]", host);
            return FLT_OK;
        }

        slab_dealloc(ctx->slab, new);
        log_error(ctx->log, "Insert into hash table failed! ret:[%x/%x] host:[%s]",
                ret, AVL_NODE_EXIST, host);
        return FLT_ERR;
    }

    memcpy(map, new, sizeof(flt_domain_ip_map_t));

    return FLT_OK;
}

/******************************************************************************
 **函数名称: flt_domain_ip_map_cmp_cb
 **功    能: 域名IP映射表的比较
 **输入参数:
 **     _domain: 域名
 **     data: 域名IP映射表数据(flt_domain_ip_map_t)
 **输出参数: NONE
 **返    回: 0:相等 <0:小于 >0:大于
 **实现描述: 
 **注意事项: 
 **     查找成功后，将会更新访问时间. 该时间将会是表数据更新的参考依据
 **作    者: # Qifeng.zou # 2014.11.14 #
 ******************************************************************************/
int flt_domain_ip_map_cmp_cb(const char *domain, const flt_domain_ip_map_t *map)
{
    return strcmp(domain, map->host);
}

/******************************************************************************
 **函数名称: flt_domain_blacklist_cmp_cb
 **功    能: 域名黑名单的比较
 **输入参数:
 **     domain: 域名
 **     blacklist: 域名黑名单数据
 **输出参数: NONE
 **返    回: 0:相等 <0:小于 >0:大于
 **实现描述: 
 **注意事项: 
 **     查找成功后，将会更新访问时间. 该时间将会是表数据更新的参考依据
 **作    者: # Qifeng.zou # 2014.11.28 #
 ******************************************************************************/
int flt_domain_blacklist_cmp_cb(const char *domain, const flt_domain_blacklist_t *blacklist)
{
    return strcmp(domain, blacklist->host);
}

/******************************************************************************
 **函数名称: flt_set_uri_exists
 **功    能: 设置uri是否已存在
 **输入参数: 
 **     ctx: Redis集群
 **     hash: 哈希表名
 **     uri: 判断对象-URI
 **输出参数:
 **返    回: true:已下载 false:未下载
 **实现描述: 
 **     1) 当URI已存在时, 返回true;
 **     2) 当URI不存在时, 返回false, 并设置uri的值为1.
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.04 #
 ******************************************************************************/
bool flt_set_uri_exists(redis_clst_t *ctx, const char *hash, const char *uri)
{
    redisReply *r;

    if (0 == ctx->num)
    {
        return !redis_hsetnx(ctx->redis[REDIS_MASTER_IDX], hash, uri, "1");
    }

    do
    {
        r = redisCommand(ctx->redis[random() % ctx->num], "HEXISTS %s %s", hash, uri);
        if (REDIS_REPLY_INTEGER != r->type)
        {
            break;
        }

        if (0 == r->integer)
        {
            break;
        }

        freeReplyObject(r);
        return true; /* 已存在 */
    } while(0);

    freeReplyObject(r);

    return !redis_hsetnx(ctx->redis[REDIS_MASTER_IDX], hash, uri, "1");
}

/******************************************************************************
 **函数名称: flt_push_url_to_crwlq
 **功    能: 将URL推送到CRWL队列
 **输入参数: 
 **     ctx: 全局对象
 **     url: 被推送的URL
 **     depth: URL的深度
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.03.13 #
 ******************************************************************************/
int flt_push_url_to_crwlq(flt_cntx_t *ctx,
        const char *url, char *host, int port, int depth)
{
    int ret;
    flt_crwl_t *crwl;
    unsigned int len, idx;
    flt_domain_ip_map_t map;

    if ('\0' == host[0])
    {
        log_error(ctx->log, "Host is invalid! url:%s", url);
        return FLT_ERR;
    }

    /* > 查询域名IP映射 */
    ret = flt_get_domain_ip_map(ctx, host, &map);
    if (0 != ret || 0 == map.ip_num)
    {
        log_error(ctx->log, "Get ip failed! uri:%s host:%s", url, host);
        return FLT_ERR;
    }

    idx = rand() % map.ip_num;

    while (1)
    {
        /* > 申请队列空间 */
        crwl = queue_malloc(ctx->crwlq);
        if (NULL == crwl)
        {
            Sleep(1);
            continue;
        }

        /* > 组装任务格式 */
        len = flt_get_task_str(crwl->task_str, sizeof(crwl->task_str),
                url, depth, map.ip[idx].ip, map.ip[idx].family);
        if (len >= sizeof(crwl->task_str))
        {
            log_info(ctx->log, "Task string is too long! uri:[%s]", url);
            queue_dealloc(ctx->crwlq, crwl);
            return FLT_ERR;
        }

        /* > 推至CRWL队列 */
        if (queue_push(ctx->crwlq, crwl))
        {
            log_info(ctx->log, "Push into queue failed! uri:[%s]", url);
            queue_dealloc(ctx->crwlq, crwl);
            return FLT_ERR;
        }
        break;
    }

    return FLT_OK;
}

/******************************************************************************
 **函数名称: flt_push_seed_to_crwlq
 **功    能: 将Seed放入CRWL队列
 **输入参数: 
 **     ctx: 全局对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.03.13 #
 ******************************************************************************/
int flt_push_seed_to_crwlq(flt_cntx_t *ctx)
{
    unsigned int idx;
    uri_field_t field;
    flt_seed_conf_t *seed;
    flt_conf_t *conf = ctx->conf;

    for (idx=0; idx<conf->seed_num; ++idx)
    {
        seed = &conf->seed[idx];
        if (seed->depth > conf->download.depth) /* 判断网页深度 */
        {
            continue;
        }

        /* > 解析URI字串 */
        if(0 != uri_reslove(seed->uri, &field))
        {
            log_error(ctx->log, "Reslove url [%s] failed!", seed->uri);
            return FLT_ERR;
        }

        /* > 推送至CRWL队列 */
        if (flt_push_url_to_crwlq(ctx, seed->uri, field.host, field.port, seed->depth))
        {
            log_info(ctx->log, "Uri [%s] is invalid!", (char *)seed->uri);
            continue;
        }
    }

    return FLT_OK;
}

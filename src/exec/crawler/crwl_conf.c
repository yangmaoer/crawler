/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: crwl_conf.c
 ** 版本号: 1.0
 ** 描  述: 网络爬虫
 **         负责爬虫配置信息的解析加载
 ** 作  者: # Qifeng.zou # 2014.10.28 #
 ******************************************************************************/

#include "syscall.h"
#include "crawler.h"
#include "crwl_conf.h"

static int crwl_conf_load_comm(xml_tree_t *xml, crwl_conf_t *conf, log_cycle_t *log);
static int crwl_conf_load_redis(xml_tree_t *xml, crwl_conf_t *conf, log_cycle_t *log);
static int crwl_conf_load_seed(xml_tree_t *xml, crwl_conf_t *conf, log_cycle_t *log);
static int crwl_conf_load_worker(xml_tree_t *xml, crwl_worker_conf_t *conf, log_cycle_t *log);
static int crwl_conf_load_parser(xml_tree_t *xml, crwl_parser_conf_t *conf, log_cycle_t *log);

/******************************************************************************
 **函数名称: crwl_conf_creat
 **功    能: 加载配置信息
 **输入参数:
 **     path: 配置路径
 **     log: 日志对象
 **输出参数: NONE
 **返    回: 配置对象
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.12 #
 ******************************************************************************/
crwl_conf_t *crwl_conf_creat(const char *path, log_cycle_t *log)
{
    int ret;
    xml_tree_t *xml;
    crwl_conf_t *conf;
    mem_pool_t *mem_pool;

    /* 1. 创建配置内存池 */
    mem_pool = mem_pool_creat(4 * KB);
    if (NULL == mem_pool)
    {
        log_error(log, "Create memory pool failed!");
        return NULL;
    }

    do
    {
        /* 2. 创建配置对象 */
        conf = mem_pool_alloc(mem_pool, sizeof(crwl_conf_t));
        if (NULL == conf)
        {
            log_error(log, "Alloc memory from pool failed!");
            break;
        }

        conf->mem_pool = mem_pool;

        /* 2. 构建XML树 */
        xml = xml_creat(path);
        if (NULL == xml)
        {
            log_error(log, "Create xml failed! path:%s", path);
            break;
        }

        /* 3. 加载通用配置 */
        ret = crwl_conf_load_comm(xml, conf, log);
        if (CRWL_OK != ret)
        {
            log_error(log, "Load common conf failed! path:%s", path);
            break;
        }

        /* 4. 提取爬虫配置 */
        ret = crwl_conf_load_worker(xml, &conf->worker, log);
        if (0 != ret)
        {
            log_error(log, "Parse worker configuration failed! path:%s", path);
            break;
        }

        /* 5. 提取解析配置 */
        ret = crwl_conf_load_parser(xml, &conf->parser, log);
        if (0 != ret)
        {
            log_error(log, "Parse worker configuration failed! path:%s", path);
            break;
        }

        /* 6. 加载种子配置 */
        ret = crwl_conf_load_seed(xml, conf, log);
        if (CRWL_OK != ret)
        {
            log_error(log, "Load seed conf failed! path:%s", path);
            break;
        }

        /* 6. 释放XML树 */
        xml_destroy(xml);
        return conf;
    } while(0);

    /* 异常处理 */
    if (NULL != xml)
    {
        xml_destroy(xml);
    }
    mem_pool_destroy(mem_pool);
    return NULL;
}

/******************************************************************************
 **函数名称: crwl_conf_load_comm
 **功    能: 加载通用配置
 **输入参数: 
 **     xml: XML配置
 **     log: 日志对象
 **输出参数:
 **     conf: 配置信息
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     通过XML查询接口查找对应的配置信息
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.16 #
 ******************************************************************************/
static int crwl_conf_load_comm(xml_tree_t *xml, crwl_conf_t *conf, log_cycle_t *log)
{
    int ret;
    xml_node_t *node, *fix;

    /* 1. 定位LOG标签
     *  获取日志级别信息
     * */
    fix = xml_search(xml, ".CRAWLER.COMMON.LOG");
    if (NULL != fix)
    {
        /* 1.1 日志级别 */
        node = xml_rsearch(xml, fix, "LEVEL");
        if (NULL != node)
        {
            conf->log.level = log_get_level(node->value);
        }

        /* 1.2 系统日志级别 */
        node = xml_rsearch(xml, fix, "LEVEL2");
        if (NULL != node)
        {
            conf->log.level2 = log_get_level(node->value);
        }
    }
    else
    {
        log_warn(log, "Didn't configure log!");
    }

    /* 2. 定位Download标签
     *  获取网页抓取深度和存储路径
     * */
    fix = xml_search(xml, ".CRAWLER.COMMON.DOWNLOAD");
    if (NULL == fix)
    {
        log_error(log, "Didn't configure download!");
        return CRWL_ERR;
    }

    /* 2.1 获取抓取深度 */
    node = xml_rsearch(xml, fix, "DEPTH");
    if (NULL == node)
    {
        log_error(log, "Get download depth failed!");
        return CRWL_ERR;
    }

    conf->download.depth = atoi(node->value);

    /* 2.2 获取存储路径 */
    node = xml_rsearch(xml, fix, "PATH");
    if (NULL == node)
    {
        log_error(log, "Get download path failed!");
        return CRWL_ERR;
    }

    snprintf(conf->download.path, sizeof(conf->download.path), "%s", node->value);

    /* 3. 获取Redis配置信息 */
    ret = crwl_conf_load_redis(xml, conf, log);
    if (CRWL_OK != ret)
    {
        log_error(log, "Get redis configuration failed!");
        return CRWL_ERR;
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_conf_load_redis
 **功    能: 加载Redis配置
 **输入参数: 
 **     xml: XML配置
 **     log: 日志对象
 **输出参数:
 **     conf: Redis配置信息
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     通过XML查询接口查找对应的配置信息
 **注意事项: 
 **     1. 因为链表中的结点的空间是从mem_pool_t内存池中分配的, 因此, 如果处理过
 **     程中出现失败的情况时, 申请的内存空间不必进行释放的操作!
 **作    者: # Qifeng.zou # 2014.10.29 #
 ******************************************************************************/
static int crwl_conf_load_redis(xml_tree_t *xml, crwl_conf_t *conf, log_cycle_t *log)
{
    list_node_t *l_node;
    xml_node_t *fix, *node, *item;
    crwl_redis_conf_t *redis = &conf->redis;
    redis_conf_t *slave;

    /* 1. 定位REDIS标签
     *  获取Redis的IP地址、端口号、队列、副本等信息
     * */
    fix = xml_search(xml, ".CRAWLER.COMMON.REDIS");
    if (NULL == fix)
    {
        log_error(log, "Didn't configure redis!");
        return CRWL_ERR;
    }

    /* 获取IP地址 */
    node = xml_rsearch(xml, fix, "IP");
    if (NULL == node)
    {
        log_error(log, "Get redis ip address failed!");
        return CRWL_ERR;
    }

    snprintf(redis->master.ip, sizeof(redis->master.ip), "%s", node->value);

    /* 获取端口号 */
    node = xml_rsearch(xml, fix, "PORT");
    if (NULL == node)
    {
        log_error(log, "Get redis port failed!");
        return CRWL_ERR;
    }

    redis->master.port = atoi(node->value);

    /* 获取队列名 */
    node = xml_rsearch(xml, fix, "QUEUE.UNDO_TASKQ");
    if (NULL == node)
    {
        log_error(log, "Get undo task queue failed!");
        return CRWL_ERR;
    }

    snprintf(redis->undo_taskq, sizeof(redis->undo_taskq), "%s", node->value);

    /* 获取哈希表名 */
    node = xml_rsearch(xml, fix, "HASH.DONE_TAB");  /* DONE哈希表 */
    if (NULL == node)
    {
        log_error(log, "Get done hash table failed!");
        return CRWL_ERR;
    }

    snprintf(redis->done_tab, sizeof(redis->done_tab), "%s", node->value);

    node = xml_rsearch(xml, fix, "HASH.PUSH_TAB");  /* PUSH哈希表 */
    if (NULL == node)
    {
        log_error(log, "Get pushed hash table failed!");
        return CRWL_ERR;
    }

    snprintf(redis->push_tab, sizeof(redis->push_tab), "%s", node->value);

    /* 获取Redis副本配置 */
    redis->slave_list.num = 0;
    redis->slave_list.head = NULL;
    redis->slave_list.tail = NULL;

    item = xml_rsearch(xml, fix, "SLAVE.ITEM");
    while (NULL != item)
    {
        if (0 != strcmp(item->name, "ITEM"))
        {
            item = item->next;
            continue;
        }

        /* 新建链表结点(注: 出现异常情况时 内存在此不必释放)  */
        l_node = (list_node_t *)mem_pool_alloc(conf->mem_pool, sizeof(list_node_t));
        if (NULL == l_node)
        {
            log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
            return CRWL_ERR;
        }

        slave = (redis_conf_t *)mem_pool_alloc(conf->mem_pool, sizeof(redis_conf_t));
        if (NULL == slave)
        {
            log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
            return CRWL_ERR;
        }

        l_node->data = slave;

        /* 获取IP地址 */
        node = xml_rsearch(xml, item, "IP");
        if (NULL == node)
        {
            item = item->next;
            continue;
        }

        snprintf(slave->ip, sizeof(slave->ip), "%s", node->value);

        /* 获取PORT地址 */
        node = xml_rsearch(xml, item, "PORT");
        if (NULL == node)
        {
            item = item->next;
            continue;
        }

        slave->port = atoi(node->value);

        /* 加入Slave链表尾 */
        list_insert_tail(&redis->slave_list, l_node);

        /* 下一结点 */
        item = item->next;
    }

    return CRWL_OK;
}


/******************************************************************************
 **函数名称: crwl_conf_load_seed
 **功    能: 加载种子配置
 **输入参数: 
 **     xml: XML配置
 **     log: 日志对象
 **输出参数:
 **     conf: 配置信息
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     通过XML查询接口查找对应的配置信息
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.28 #
 ******************************************************************************/
static int crwl_conf_load_seed(xml_tree_t *xml, crwl_conf_t *conf, log_cycle_t *log)
{
    list_node_t *node;
    crwl_seed_conf_t *seed;
    xml_node_t *cf_node, *cf_item;

    /* 1. 定位SEED->ITEM标签 */
    cf_item = xml_search(xml, ".CRAWLER.SEED.ITEM");
    if (NULL == cf_item)
    {
        log_error(log, "Didn't configure seed item!");
        return CRWL_ERR;
    }

    /* 2. 提取种子信息 */
    conf->seed.num = 0;
    conf->seed.head = NULL;
    conf->seed.tail = NULL;

    while (NULL != cf_item)
    {
        if (0 != strcasecmp(cf_item->name, "ITEM"))
        {
            cf_item = cf_item->next;
            continue;
        }

        /* 申请配置空间(注: 出现异常情况时 内存在此不必释放) */
        node = (list_node_t *)mem_pool_alloc(conf->mem_pool, sizeof(list_node_t));
        if (NULL == node)
        {
            log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
            return CRWL_ERR;
        }

        seed = (crwl_seed_conf_t *)mem_pool_alloc(conf->mem_pool, sizeof(crwl_seed_conf_t));
        if (NULL == seed)
        {
            log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
            return CRWL_ERR;
        }

        node->data = seed;

        /* 提取URI */
        cf_node = xml_rsearch(xml, cf_item, "URI");
        if (NULL == cf_node)
        {
            log_error(log, "Get uri failed!");
            return CRWL_ERR;
        }

        snprintf(seed->uri, sizeof(seed->uri), "%s", cf_node->value);

        /* 获取DEPTH */
        cf_node = xml_rsearch(xml, cf_item, "DEPTH");
        if (NULL == cf_node)
        {
            seed->depth = 0;
            log_info(log, "Didn't set depth of uri!");
        }
        else
        {
            seed->depth = atoi(cf_node->value);
        }

        /* 加入配置链表 */
        list_insert_tail(&conf->seed, node);

        cf_item = cf_item->next;
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_conf_load_worker
 **功    能: 提取配置信息
 **输入参数: 
 **     xml: 配置文件
 **输出参数:
 **     conf: 配置信息
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.09.05 #
 ******************************************************************************/
static int crwl_conf_load_worker(
        xml_tree_t *xml, crwl_worker_conf_t *conf, log_cycle_t *log)
{
    xml_node_t *curr, *node;

    /* 1. 定位工作进程配置 */
    curr = xml_search(xml, ".CRAWLER.WORKER");
    if (NULL == curr)
    {
        log_error(log, "Didn't configure worker process!");
        return CRWL_ERR;
    }

    /* 2. 爬虫线程数(相对查找) */
    node = xml_rsearch(xml, curr, "NUM");
    if (NULL == node)
    {
        conf->num = CRWL_THD_DEF_NUM;
        log_warn(log, "Set thread number: %d!", conf->num);
    }
    else
    {
        conf->num = atoi(node->value);
    }

    if (conf->num <= 0)
    {
        conf->num = CRWL_THD_MIN_NUM;
        log_warn(log, "Set thread number: %d!", conf->num);
    }

    /* 3. 并发网页连接数(相对查找) */
    node = xml_rsearch(xml, curr, "CONNECTIONS.MAX");
    if (NULL == node)
    {
        log_error(log, "Didn't configure download webpage number!");
        return CRWL_ERR;
    }

    conf->conn_max_num = atoi(node->value);
    if (conf->conn_max_num <= 0)
    {
        conf->conn_max_num = CRWL_CONN_MIN_NUM;
    }
    else if (conf->conn_max_num >= CRWL_CONN_MAX_NUM)
    {
        conf->conn_max_num = CRWL_CONN_MAX_NUM;
    }

    /* 连接超时时间 */
    node = xml_rsearch(xml, curr, "CONNECTIONS.TIMEOUT");
    if (NULL == node)
    {
        conf->conn_tmout_sec = CRWL_CONN_TMOUT_SEC;

        log_error(log, "Didn't configure download webpage number!");
    }

    conf->conn_tmout_sec = atoi(node->value);
    if (conf->conn_tmout_sec <= 0)
    {
        conf->conn_tmout_sec = CRWL_CONN_TMOUT_SEC;
    }

    /* 4. Undo任务队列配置(相对查找) */
    node = xml_rsearch(xml, curr, "TASKQ.COUNT");
    if (NULL == node)
    {
        log_error(log, "Didn't configure count of undo task queue unit!");
        return CRWL_ERR;
    }

    conf->taskq_count = atoi(node->value);
    if (conf->taskq_count <= 0)
    {
        conf->taskq_count = CRWL_TASK_QUEUE_MAX_NUM;
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_conf_load_parser
 **功    能: 提取Parser配置信息
 **输入参数: 
 **     xml: 配置文件
 **输出参数:
 **     conf: 配置信息
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.04 #
 ******************************************************************************/
static int crwl_conf_load_parser(
        xml_tree_t *xml, crwl_parser_conf_t *conf, log_cycle_t *log)
{
    xml_node_t *curr, *node;

    /* 1. 定位工作进程配置 */
    curr = xml_search(xml, ".CRAWLER.PARSER");
    if (NULL == curr)
    {
        log_error(log, "Didn't configure parser process!");
        return CRWL_ERR;
    }

    /* 2. 存储路径(相对查找) */
    node = xml_rsearch(xml, curr, "STORE.PATH");
    if (NULL == node)
    {
        log_error(log, "Didn't configure store path!");
        return CRWL_ERR;
    }

    snprintf(conf->store.path, sizeof(conf->store.path), "%s", node->value);

    Mkdir(conf->store.path, DIR_MODE);

    /* 3. 错误信息存储路径(相对查找) */
    node = xml_rsearch(xml, curr, "STORE.ERR_PATH");
    if (NULL == node)
    {
        log_error(log, "Didn't configure error store path!");
        return CRWL_ERR;
    }

    snprintf(conf->store.err_path, sizeof(conf->store.err_path), "%s", node->value);

    Mkdir(conf->store.err_path, 0777);

    return CRWL_OK;
}
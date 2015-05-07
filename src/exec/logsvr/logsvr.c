/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: logd.c
 ** 版本号: 1.0
 ** 描  述: 异步日志模块 - 服务端代码
 **         1. 负责共享内存的初始化
 **         2. 负责把共享内存中的日志同步到指定文件。
 ** 作  者: # Qifeng.zou # 2013.11.07 #
 ******************************************************************************/
#include "log.h"
#include "comm.h"
#include "logsvr.h"
#include "shm_opt.h"
#include "syscall.h"

#define LOGD_PLOG_PATH   "../log/logd.plog"

static logd_cntx_t *logd_cntx_init(void);
static void *logd_timeout_routine(void *args);
int logd_sync_work(int idx, logd_cntx_t *ctx);
static int logd_proc_lock(void);

static char *logd_creat_shm(int fd);

/******************************************************************************
 **函数名称: main 
 **功    能: 日志服务主程序
 **输入参数: NONE
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 完成日志模块的初始化
 **     2. 启动命令接收和超时处理
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.10.28 #
 ******************************************************************************/
int main(void)
{
    logd_cntx_t *ctx;

    if (daemon(1, 0))
    {
        fprintf(stderr, "errmsg:[%d] %s!", errno, strerror(errno));
        return -1;
    }

    /* 2. 初始化日志服务 */
    ctx = logd_cntx_init();
    if (NULL == ctx)
    {
        plog_error("Init log failed!");
        return -1;
    }

    /* 3. 启动超时扫描线程 */
    thread_pool_add_worker(ctx->pool, logd_timeout_routine, ctx);

    while (1){ pause(); }

    return 0;
}

/******************************************************************************
 **函数名称: logd_proc_lock
 **功    能: 日志服务进程锁，防止同时启动两个服务进程
 **输入参数: 
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.11.06 #
 ******************************************************************************/
int logd_proc_lock(void)
{
    int fd;
    char path[FILE_PATH_MAX_LEN];

    logd_proc_lock_path(path, sizeof(path));

    Mkdir2(path, DIR_MODE);

    fd = Open(path, OPEN_FLAGS, OPEN_MODE);
    if (fd < 0)
    {
        plog_error("errmsg:[%d]%s! path:[%s]", errno, strerror(errno), path);
        return -1;
    }

    if (logd_proc_trylock(fd))
    {
        plog_error("errmsg:[%d]%s! path:[%s]", errno, strerror(errno), path);
        CLOSE(fd);
        return -1;
    }

    return 0;
}

/******************************************************************************
 **函数名称: logd_cntx_init
 **功    能: 初始化处理
 **输入参数: NONE
 **输出参数: NONE
 **返    回: 日志服务对象
 **实现描述: 依次初始化日志服务所依赖的资源
 **注意事项: 加文件锁防止同时启动多个日志服务进程
 **作    者: # Qifeng.zou # 2013.10.28 #
 ******************************************************************************/
static logd_cntx_t *logd_cntx_init(void)
{
    void *addr;
    logd_cntx_t *ctx;
    char path[FILE_PATH_MAX_LEN];
    thread_pool_option_t option;

    /* > 初始化日志 */
    if (plog_init(LOG_LEVEL_DEBUG, LOGD_PLOG_PATH))
    {
        fprintf(stderr, "errmsg:[%d] %s!\n", errno, strerror(errno));
        return NULL;
    }

    /* > 服务进程锁 */
    if (logd_proc_lock())
    {
        plog_fatal("Log server is already running...");
        return NULL;    /* 日志服务进程正在运行... */
    }

    ctx = (logd_cntx_t *)calloc(1, sizeof(logd_cntx_t));
    if (NULL == ctx)
    {
        plog_fatal("errmsg:[%d] %s!\n", errno, strerror(errno));
        return NULL;
    }

    /* > 文件缓存锁 */
    log_get_lock_path(path, sizeof(path));

    Mkdir2(path, DIR_MODE);

    ctx->fd = Open(path, OPEN_FLAGS, OPEN_MODE);
    if (ctx->fd < 0)
    {
        plog_error("errmsg:[%d] %s! path:[%s]", errno, strerror(errno), path);
        return NULL;
    }

    /* > 创建/连接共享内存 */
    ctx->addr = logd_creat_shm(ctx->fd);
    if (NULL == ctx->addr)
    {
        plog_error("Create SHM failed!");
        return NULL;
    }

    /* > 创建内存池 */
    addr = (void *)calloc(1, LOGD_SLAB_SIZE);
    if (NULL == addr)
    {
        plog_error("errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    ctx->slab = slab_init(addr, LOGD_SLAB_SIZE);
    if (NULL == ctx->slab)
    {
        plog_error("Inititalize slab failed!");
        return NULL;
    }

    /* > 启动多个线程 */
    memset(&option, 0, sizeof(option));

    option.pool = (void *)ctx->slab;
    option.alloc = (mem_alloc_cb_t)slab_alloc;
    option.dealloc = (mem_dealloc_cb_t)slab_dealloc;

    ctx->pool = thread_pool_init(LOGD_THREAD_NUM, &option, NULL);
    if (NULL == ctx->pool)
    {
        thread_pool_destroy(ctx->pool);
        ctx->pool = NULL;
        plog_error("errmsg:[%d]%s!", errno, strerror(errno));
        return NULL;
    }

    return ctx;
}

/******************************************************************************
 **函数名称: logd_creat_shm
 **功    能: 创建或连接共享内存
 **输入参数: 
 **输出参数: 
 **     cfg: 日志配置信息
 **返    回: Address of SHM
 **实现描述: 
 **     1. 创建共享内存
 **     2. 连接共享内存
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.10.28 #
 ******************************************************************************/
static char *logd_creat_shm(int fd)
{
    key_t key;
    int idx, shmid;
    void *addr = NULL, *p = NULL;
    log_file_info_t *file = NULL;

    key = shm_ftok(LOG_KEY_PATH, 0);
    if (-1 == key)
    {
        return NULL;
    }

    /* 1. 创建共享内存 */
    /* 1.1 判断是否已经创建 */
    shmid = shmget(key, 0, 0666);
    if (shmid >= 0)
    {
        return shmat(shmid, NULL, 0);  /* 已创建 */
    }

    /* 1.2 异常，则退出处理 */
    if (ENOENT != errno)
    {
        return NULL;
    }

    /* 1.3 创建共享内存 */
    shmid = shmget(key, LOG_SHM_SIZE, IPC_CREAT|0666);
    if (shmid < 0)
    {
        return NULL;
    }

    /* 2. ATTACH共享内存 */
    addr = (void *)shmat(shmid, NULL, 0);
    if ((void *)-1 == addr)
    {
        plog_error("Attach shm failed! shmid:[%d] key:[0x%x]", shmid, key);
        return NULL;
    }

    /* 3. 初始化共享内存 */
    p = addr;
    for (idx=0; idx<LOG_FILE_MAX_NUM; idx++)
    {
        file = (log_file_info_t *)(p + idx * LOG_FILE_CACHE_SIZE);

        proc_spin_wrlock_b(fd, idx+1);
        
        file->idx = idx;
        file->pid = INVALID_PID;

        proc_unlock_b(fd, idx+1);
    }

    return addr;
}

/******************************************************************************
 **函数名称: logd_timeout_routine
 **功    能: 日志超时处理
 **输入参数: 
 **     args: 参数
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 睡眠指定时间
 **     2. 依次遍历日志缓存
 **     3. 进行超时判断
 **     4. 进行缓存同步处理
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.10.25 #
 ******************************************************************************/
static void *logd_timeout_routine(void *args)
{
    int idx;
    struct timeb ctm;
    log_file_info_t *file = NULL;
    logd_cntx_t *ctx = (logd_cntx_t *)args;


    while (1)
    {
        memset(&ctm, 0, sizeof(ctm));

        ftime(&ctm);

        for (idx=0; idx<LOG_FILE_MAX_NUM; idx++)
        {
            /* 1. 尝试加锁 */
            proc_spin_wrlock_b(ctx->fd, idx+1);

            /* 2. 路径为空，则不用同步 */
            file = (log_file_info_t *)(ctx->addr + idx*LOG_FILE_CACHE_SIZE);
            if ('\0' == file->path[0])
            {
                proc_unlock_b(ctx->fd, idx+1);
                continue;
            }

            log_sync(file);
        
            /* 判断文件是否还有运行的进程正在使用文件缓存 */
            if (!proc_is_exist(file->pid))
            {
                memset(file, 0, sizeof(log_file_info_t));

                file->pid = INVALID_PID;
            }
            file->idx = idx;
            
            proc_unlock_b(ctx->fd, idx+1);
        }
        
        Sleep(LOG_SYNC_TIMEOUT);
    }

    return (void *)-1;
}

/******************************************************************************
 **函数名称: logd_sync_work
 **功    能: 日志同步处理
 **输入参数: 
 **     idx: 缓存索引
 **     logd: 日志服务对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 缓存加锁
 **     2. 写入文件
 **     3. 缓存解锁
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.10.28 #
 ******************************************************************************/
int logd_sync_work(int idx, logd_cntx_t *ctx)
{
    log_file_info_t *file = NULL;

    file = (log_file_info_t *)(ctx->addr + idx*LOG_FILE_CACHE_SIZE);
    
    log_sync(file);
    
    return 0;
}

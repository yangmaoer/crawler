#include "hash.h"

/******************************************************************************
 **函数名称: hash_time33
 **功    能: TIME33哈希算法
 **输入参数: 
 **     str: 字符串
 **输出参数: NONE
 **返    回: 哈希值
 **实现描述: 
 **注意事项: 该算法特点: 算法简单、性能高效、散列效果佳.
 **作    者: # Qifeng.zou # 2015.01.28 #
 ******************************************************************************/
unsigned int hash_time33(const char *str)
{
    const char *p = str;
    unsigned int hash = 5381;

    while (*p)
    {
        hash += (hash << 5) + (*p++);
    }

    return (hash & 0x7FFFFFFF);
}

/******************************************************************************
 **函数名称: hash_time33_ex
 **功    能: TIME33哈希算法
 **输入参数: 
 **     addr: 内存地址
 **     len: 长度
 **输出参数: NONE
 **返    回: 哈希值
 **实现描述: 
 **注意事项: 该算法特点: 算法简单、性能高效、散列效果佳.
 **作    者: # Qifeng.zou # 2015.01.28 #
 ******************************************************************************/
unsigned int hash_time33_ex(const void *addr, size_t len)
{
    const char *p = addr;
    unsigned int hash = 5381;

    while (len-- > 0)
    {
        hash += (hash << 5) + (*p++);
    }

    return (hash & 0x7FFFFFFF);
}

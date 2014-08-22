#include "stack.h"


/******************************************************************************
 **函数名称: stack_init
 **功    能: 栈初始化
 **输入参数:
 **      stack: 栈
 **      size: 栈的大小
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
int stack_init(Stack_t *stack, int size)
{
    memset(stack, 0, sizeof(Stack_t));

    stack->base = (void**)calloc(size, sizeof(void*));
    if (NULL == stack->base)
    {
        return -1;
    }
    stack->top= stack->base;
    stack->size = size;

    return 0;
}

#if !defined(__STACK_OPTIMIZE__)
/******************************************************************************
 **函数名称: stack_destroy
 **功    能: 释放栈
 **输入参数:
 **      stack: 被释放的栈
 **输出参数:
 **返    回: 0: 成功  !0: 失败
 **实现描述: 
 **注意事项: 
 **      在此处并不释放xml_stack_node_t中name, value的内存空间。
 **      因为这些空间已交由xml树托管。释放xml树时，自然会释放这些空间。
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
void stack_destroy(Stack_t *stack)
{
    free(stack->base);
    stack->base = NULL;
    stack->top = NULL;
    stack->size = 0;
}

/******************************************************************************
 **函数名称: stack_push
 **功    能: 入栈
 **输入参数:
 **      stack: 栈
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
int stack_push(Stack_t *stack, void *node)
{
    if ((stack->top - stack->base) >= stack->size)
    {
        return -1;
    }

    *(stack->top) = node;
    stack->top++;

    return 0;
}

/******************************************************************************
 **函数名称: stack_pop
 **功    能: 出栈
 **输入参数:
 **      stack: 栈
 **输出参数:
 **返    回: 栈顶节点地址
 **实现描述: 
 **注意事项: 
 **      在此只负责将节点弹出栈，并不负责内存空间的释放
 **作    者: # Qifeng.zou # 2013.02.05 #
 ******************************************************************************/
int stack_pop(Stack_t *stack)
{
    if (stack->base == stack->top)
    {
        return -1;
    }

    stack->top--;
    *(stack->top) = NULL;
    return 0;
}
#endif /*__STACK_OPTIMIZE__*/

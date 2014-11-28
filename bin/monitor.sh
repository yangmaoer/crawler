#!/bin/sh

###############################################################################
# 功能描述: 打印指定进程的网络状态统计信息
# 参数说明:
#   参数1: 进程名
# 输出结果: 网络状态统计信息
# 注意事项: 暂时无法做到精确匹配
# 作    者: # Qifeng.zou # 2014.11.28 #
###############################################################################
print_netstat()
{
    total=0
    listen_num=0
    active_num=0
    close_num=0
    sync_num=0

    while read item
    do
        num=`echo $item | grep $1 | wc -l`
        if [ $num -eq 0 ]; then
            continue;
        fi

        # 统计套接字总数
        total=`expr $total + $num`

        # 统计活跃的套接字
        num=`echo $item | grep 'LISTEN' | wc -l`
        if [ $num -gt 0 ]; then
            listen_num=`expr $listen_num + $num`
        fi

        # 统计活跃的套接字
        num=`echo $item | grep 'ESTABLISHED' | wc -l`
        if [ $num -gt 0 ]; then
            active_num=`expr $active_num + $num`
        fi

        # 统计关闭的套接字
        num=`echo $item | grep -e 'CLOSE_WAIT' | wc -l`
        if [ $num -gt 0 ]; then
            close_num=`expr $close_num + $num`
        fi

        # 统计SYNC的套接字
        num=`echo $item | grep -e 'SYN_SENT' | wc -l`
        if [ $num -gt 0 ]; then
            sync_num=`expr $sync_num + $num`
        fi
    done < netstat.list

    # 打印统计信息
    echo "$1\t\t$listen_num\t$active_num\t$close_num\t$sync_num\t$total"
}

###############################################################################
# 功能描述: 系统监控主程序
# 参数说明: NONE
# 输出结果: 系统监控信息
# 注意事项:
# 作    者: # Qifeng.zou # 2014.11.28 #
###############################################################################
main()
{
    while true
    do
        clear
        echo "Copyright(C) 2014-2024 XunDao Technology Co.,Ltd"

        echo ""
        echo "Process stat:"
        echo "---------------------------------------------------------------------"
        echo "USER       PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND"
        ps -axu | grep -e "crawler" -e "logsvr" -e "redis" | grep -v grep | sort
        echo "---------------------------------------------------------------------"

        echo ""
        echo "Shared memory stat:"
        echo "---------------------------------------------------------------------"
        ipcs -m | grep -v "dest" | grep "qifeng"
        echo "---------------------------------------------------------------------"

        echo ""
        echo "Net stat:"
        echo "---------------------------------------------------------------------"
        IFS='\n'
        echo "PROC\t\tLISTEN\tESTAB\tCLOSE\tSYNC\tTOTAL"

        sudo netstat -antp | grep -v grep  > netstat.list

        print_netstat "crawler"
        print_netstat "redis"
        echo "---------------------------------------------------------------------"

        sleep 1
    done;
}

main

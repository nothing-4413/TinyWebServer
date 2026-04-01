#ifdef LST_TIMER
#define LST_TIMER
//头文件保护
#include<unistd.h>
//提供 Unix/Linux 系统调用声明
#include<signal>
//信号相关头文件
#include<sys/types.h>
//提供系统数据类型定义
#include<sys/epoll.h>
//epoll 相关
#include<fcntl.h>
//文件描述符控制
#include<sys/socket.h>
//socket 编程基础头文件
#include<natinet/in.h>
//IPv4 网络地址结构体等内容
#include<arpa/inet.h>
//IP 地址转换
#include<assert.h>
//assert() 用于调试时检查某个条件是否成立
#include<sys/stat.h>
//文件状态信息
#include<string.h>
//C 风格字符串处理
#include<pthread.h>
//POSIX 线程库
#include<stdio.h>
//标准输入输出
#include<stdlib.h>
//标准库
#include<sys/mmam.h>
//内存映射相关
#include<stdarg.h>
//可变参数
#include<errno.h>
//错误码变量 errno
#include<sys/uio.h>
//分散/聚集 I/O
#include<time.h>
//时间相关
#include"../log/log.h"
//日志模块头文件
class util_timer;
//前向声明
struct client_date
{
    //客户端数据结构
    sockaddr_in address;        //保存客户端的网络地址信息
    int sockfd;                 //客户端连接对应的socket 文件描述符
    util_timer *timer;          //指向这个客户端对应的定时器对象
};
class util_timer
{
    //定时器类
    public:
        util_timer():prev(NULL),next(NULL){}
        //构造函数
    public:
        time_t expire;
        //定时器的到期时间
        void (* cb_func)(client_date *);
        //回调函数函数指针
        client_date *user_date;
        //定时器关联的用户数据
        util_timer *prev;
        util_timer *next;
        //前驱和后继指针
}
class sort_timer_lst
{
    //升序定时器链表类
    public:
        sort_timer_lst();
        ~sort_timer_lst();
        //构造函数,析构函数
        void add_timer(util_timer *timer);
        //添加定时器
        void adjust_timer(util_timer *timer);
        //调整定时器
        void del_timer(util_timer *timer);
        //删除定时器
        void tick();
        //定时器检测函数
        //周期性检查链表中已经到期的定时器，并执行回调
    private:
        void add_timer(util_timer *timer,util_timer *lst_head);
        //私有重载版本的 add_timer
        util_timer *head;
        util_timer *tail;
        //双向链表的头指针和尾指针
}
class Utils
{
    //工具类，封装了一些服务器运行中常用的辅助操作
    public:
        Utils(){}
        ~Utils(){}
        void init(int timeslot);
        //初始化
        int setnonlocking(int fd);
        //设置非阻塞
        void addfd(int epollfd,int fd,bool one_shot,int TRIGMode);
        //注册文件描述符到 epoll
        static void sig_handler(int sig);
        //信号处理函数
        void addsig(int sig,void(handler)(int),bool restart = true);
        //设置信号捕获
        //用来注册一个信号处理函数
        void timer_handler();
        //定时处理任务
        void show_error(int connfd,const char *info);
        //显示错误并关闭连接
    private:
        static int *u_pipefd;
        //u_pipefd[0]：读端
        //u_pipefd[1]：写端
        sort_timer_lst m_timer_lst;
        //一个升序定时器链表对象
        static int u_epollfd;
        //保存 epoll 实例的文件描述符
        int m_TIMESLOT;
        //时间槽大小  
}
void cb_func(client_date *user_date);
//全局回调函数声明
#endif
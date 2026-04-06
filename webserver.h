#ifndef WEBSERVER_H
#define WEBSERVER_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <string>
#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"
using namespace std;
const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位
class WebServer
{
public:
    WebServer();
    ~WebServer();
    void init(int port, string user, string passWord, string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);
    // 初始化整个服务器的核心配置参数
    // port：监听端口
    // log_write：日志写入方式（同步/异步）
    // opt_linger：是否优雅关闭连接
    // trigmode：触发模式组合
    // sql_num：数据库连接池大小
    // thread_num：线程池线程数量
    // actor_model：并发模型（Proactor/Reactor）
    void thread_pool();
    // 初始化线程池
    void sql_pool();
    // 初始化数据库连接池
    void log_write();
    // 初始化日志系统
    void trig_mode();
    // 设置触发模式
    void eventListen();
    // 创建监听 socket，并把它注册到 epoll 中
    void eventLoop();
    // 服务器主事件循环
    void timer(int connfd, struct sockaddr_in client_address);
    // 给一个新连接创建对应的定时器
    void adjust_timer(util_timer *timer);
    // 调整某个连接的定时器
    void deal_timer(util_timer *timer, int sockfd);
    // 处理定时器到期事件
    bool dealclientdata();
    // 处理新客户端连接到来的事件
    bool dealwithsignal(bool &timeout, bool &stop_server);
    // 处理信号事件
    void dealwithread(int sockfd);
    // 处理某个连接上的读事件
    void dealwithwrite(int sockfd);
    // 处理某个连接上的写事件
public:
    int m_port;
    // 服务器监听端口号
    char *m_root;
    // 服务器资源根目录
    int m_log_write;
    // 日志写入方式
    int m_close_log;
    // 是否关闭日志功能
    int m_actormodel;
    // 并发模型选择
    int m_pipefd[2];
    // 管道文件描述符数组
    int m_epollfd;
    // epoll 实例的文件描述符
    http_conn *users;
    // 保存所有客户端连接对象的数组
    connection_pool *m_connPool;
    // 指向数据库连接池对象
    string m_user;
    // 登录数据库用户名
    string m_passWord;
    // 登录数据库密码
    string m_databaseName;
    // 使用数据库名
    int m_sql_num;
    // 数据库连接池中的连接数量
    threadpool<http_conn> *m_pool;
    // 线程池对象指针
    int m_thread_num;
    // 线程池中的线程数量
    epoll_event events[MAX_EVENT_NUMBER];
    // 存放 epoll_wait() 返回的就绪事件
    int m_listenfd;
    // 监听 socket 的文件描述符
    int m_OPT_LINGER;
    // 是否启用优雅关闭连接
    int m_TRIGMode;
    // 触发模式组合值
    int m_LISTENTrigmode;
    // 监听 socket 的触发模式
    int m_CONNTrigmode;
    // 已连接 socket 的触发模式
    client_data *users_timer;
    // 保存每个客户端对应的定时器数据
    Utils utils;
    // 工具类对象
};
#endif
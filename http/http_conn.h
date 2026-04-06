#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
//防止这个头文件被重复包含

#ifdef __linux__

#include <unistd.h>
//Unix/Linux 系统调用相关头文件
//HTTP 连接处理里会经常用到文件描述符操作

#include <signal.h>
//信号处理相关头文件

#include <sys/types.h>
//系统数据类型头文件

#include <sys/epoll.h>
//Linux的epoll 相关头文件
//这是高并发服务器的核心之一

#include <fcntl.h>
//文件控制相关头文件

#include <sys/socket.h>
//socket 编程核心头文件

#include <netinet/in.h>
//网络地址结构体定义
//提供：sockaddr_in(这是 IPv4 地址常用结构体)

#include <arpa/inet.h>
//IP 地址转换函数头文件

#include <assert.h>
//断言相关头文件

#include <sys/stat.h>
//文件状态相关头文件

#include <string.h>
//C 风格字符串处理相关头文件

#include <pthread.h>
//POSIX 线程库

#include <stdio.h>
//C标准输入输出相关头文件

#include <stdlib.h>
//C标准库相关头文件

#include <sys/mman.h>
//内存映射相关头文件

#include <stdarg.h>
//可变参数相关头文件

#include <errno.h>
//错误码相关头文件

#include <sys/wait.h>
//进程等待相关头文件

#include <sys/uio.h>
//分散/聚集 I/O 头文件

#include <map>
//C++STL里的映射容器

#include <string>
//string 头文件

#include "../lock/locker.h"
//锁相关封装

#include "../CGImysql/sql_connection_pool.h"
//数据库连接池头文件

#include "../timer/lst_timer.h"
//定时器相关头文件

#include "../log/log.h"
//日志相关头文件

using namespace std;

class http_conn
{
    //HTTP 连接对象
public:
    static const int FILENAME_LEN = 200;
    //文件名的最大长度

    static const int READ_BUFFER_SIZE = 2048;
    //读缓冲区大小 2048 字节
    //保存从 socket 读到的 HTTP 请求数据

    static const int WRITE_BUFFER_SIZE = 1024;
    //写缓冲区大小 1024 字节
    //保存要发送给客户端的 HTTP 响应头等内容

    enum METHOD
    {
        GET = 0,   //获取资源
        POST,      //提交数据
        HEAD,      //只要响应头
        PUT,       //上传/替换资源
        DELETE,    //删除资源
        TRACE,     //追踪请求
        OPTIONS,   //选项请求
        CONNECT,   //连接请求
        PATH       //路径请求
    };
    //HTTP 请求方法枚举
    //表示客户端请求类型

    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, //解析请求行
        CHECK_STATE_HEADER,          //解析请求头
        CHECK_STATE_CONTENT          //解析请求体
    };
    //HTTP 请求解析的主状态机状态
    //表示当前解析到 HTTP 请求的哪个部分

    enum HTTP_CODE
    {
        NO_REQUEST,          //请求还没读完整
        GET_REQUEST,         //已经得到一个完整请求
        BAD_REQUEST,         //错误请求
        NO_RESOURCE,         //请求资源不存在
        FORBIDDEN_REQUEST,   //没有权限访问
        FILE_REQUEST,        //文件请求，且资源有效
        INTERNAL_ERROR,      //服务器内部错误
        CLOSED_CONNECTION    //客户端已经关闭连接
    };
    //表示 HTTP 请求处理结果

    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
    //表示“按行解析”时一行的状态

public:
    http_conn() {}
    ~http_conn() {}
    //默认函数和析构函数

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //初始化一个连接对象
    //sockfd：客户端 socket 文件描述符
    //addr：客户端地址信息
    //char *：通常是网站根目录 doc_root
    //int：可能是触发模式、日志模式之类
    //int：另一个配置项
    //string user：数据库用户名
    //string passwd：数据库密码
    //string sqlname：数据库名
    //accept 一个新连接后，用它把这个 http_conn 对象配置好

    void close_conn(bool real_close = true);
    //关闭连接
    //real_close = true：默认真关闭
    //关闭 socket、从 epoll 中移除、更新用户计数等

    void process();
    //处理连接的核心函数

    bool read_once();
    //从 socket 读一次数据到读缓冲区

    bool write();
    //向客户端发送响应

    sockaddr_in *get_address()
    {
        return &m_address;
    }
    //返回客户端地址结构体指针

    void initmysql_result(connection_pool *connPool);
    //初始化数据库中的用户信息结果

    int timer_flag;
    //是否需要调整/触发定时器

    int improv;
    //是否有事件发生、任务是否已改进/处理

    //它们通常是主线程和工作线程之间用于通信的标记位

private:
    void init();
    //内部初始化函数
    //重置连接对象内部状态
    //公有 init(...)：接收外部参数，初始化整个连接
    //私有 init()：重置内部解析状态、缓冲区等

    HTTP_CODE process_read();
    //对 m_read_buf 中的 HTTP 请求进行解析，返回解析结果

    bool process_write(HTTP_CODE ret);
    //根据处理结果组织响应数据
    //ret：请求处理结果
    //把对应响应头和响应内容写入发送缓冲区

    HTTP_CODE parse_request_line(char *text);
    //解析 HTTP 请求行
    //从第一行里解析出:请求方法,URL,HTTP 版本

    HTTP_CODE parse_headers(char *text);
    //解析 HTTP 请求头

    HTTP_CODE parse_content(char *text);
    //解析 HTTP 请求体
    //主要用于 POST 请求

    HTTP_CODE do_request();
    //真正执行请求
    //通常包括：拼接文件真实路径,检查文件是否存在,检查权限,用 mmap 映射文件,处理 CGI 登录注册逻辑等

    char *get_line()
    {
        return m_read_buf + m_start_line;
    }
    //获取当前正在解析的这一行的起始地址

    LINE_STATUS parse_line();
    //从读缓冲区里尝试解析出一行

    void unmap();
    //解除内存映射
    //对应前面的 mmap，文件发送完后要 munmap，否则会造成资源泄漏

    bool add_response(const char *format, ...);
    //往写缓冲区添加格式化内容
    //用于构造响应头

    bool add_content(const char *content);
    //添加响应正文内容

    bool add_status_line(int status, const char *title);
    //添加响应状态行

    bool add_headers(int content_length);
    //统一添加响应头

    bool add_content_type();
    //添加 Content-Type 头

    bool add_content_length(int content_length);
    //添加 Content-Length

    bool add_linger();
    //添加连接保持头

    bool add_blank_line();
    //添加空行

public:
    static int m_epollfd;
    //所有 http_conn 对象共享一个 epoll 文件描述符
    //整个服务器通常只维护一个 epoll 实例

    static int m_user_count;
    //当前连接用户数

    MYSQL *mysql;
    //MySQL 连接指针

    int m_state;
    //表示当前连接在 reactor 模式下的处理状态
    //0：读事件；1：写事件

private:
    int m_sockfd;
    //客户端连接对应的 socket 文件描述符
    //这个连接最核心的标识之一

    sockaddr_in m_address;
    //客户端地址信息
    //这个连接最核心的标识之一

    char m_read_buf[READ_BUFFER_SIZE];
    //读缓冲区
    //存放从客户端 socket 读进来的请求数据

    int m_read_idx;
    //当前已经读入缓冲区的数据总长度
    //指示 m_read_buf 里哪部分是有效数据

    int m_checked_idx;
    //当前已经检查到的位置

    int m_start_line;
    //当前行的起始位置
    //配合 get_line() 使用，定位当前要解析的这一行

    char m_write_buf[WRITE_BUFFER_SIZE];
    //写缓冲区
    //存放将要发给客户端的 HTTP 响应头等内容

    int m_write_idx;
    //写缓冲区中已写入的长度

    CHECK_STATE m_check_state;
    //主状态机当前状态

    METHOD m_method;
    //当前请求的方法

    char m_real_file[FILENAME_LEN];
    //请求资源对应的服务器真实文件路径

    char *m_url;
    //请求 URL

    char *m_version;
    //HTTP 版本号

    char *m_host;
    //Host 头字段内容

    int m_content_length;
    //HTTP 请求体长度

    bool m_linger;
    //是否保持长连接
    //对应 HTTP 的 keep-alive

    char *m_file_address;
    //被 mmap 映射到内存中的文件地址

    struct stat m_file_stat;
    //保存文件状态信息

    struct iovec m_iv[2];
    //定义两个分散 I/O 缓冲区
    //m_iv[0]：响应头
    //m_iv[1]：文件内容

    int m_iv_count;
    //当前 iovec 数组中有效块数量
    //告诉 writev() 这次要发几个缓冲区

    int cgi;
    //是否启用 CGI 处理 POST 请求

    char *m_string;
    //存储请求体数据

    int bytes_to_send;
    //剩余待发送字节数

    int bytes_have_send;
    //已经发送字节数

    char *doc_root;
    //网站根目录
    //拼接真实文件路径时使用

    map<string, string> m_users;
    //用户名和密码映射表

    int m_TRIGMode;
    //触发模式
    //一般表示 LT / ET 模式。
    //LT：水平触发
    //ET：边缘触发

    int m_close_log;
    //日志开关

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
    //保存数据库配置
};

#endif

#endif
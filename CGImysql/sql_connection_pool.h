#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>

#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
    MYSQL *GetConnection();
    //获取连接

    bool ReleaseConnection(MYSQL *conn);
    //释放连接

    int GetFreeConn();
    //获取空闲连接数

    void DestroyPool();
    //销毁连接池

    static connection_pool *GetInstance();
    //单例模式相关

    void init(string url, string User, string PassWord, string DateBaseName, int Port, int MaxConn, int close_log);
    //初始化连接池

private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn;
    //最大连接数

    int m_CurConn;
    //当前已使用的连接数

    int m_FreeConn;
    //当前空闲的连接数

    locker lock;

    list<MYSQL *> connList;
    //连接池

    sem reserve;
    //信号量对象
    //控制可用连接资源的数量

public:
    string m_url;
    //主机地址

    int m_Port;
    //数据库端口号

    string m_User;
    //登陆数据库用户名

    string m_PassWord;
    //登陆数据库密码

    string m_DatabaseName;
    //数据库名

    int m_close_log;
    //日志开关
};

class connectionRAII
{
    //定义一个 RAII 类
    //专门用来管理数据库连接资源
public:
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *conRAII;
    //保存当前 RAII 对象管理的数据库连接指针

    connection_pool *poolRAII;
    //保存所属的连接池指针
};

#endif
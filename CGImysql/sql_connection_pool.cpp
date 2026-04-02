#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"
using namespace std;
connection_pool::connection_pool()
{
    //构造函数
    m_CurConn = 0;
    //当前正在使用的连接数量
    m_FreeConn = 0;
    //当前空闲连接数量
}
connection_pool *cennection_pool::GetInstance()
{
    //单例模式
    //用来获取连接池的唯一实例
    static connection_pool connPool;
    return &connPool;
    //返回这个唯一对象的地址
}
void connection_pool::init(string url,string User,string PassWord,strinf DBName,int Port,int MaxConn,int close_log)
{
    //连接池真正的初始化函数
    m_url = url;                //数据库服务器地址
	m_Port = Port;              //端口号
	m_User = User;              //用户名
	m_PassWord = PassWord;      //密码
	m_DatabaseName = DBName;    //数据库名
	m_close_log = close_log;    //是否关闭日志
    for(int i=0;i<Max_conn;i++)
    {
        //循环创建多个数据库连接
        MYSQL *con = NULL;
        //定义一个 MYSQL* 指针 con指向当前创建的数据库连接对象
        con = mysql_init(con);
        //初始化一个 MYSQL 对象
        if(con==NULL)
        {
            LOG_ERROR("MySQL Error");
            //记录错误日志
            exit(1);
        }
        con=mysql_real_connect(con,url.c_str(),User.c_str(),PassWord.c_str(),DBName.c_str(),Port,NULL,0);
        //真正连接数据库服务器
        if(con==NULL)
        {
            LOG_ERROR("MySQL Error");
            //记录错误日志
            exit(1);
        }
        connList.push_back(con);
        //把这个成功连接的 MYSQL* 放进连接池链表 connList 里
        //这里的 connList 存的是 空闲连接
        ++m_FreeConn;
        //空闲连接数加 1
    }
    reverse = sem(m_FreeConn);
    //用当前空闲连接数初始化信号量 reserve
    m_MaxConn = m_FreeConn;
    //初始化完成后，最大连接数就等于当前创建好的空闲连接数
}
//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
    //从池中取出一个连接
    MYSQL *con =NULL;
    //con 用来保存即将返回的连接
    if(0==connList.size())
        return NULL;
    //如果链表里一个连接都没有，直接返回 NULL
    reserve.wait();
    //如果有可用连接，信号量减 1，继续执行
    //如果没有可用连接，当前线程阻塞
    lock.lock();
    con = connList.front();
    //取出链表头部的一个连接
    //相当于从空闲连接池中拿一个可用连接
    connList.pop_front();
    //把这个连接从空闲链表中删除
    --m_FreeConn;
    ++m_CurConn;
    //空闲连接数减 1
    //正在使用的连接数加 1
    lock.unlock();
    return con;
    //返回刚刚取到的连接指针
}
bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if(NULL==con)
        //如果传进来的连接指针是空，说明无效
        return false;
    lock.lock();
    connList.push_back(con);
    //把连接重新放回空闲连接链表尾部
    ++m_FreeConn;
    --m_CurConn;
    //空闲连接数加 1
    //当前使用连接数减 1
    lock.unlock();
    reserve.post();
    //对信号量执行 V 操作（释放）
    return true;
}
void connection_pool::DestroyPool()
{
    //销毁连接池里的所有连接
    lock.lock();
    if(connList.size()>0)
    {
        //如果空闲连接链表里还有连接，就逐个关闭
        list<MYSQL *>::iterator it;
        //定义一个链表迭代器，用来遍历 connList
        for(it = connList.begin();it!=connList.end();++it)
        {
            //遍历所有空闲连接
            MYSQL *con = *it;
            //取出当前迭代器指向的连接指针
            mysql_close(con);
            //关闭这个数据库连接
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        conList.clear();
    }
    lock.unlock();
}
int connection_pool::GetFreeConn()
{
    //获取空闲连接数
    return this->m_FreeConn;
}
connection_pool::~connection_pool()
{
    DestroyPool();
}
//connectionRAII自动管理连接的借出与归还
connectionRAII::connectionRAII(MYSQL **SQL,connection_pool *connPool)
{
    *SQL = connPool->GetConnection();
    //把获取到的连接“传回外部”
    conRAII = *SQL;
    //把这个连接保存到 connectionRAII 对象自己的成员变量 conRAII 中
    poolRAII = connPool;
    //保存连接池指针到成员变量 poolRAII
}
connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}
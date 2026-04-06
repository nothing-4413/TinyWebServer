#ifndef THREADPOOL_H
#define THREADPOOL_H
//头文件保护

#include <list>
//引入 STL 的双向链表容器

#include <cstdio>
//C 风格标准输入输出库

#include <exception>
//抛出异常对象

#include <pthread.h>
//POSIX 线程库 头文件

#include "../lock/locker.h"
//自定义的锁封装头文件

#include "../CGImysql/sql_connection_pool.h"
//数据库连接池相关的头文件

template <typename T>
//线程池并不绑定某一种具体任务类型,而是可以处理任意类型 T 的任务
class threadpool
{
public:
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 10000);
    //线程池的构造函数
    //actor_model：并发模型选择
    //connPool：数据库连接池指针
    //thread_number = 8：默认创建 8 个线程
    //max_requests = 10000：任务队列最多可容纳 10000 个请求

    ~threadpool();
    //线程池析构时会释放动态申请的线程数组

    bool append(T *request, int state);
    //向任务队列中添加请求的函数
    //会额外设置:request->m_state = state;
    //0 表示读任务
    //1 表示写任务

    bool append_p(T *request);
    //添加任务，但不设置状态

private:
    static void *worker(void *arg);
    //线程入口函数
    //把 arg 强转为线程池对象指针
    //调用该对象的 run()

    void run();
    //真正的工作逻辑函数

private:
    int m_thread_number;
    //线程池中的线程数

    int m_max_requests;
    //请求队列中允许的最大请求数

    pthread_t *m_threads;
    //描述线程池的数组，其大小为m_thread_number

    std::list<T *> m_workqueue;
    //请求队列

    locker m_queuelocker;
    //保护请求队列的互斥锁

    sem m_queuestat;
    //队列状态信号量
    //表示当前“可取出的任务数”有多少

    connection_pool *m_connPool;
    //数据库

    int m_actor_model;
    //模型切换
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests)
    : m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool), m_actor_model(actor_model)
{
    //构造函数的正式实现
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    //参数合法性检查

    m_threads = new pthread_t[m_thread_number];
    //创建线程数组
    //动态申请一个 pthread_t 数组，用于保存每个线程的句柄

    if (!m_threads)
        throw std::exception();
    //分配失败检查

    for (int i = 0; i < thread_number; ++i)
    {
        //创建每个线程
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            //创建线程
            delete[] m_threads;
            throw std::exception();
            //创建线程失败
            //释放已经申请的线程数组
            //抛异常
        }

        if (pthread_detach(m_threads[i]))
        {
            //线程分离
            //把第 i 个线程设置为分离态（detached）
            delete[] m_threads;
            throw std::exception();
            //分离失败，同样释放数组并抛异常
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    //析构函数
    delete[] m_threads;
    //释放线程 ID 数组
}

template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    //添加带状态的任务
    m_queuelocker.lock();
    //加锁

    if (m_workqueue.size() >= (size_t)m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
        //队列满了就拒绝
    }

    request->m_state = state;
    //设置任务状态

    m_workqueue.push_back(request);
    //入队

    m_queuelocker.unlock();
    //解锁

    m_queuestat.post();
    //通知工作线程

    return true;
    //任务成功加入队列
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    //添加不带状态的任务
    m_queuelocker.lock();
    //加锁

    if (m_workqueue.size() >= (size_t)m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
        //队列满了就拒绝
    }

    m_workqueue.push_back(request);
    //入队

    m_queuelocker.unlock();
    //解锁

    m_queuestat.post();
    //通知工作线程

    return true;
    //任务成功加入队列
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool<T> *pool = (threadpool<T> *)arg;
    //把参数转回线程池对象

    pool->run();
    //调用成员函数 run

    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    //核心函数 run
    //永远等待任务 → 取任务 → 处理任务 → 再等待下一个
    while (true)
    {
        m_queuestat.wait();
        //等待任务到来

        m_queuelocker.lock();
        //加锁保护队列

        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
            //双重检查队列是否为空
        }

        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        //取出队首任务

        m_queuelocker.unlock();
        //解锁

        if (!request)
            continue;
        //空指针保护

        if (1 == m_actor_model)
        {
            //根据并发模型决定处理方式
            if (0 == request->m_state)
            {
                //Reactor 模式：读事件
                if (request->read_once())
                {
                    //读数据
                    request->improv = 1;
                    //标记 improv = 1

                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    //从连接池取数据库连接

                    request->process();
                    //执行业务处理
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                    //读失败分支
                    //timer_flag = 1 往往表示:这个连接应当被关闭/定时器应当清理它
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                    //执行写操作
                }
                else
                {
                    //写失败分支
                    request->improv = 1;
                    request->timer_flag = 1;
                    //设置超时/关闭标记
                }
            }
        }
        else
        {
            //非 Reactor 模式
            //通常就是 Proactor
            //线程池不再区分“读/写事件处理”
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif
/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/
#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

using namespace std;

template<class T>
class block_queue
{
    //类模板
public:
    block_queue(int max_size = 1000)
    {
        //构造函数
        if (max_size <= 0)
        {
            //检查用户传入的最大容量是否合法。
            exit(-1);
            //如果容量不合法，程序直接退出
        }

        m_max_size = max_size;
        //保存队列最大容量

        m_array = new T[max_size];
        //动态分配一个长度为 max_size 的数组，作为循环队列的底层存储区。

        m_size = 0;
        //当前队列元素个数初始化为 0

        m_front = -1;
        m_back = -1;
        //初始化队头和队尾位置为 -1
    }

    void clear()
    {
        //清空队列
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue()
    {
        //析构函数
        m_mutex.lock();
        if (m_array != NULL)
            delete [] m_array;
        //释放数组内存
        m_mutex.unlock();
    }

    bool full()
    {
        //判断队列是否满
        m_mutex.lock();
        if (m_size >= m_max_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty()
    {
        //判断队列是否空
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool front(T &value)
    {
        //返回队首元素
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[(m_front + 1) % m_max_size];
        m_mutex.unlock();
        return true;
    }

    bool back(T &value)
    {
        //返回队尾元素
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size()
    {
        //返回当前元素个数
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
        //为什么要用 tmp
        //避免把共享变量直接暴露在加锁范围之外
    }

    int max_size()
    {
        //返回最大容量
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item)
    {
        //这个函数负责把一个元素插入队列尾部
        m_mutex.lock();

        if (m_size >= m_max_size)
        {
            //队列已满，无法继续插入，返回 false
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        //更新队尾指针，指向新的插入位置

        m_array[m_back] = item;
        //把新元素存到队尾位置

        m_size++;
        //队列元素个数加 1

        m_cond.broadcast();
        //插入新元素后，唤醒等待条件变量的线程

        m_mutex.unlock();
        return true;
    }

    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {
        //这个函数负责从队首取出一个元素
        //如果队列为空，则当前线程会阻塞等待
        m_mutex.lock();

        while (m_size <= 0)
        {
            if (!m_cond.wait(m_mutex.get()))
            {
                //线程在条件变量上等待
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        //队首位置向前移动一格，指向真正要弹出的元素

        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    bool pop(T &item, int ms_timeout)
    {
        //带超时的 pop
        struct timespec t = {0, 0};
        //保存绝对超时时间

        struct timeval now = {0, 0};
        //保存当前时间

        gettimeofday(&now, NULL);
        //获取当前时间，存入 now

        m_mutex.lock();

        if (m_size <= 0)
        {
            //如果当前队列为空，就计算一个等待截止时间，然后进行定时等待
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            //把毫秒超时时间中的“秒部分”加到当前秒数上

            t.tv_nsec = (now.tv_usec + (ms_timeout % 1000) * 1000) * 1000;
            //把毫秒部分换算成纳秒，并加到当前微秒基础上

            if (t.tv_nsec >= 1000000000)
            {
                t.tv_sec += 1;
                t.tv_nsec -= 1000000000;
            }

            if (!m_cond.timewait(m_mutex.get(), t))
            {
                //在条件变量上等待，最多等到时间 t
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    //互斥锁对象

    cond m_cond;
    //条件变量对象

    T *m_array;
    //底层循环数组指针

    int m_size;
    //当前队列中已有元素数量

    int m_max_size;
    //队列最大容量

    int m_front;
    int m_back;
    //队头和队尾相关索引
};

#endif
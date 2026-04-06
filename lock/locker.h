#ifndef LOCKER_H
#define LOCKER_H
//头文件保护，防止重复包含

#include <exception>
//为了 throw std::exception()

#include <pthread.h>
//POSIX 线程库，提供互斥锁、条件变量等

#include <semaphore.h>
//POSIX 信号量库，提供信号量功能

class sem
{
    //封装信号量的类
    //信号量你可以理解成一个计数器。
public:
    //提供一个构造函数来初始化信号量
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
        //m_sem为要初始化的信号量对象
        //0表示信号量在进程内共享，非0表示在进程间共享
        //最后一个参数0表示信号量的初始值
    }

    //带参构造函数
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
        //num表示信号量的初始值
    }

    //析构函数销毁信号量
    ~sem()
    {
        sem_destroy(&m_sem);
        //销毁信号量对象，释放资源
    }

    //等待信号量
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
        //若信号量值 > 0，则减 1 并返回
        //若信号量值 == 0，则阻塞等待
    }

    //增加信号量
    bool post()
    {
        return sem_post(&m_sem) == 0;
        //信号量值加 1
        //可能唤醒一个阻塞在线程上的 wait()
    }

private:
    sem_t m_sem;
    //信号量对象，类型为 sem_t
    //m_sem 是底层的 POSIX 信号量对象
};

class locker
{
    //封装互斥锁的类
    //同一时刻只允许一个线程进入临界区
public:
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
        //构造函数初始化互斥锁
        //m_mutex为要初始化的互斥锁对象，NULL表示使用默认属性
    }

    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
        //析构函数销毁互斥锁，释放资源
    }

    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
        //锁定互斥锁
        //如果锁当前没人持有，则当前线程拿到锁，继续执行
        //如果锁已被别的线程持有，则当前线程阻塞等待
    }

    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
        //解锁互斥锁
        //如果当前线程持有锁，则释放锁，可能唤醒一个阻塞在线程上的 lock()
        //如果当前线程不持有锁，行为未定义
    }

    pthread_mutex_t *get()
    {
        return &m_mutex;
        //返回底层互斥锁的地址。
    }

private:
    pthread_mutex_t m_mutex;
    //互斥锁对象，类型为 pthread_mutex_t
    //m_mutex 是底层的 POSIX 互斥锁对象。
};

class cond
{
    //条件变量类
    //让线程在某个条件不满足时睡眠，等条件满足后再被唤醒。
    //必须配合互斥锁使用
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw std::exception();
        }
    }

    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_cond_wait:调用前，传入的 mutex 必须已经被当前线程加锁
        //调用后，mutex 会被自动解锁，线程进入睡眠状态
        //被唤醒后，重新获取 mutex并继续执行
        return ret == 0;
        //条件变量的核心逻辑是：检查条件,如果条件不满足，则等待,被唤醒后再检查条件
    }

    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
        //带超时的等待
        //条件满足前被唤醒，则返回成功
        //如果等到指定时间还没被唤醒，则超时返回
    }

    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
        //唤醒一个等待在条件变量上的线程
    }

    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
        //唤醒所有等待在条件变量上的线程
    }

private:
    pthread_cond_t m_cond;
    //条件变量对象，类型为 pthread_cond_t
    //m_cond 是底层的 POSIX 条件变量对象。
};

#endif
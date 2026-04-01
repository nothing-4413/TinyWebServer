#ifdef LOG_H
#define LOG_H
#include<stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"
using namespace std;
class Log
{
    public:
        static Log *get_instance()
        {
            //单例获取函数
            static Log instance;
            return &instance;
        }
        static void *flush_log_thread(void *args)
        {
            //异步写线程入口函数
            Log::get_instance()->async_write_log();
            //线程启动后，就调用单例对象的：async_write_log()
        }
        bool init(const char *file_name,int close_log,int log_buf_size = 8192,int split_lines = 5000000,int max_queue_size = 0);
        //日志系统的初始化接口
        void write_log(int level,const char *format,...);
        //写日志函数
        void flush(void);
        //强制把文件缓冲区刷新到磁盘
    private:
        Log();
        virtual ~Log();
        void *async_write_log()
        {
            //异步写日志函数
            string single_log;
            //存储从队列中取出的一条日志
            while(m_log_queue->pop(single_log))
            {
                //从阻塞队列中不断取日志
                m_mutex.lock();
                fputs(single_log.c_str(),m_fp);
                //把日志字符串写入文件
                m_mutex.unlock();
            }
        }
    private:
        char dir_name[128];                 //路径名
        char log_name[128];                 //log文件名
        int m_split_lines;                  //日志最大行数
        int m_log_buf_size;                 //日志缓冲区大小
        long long m_count;                  //日志行数记录
        int m_today;                        //因为按天分类,记录当前时间是那一天
        FILE *m_fp;                         //打开log的文件指针
        char *m_buf;                        //日志缓冲区
        block_queue<string> *m_log_queue;   //阻塞队列
        bool m_is_async;                    //是否同步标志位
        locker m_mutex;                     //互斥锁对象
        int m_close_log;                    //关闭日志
};
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
//DEBUG 宏
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
//INFO 宏
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
//WARN 宏
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}
//ERROR 宏
#endif
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;
Log::log()
{
    //构造函数
    m_count = 0;
    //当前已经写了多少条日志，初始为 0
    m_is_asunc = false;
    //默认不是异步写日志，而是同步
}
Log::~log()
{
    //析构函数
    if(m_fp!=NULL)
    {
        fclose(m_fp);
        //关闭日志文件
    }
}
bool Log::init(const char *file_name,int close_log,int log_buf_size,int split_lines,int max_queue_size)
{
    //file_name：日志文件名
    //close_log：是否关闭日志
    //log_buf_size：日志缓冲区大小
    //split_lines：一个文件最多写多少行，超过就分文件
    //max_queue_size：异步队列长度，>=1 表示启用异步
    //如果设置了max_queue_size,则设置为异步
    if(max_queue_size>=1)
    {
        //如果队列长度至少为 1，就说明用户希望启用异步日志模式
        m_is_async = true;
        //标记当前日志系统为异步模式
        m_log_queue = new block_queue<string>(max_queue_size);
        //创建一个阻塞队列，队列里存的是 string 类型日志字符串
        pthread_t tid;
        //定义一个线程 ID
        pthread_creat(&tid,NULL,flush_log_thread,NULL);
        //创建一个新线程
        //&tid：线程 ID 输出位置
        //NULL：使用默认线程属性
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        //NULL：传给线程函数的参数
    }
    m_close_log = close_log;
    //记录是否关闭日志功能
    m_log_buf_size=log_buf_size;
    //保存日志缓冲区大小
    m_buf = new char[m_log_buf_size];
    //申请一块字符数组，作为格式化日志的临时缓冲区
    memset(m_buf,'\0',m_log_buf_size);
    //把整个缓冲区清零，防止里面有脏数据
    m_split_lines = split_lines;
    //记录单个日志文件允许的最大行数
    time_t t = time(NULL);
    //获取当前时间，单位是秒，返回自 Unix Epoch 以来的秒数
    struct tm *sys_tm = localtime(&t);
    //把时间戳转为本地时间结构体 tm
    struct tm my_tm = *sys_tm;
    //把 localtime 返回的结果拷贝到局部变量里
    const char *p = strrchr(file_name,'/');
    //从 file_name 里找最后一个 '/'
    //把路径和文件名拆开
    char log_full_name[256]={0};
    //定义最终完整日志文件名缓冲区
    if(p==NULL)
    {
        //如果没有目录
        //生成带日期前缀的日志文件名
        snprintf(log_full_name,255,"%d_%02d%02d%s",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,file_name);
    }
    else
    {
        //如果带目录
        strcpy(log_name,p+1);
        //把最后一个 / 后面的内容拷贝到 log_name
        strncpy(dir_name,file_name,p-file_name+1);
        //把路径部分提取出来，包括最后那个 /
        snprintf(log_full_name,255,"%s%d_%02d_%02d_%s",dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
        //拼成完整日志路径
    }
    m_today = my_tm.tm_mday;
    //记录今天日期
    m_fp=fopen(log_full_name,"a");
    //打开日志文件
    if(m_fp==NULL)
    {
        return false;
    }
    return true;
}
void Log::write_log(int level,const char *format,...)
{
    //这是日志模块最核心的函数
    //1）获取当前精确时间
    struct timeval now ={0,0};
    //定义一个 timeval 结构，包含:tv_sec：秒,tv_usec：微秒
    gettimeofday(&now,NULL);
    //获取当前时间，精确到微秒
    time_t t = now7.tv_sec;
    //把秒部分取出来，准备转本地时间0.
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    //把当前时间转成本地年月日时分秒，并拷贝到局部变量里
    //2）生成日志级别字符串
    char s[16]={0};
    //定义一个小字符数组，用来存日志级别前缀
    switch (level)
    {
        //根据整数级别决定写哪种日志类型。
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }
    //3）检查是否需要切换日志文件
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;
    //每写一条日志，行数加一
    if(m_today!=my_tm.tm_mday || m_count%m_split_lines == 0)//everyday log
    {
        //满足以下任一条件就切换日志文件
        //日期变了：跨天了
        //当前行数达到分割阈值：比如每 500 万行一个文件
        char new_log[256]={0};
        //新日志文件名缓冲区
        fflush(m_fp);
        //把当前文件流缓冲区强制刷新到磁盘
        fclose(m_fp);
        //关闭当前日志文件
        char tail[16]={0};
        snprintf(tail,16,"%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        //构造日期前缀
        if(m_today!=my_tm.tm_mday)
        {
            //跨天情况
            snprintf(new_log,255,"%s%s%s",dir_name,tail,log_name);
            //新文件名变成当天日期对应的文件
            m_today = my_tm.tm_mday;
            //更新 m_today
            m_count = 0;
            //把 m_count 重新从 0 开始计数
        }
        else
        {
            //达到最大行数情况
            snprintf(new_log,255,"%s%s%s.%lld",dir_name,tail,log_name,m_count/m_split_lines);
            //m_count / m_split_lines是分片编号
        }
        m_fp=fopen(new_log,"a");
    }
    m_mutex.unlock();
    //4）处理可变参数，拼装完整日志字符串
    va_list valst;
    //定义一个可变参数列表对象
    va_start(valst,foramt);
    //初始化参数列表，从 format 后面的参数开始取
    string log_str;
    //保存最终日志内容
    m_mutex.lock();
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                 my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                 my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    //时间和日志级别前缀
    //%d-%02d-%02d：年月日
    //%02d:%02d:%02d：时分秒
    //.%06ld：微秒，固定 6 位
    //%s：日志级别
    //n 表示已经写入了多少字符
    int m = vsnprintf(m_buf+n,m_log_buf_size-n-1,format,valst);
    //把用户真正的日志内容接到后面
    m_buf[n + m] = '\n';
    //给日志补一个换行，这样每条日志单独占一行
    m_buf[n + m + 1] = '\0';
    //加字符串结束符
    log_str = m_buf;
    //把 m_buf 中的内容拷贝到 string 中
    m_mutex.unlock();
    if(m_is_async && !m_log_queue->full())
    {
        //异步模式
        m_log_queue->push(log_str);
        //把日志字符串压入阻塞队列，后台线程会自己去取并写文件
    }
    else
    {
        //同步模式 / 队列满了
        //当前线程直接写文件
        m_mutex.lock();
        fputs(log_str.c_str(),m_fp);
        //把日志字符串写入日志文件
        m_mutex.unlock();
    }
    va_end(valst);
    //可变参数处理结束，做清理
}
void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp);
    //强制刷新写入流缓冲区
    m_mutex.unlock();
}

#include"lst_timer.h"
#include"../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    //sort_timer_lst 构造
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst()
{
    //析构函数
    util_timer *tmp=head;
    //从链表头开始遍历。
    while(tmp)
    {
        head = tmp->next;
        //先把 head 移动到下一个节点。
        delete tmp;
        //释放当前定时器节点。
        tmp = head;
        //继续处理新的头节点
    }
}
void sort_timer_lst::add_timer(util_timer *timer)
{
    //添加定时器
    if(!timer)
        return;
        //如果传进来的指针是空指针，直接返回
    if(!head)
    {
        //如果链表为空
        head = tail = timer;
        //链表里第一个节点，头尾都指向它
        return;
    }
    if(timer->expire<head->expire)
    {
        //如果新定时器的到期时间比当前头节点还早,它应该插到最前面
        timer->next=head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer,head);
    //如果不是空链表，也不是插到头部，那就调用重载版本
}
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    //调整定时器
    if(!timer)
    {
        //空指针检查
        return;
    }
    util_timer *tmp = timer->next;
    //取出当前节点的后一个节点
    if(!tmp||(timer->expire<tmp->expire))
    {
        //当前节点后面没有节点
        //当前节点新的过期时间仍然比后继节点小，顺序没乱
        return;
    }
    //顺序乱了,当前 timer 应该往后移动
    //
    if(timer==head)
    {
        //从新的头节点开始，重新把它插入到合适位置
        head=head->next;
        head->prev=NULL;
        timer->next=NULL;
        add_timer(timer,head);
    }
    else
    {
        //从原来它的后继位置开始，向后寻找合适插入点
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer,timer->next);
    }
}
void sort_timer_lst::del_timer(util_timer *timer)
{
    //删除定时器
    if(!timer)
    {
        return;
    }
    if((timer==head)&&(timer==tail))
    {
        delelte timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if(timer==head)
    {
        head=head->next;
        head->prev=NULL;
        delete timer;
        return;
    }
    if(timer==tail)
    {
        tail=tail->prev;
        tail->next=NULL;
        delete timer;
        return;
    }
    timer->prev->next=timer->next;
    timer->next->prev=timer->prev;
    delete timer;
}
void sort_timer_lst::tick()
{
    //处理超时定时器
    //检查当前时间，把所有已经到期的定时器从链表头开始依次处理掉
    if(!head)
    {
        return;
    }
    time_t cur = time(NULL);
    //获取当前时间
    util_timer *tmp = head;
    while(tmp)
    {
        if(cur<tmp->expire)
        {
            break;
        }
        tmp->cb_func(tmp->user_date);
        head = tmp->next;
        if(head)
        {
            head->prev=NULL;
        }
        delete tmp;
        tmp = head;
    }
}
void sort_timer_lst::add_timer(util_timer *timer,util_timer *lst_head)
{
    //内部辅助函数
    //从 lst_head 开始往后找，把 timer 插到合适位置
    util_timer *prev = lst_head;
    //prev 表示当前考察位置的前一个节点，初始为 lst_head
    util_timer *tmp = prev->next;
    //tmp 是当前要比较的位置
    while(tmp)
    {
        //只要还没走到链表末尾，就继续查找
        if(time->expire<tmp->expire)
        {
            //如果新节点过期时间比 tmp 小
            //说明应该插在 prev 和 tmp 之间
            prev->next = timer;
            timer->next=tmp;
            tmp->prev=timer;
            timer->prev=prev;
            break;
        }
        prev=tmp;
        //没找到位置，就继续往后走
        tmp=tmp->next;
    }
    if(!tmp)
    {
        //如果循环结束还没插进去
        //说明新节点的过期时间是最大的，应该插到链表尾部
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}
void Utils::init(int timeslot)
{
    //初始化时间槽（定时器周期）
    m_TIMESLOT = timeslot;
}
int Utils::setnonblocking(int fd)
{
    //把一个文件描述符设置成非阻塞模式
    int old_option = fcntl(fd,F_GETFL);
    //获取 fd 当前的文件状态标志
    int new_option = old_option | O_NONBLOCK;
    //把 O_NONBLOCK 这一位打开
    fcntl(fd,F_SETFL,new_option);
    //把新的状态写回去
    return old_option;
    //返回原来的状态，方便以后恢复
}
void Utils::addfd(int epollfd,int fd,bool one_shot,int TRIGMode)
{
    //把一个文件描述符注册到 epoll 内核事件表中
    epoll_event event;
    //定义 epoll 事件结构体。
    event.date.fd = fd;
    //把当前文件描述符保存进去
    if(1 == TRIGMode)
        //触发模式是 1，则使用 ET 模式
        event.events = EROLLIN | EPOLLET | EPOLLRDHUP;
        //EPOLLIN：可读事件
        //EPOLLET：边缘触发 ET
        //EPOLLRDHUP：对端关闭连接或半关闭
    else
        //LT 模式
        event.events = EROLLIN | EPOLLRDHUP;
    if(one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    //把 fd 加入 epoll 监听集合
    setnonblocking(fd);
    //把这个 fd 设置为非阻塞
}
void Utils::sig_handler(int sig)
{
    //信号处理函数
    int save_errno = errno;
    //把当前 errno 保存下来
    int msg = sig;
    //把信号值保存到变量 msg
    send(u_pipefd[1],(char*)&msg,1,0);
    //往管道写端写入一个字节，通知主循环
    errno = save_errno;
    //恢复原来的 errno，保证信号处理前后程序状态尽量一致
}
void Utils::addsig(int sig,void(handler)(int),bool restart)
{
    //给某个信号注册处理函数
    struct sigaction sa;
    //定义信号处理配置结构体
    memset(&sa,'\0',sizeof(sa));
    //把结构体清零，避免脏数据
    sa.sa_handler = handler;
    //指定信号处理函数
    if(restart)
        //被信号中断的某些系统调用，会自动重启，而不是直接失败返回
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    //在处理当前信号期间，暂时阻塞所有其他信号
    assert(sigaction(sig,&sa,NULL)!=-1);
    //调用 sigaction 注册信号处理动作
}
void Utils::timer_handler()
{
    //定时任务的核心入口
    m_timer_lst.tick();
    //扫描定时器链表，把到期的连接清理掉
    alarm(m_TIMESLOT);
    //重新设置一个闹钟
}
void Utils::show_error(int connfd,const char *info)
{
    //给客户端发一条错误信息，然后关闭连接
    send(connfd,info,strlen(info),0);
    close(connfd);
}
int *Utils::u_pipefd = 0;
int Utils::e_epollfd = 0;
//给 Utils 类中的静态成员分配并初始化
class Utils;
//前向声明
void cb_func(client_date *user_date)
{
    //超时回调函数
    epoll_ctl(Utils::u_epollfd,EPOLL_CTL_DEL,user_date->sockfd,0);
    //把这个客户端 socket 从 epoll 监听集合中删除
    assert(user_date);
    //断言 user_data 不为空
    close(user_date->sockfd);
    http_conn::m_user_count--;
}
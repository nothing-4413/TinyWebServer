#include "http_conn.h"
//HTTP 连接对象实现文件

#include <mysql/mysql.h>
//MySQL 数据库相关头文件

#include <fstream>
//文件流相关头文件

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";
//响应HTTP状态码和对应的描述文本

locker m_lock;
//封装的互斥锁类对象

map<string, string> users;
//全局用户名密码表

int setnonblocking(int fd)
{
    //将文件描述符设置为非阻塞模式
    int old_option = fcntl(fd, F_GETFL);
    //获取文件描述符当前的状态标志

    int new_option = old_option | O_NONBLOCK;
    //在原有状态标志基础上添加非阻塞标志

    fcntl(fd, F_SETFL, new_option);
    //重新设置回去

    return old_option;
    //返回原来的状态标志，以便之后恢复
}

void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    //向 epoll 注册 fd
    epoll_event event;
    event.data.fd = fd;
    //创建事件对象

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    //EPOLLIN:监听可读事件
    //EPOLLET:边缘触发模式
    //EPOLLRDHUP:监听对方关闭连接事件

    //根据触发模式设置事件类型
    if (one_shot)
        event.events |= EPOLLONESHOT;
    //一个 socket 触发一次事件后，除非你手动重置，否则不会再次触发
    //适用于多线程处理同一 socket 的情况，防止多个线程同时处理同一 socket 导致问题

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //把 fd 加入 epoll 监听集合

    setnonblocking(fd);
    //把 fd 设成非阻塞
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    //从 epoll 监听集合删除 fd

    close(fd);
    //关闭 fd
}

void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    //“重置监听状态”
    epoll_event event;
    event.data.fd = fd;
    //重新组织事件

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    //ev 一般传：EPOLLIN 或 EPOLLOUT

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    //提交修改
}

int http_conn::m_user_count = 0;
//当前连接总数

int http_conn::m_epollfd = -1;
//整个服务器共用的 epoll 实例 fd

//静态成员初始化
void http_conn::initmysql_result(connection_pool *connPool)
{
    //从数据库里把用户名和密码全部读出来，放进全局 users map
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
    //从连接池取一个现成连接
    //connectionRAII 是个 RAII 封装类

    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
        //向 MySQL 发送 SQL 查询失败，记录错误日志
        return;
    }

    MYSQL_RES *result = mysql_store_result(mysql);
    //从 MySQL 连接获取查询结果

    if (result == NULL)
        return;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
        //row[0] 是 username
        //row[1] 是 passwd
    }
    //把查询结果的每一行的用户名和密码存入全局 users map

    mysql_free_result(result);
}

void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname)
{
    //外部初始化连接
    //这是“接受新连接后”的初始化入口
    m_sockfd = sockfd;
    m_address = addr;
    //记录 socket 和客户端地址

    addfd(m_epollfd, sockfd, true, TRIGMode);
    m_user_count++;
    //注册到 epoll 监听集合，并更新用户计数

    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;
    //设置站点根目录与日志开关

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());
    //把 std::string 转成 C 风格字符串，保存到成员字符数组里

    init();
    //上面先初始化“连接外部信息”，这里再初始化“连接内部状态机和缓冲区”
}

void http_conn::init()
{
    //重置连接内部状态
    mysql = NULL;
    //当前请求没绑定数据库连接

    bytes_to_send = 0;
    //还要发多少字节

    bytes_have_send = 0;
    //已经发了多少字节

    m_check_state = CHECK_STATE_REQUESTLINE;
    //HTTP 解析从“请求行”开始

    m_linger = false;
    //默认不保持长连接

    m_method = GET;
    //默认请求方法是 GET

    m_url = 0;
    m_version = 0;
    m_host = 0;
    //这些都是指向读缓冲区中某一段位置的指针，先清空

    m_content_length = 0;
    //请求体长度先清零

    m_start_line = 0;
    //当前要解析的行起始位置

    m_checked_idx = 0;
    //已经检查到读缓冲区的哪个位置

    m_read_idx = 0;
    //读缓冲区总共读入了多少字节

    m_write_idx = 0;
    //写缓冲区当前写到哪里

    cgi = 0;
    //默认不启用 CGI

    m_state = 0;
    timer_flag = 0;
    improv = 0;
    //这些一般是和线程池/定时器相关的标志位

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
    //缓冲区清零
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];

        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

bool http_conn::read_once()
{
    //读取客户端数据
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        //读缓冲区满了，无法继续读入
        return false;
    }

    int bytes_read = 0;

    if (0 == m_TRIGMode)
    {
        //LT 模式
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        //m_read_buf + m_read_idx表示从缓冲区当前末尾开始追加

        if (bytes_read <= 0)
        {
            return false;
            //0：对端关闭
            //<0：读失败
        }

        m_read_idx += bytes_read;
        //总已读字节数增加

        return true;
    }
    else
    {
        //ET 模式
        //ET 模式下不能只读一次，要一直读到“内核缓冲区空了”为止
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                //这两种错误表示“没有数据可读了”，不是读错误
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }

            m_read_idx += bytes_read;
        }
        return true;
    }
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //这个函数处理第一行
    m_url = strpbrk(text, " \t");
    //在字符串中找到第一个空格或 tab
    //text 开头是方法
    //m_url 指向方法后面的空白处

    if (!m_url)
    {
        return BAD_REQUEST;
        //请求行不合法
    }

    *m_url++ = '\0';
    char *method = text;
    //截断 method
    //text 单独成了 "GET"
    //m_url 往后移一位，准备指向 URL

    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
        //表示后面按带请求体的动态处理方式走
    }
    else
        return BAD_REQUEST;
    //只支持 GET 和 POST 方法

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    //拆成三段：method、url、version

    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    //只支持 HTTP/1.1

    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    //兼容完整 URL

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //URL 必须以 / 开头

    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    //默认首页跳转

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
    //请求行解析完了，但整个请求还没完，所以返回 NO_REQUEST。
    //并告诉状态机：下一步去解析 header
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    //解析请求头
    if (text[0] == '\0')
    {
        //空行：头部结束
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
        //一个完整 HTTP 请求已经拿到
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        //Connection 头
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
            //后面响应发完后，不立即关闭连接
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
        //解析 POST 请求体长度
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
        //记录主机名字段
    }
    else
    {
        LOG_INFO("oop! unknow header: %s", text);
        //如果不是这几种已知头，就只记日志，不报错
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    //解析请求体
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        //判断 body 是否完整到达
        //m_checked_idx：当前已经解析到头部结束位置
        //m_content_length：body 应该有多长

        text[m_content_length] = '\0';
        //把 body 结尾补成字符串结束符

        m_string = text;
        //保存请求体
        //m_string 就是 POST 提交的数据字符串

        return GET_REQUEST;
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    //“HTTP 解析总控函数
    //整体读取并解析请求
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    //初始变量

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK))
    {
        //主循环条件
        //只要满足下面任一情况，就继续循环：
        //当前正在解析 body，并且行状态 OK
        //或者从缓冲区成功解析出一行

        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        //取当前行
        //get_line() 一般就是返回 m_read_buf + m_start_line,也就是当前行起始地址
        //m_start_line 更新为下一行的起始位置

        switch (m_check_state)
        {
        //状态机分发
        case CHECK_STATE_REQUESTLINE:
        {
            //解析请求行
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            //解析请求头
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
                return do_request();
            //如果头部解析完后，整个请求已经完整了，就直接进入do_request()
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            //解析请求体
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();

            line_status = LINE_OPEN;
            //因为 body 不是按行继续切分的，所以这里强制让外层循环下次不要再靠“读到一行”推进，而是按 body 逻辑判断
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    //真正处理请求
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/');
    //拼接根目录
    //找到 URL 中最后一个 /

    if (cgi == 1 && p && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        //处理 CGI 登录注册
        char flag = m_url[1];
        //取标志位

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);
        //去掉前两位构造真实 URL
        //如果 m_url 是某种带业务前缀的路径，它把前两位去掉，再拼到真实文件路径里

        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';
        //从 POST 请求体中提取用户名密码
        //默认请求体格式固定是：user=123&passwd=456
        //从下标 5 开始跳过 "user="
        //找到 & 符号，& 前面是用户名
        //再跳过 "&passwd="，后面是密码

        if (*(p + 1) == '3')
        {
            //注册逻辑
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username,passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            //构造 SQL

            if (users.find(name) == users.end())
            {
                //检查是否重名
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                if (!res)
                    users.insert(pair<string, string>(name, password));
                m_lock.unlock();
                //如果不重名，就插入数据库，更新全局 users 表

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
                //根据插入结果跳转页面
            }
            else
            {
                strcpy(m_url, "/registerError.html");
            }

            free(sql_insert);
        }
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }

        if (flag == '3' || flag == '2')
            strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    if (p && *(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (p && *(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (p && *(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (p && *(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (p && *(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    //特殊页面映射
    //本质上是一个“简陋路由表”
    //直接把原 URL 拼到网站根目录后面

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    //检查文件合法性

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    //检查文件权限

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    //检查是否是目录
    //如果目标是目录，不允许直接当文件发

    int fd = open(m_real_file, O_RDONLY);
    //open打开文件只读

    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
    //用 mmap 映射文件到内存空间
}

void http_conn::unmap()
{
    //解除 mmap 映射
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
    //发送完后把文件映射释放掉
}

bool http_conn::write()
{
    int temp = 0;
    //响应 HTTP 请求

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
        //没数据可发了，重置 EPOLLIN 监听，准备下一次请求
    }

    while (1)
    {
        //循环 writev
        temp = writev(m_sockfd, m_iv, m_iv_count);
        //writev 是分散写，可以一次写多个缓冲区

        if (temp < 0)
        {
            //写失败
            if (errno == EAGAIN)
            {
                //当前 socket 发送缓冲区满了，还不能继续发
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            //就真正失败，解除映射并返回 false
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        //更新发送进度
        //一次 writev 不一定把全部响应发完，所以必须记录进度

        if (bytes_have_send >= (int)m_iv[0].iov_len)
        {
            //当已经发送的总字节数，大于等于响应头长度，就说明头已经发完了
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
            //头部长度设为 0
            //文件内容起始位置往后偏移到“还没发的地方”
        }
        else
        {
            //头部都还没发完
            //说明上次只发掉了头部的一部分，所以头部起始地址也要后移
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            //全部发送完成
            //解除文件映射
            unmap();
            //解除文件映射

            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            //如果 keep-alive，则重置状态继续复用连接
            if (m_linger)
            {
                init();
                return true;
            }
            //否则返回 false，外层通常会关闭连接
            else
            {
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char *format, ...)
{
    //向写缓冲区追加格式化内容
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    //空间检查
    //缓冲区满了就失败

    va_list arg_list;
    va_start(arg_list, format);

    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    //可变参数格式化
    //vsnprintf 会把格式化后的字符串写到 m_write_buf 当前末尾

    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
        //长度检查
    }

    m_write_idx += len;
    va_end(arg_list);
    //更新尾指针

    LOG_INFO("request:%s", m_write_buf);
    //记录日志

    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
    //构造状态行
}

bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_blank_line();
    //构造头部
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-length:%d\r\n", content_len);
    //Content-Length
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
    //Content-Type
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
    //Connection
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
    //空行
    //HTTP 头部结束必须空一行
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
    //响应正文
}

bool http_conn::process_write(HTTP_CODE ret)
{
    //根据处理结果组织响应
    //这个函数把“业务处理结果”变成 HTTP 响应
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
        //返回 500
    }
    case BAD_REQUEST:
    {
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
            return false;
        break;
        //返回 400
    }
    case NO_RESOURCE:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
        //返回 404
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
        //返回 403
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
        break;
    }
    default:
        return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process()
{
    //总控入口
    HTTP_CODE read_ret = process_read();

    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
        //如果请求还没收完整，就继续监听可读，等下一批数据
    }

    bool write_ret = process_write(read_ret);
    //组织响应

    if (!write_ret)
    {
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
    //如果成功构造出响应，就把监听事件改成可写，等发送
    //如果失败就关闭连接
}

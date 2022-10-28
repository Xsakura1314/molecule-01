#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

// 当浏览器出现连接重置时，可能是网站根目录出错或 http 响应格式出错或者访问的文件中内容完全为空
const char *doc_root = "/home/xsakura/project/molecule-01/root";

int HttpConn::m_epollfd = -1;       // 所有的 socket 上的事件都被注册同一个 epoll 对象
int HttpConn::m_user_count = 0;     // 统计用户的数量

// ---------- 一系列操作文件描述符的操作 ----------

// 对文件描述符设置非阻塞
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 在 http_conn.cpp 中定义，添加文件描述符到 epollfd 中
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    // 处理有数据可读、对方关闭写操作这两个情况
    event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 在 http_conn.cpp 中定义，从 epollfd 中删除文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改描述符，重置 socket 上的 EPOLLONESHOT 事件，确保下一次可读时，EPOLLIN 事件能够被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}



// ---------- http 请求的基本操作函数 ----------

// 初始化新接受的连接，内部操作
void HttpConn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始化状态为解析请求行

    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;

    m_content_length = 0;
    m_linger = false;
    m_host = 0;

    m_write_idx = 0;
    bytes_have_send = 0;
    bytes_to_send = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 初始化连接，外部调用初始化套接字地址
void HttpConn::init(int sockfd, const sockaddr_in &address) {
    m_sockfd = sockfd;
    m_address = address;

    // 端口复用
    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到 epoll 对象中
    addfd(m_epollfd, sockfd, true);

    // 用户总数加一
    m_user_count++;

    // 初始化基本信息
    init();
}

// 关闭连接
void HttpConn::close_conn() {
    if (m_sockfd != -1) {
        char client_info[16] = {0};
        printf("%s 关闭连接", inet_ntop(AF_INET, &m_address.sin_addr.s_addr, client_info, 16));
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // 用户数量减一
    }
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
bool HttpConn::read() {
    // 读取的数据已经超过了设定的读缓冲区大小
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    // 下次读取的总长度要根据数组中已经存在的数据长度读入
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0) {
        return false;
    }

    printf("读取到了数据\n");
    printf("%s", m_read_buf);

    return true;
}

// 写 http 响应
bool HttpConn::write() {
    // printf("写入数据功能正在实现...\n");
    int temp = 0;

    printf("%d %s\n", m_sockfd, m_url);

    if (bytes_to_send == 0) {
        // 要发送的字节为 0，这一次响应结束
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp < 0) {
            // 在非阻塞读取中，在没有数据读取后会有 EAGAIN 错误
            // 如果 TCP 写缓冲没有空间，则等待下一轮 EPOLLOUT 事件，
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len) {
            // 如果响应头信息发送完毕
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            // 从已发送的位置往后继续发送
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0) {
            // 数据发送完毕
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger) {
                init();
                return true;
            }
            else {
                return false;
            }
        }
    }

    return false;
}

// 有线程池中的工作线程调用，这是处理 http 请求的入口函数
void HttpConn::process() {

    // 解析 HTTP 请求，根据返回的状态值判断 HTTP 报文是否完整
    HTTP_CODE read_ret = process_read();

    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}


// ---------- 一系列读取请求报文的函数 ----------
// 根据请求，建立磁盘资源到内存的映射

// 根据请求，建立磁盘资源到内存的映射
HttpConn::HTTP_CODE HttpConn::do_request() {
    // 获取 m_real_file 文件的相关的状态信息，-1 失败，0 成功
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    // 是否有读权限
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 解除映射，对内存映射区进行 munmap 操作
void HttpConn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


// 从状态机，用于分析出一行内容，判断依据时 \r\n
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
HttpConn::LINE_STATUS HttpConn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n') {
            if (m_read_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析 http 请求行，获得请求方法，目标 url 及 http 版本号
HttpConn::HTTP_CODE HttpConn::parse_request_line(char *text) {
    // strpbrk：返回 accept 串中任一字符在 s 串中最先出现的位置
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
    }
    else {
        return BAD_REQUEST;
    }
    // strspn：从 s 串头部开始计算连续包含任一 accept 串中字符的总长度
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // m_url 中记录相对服务器中文件的地址

    // strncasecmp：指定 n 个长度的比较
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        // strchr：返回 s 中 c 第一次出现的位置
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 7) == 0) {
        m_url += 8;
        // strchr：返回 s 中 c 第一次出现的位置
        m_url = strchr(m_url, '/');
    }

    // 第二种情况时针对上面两种条件判断都没执行成功，访问路径不正确
    if (!m_url || m_url[0] != '/') return BAD_REQUEST;

    // 当 url 只为 / 时，显示界面
    if (strlen(m_url) == 1) {
        strcat(m_url, "index.html");
    }
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析 http 请求的头部信息
HttpConn::HTTP_CODE HttpConn::parse_headers(char *text) {
    // 遇到空行，表示头部信息读取完毕
    if (text[0] == '\0') {
        // 如果 http 请求有消息体，则需要读取 m_content_length 字节长度的消息体
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明已经得到一个完整的 http 请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理 Connection 头部字段，判断是否保持长连接
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0) {
        // 获取请求体的长度
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {
        // 获取 Host 头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        // 无法解析首部字段名
        printf("oop!unknow header: %s\n", text);
    }
    return NO_REQUEST;
}

// 判断 http 请求是否被完整读入
HttpConn::HTTP_CODE HttpConn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析 http 请求自动机
HttpConn::HTTP_CODE HttpConn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char *text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        // 1. 解析一行完整的数据
        // 2. 解析为请求体，也是完整的一行
        text = get_line();
        m_start_line = m_checked_idx;

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST)
                    return do_request();
                line_status = LINE_OPEN;
                break;
            }

            default:
                return INTERNAL_ERROR;
        }
    }

    return NO_REQUEST;
}



// ---------- 一系列生成相应响应报文的函数 ----------

// 将响应内容写入写缓冲区中
bool HttpConn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) return false;
    va_list arg_list;   // 指针类型，指向参数列表中的参数
    va_start(arg_list, format); // 指向第一个参数
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - m_read_idx - 1, format, arg_list);   // s 存放生成的字符串，max_len 最大字符串长度，format 输出格式的字符串，arg 参数列表指针
    if (len >= WRITE_BUFFER_SIZE - m_write_idx - 1) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list); // 回收指针
    printf("回复\n");
    printf("%s", m_write_buf);
    return true;
}

// 生成响应行
bool HttpConn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 内容长度
bool HttpConn::add_content_length(int content_length) {
    return add_response("Content-Length:%d\r\n", content_length);
}
// 响应内容类型
bool HttpConn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
// 是否保持长连接
bool HttpConn::add_linger() {
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
// 添加空行，分割响应头和内容
bool HttpConn::add_blank_line() {
    return add_response("%s", "\r\n");
}
// 生成响应头
bool HttpConn::add_headers(int content_length) {
    add_content_length(content_length);
    add_linger();
    add_blank_line();
    return true;
}

// 生成响应内容
bool HttpConn::add_content(const char *content) {
    return add_response("%s", content);
}

// 根据解析的请求生成相应响应报文
bool HttpConn::process_write(HTTP_CODE ret) {

    switch (ret) {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        // 针对没资源和请求错误
        case BAD_REQUEST: case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
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
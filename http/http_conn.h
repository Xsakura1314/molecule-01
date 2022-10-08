#ifndef HTTP_CONN_H_
#define HTTP_CONN_H_

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/uio.h>

class HttpConn {
public:
    // HTTP 请求方式，目前只支持 GET
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE     ：   当前正在分析请求行
        CHECK_STATE_HEADER          ：   当前正在分析头请求头部
        CHECK_STATE_CONTENT         ：   当前正在分析请求体
     */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    /*
        服务器处理 HTTP 请求的可能结果，报文解析的结果
        NO_REQUEST          ：      请求不完整，需要继续读取客户数据
        GET_REQUEST         ：      表示获得了一个完成的客户请求
        BAD_REQUEST         ：      表示客户请求语法错误
        NO_RESOURCE         ：      表示服务器没有资源
        FORBIDDEN_REQUEST   ：      表示客户对资源没有足够的访问权限
        FILE_REQUEST        ：      文件请求，获取文件成功
        INTERNAL_ERROR      ：      表示服务器内部错误
        CLOSED_CONNECTION   ：      表示客户端已经关闭连接了
     */
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    /*
        从状态机的三种可能状态，即行的读取状态
        LINE_OK     ：      读取到一个完整的行
        LINE_BAD    ：      行出错
        LINE_OPEN   ：      行数据尚不完整
     */
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    static const int FILENAME_LEN = 200;        // 实际文件名长度
    static const int READ_BUFFER_SIZE = 2048;   // 定义读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 定义写缓冲区的大小
    static int m_epollfd;                       // 所有的 socket 上的事件都被注册同一个 epoll 对象
    static int m_user_count;                    // 统计用户的数量

public:
    HttpConn() {}
    ~HttpConn() {}

    void init(int sockfd, const sockaddr_in& address);   // 初始化新接收的连接
    void close_conn();                                   // 关闭连接
    void process();                                      // 用户处理客户端请求
    bool read();                                         // 循环读取客户数据，直到无数据可读或者对方关闭连接
    bool write();                                        // 向客户端发送数据

private:
    // 记录 HTTP 请求报文中相关的信息

    char m_real_file[FILENAME_LEN];         // 本地资源文件路径
    struct stat m_file_stat;                // 存储文件状态
    char* m_file_address;                   // 内存映射地址

    char* m_url;                            // 请求行，请求地址
    METHOD m_method;                        // 请求行，请求方法
    char* m_version;                        // 请求行，请求协议,只支持 HTTP1.1
    long m_content_length;                  // 请求头，请求体的长度
    bool m_linger;                          // 请求头，保持长连接
    char* m_host;                           // 请求头，客户机信息
    char* m_string;                         // 存储请求头数据?

    int m_sockfd;                           // 客户端的套接字
    sockaddr_in m_address;                  // 客户端的信息

    char m_read_buf[READ_BUFFER_SIZE];      // 读缓冲区
    int m_read_idx;                         // 表示读缓冲区中读入的客户端的最后一个字节的下一个位置。因为数据可能不是一次性读完
    int m_checked_idx;                      // 当前正在解析的字符正在读缓冲区的位置
    int m_start_line;                       // 当前正在解析的行的起始位置

    char m_write_buf[WRITE_BUFFER_SIZE];    // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数
    struct iovec m_iv[2];                   // 用于 writev 函数。0 是响应头，1 是内容
    int m_iv_count;                         // 分散的文件个数
    int bytes_to_send;                      // 将要发送的字节
    int bytes_have_send;                    // 已经发送的字节

    CHECK_STATE m_check_state;              // 主状态机当前所处的位置

private:
    void init();                                // 初始化新接受的连接，内部操作

    LINE_STATUS parse_line();                   // 解析具体的行
    char* get_line() { return m_read_buf + m_start_line; }; // 返回行

    HTTP_CODE parse_request_line(char* text);   // 解析请求行
    HTTP_CODE parse_headers(char* text);        // 解析请求头
    HTTP_CODE parse_content(char* text);        // 解析请求体
    HTTP_CODE process_read();                   // 解析 HTTP 请求

    HTTP_CODE do_request();                     // 根据请求，建立磁盘资源到内存的映射
    void unmap();                               // 解除映射，对内存映射区进行 munmap 操作

    bool add_response(const char* format, ...);             // 将响应内容写入写缓冲区中
    bool add_status_line(int status, const char* title);    // 生成响应行
    bool add_content_length(int content_length);            // 响应头，内容长度
    bool add_content_type();                                // 响应头，内容类型
    bool add_linger();                                      // 响应头，是否保持长连接
    bool add_blank_line();                                  // 响应头，添加空行，分割响应头和内容
    bool add_headers(int content_length);                   // 生成响应头
    bool add_content(const char* content);                  // 生成响应内容
    bool process_write(HTTP_CODE ret);                      // 根据解析的请求生成相应响应报文
};

#endif // HTTP_CONN_H_
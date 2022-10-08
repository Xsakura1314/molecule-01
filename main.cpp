#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数


//这三个函数在 http_conn.cpp 中定义，改变链接属性
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern void setnonblocking(int fd);

static int epollfd = 0;

// 添加信号捕捉
void addsig(int sig, void (handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    // 重新调用被该信号终止的系统调用
    if (restart == true) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char const* argv[]) {
    // printf("一切正待开始🎉...\n");

    // 判断参数是否输入正确
    if (argc <= 1) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 对 SIGPIE 信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    ThreadPool<HttpConn>* pool = NULL;
    try {
        pool = new ThreadPool<HttpConn>;
    }
    catch (...) {
        exit(-1);
    }

    // 创建用户信息数组
    HttpConn* users = new HttpConn[MAX_FD];
    assert(users);

    // 创建 socket 套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    // 端口复用。设置 socket 选项，可以立刻重用 socket 地址
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    // 绑定具体的 socket 地址
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    // 监听 socket
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 创建 epoll 事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);

    // 将监听的文件描述符添加到 epoll 对象中
    addfd(epollfd, listenfd, false);
    HttpConn::m_epollfd = epollfd;

    bool stop_server = false;

    while (!stop_server) {
        // 阻塞等待事件到来
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        // 如果非系统中断出现返回值为负数的情况，则跳出循环
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                // 表示有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);

                if (connfd < 0) {
                    printf("%s:errno is:%d", "accept error", errno);
                    continue;
                }

                if (HttpConn::m_user_count >= MAX_FD) {
                    // 目前连接数满了
                    // 可以回写用户端一个信息
                    close(connfd);
                    continue;
                }
                // 根据 connfd 初始化相应的客户信息
                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或者错误等事件
                users[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    // 一次性把所有的数据读完, 将该事件放入请求队列
                    pool->append(users + sockfd);
                }
                else {
                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    // 一次性写完所有的数据
                    users->close_conn();
                }
            }
        }
    }

    printf("服务程序关闭");
    // 关闭文件描述符
    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;

    return 0;
}
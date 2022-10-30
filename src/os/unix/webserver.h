#ifndef WEBSERVER_H_
#define WEBSERVER_H_

#include <cstdio>
#include <cstring>
#include <cassert>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "../../conf/config.h"
#include "../../core/threadpool/threadpool.h"
#include "../../core/timer/lst_timer.h"
#include "../../http/http_conn.h"

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(Config &config);

    // 初始化线程池
    void thread_pool();
    // 事件监听
    void event_listen();
    // 处理事件
    void event_loop();

    // 处理用户的信息
    bool deal_client_data();
    // 处理信号
    bool deal_with_signal(bool &timeout, bool &stop_server);
    // 读取用户请求
    void deal_with_read(int sockfd);
    // 响应用户请求
    void deal_with_write(int sockfd);

    // 将用户加入定时器
    void init_timer(int connfd, struct sockaddr_in client_address);
    // 更新定时器
    void adjust_timer(util_timer *timer);
    // 处理超时用户
    void expire_timer(util_timer *timer, int sockfd);

public:
    char *m_root;                       // 资源文件根目录

    int m_listenfd;                     // sockt 套接字
    int m_epollfd;                      // epoll 套接字
    int m_pipefd[2];                    // 管道套接字

    epoll_event *events;                // 事件数组
    client_data *users_timer;
    HttpConn *users;
    ThreadPool<HttpConn> *m_pool;
    Utils utils;
    Config config;
};

#endif // WEBSERVER_H_
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
#include "./timer/lst_timer.h"

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

//这三个函数在 http_conn.cpp 中定义，改变链接属性
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern void setnonblocking(int fd);

static int pipefd[2];                // 管道，存储信号
static sort_timer_lst timer_lst;    // 排序定时链表
static int epollfd = 0;             // epoll 文件描述符，用于监听其他文件描述符

// 信号处理函数
void sig_handler(int sig) {
    //为保证函数的可重入性，保留原来的 errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

// 添加信号捕捉
void addsig(int sig, void (handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    // 重新调用被该信号终止的系统调用
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时以不断触发 SIGALRM 信号
void timer_handler() {
    timer_lst.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数，删除非活动连接在 socket 上注册时间，并关闭
void cb_func(client_data *user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    HttpConn::m_user_count--;
}

int main(int argc, char const *argv[]) {
    // printf("一切正待开始🎉...\n");

    // 判断参数是否输入正确
    if (argc <= 1) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 不对 SIGPIE 信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    ThreadPool<HttpConn> *pool = NULL;
    try {
        pool = new ThreadPool<HttpConn>;
    }
    catch (...) {
        return 1;
    }

    // 创建用户信息数组
    HttpConn *users = new HttpConn[MAX_FD];
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
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
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

    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    bool stop_server = false;

    client_data *users_timer = new client_data[MAX_FD];

    bool timeout = false;
    alarm(TIMESLOT);


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
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);

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

                // 初始化 client_data 数据
                // 创建定时器，设置回调函数和超时时间，绑定用户数据，讲定时器添加到链表中
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 30 * TIMESLOT;

                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                users_timer[connfd].timer = timer;

                timer_lst.add_timer(timer);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或者错误等事件
                // users[sockfd].close_conn();

                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                if (timer) {
                    timer_lst.del_timer(timer);
                }

            }
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                // 监听信号
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {
                    continue;
                }
                else if (ret == 0) {
                    continue;
                }
                else {
                    for (int i = 0; i < ret; ++i) {
                        switch (signals[i]) {
                            case SIGALRM:
                            {
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else if (events[i].events & EPOLLIN) {
                util_timer *timer = users_timer[sockfd].timer;
                // 客户端发送请求
                if (users[sockfd].read()) {
                    // 一次性把所有的数据读完, 将该事件放入请求队列
                    pool->append(users + sockfd);

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                }
                else {
                    // users[sockfd].close_conn();
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if (events[i].events & EPOLLOUT) {
                // 响应客户端请求
                util_timer *timer = users_timer[sockfd].timer;

                // 错误代码 !users[sockfd].write() 没传输完成就关闭了连接，导致请求有问题
                if (users[sockfd].write()) {
                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                }
                else {
                    // 数据传输完毕，关闭连接
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }

        // 处理完所有的事件后再判断Z
        if (timeout) {
            // 周期结束，清除过期对象
            timer_handler();
            timeout = false;
        }
    }

    printf("服务程序关闭");
    // 关闭文件描述符
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;

    return 0;
}
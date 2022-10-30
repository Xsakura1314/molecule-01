#ifndef LST_TIMER_H_
#define LST_TIMER_H_

#include <cstring>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "../../http/http_conn.h"

class util_timer;

struct client_data {
    int sockfd;
    sockaddr_in address;
    util_timer *timer;
};

class util_timer {
public:
    util_timer() :prev(NULL), next(NULL) {}
public:
    time_t expire;
    void (*cb_func)(client_data *);
    client_data *user_data;
    util_timer *prev;
    util_timer *next;
};

class sort_timer_lst {
public:
    sort_timer_lst() :head(NULL), tail(NULL) {}
    ~sort_timer_lst();
    // 加入节点
    void add_timer(util_timer *timer);
    // 更新时间
    void adjust_timer(util_timer *timer);
    // 删除指定对象
    void del_timer(util_timer *timer);
    // 遍历删除超时对象
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

private:
    util_timer *head;
    util_timer *tail;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

// 定时器回调函数，删除非活动连接在 socket 上注册时间，并关闭
void cb_func(client_data *user_data);

#endif // LST_TIMER_H_
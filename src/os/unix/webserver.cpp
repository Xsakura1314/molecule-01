#include "webserver.h"

WebServer::WebServer() {
    // http_conn类对象
    users = new HttpConn[config.MAX_FD];
    events = new epoll_event[config.MAX_EVENT_NUMBER];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[config.MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] events;
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::thread_pool() {
    m_pool = new ThreadPool<HttpConn>;
}

void WebServer::event_listen() {
    // 创建 socket 套接字
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config.port);

    int flag = 1;
    // 端口复用。设置 socket 选项，可以立刻重用 socket 地址
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    // 绑定具体的 socket 地址
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);

    // 监听 socket
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(config.TIMESLOT);

    // 创建 epoll 事件数组
    epoll_event events[config.MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    // 将监听的文件描述符添加到 epoll 对象中
    utils.addfd(m_epollfd, m_listenfd, false);
    HttpConn::m_epollfd = m_epollfd;

    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(config.TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::init_timer(int connfd, struct sockaddr_in client_address) {
    // 根据 connfd 初始化相应的客户信息
    users[connfd].init(connfd, client_address);

    // 初始化 client_data 数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，讲定时器添加到链表中
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * config.TIMESLOT;

    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    users_timer[connfd].timer = timer;

    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * config.TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
}

void WebServer::expire_timer(util_timer *timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if (timer) {
        utils.m_timer_lst.del_timer(timer);
    }
}

bool WebServer::deal_client_data() {
    struct sockaddr_in client_address;
    socklen_t client_addrlen = sizeof(client_address);
    int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlen);

    if (connfd < 0) {
        // printf("%s:errno is:%d", "accept error", errno);
        return false;
    }

    if (HttpConn::m_user_count >= config.MAX_FD) {
        // 目前连接数满了
        // 可以回写用户端一个信息
        close(connfd);
        return false;
    }
    init_timer(connfd, client_address);
    return true;
}

bool WebServer::deal_with_signal(bool &timeout, bool &stop_server) {
    // 监听信号
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1) {
        return false;
    }
    else if (ret == 0) {
        return false;
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
                    break;
                }
            }
        }
    }
    return true;
}

void WebServer::deal_with_read(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer;
    // 客户端发送请求
    if (users[sockfd].read()) {
        // 一次性把所有的数据读完, 将该事件放入请求队列
        m_pool->append(users + sockfd);

        //若有数据传输，则将定时器往后延迟3个单位
        //并对新的定时器在链表上的位置进行调整
        if (timer) {
            adjust_timer(timer);
        }
    }
    else {
        expire_timer(timer, sockfd);
    }
}

void WebServer::deal_with_write(int sockfd) {
    // 响应客户端请求
    util_timer *timer = users_timer[sockfd].timer;

    // 错误代码 !users[sockfd].write() 没传输完成就关闭了连接，导致请求有问题
    if (users[sockfd].write()) {
        //若有数据传输，则将定时器往后延迟3个单位
        //并对新的定时器在链表上的位置进行调整
        if (timer) {
            adjust_timer(timer);
        }
    }
    else {
        expire_timer(timer, sockfd);
    }
}

void WebServer::event_loop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(m_epollfd, events, config.MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            break;
        }

        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;

            // 新客户连接
            if (sockfd == m_listenfd) {
                bool flag = deal_client_data();
                if (false == flag) continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                expire_timer(timer, sockfd);
            }
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                // 处理信号
                bool flag = deal_with_signal(timeout, stop_server);
            }
            else if (events[i].events & EPOLLIN) {
                deal_with_read(sockfd);
            }
            else if (events[i].events & EPOLLOUT) {
                deal_with_write(sockfd);
            }
        }

        if (timeout) {
            utils.timer_handler();
            timeout = false;
        }
    }
}



#ifndef LST_TIMER_H_
#define LST_TIMER_H_

#include <time.h>
#include <arpa/inet.h>

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
    ~sort_timer_lst() {
        util_timer *tmp = head;
        while (tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    // 加入节点
    void add_timer(util_timer *timer) {
        if (!timer) {
            return;
        }
        if (!head) {
            head = tail = timer;
            return;
        }
        if (timer->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }
    // 更新时间
    void adjust_timer(util_timer *timer) {
        if (!timer) {
            return;
        }
        util_timer *tmp = timer->next;
        if (!tmp || (timer->expire < tmp->expire)) {
            // 如果是尾部节点或者当前节点还是小于后面节点的时间
            return;
        }
        if (timer == head) {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        else {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }
    // 删除指定对象
    void del_timer(util_timer *timer) {
        if (!timer) {
            return;
        }
        if ((timer == head) && (timer == tail)) {
            delete timer;
            head = tail = NULL;
            return;
        }
        if (timer == head) {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if (timer == tail) {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    // 遍历删除超时对象
    void tick() {
        if (!head) {
            return;
        }
        time_t cur = time(NULL);
        util_timer *tmp = head;
        while (tmp) {
            if (cur < tmp->expire) {
                break;
            }
            tmp->cb_func(tmp->user_data);
            head = tmp->next;
            if (head) {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(util_timer *timer, util_timer *lst_head) {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        while (tmp) {
            if (timer->expire < tmp->expire) {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if (!tmp) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    util_timer *head;
    util_timer *tail;
};

#endif // LST_TIMER_H_
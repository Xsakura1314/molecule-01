#ifndef LOCKER_H_
#define LOCKER_H_

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 信号量类
class Sem {
public:
    // 创建并初始化信号量
    Sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    // 按照指定个数创建并初始化信号量
    Sem(int num) {
        if (sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~Sem() {
        sem_destroy(&m_sem);
    }
    // 信号量减 1。信号量为 0 时阻塞
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    // 信号量加 1。当信号量大于 0 时，其他正在调用 sem_wait 等待信号量的线程将会被唤醒
    bool post() {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

// 互斥锁类
class Locker {
public:
    // 创建并初始化互斥锁
    Locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    ~Locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    // 获取互斥锁
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    // 释放互斥锁
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // 获取互斥锁
    pthread_mutex_t* get() {
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;
};

// 条件变量类
class Cond {
public:
    // 创建并初始化条件变量类
    Cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~Cond() {
        pthread_cond_destroy(&m_cond);
    }
    //等待条件变量
    bool wait(pthread_mutex_t* mutex) {
        int ret = 0;
        // 阻塞的时候，互斥锁会进行解锁
        // 当不阻塞的时候，会恢复互斥锁为加锁状态
        // pthread_mutex_lock(&mutex);
        ret = pthread_cond_wait(&m_cond, mutex);
        // pthread_mutex_unlock(&mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t* mutex, struct timespec t) {
        int ret = 0;
        // pthread_mutex_lock(&mutex);
        ret = pthread_cond_timedwait(&m_cond, mutex, &t);
        // pthread_mutex_unlock(&mutex);
        return ret == 0;
    }
    // 唤醒条件变量
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

#endif // LOCKER_H_
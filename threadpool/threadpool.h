#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"

template<typename T>
class ThreadPool {
public:
    // 初始化线程池
    // actor_model 
    // thread_number 线程池中线程的数量
    // max_requests 请求队列中最多允许的、等待处理的请求的数量
    ThreadPool(int thread_number = 8, int max_requests = 10000);
    ~ThreadPool();
    // 添加请求
    bool append(T* request);
private:
    // 工作线程运行函数，不断从工作队列中取出任务执行
    static void* worker(void* arg);
    void run();
private:
    int m_thread_number;        // 线程池中线程的数量
    int m_max_requests;         // 请求队列中允许的最大请求数
    pthread_t* m_threads;       // 描述线程池的数组，大小为 m_thread_number;
    std::list<T*> m_work_queue; // 请求队列
    Locker m_queue_locker;      // 保护请求队列的数组
    Sem m_queue_stat;           // 是否有任务需要处理
    bool m_stop;                 // 是否结束线程
};

template<typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests) {
    m_thread_number = thread_number;
    m_max_requests = max_requests;
    m_threads = NULL;
    m_stop = false;

    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    // 创建线程池
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    // 遍历初始化线程池
    for (int i = 0; i < thread_number; ++i) {
        printf("create the %dth thread\n", i);
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }

}

template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool ThreadPool<T>::append(T* request) {
    m_queue_locker.lock();
    if (m_work_queue.size() > m_max_requests) {
        // 当前请求队列中的请求数量已经超过了设定的最大值
        m_queue_locker.unlock();
        return false;
    }
    m_work_queue.push_back(request);
    m_queue_locker.unlock();
    m_queue_stat.post();
    return true;
}

template<typename T>
void* ThreadPool<T>::worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    pool->run();
    return pool;
}

template<typename T>
void ThreadPool<T>::run() {
    while (!m_stop) {
        // 等待任务到来
        m_queue_stat.wait();
        m_queue_locker.lock();
        if (m_work_queue.empty()) {
            m_queue_locker.unlock();
            continue;
        }

        T* request = m_work_queue.front();
        m_work_queue.pop_front();
        m_queue_locker.unlock();

        if (!request) {
            continue;
        }

        request->process();

    }
}
#endif // THREADPOOL_H_
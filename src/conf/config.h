#ifndef CONFIG_H_
#define CONFIG_H_

#include <unistd.h>
#include <cstdlib>

class Config
{
public:
    Config() {};

    // 解析命令行参数
    void parse_arg(int argc, char *argv[]);

    int port = 8808;        // 端口，默认 8808
    int thread_num = 8;     // 线程池内的线程数量, 默认 8

    const int MAX_FD = 65536;           //最大文件描述符
    const int MAX_EVENT_NUMBER = 10000; //最大事件数
    const int TIMESLOT = 5;             //最小超时单位
};

#endif // CONFIG_H_
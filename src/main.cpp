#include "./os/unix/webserver.h"

int main(int argc, char *argv[]) {
    // printf("一切正待开始🎉...\n");

    WebServer server;
    server.config.parse_arg(argc, argv);

    // 线程池
    server.thread_pool();
    // 监听
    server.event_listen();
    // 运行 
    server.event_loop();

    return 0;
}
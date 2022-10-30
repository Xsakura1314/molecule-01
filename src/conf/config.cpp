#include "config.h"

void Config::parse_arg(int argc, char *argv[]) {
    int opt;
    const char *str = "p:t:";
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 't':
                thread_num = atoi(optarg);
            default:
                break;
        }
    }
}


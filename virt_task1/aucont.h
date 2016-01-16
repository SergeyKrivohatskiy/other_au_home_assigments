#ifndef AUCONT_H
#define AUCONT_H
#include <netinet/in.h>
#include <string>

struct start_arguments {
    std::string image_path;
    std::string cmd;
    char const **cmd_args;
    size_t cmd_args_count;
    int cpu_limit;
    in_addr_t cont_ip;
    in_addr_t host_ip;
    bool net_enabled;
    bool daemonize;
    bool debug_enabled;
};

int aucont_start(start_arguments const &args);

struct stop_arguments {
    int pid;
    int signal;
    bool debug_enabled;
};

int aucont_stop(stop_arguments const &args);
int aucont_list();

struct exec_arguments {
    int pid;
    std::string cmd;
    char const **cmd_args;
    size_t cmd_args_count;
    bool debug_enabled;
};

int aucont_exec(exec_arguments const &args);


#endif // AUCONT_H

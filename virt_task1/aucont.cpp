#include "aucont.h"
#include <iostream>
#include <arpa/inet.h>

std::ostream& printDebug() {
    return std::cout << "Debug: ";
}

std::string to_string(in_addr_t ip) {
    char buffer[INET_ADDRSTRLEN + 1];
    inet_ntop(AF_INET, &ip, buffer, INET_ADDRSTRLEN);
    return buffer;
}

int aucont_start(start_arguments const &args) {
    if (args.debug_enabled) {
        printDebug() << "Cpu limit is " << args.cpu_limit << std::endl;
        printDebug() << "Container " << (args.daemonize ? "will" : "won't") << " be daemonized" << std::endl;
        printDebug() << "Network is " << (args.net_enabled ? "enabled" : "disabled") << std::endl;
        if (args.net_enabled) {
            printDebug() << "\tContainer IP is " << to_string(args.cont_ip) << std::endl;
            printDebug() << "\tHost IP is " << to_string(args.host_ip) << std::endl;
        }
        printDebug() << "image_path is '" << args.image_path << '\'' << std::endl;
        printDebug() << "cmd is '" << args.cmd << '\'' << std::endl;
        if (args.cmd_args_count) {
            printDebug() << "cmd_args:" << std::endl;
            for (size_t cmd_arg_idx = 0; cmd_arg_idx < args.cmd_args_count; ++cmd_arg_idx) {
                printDebug() << '\t' << cmd_arg_idx << ": '" << args.cmd_args[cmd_arg_idx] << '\'' << std::endl;
            }
        }
    }

    //TODO implementation
    return 0;
}


int aucont_stop(const stop_arguments &args) {
    if (args.debug_enabled) {
        printDebug() << "PID is " << args.pid << std::endl;
        printDebug() << "SIGNAL is " << args.signal << std::endl;
    }

    // TODO
    return 0;
}

int aucont_list() {

    // TODO
    return 0;
}

int aucont_exec(exec_arguments const &args) {
    if (args.debug_enabled) {
        printDebug() << "PID is " << args.pid << std::endl;
        printDebug() << "cmd is '" << args.cmd << '\'' << std::endl;
        if (args.cmd_args_count) {
            printDebug() << "cmd_args:" << std::endl;
            for (size_t cmd_arg_idx = 0; cmd_arg_idx < args.cmd_args_count; ++cmd_arg_idx) {
                printDebug() << '\t' << cmd_arg_idx << ": '" << args.cmd_args[cmd_arg_idx] << '\'' << std::endl;
            }
        }
    }

    // TODO
    return 0;
}

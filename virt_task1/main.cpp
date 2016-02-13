#include <iostream>
#include <string>
#include "aucont.h"
#include "error_codes.h"
#include "optionparser.h"
#include "error_codes.h"
#include "stdlib.h"
#include <signal.h>
#include <iostream>
#include <arpa/inet.h>

void print_option_error_message(option::Option const &opt, std::string const &err_msg) {
    std::cerr << "Option '" << opt.name  << "' " << err_msg;
}

void print_arg_error_message(std::string const &arg_name, std::string const &err_msg) {
    std::cerr << "Option '" << arg_name  << "' " << err_msg;
}

option::ArgStatus percent(const option::Option& option, bool print_err_msg) {
    char* endptr = 0;
    int percent;
    if (option.arg != nullptr) {
        percent = strtol(option.arg, &endptr, 10);
    }
    if (endptr != option.arg && *endptr == 0 && percent >= 0 && percent <= 100) {
      return option::ARG_OK;
    }

    if (print_err_msg) {
        print_option_error_message(option, "requires a numeric percent argument from 0 to 100\n");
    }
    return option::ARG_ILLEGAL;
}

option::ArgStatus ip(const option::Option& option, bool print_err_msg) {
    in_addr_t ip;
    if (option.arg != nullptr && inet_pton(AF_INET, option.arg, &ip) && ip != 0xFFFFFFFF) {
        return option::ARG_OK;
    }

    if (print_err_msg) {
        print_option_error_message(option, "requires an ip argument from 0.0.0.0 to 255.255.255.254\n");
    }
    return option::ARG_ILLEGAL;
}

enum  allOptionsIndex { UNKNOWN, HELP, DEBUG, DAEMONIZE, CPU_PERC, NET };
const option::Descriptor startUsage[] = {
    {UNKNOWN, 0, "" , "", option::Arg::None, "USAGE: ./aucont_start [options] IMAGE_PATH CMD [CMD_ARGS]\n\n"
                                             "Options:" },
    {HELP, 0, "h" , "help", option::Arg::None, "  --help, -h  \tprint usage." },
    {DEBUG, 0, "" , "debug", option::Arg::None, "  --debug  \tprint debug output." },
    {DAEMONIZE, 0, "d" , "daemonize", option::Arg::None, "  --daemonize, -d  \tdaemonize container." },
    {CPU_PERC, 0, "", "cpu", percent, "  --cpu CPU_PERCENT \tpercent of cpu resources "
                                                "allocated for container 0..100." },
    {NET, 0, "", "net", ip, "  --net IP \tcreate virtual network between host and container. "
                                           "IP ­- container ip address, IP+1 ­- host side ip address." },
    {0,0,0,0,0,0}
};

int aucont_start_main(int argc, char *argv[]) {
    if (argc) {
        argc -= 1;
        argv += 1;
    }
    option::Stats  stats(startUsage, argc, argv);
    option::Option options[stats.options_max], buffer[stats.buffer_max];
    option::Parser parse(startUsage, argc, argv, options, buffer);

    if (parse.error()) {
        return PARSE_OPTIONS_ERROR;
    }

    if (options[HELP] || parse.nonOptionsCount() < 2) {
        option::printUsage(std::cout, startUsage);
        return 0;
    }

    for (option::Option* opt = options[UNKNOWN]; opt; opt = opt->next()) {
        std::cout << "Unknown option: " << opt->name << "\n";
    }

    start_arguments args;
    args.image_path = parse.nonOption(0);
    args.cmd = parse.nonOption(1);
    args.cmd_args = const_cast<char*const*>(parse.nonOptions() + 1);
    args.cmd_args_count = parse.nonOptionsCount() - 2;
    if (options[CPU_PERC]) {
        args.cpu_limit = strtol(options[CPU_PERC].arg, nullptr, 10);
    } else {
        args.cpu_limit = 100;
    }
    if (options[NET]) {
        inet_pton(AF_INET, options[NET].arg, &args.cont_ip);
        args.host_ip = args.cont_ip;
        args.host_ip = ntohl(args.host_ip);
        args.host_ip += 1;
        args.host_ip = ntohl(args.host_ip);
    }
    args.net_enabled = options[NET];
    args.daemonize = options[DAEMONIZE];
    args.debug_enabled = options[DEBUG];

    return aucont_start(args);
}

const option::Descriptor stopUsage[] = {
    {UNKNOWN, 0, "" , "", option::Arg::None, "USAGE: ./aucont_stop [options] PID [SIGTERM]\n"
                                             "Kills root container process with signal and cleans up container resources\n\n"
                                             "Options:" },
    {HELP, 0, "h" , "help", option::Arg::None, "  --help, -h  \tprint usage." },
    {DEBUG, 0, "" , "debug", option::Arg::None, "  --debug  \tprint debug output." },
    {UNKNOWN, 0, "" , "", option::Arg::None,
        "PID ­ container init process pid in its parent PID namespace\n"
        "SIG_NUM ­ number of signal to send to container process\n"
                  "\tdefault is SIGTERM (15)" },
    {0,0,0,0,0,0}
};

int aucont_stop_main(int argc, char *argv[]) {
    if (argc) {
        argc -= 1;
        argv += 1;
    }
    option::Stats  stats(stopUsage, argc, argv);
    option::Option options[stats.options_max], buffer[stats.buffer_max];
    option::Parser parse(stopUsage, argc, argv, options, buffer);

    if (parse.error()) {
        return PARSE_OPTIONS_ERROR;
    }

    if (options[HELP] || parse.nonOptionsCount() < 1) {
        option::printUsage(std::cout, stopUsage);
        return 0;
    }

    for (option::Option* opt = options[UNKNOWN]; opt; opt = opt->next()) {
        std::cout << "Unknown option: " << opt->name << "\n";
    }

    stop_arguments args;
    char* endptr = 0;
    args.pid = strtol(parse.nonOption(0), &endptr, 10);
    if (endptr == parse.nonOption(0) || *endptr != 0) {
        print_arg_error_message("PID", "should be numeric\n");
        return PARSE_ARG_ERROR;
    }
    if (parse.nonOptionsCount() >= 2) {
        args.signal = strtol(parse.nonOption(1), &endptr, 10);
        if (endptr == parse.nonOption(1) || *endptr != 0) {
            print_arg_error_message("SIGNAL", "should be numeric\n");
            return PARSE_ARG_ERROR;
        }
    } else {
        args.signal = SIGTERM;
    }
    args.debug_enabled = options[DEBUG];

    return aucont_stop(args);
}

const option::Descriptor listUsage[] = {
    {UNKNOWN, 0, "" , "", option::Arg::None, "USAGE: ./aucont_list\n\n"
                                             "Options:" },
    {HELP, 0, "h" , "help", option::Arg::None, "  --help, -h  \tprint usage." },
    {0,0,0,0,0,0}
};

int aucont_list_main(int argc, char *argv[]) {
    if (argc) {
        argc -= 1;
        argv += 1;
    }
    option::Stats  stats(listUsage, argc, argv);
    option::Option options[stats.options_max], buffer[stats.buffer_max];
    option::Parser parse(listUsage, argc, argv, options, buffer);

    if (parse.error()) {
        return PARSE_OPTIONS_ERROR;
    }

    if (options[HELP]) {
        option::printUsage(std::cout, listUsage);
        return 0;
    }

    for (option::Option* opt = options[UNKNOWN]; opt; opt = opt->next()) {
        std::cout << "Unknown option: " << opt->name << "\n";
    }

    return aucont_list();
}

const option::Descriptor execUsage[] = {
    {UNKNOWN, 0, "" , "", option::Arg::None, "USAGE: ./aucont_exec PID CMD [ARGS]\n\n"
                                             "Options:" },
    {HELP, 0, "h" , "help", option::Arg::None, "  --help, -h  \tprint usage." },
    {DEBUG, 0, "" , "debug", option::Arg::None, "  --debug  \tprint debug output." },
    {UNKNOWN, 0, "" , "", option::Arg::None,
        "PID ­ container init process pid in its parent PID namespace\n"
        "CMD ­ command to run inside container\n"
        "ARGS ­ arguments for CMD" },
    {0,0,0,0,0,0}
};

int aucont_exec_main(int argc, char *argv[]) {
    if (argc) {
        argc -= 1;
        argv += 1;
    }
    option::Stats  stats(execUsage, argc, argv);
    option::Option options[stats.options_max], buffer[stats.buffer_max];
    option::Parser parse(execUsage, argc, argv, options, buffer);

    if (parse.error()) {
        return PARSE_OPTIONS_ERROR;
    }

    if (options[HELP] || parse.nonOptionsCount() < 1) {
        option::printUsage(std::cout, execUsage);
        return 0;
    }

    for (option::Option* opt = options[UNKNOWN]; opt; opt = opt->next()) {
        std::cout << "Unknown option: " << opt->name << "\n";
    }

    exec_arguments args;
    char* endptr = 0;
    args.pid = strtol(parse.nonOption(0), &endptr, 10);
    if (endptr == parse.nonOption(0) || *endptr != 0) {
        print_arg_error_message("PID", "should be numeric\n");
        return PARSE_ARG_ERROR;
    }
    args.cmd = parse.nonOption(1);
    args.cmd_args = const_cast<char*const*>(parse.nonOptions() + 1);
    args.cmd_args_count = parse.nonOptionsCount() - 2;
    args.debug_enabled = options[DEBUG];


    return aucont_exec(args);
}

/***********************************************/
/* Command strings *****************************/
/***********************************************/
static const std::string START_CMD("start");
static const std::string STOP_CMD("stop");
static const std::string LIST_CMD("list");
static const std::string EXEC_CMD("exec");
/***********************************************/

void print_aucont_usage_string() {
    std::cerr << "usage: aucont cmd cmd_args" << std::endl
              << "where cmd is" << std::endl
              << START_CMD << '|' << STOP_CMD << '|'
              << LIST_CMD << '|' << EXEC_CMD << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_aucont_usage_string();
        return INVALID_ARGS_ERROR;
    }

    std::string cmd(argv[1]);
    argv[1] = argv[0];

    if (cmd == START_CMD) {
        return aucont_start_main(argc - 1, argv + 1);
    }
    if (cmd == STOP_CMD) {
        return aucont_stop_main(argc - 1, argv + 1);
    }
    if (cmd == LIST_CMD) {
        return aucont_list_main(argc - 1, argv + 1);
    }
    if (cmd == EXEC_CMD) {
        return aucont_exec_main(argc - 1, argv + 1);
    }

    std::cerr << "command \"" << cmd << "\" not found" << std::endl;
    print_aucont_usage_string();
    return INVALID_ARGS_ERROR;
}

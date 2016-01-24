#include "aucont.h"
#include "error_codes.h"
#include <iostream>
#include <algorithm>
#include <arpa/inet.h>
#include <sys/types.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <vector>
#include <memory>
#include <fstream>

class aucont_exception: public std::exception
{
public:
    aucont_exception(std::string const &what_str):
        what_str(what_str)
    {
    }

    const char* what() const throw()
    {
        return what_str.c_str();
    }

private:
    std::string what_str;
};

static char const *SEM_NAME = "/aucont_pids_storage_semaphore";
static char const *PIDS_FILE_NAME = "aucont_pids";

sem_t* lock(sem_t *sem) {
    if (sem_wait(sem) == -1) {
        throw(aucont_exception("Failed to wait named semaphore"));
    }
    return sem;
}

void unlock(sem_t *sem) {
    if (sem_post(sem) == -1) {
        throw(aucont_exception("Failed to wait named semaphore"));
    }
}

class pids_storage {
public:
    pids_storage() {
        sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
        if (sem == SEM_FAILED) {
            throw(aucont_exception("Failed to open named semaphore"));
        }
        file.open(PIDS_FILE_NAME);
        if (!file.is_open()) {
            sem_close(sem);
            throw(aucont_exception("Failed to open pids file"));
        }
    }

    ~pids_storage() {
        sem_close(sem);
    }

    void push_back(int pid) {
        std::unique_ptr<sem_t, void (*)(sem_t*)> guard(lock(sem), unlock);
        file.seekp(0L, std::ios_base::end).write(reinterpret_cast<char*>(&pid), sizeof(int)).flush();
    }

    bool remove(int pid) {
        std::unique_ptr<sem_t, void (*)(sem_t*)> guard(lock(sem), unlock);
        size_t count = pids_count();
        size_t pos_found = 0;
        while(pos_found != count) {
            int cur_pid;
            file.seekg(pos_found * sizeof(int), std::ios_base::beg).
                    read(reinterpret_cast<char*>(&cur_pid), sizeof(int));
            if (cur_pid == pid) {
                break;
            }
            pos_found += 1;
        }
        if (pos_found == count) {
            return false;
        }

        file.seekg(sizeof(int), std::ios_base::end).read(reinterpret_cast<char*>(&pid), sizeof(int));
        file.seekp(pos_found * sizeof(int), std::ios_base::beg).
                write(reinterpret_cast<char*>(&pid), sizeof(int)).flush();

        truncate(PIDS_FILE_NAME, (count - 1) * sizeof(int));
        return true;
    }

    bool get_pid(size_t idx, int &pid) {
        std::unique_ptr<sem_t, void (*)(sem_t*)> guard(lock(sem), unlock);
        if (pids_count() <= idx) {
            return false;
        } else {
            file.seekg(idx * sizeof(int), std::ios_base::beg).read(reinterpret_cast<char*>(&pid), sizeof(int));
            return true;
        }
    }

private:
    size_t pids_count() {
        return static_cast<size_t>(file.seekg(0L, std::ios_base::end).tellg() / sizeof(int));
    }

private:
    std::fstream file;
    sem_t *sem;
};

std::ostream& printDebug() {
    return std::cout << "Debug: ";
}

std::string to_string(in_addr_t ip) {
    char buffer[INET_ADDRSTRLEN + 1];
    inet_ntop(AF_INET, &ip, buffer, INET_ADDRSTRLEN);
    return buffer;
}

int aucont_start(start_arguments const &args) {
    try {
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

        pids_storage pids;
        // TODO
        pids.push_back(12);

        return 0;
    } catch(std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXCEPTION_OCCURED_ERROR;
    }
}


int aucont_stop(const stop_arguments &args) {
    try {
        if (args.debug_enabled) {
            printDebug() << "PID is " << args.pid << std::endl;
            printDebug() << "SIGNAL is " << args.signal << std::endl;
        }
        pids_storage pids;

        bool removed = pids.remove(args.pid);
        if (args.debug_enabled) {
            printDebug() << "PID is " << (removed ? "" : "not ") << "removed from pids" << std::endl;
        }
        // TODO

        return 0;
    } catch(std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXCEPTION_OCCURED_ERROR;
    }
}

int aucont_list() {
    try {
        pids_storage pids;

        std::cout << "Running containers pids:" << std::endl;

        int pid;
        for (size_t pid_idx = 0; pids.get_pid(pid_idx, pid); ++pid_idx) {
            std::cout << pid_idx << ": " << pid << std::endl;
        }

        return 0;
    } catch(std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXCEPTION_OCCURED_ERROR;
    }
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

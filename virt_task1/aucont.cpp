#include "aucont.h"
#include "error_codes.h"
#include <iostream>
#include <algorithm>
#include <arpa/inet.h>
#include <sys/types.h>
#include <semaphore.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <vector>
#include <memory>
#include <fstream>
#include <wait.h>

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

// Checks if 'return_code' != -1
// Throws aucont_exception if 'return_code' == -1 with 'exception_message'
// Returns return_code
int check_result(int return_code, std::string const &exception_message) {
    if (return_code == -1) {
        throw(aucont_exception(exception_message));
    }
    return return_code;
}

static char const *SEM_NAME = "/aucont_pids_storage_sem";
static char const *PIDS_FILE_NAME = "aucont_pids";

sem_t* lock(sem_t *sem) {
    check_result(sem_wait(sem), "Failed to wait named semaphore");
    return sem;
}

void unlock(sem_t *sem) {
    check_result(sem_post(sem), "Failed to wait named semaphore");
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

    bool remove_by_idx(size_t id) {
        std::unique_ptr<sem_t, void (*)(sem_t*)> guard(lock(sem), unlock);
        return remove_by_idx_unsafe(id, pids_count());
    }

    // Warn: complexity is O(n)
    bool remove_by_pid(int pid) {
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

        return remove_by_idx_unsafe(pos_found, count);
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
    bool remove_by_idx_unsafe(size_t idx, size_t count)
    {
        if (idx >= count) {
            return false;
        }
        int pid;
        file.seekg(sizeof(int), std::ios_base::end).read(reinterpret_cast<char*>(&pid), sizeof(int));
        file.seekp(idx * sizeof(int), std::ios_base::beg).
                write(reinterpret_cast<char*>(&pid), sizeof(int)).flush();

        bool tr_succ = truncate(PIDS_FILE_NAME, (count - 1) * sizeof(int)) == 0;
        assert(tr_succ);
        return true;
    }

    size_t pids_count() {
        file.clear();
        auto file_size_in_bytes = file.seekg(0L, std::ios_base::end).tellg();
        if (file_size_in_bytes % sizeof(int) != 0) {
            throw(aucont_exception("Invalid pids file. Size % sizeof(int) != 0. File size = " +
                                   std::to_string(file_size_in_bytes)));
        }
        return static_cast<size_t>(file_size_in_bytes / sizeof(int));
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

bool process_exist(int pid) {
    // man kill
    // If sig is 0, then no signal is sent,
    // but error checking is still performed;
    // this can be used to check for the
    // existence of a process ID or process group ID.
    return kill(pid, 0) == 0;
}

typedef std::pair<std::pair<int, int>, start_arguments> container_main_args;

int container_main(void *container_main_args_ptr) {
    std::unique_ptr<container_main_args> args_holder(
                reinterpret_cast<container_main_args*>(container_main_args_ptr));
    start_arguments &args = args_holder->second;
    if (close(args_holder->first.second) == -1) {
        return -1;
    }
    char go;
    if (read(args_holder->first.first, &go, 1) == -1) {
        return -2;
    }
    if (go != 'g') {
        return -3;
    }



    return 0;
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

        static size_t const CONTAINER_MAIN_STACK_SIZE = 1 << 12; // 4kb
        static char CONTAINER_MAIN_STACK[CONTAINER_MAIN_STACK_SIZE] = {0};
        static char *CONTAINER_MAIN_STACK_TOP = CONTAINER_MAIN_STACK + CONTAINER_MAIN_STACK_SIZE - 1;
        static int const CLONE_FLAGS =
                    CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD | CLONE_NEWUSER | CLONE_NEWNET;
        int pipe_descriptors[2];
        check_result(pipe(pipe_descriptors), "Faled to create pipe");
        std::unique_ptr<container_main_args> cont_main_args(new container_main_args(
                {pipe_descriptors[0], pipe_descriptors[1]}, args));
        int pid = check_result(clone(container_main, CONTAINER_MAIN_STACK_TOP, CLONE_FLAGS, cont_main_args.get()),
                               "Failed to clone child process");
        check_result(close(pipe_descriptors[0]), "Failed to close read pipe");


//        setup_cpu();

//        setup_net();


        check_result(write(pipe_descriptors[1], "g", 1), "Failed to write to pipe");
        check_result(close(pipe_descriptors[1]), "Failed to close write pipe");

        pids_storage pids;
        pids.push_back(pid);

        std::cout << "Container with pid '" << pid << "' started" << std::endl;

        if (args.debug_enabled) {
            printDebug() << "Container is" << (process_exist(pid) ? "" : "n't") << " working at the moment ..." << std::endl;
        }

        if (!args.daemonize) {
            int cont_main_return_code;
            waitpid(pid, &cont_main_return_code, 0);
            bool removed = pids.remove_by_pid(pid);
            if (args.debug_enabled) {
                printDebug() << "Container finished. Exit code: " << cont_main_return_code << std::endl;
                printDebug() << "Pid " << (removed ? "is removed" : "was already removed") << std::endl;
            }
            return cont_main_return_code;
        }

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

        bool removed = pids.remove_by_pid(args.pid);
        bool p_exist = process_exist(args.pid);
        if (removed && p_exist) {
            bool signal_sent = kill(args.pid, args.signal);
            std::cout << "Signal " << args.signal << (signal_sent ? " was " : " wasn't ")
                      << "sent to process with pid " << args.pid << std::endl;
        } else {
            std::cout << "Signal won't be sent to process with pid "
                      << args.pid << " because" << std::endl;
            if (!removed) {
                std::cout << "\tProcess wasn't started by aucont_start" << std::endl;
            }
            if (!p_exist) {
                std::cout << "\tProcess is not running atm" << std::endl;
            }
        }

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
            if (process_exist(pid)) {
                std::cout << pid_idx << ": " << pid << std::endl;
            } else {
                pids.remove_by_idx(pid_idx); // Not safe! No locking between get_pid and remove_by_idx
                pid_idx -= 1;
            }
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

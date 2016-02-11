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
#include <syscall.h>
#include <grp.h>
#include <sys/mount.h>


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

// Checks if check_return_code(return_code)
// Throws aucont_exception if !check_return_code(return_code) with 'exception_message'
// Returns return_code
int check_result(int return_code, std::string const &exception_message,
                 bool (*check_return_code)(int) = [](int code){return code != -1;}) {
    if (!check_return_code(return_code)) {
        throw(aucont_exception(exception_message));
    }
    return return_code;
}

void exec_check_result(std::string const &command) {
    check_result(system(command.c_str()),
                 "command '" + command + "' execution failed",
                 [](int code) {return code == 0;});
}

void mount_cgroup(std::string const &base_dir, std::string const &cgroup) {
    static int const ALREADY_MOUNTED_ERR = 8192;
    std::string cgroup_dir = base_dir + '/' + cgroup;
    std::string exec_str = "sudo mount -t cgroup " + cgroup + " -o " + cgroup + " " + cgroup_dir +
            "  > /dev/null 2>&1";
    exec_check_result("mkdir -p " + cgroup_dir);
    check_result(system(exec_str.c_str()), "Failed to execute mount command:\n\t'" +
                 exec_str + '\'', [](int err) { return err == 0 || err == ALREADY_MOUNTED_ERR;});
    exec_check_result("sudo chown 1000:1000 -R " + cgroup_dir);
}

static char const *SEM_NAME = "/aucont_pids_storage_sem";
static std::string const PIDS_FILE_NAME = "/tmp/aucont/pids_storage";

sem_t* lock(sem_t *sem) {
    check_result(sem_wait(sem), "Failed to wait named semaphore");
    return sem;
}

void unlock(sem_t *sem) {
    check_result(sem_post(sem), "Failed to wait named semaphore");
}

class pids_storage {
private:
    typedef std::unique_ptr<sem_t, void (*)(sem_t*)> sem_guard;
public:
    pids_storage() {
        sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
        if (sem == SEM_FAILED) {
            throw(aucont_exception("Failed to open named semaphore"));
        }
        exec_check_result("touch " + PIDS_FILE_NAME);
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
        sem_guard guard(lock(sem), unlock);
        file.seekp(0L, std::ios_base::end).write(reinterpret_cast<char*>(&pid), sizeof(int)).flush();
    }

    bool remove_by_idx(size_t id) {
        sem_guard guard(lock(sem), unlock);
        return remove_by_idx_unsafe(id, pids_count());
    }

    // Warn: complexity is O(n)
    bool remove_by_pid(int pid) {
        sem_guard guard(lock(sem), unlock);
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
        sem_guard guard(lock(sem), unlock);
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

        bool tr_succ = truncate(PIDS_FILE_NAME.c_str(), (count - 1) * sizeof(int)) == 0;
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
    // from man kill
    // If sig is 0, then no signal is sent,
    // but error checking is still performed;
    // this can be used to check for the
    // existence of a process ID or process group ID.
    return kill(pid, 0) == 0;
}

void mknod_mount_dev(std::string const &what) {
    mknod(what.c_str(), S_IFREG | 0666, 0);
    check_result(mount(what.c_str(), what.c_str(), nullptr, MS_BIND, nullptr), "Failed to mount " + what);
}

typedef std::pair<std::pair<int, int>, start_arguments> container_main_args;

int container_main(void *container_main_args_ptr) {
    std::unique_ptr<container_main_args> args_holder(
                reinterpret_cast<container_main_args*>(container_main_args_ptr));
    start_arguments &args = args_holder->second;
    if (close(args_holder->first.second) == -1) {
        if (args.debug_enabled) {
            printDebug() << "Failed to close pipe" << std::endl;
        }
        return CLOSE_PIPE_ERROR;
    }
    char go;
    if (read(args_holder->first.first, &go, 1) == -1 || go != 'g') {
        if (args.debug_enabled) {
            printDebug() << "Failed to read 'go' command" << std::endl;
        }
        return GO_COMMAND_ERROR;
    }

    if (args.debug_enabled) {
        printDebug() << "container_main GO now ..." << std::endl;
    }

    check_result(mount("proc", (args.image_path + "/proc").c_str(), "proc", 0, nullptr), "Failed to mount proc fs");
    check_result(mount("tmp", (args.image_path + "/tmp").c_str(), "tmpfs", 0, nullptr), "Failed to mount tmp fs");

    check_result(chdir(args.image_path.c_str()), "Failed to chdir to image_path");

    check_result(mount("sandbox-dev", "dev", "tmpfs",
                       MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_NOATIME,
                       "size=64k,nr_inodes=16,mode=755"), "sand box dev");

    mknod_mount_dev("/dev/zero");
    mknod_mount_dev("/dev/null");
    mknod_mount_dev("/dev/random");
    mknod_mount_dev("/dev/urandom");

    std::string tmp_old_root = "/old_root";
    std::string tmp_old_root_dir = args.image_path + tmp_old_root;
    check_result(mkdir(tmp_old_root_dir.c_str(), 0777), "Make tmp_old_root_dir", [](int code) {
        return code != -1 || errno ==EEXIST;
    });

    check_result(mount(args.image_path.c_str(), args.image_path.c_str(), "bind", MS_BIND | MS_REC, NULL), "Mount image_path");
    check_result(syscall(SYS_pivot_root, args.image_path.c_str(), tmp_old_root_dir.c_str()), "Change root dir");
    check_result(umount2(tmp_old_root.c_str(), MNT_DETACH), "Umount old root");

    setgroups(0, nullptr);
    umask(0);

    static std::string const HOSTNAME("container");
    if (sethostname(HOSTNAME.c_str(), HOSTNAME.length())) {
        if (args.debug_enabled) {
            printDebug() << "sethostname failed" << std::endl;
        }
        return SETHOSTNAME_ERROR;
    }

    if (args.daemonize) {
       freopen("/dev/null", "r", stdin);
       freopen("/dev/null", "w", stdout);
       freopen("/dev/null", "w", stderr);
    }


    if (args.debug_enabled) {
        printDebug() << "executing command ..." << std::endl;
    }
    if (execv(args.cmd.c_str(), args.cmd_args) == -1) {
        if (args.debug_enabled) {
            printDebug() << "Failed to execute command. Errno = " << errno << std::endl;
        }
        return EXECUTE_COMMAND_ERROR;
    }
    return 0;
}

int aucont_start(start_arguments const &args) {
    int pipe_descriptors[2] = {0};
    try {
        assert(system(nullptr)); // shel is available
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
                    printDebug() << '\t' << cmd_arg_idx << ": '" << args.cmd_args[cmd_arg_idx + 1] << '\'' << std::endl;
                }
            }
        }

        static size_t const CONTAINER_MAIN_STACK_SIZE = 1 << 12; // 4kb
        static char CONTAINER_MAIN_STACK[CONTAINER_MAIN_STACK_SIZE] = {0};
        static char *CONTAINER_MAIN_STACK_TOP = CONTAINER_MAIN_STACK + CONTAINER_MAIN_STACK_SIZE - 1;
        static int const CLONE_FLAGS =
                    CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD | CLONE_NEWUSER | CLONE_NEWNET;
        check_result(pipe(pipe_descriptors), "Faled to create pipe");
        std::unique_ptr<container_main_args> cont_main_args(new container_main_args(
                {pipe_descriptors[0], pipe_descriptors[1]}, args));
        int pid = check_result(clone(container_main, CONTAINER_MAIN_STACK_TOP, CLONE_FLAGS, cont_main_args.get()),
                               "Failed to clone child process");
        check_result(close(pipe_descriptors[0]), "Failed to close read pipe");

        static std::string const CGROUP_DIR = "/tmp/aucont/cgroup";

        /*Create cgroups******************************/
        mount_cgroup(CGROUP_DIR, "cpu");
        mount_cgroup(CGROUP_DIR, "memory");
        mount_cgroup(CGROUP_DIR, "blkio");
        mount_cgroup(CGROUP_DIR, "cpuacct");
        mount_cgroup(CGROUP_DIR, "cpuset");

        /*Create cpu cgroup***************************/
        static std::string const CPU_CGROUP_DIR = CGROUP_DIR + "/cpu";
        std::string const current_cpu_dir = CPU_CGROUP_DIR + "/" + std::to_string(pid);
        // exec_check_result("rmdir " + current_cpu_dir); // To work fine after restart. Disabled to feel comfortable
        exec_check_result("mkdir -p " + current_cpu_dir);

        /*Setup CPU limit*************************/
        static long const CPU_PERIOD_US = 50000;
        static long const CPU_QUOTA_PER_PERCENT = CPU_PERIOD_US / 100;
        long cpus_count = sysconf(_SC_NPROCESSORS_ONLN);
        long cpu_quota = CPU_QUOTA_PER_PERCENT * args.cpu_limit * cpus_count;
        if (args.debug_enabled) {
            printDebug() << "Cpus count is " << cpus_count << std::endl;
            printDebug() << "Cpus quota is " << cpu_quota << '/' << CPU_PERIOD_US << std::endl;
        }
        exec_check_result("echo " + std::to_string(CPU_PERIOD_US) +  " >> " + current_cpu_dir + "/cpu.cfs_period_us");
        exec_check_result("echo " + std::to_string(cpu_quota) +  " >> " + current_cpu_dir + "/cpu.cfs_quota_us");
        exec_check_result("echo " + std::to_string(pid) +  " >> " + current_cpu_dir + "/tasks");

        /*Setup networking************************/
        // TODO


        pids_storage pids;
        pids.push_back(pid);

        check_result(write(pipe_descriptors[1], "g", 1), "Failed to write to pipe");
        check_result(close(pipe_descriptors[1]), "Failed to close write pipe");




        std::cout << pid << std::endl;

        if (args.debug_enabled) {
            printDebug() << "Container is" << (process_exist(pid) ? "" : "n't") << " working at the moment ..." << std::endl;
        }

        if (!args.daemonize) {
            int cont_main_return_code;
            waitpid(pid, &cont_main_return_code, 0);
            bool removed = pids.remove_by_pid(pid);
            if (args.debug_enabled) {
                printDebug() << "Container finished. Exit code: " << cont_main_return_code << std::endl;
                printDebug() << "Pid " << (removed ? "is removed" : "has been already removed") << std::endl;
            }
            return cont_main_return_code;
        }

        return 0;
    } catch(std::exception &e) {
        if (pipe_descriptors[0] != 0 || pipe_descriptors[1] != 0) { // to stop cloned process
            write(pipe_descriptors[1], "s", 1);
            close(pipe_descriptors[1]);
        }
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
            bool signal_sent = kill(args.pid, args.signal) == 0;
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

        //std::cout << "Running containers pids:" << std::endl;

        int pid;
        for (size_t pid_idx = 0; pids.get_pid(pid_idx, pid); ++pid_idx) {
            if (process_exist(pid)) {
                //std::cout << pid_idx << ": " << pid << std::endl;
                std::cout << pid << std::endl;
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

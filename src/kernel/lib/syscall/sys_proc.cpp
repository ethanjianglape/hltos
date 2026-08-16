#include <arch.hpp>
#include <fs/fs.hpp>
#include <kassert/kassert.hpp>
#include <log/log.hpp>
#include <process/process.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>
#include <syscall/sys_proc.hpp>

#include <cstdint>

namespace syscall {

int sys_getpid()
{
    auto* proc = arch::percpu::current_process();

    return proc->pid;
}

int sys_fork()
{
    process::Process* current = arch::percpu::current_process();
    process::Process* forked = current->fork();

    scheduler::mechanism::add_process(forked);

    return forked->pid;
}

int sys_execve(const char* path, char* argv[], char* envp[])
{
    kstring path_str = kstring::from_userspace(path);
    fs::FileDescriptor* fd = fs::open(path_str, 0);

    if (!fd) {
        log::errorf("sys_execve failed to open file at {}", path_str);
        return -1;
    }

    auto size = fd->inode->size;
    auto* data = new std::uint8_t[size];

    fd->inode->read(fd, data, size);

    process::Process* current = arch::percpu::current_process();

    std::size_t argc = 0;

    kvector<kstring> argv_strs{};
    kvector<kstring> envp_strs{};

    arch::cpu::stac();

    if (argv != nullptr) {
        while (true) {
            const char* arg = argv[argc];

            if (arg == nullptr) {
                break;
            }

            argv_strs.push_back(kstring::from_userspace(arg));
            argc++;
        }
    }

    arch::cpu::clac();

    current->exec_elf64(data, size, argv_strs, envp_strs);

    scheduler::mechanism::yield_new_process();
}

int sys_vfork()
{
    log::warn("sys_vfork not implemented");
    return -1;
}

int sys_wait4(int pid, int*, int, void*)
{
    log::debugf("parent pid={} waiting on child pid={}", arch::percpu::current_process()->pid, pid);

    return scheduler::mechanism::yield_to_child(pid);
}

int sys_exit(int status)
{
    auto* proc = arch::percpu::current_process();

    proc->exit_status = status;

    scheduler::mechanism::yield_zombie();
}

}

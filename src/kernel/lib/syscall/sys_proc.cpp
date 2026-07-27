#include <arch.hpp>
#include <fs/fs.hpp>
#include <kassert/kassert.hpp>
#include <log/log.hpp>
#include <process/process.hpp>
#include <scheduler/scheduler.hpp>
#include <syscall/sys_proc.hpp>

#include <cstdint>

namespace syscall {

int sys_getpid()
{
    auto* proc = arch::percpu::current_process();

    return proc->pid;
}

int sys_fork(arch::trap::SyscallFrame* syscall_frame)
{
    process::Process* current = arch::percpu::current_process();
    process::Process* created = current->fork(syscall_frame);

    kassert_not_null(created);

    scheduler::add_process(created);

    return created->pid;
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

    current->exec_elf64(data, size, argv, envp);

    scheduler::yield_new_process();
}

int sys_vfork()
{
    log::warn("sys_vfork not implemented");
    return -1;
}

int sys_wait4(int pid, int*, int, void*)
{
    log::debugf("parent pid={} waiting on child pid={}", arch::percpu::current_process()->pid, pid);

    return scheduler::yield_to_child(pid);
}

int sys_exit(int status)
{
    auto* proc = arch::percpu::current_process();

    proc->exit_status = status;

    scheduler::yield_zombie();
}

}

#pragma once

#include <arch.hpp>

namespace syscall {

int sys_getpid();

int sys_fork();

int sys_vfork();

int sys_wait4(int pid, int* wstatus, int options, void* unused);

int sys_execve(const char* path, char* argv[], char* envp[]);

[[noreturn]]
int sys_exit(int status);

}

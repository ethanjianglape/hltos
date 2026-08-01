#pragma once

#include <arch.hpp>
#include <containers/kvector.hpp>
#include <fs/fs.hpp>

#include <cstddef>
#include <cstdint>

namespace process {

enum class ProcessState : std::uint8_t {
    NEW = 0,
    RUNNING = 1,
    READY = 2,
    BLOCKED = 3,
    SLEEPING = 4,
    DEAD = 5,
    ZOMBIE = 6
};

enum class WaitReason : std::uint8_t {
    NONE = 0,
    KEYBOARD = 1,
    SLEEP = 2,
    FRAMEBUFFER = 3,
    CHILD_PROCESS = 4
};

struct Process {
private:
    void terminate();

public:
    // Process meta info
    int pid;
    Process* parent;
    ProcessState state;
    WaitReason wait_reason;
    int wait_pid;
    int exit_status;
    std::uint64_t context_switches;

    std::uint64_t wake_time_ns;
    std::uint64_t total_sleep_ns;
    std::uint64_t sleep_start_ns;

    fs::Inode* cwd_inode;

    // Address space
    arch::vmm::PML4E* pml4;
    std::uintptr_t heap_break;
    std::uintptr_t mmap_min_addr;

    arch::vmm::Heap uheap;

    std::uint8_t* kernel_stack;      // Base of kernel stack
    std::uintptr_t kernel_rsp;       // Top of stack (initially)
    std::uintptr_t kernel_rsp_saved; // Kernel rsp used during context_switch

    arch::context::ContextFrame* context_frame;
    arch::trap::SyscallFrame* syscall_frame;

    kvector<fs::FileDescriptor*> fd_table;

    std::uintptr_t entry;

    std::uint64_t fs_base; // For thread local storage (TLS)
    int* tidptr;

    Process() = default;
    virtual ~Process();

    Process(const Process&) = delete;
    Process(Process&&) = delete;

    Process& operator=(const Process&) = delete;
    Process& operator=(Process&&) = delete;

    Process* fork(arch::trap::SyscallFrame* parent_frame);

    const char* get_state_str() const;

    kstring to_string() const;

    bool is_running() const;
    bool is_ready() const;
    bool is_zombie() const;
    bool is_dead() const;
    bool is_blocked() const;
    bool is_waiting_for(WaitReason reason) const;
    bool is_waiting_for_child(int pid) const;

    void log() const;
    void log_syscall_frame() const;

    void wake();
    void pause();
    void resume();
    void kill();
    void zombify();
    void wait_for(WaitReason reason);
    void wait_for_child(int child_pid);
    void sleep_for(std::uint64_t duration_ns);

    void exec_elf64(std::uint8_t* buffer, std::size_t size, char* const argv[], char* const envp[]);
};

struct KThread final : public Process {
public:
    KThread(void (*func)());
};

struct ELF64Process final : public Process {
public:
    ELF64Process(std::uint8_t* buffer, std::size_t size);
};

}

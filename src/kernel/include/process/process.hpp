#pragma once

#include <arch.hpp>
#include <containers/kvector.hpp>
#include <exclusive/kmutex.hpp>
#include <fs/fs.hpp>

#include <cstddef>
#include <cstdint>

#include "arch/x64/trap/syscall_entry.hpp"
#include "elf.hpp"

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
    CHILD_PROCESS = 4,
    MUTEX = 5
};

struct Process {
private:
    void terminate();

protected:
    void build_synthetic_context_frame(void (*entry)());
    void copy_syscall_frame(arch::trap::SyscallFrame* frame);
    void map_elf64_header(std::uint8_t* file_buffer, const elf::Elf64_ProgramHeader& header);
    void build_user_stack(const kvector<kstring>& argv_strs, const kvector<kstring>& envp_strs);

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
    std::uint64_t quantum_start_ns;

    fs::Inode* cwd_inode;

    kmutex* wait_mutex;

    // Address space
    arch::vmm::PML4E* pml4;
    std::uintptr_t heap_break;

    arch::vmm::Heap uheap;

    std::uint8_t* kernel_stack;      // Base of kernel stack
    std::uintptr_t kernel_rsp;       // Top of stack (initially)
    std::uintptr_t kernel_rsp_saved; // Kernel rsp used during context_switch
    std::uintptr_t user_rsp;

    arch::context::ContextFrame* context_frame;
    arch::trap::SyscallFrame* syscall_frame;

    kvector<fs::FileDescriptor*> fd_table;

    std::uintptr_t entry;

    std::uint64_t fs_base; // For thread local storage (TLS)
    int* tidptr;

    Process();
    virtual ~Process();

    Process(const Process&) = delete;
    Process(Process&&) = delete;

    Process& operator=(const Process&) = delete;
    Process& operator=(Process&&) = delete;

    Process* fork();

    const char* get_state_str() const;

    kstring to_string() const;

    bool is_running() const;
    bool is_ready() const;
    bool is_zombie() const;
    bool is_dead() const;
    bool is_blocked() const;
    bool is_waiting_for(WaitReason reason) const;
    bool is_waiting_for_child(int pid) const;
    bool is_waiting_for_mutex(kmutex* mutex) const;

    void log() const;
    void log_syscall_frame() const;

    void build_stdio();

    void wake();
    void pause();
    void resume();
    void kill();
    void zombify();
    void wait_for(WaitReason reason);
    void wait_for_child(int child_pid);
    void wait_for_mutex(kmutex* mutex);
    void sleep_for(std::uint64_t duration_ns);

    void exec_elf64(std::uint8_t* buffer, std::size_t size, kvector<kstring>& argv_strs, kvector<kstring>& envp_strs);
    // void exec_elf64(std::uint8_t* buffer, std::size_t size, char* const argv[], char* const envp[]);
};

struct KThread final : public Process {
public:
    KThread(void (*func)());
};

}

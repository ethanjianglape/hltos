#include <arch.hpp>
#include <clock/clock.hpp>
#include <crt/crt.h>
#include <exclusive/katomic.hpp>
#include <fmt/fmt.hpp>
#include <fs/devfs/dev_tty.hpp>
#include <fs/fs.hpp>
#include <kassert/kassert.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>
#include <memory/pmm.hpp>
#include <memory/slab.hpp>
#include <process/elf.hpp>
#include <process/process.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>

#include <cstddef>
#include <cstdint>

namespace process {

constexpr std::uintptr_t USER_STACK_BASE = 0x00800000;
constexpr std::uintptr_t USER_STACK_SIZE = 32 * 1024;   // 32KiB
constexpr std::uintptr_t USER_STACK_TOP = USER_STACK_BASE + USER_STACK_SIZE;

constexpr std::uintptr_t KERNEL_STACK_SIZE = 32 * 1024; // 32KiB

/// based on /proc/sys/vm/mmap_min_addr in Linux
constexpr std::uintptr_t DEFAULT_MMAP_MIN_ADDR = 65536;

static katomic<int> g_pid{1};

extern "C" void userspace_entry_context_switch(std::uintptr_t rsp, std::uintptr_t rip);

extern "C" void forked_entry_context_switch();

extern "C" void userspace_entry_trampoline()
{
    auto* proc = arch::percpu::current_process();

    std::uintptr_t rsp = proc->user_rsp;
    std::uintptr_t rip = proc->entry;

    userspace_entry_context_switch(rsp, rip);
}

extern "C" void kthread_entry_trampoline()
{
    auto* proc = arch::percpu::current_process();
    auto* entry_func = reinterpret_cast<void (*)()>(proc->entry);

    kassert_not_null(entry_func);

    entry_func();

    scheduler::mechanism::yield_dead();
}

Process::Process()
{
    pid = g_pid++;
    parent = nullptr;
    pml4 = nullptr;
    exit_status = 0;
    state = ProcessState::NEW;
    wait_reason = WaitReason::NONE;
    heap_break = 0;
    quantum_start_ns = 0;
    wake_time_ns = 0;
    fs_base = 0;
    tidptr = 0;

    kernel_stack = new std::uint8_t[KERNEL_STACK_SIZE];
    kernel_rsp = reinterpret_cast<std::uintptr_t>(kernel_stack + KERNEL_STACK_SIZE);
    fd_table = {};
    cwd_inode = nullptr;
}

Process::~Process()
{
    terminate();
}

void Process::log() const
{
    log::debugf("**** User process ****");
    log::debugf("* pid = {}", pid);
    log::debugf("* kernel_stack  @ {}", fmt::hex{kernel_stack});
    log::debugf("* kernel_rsp    @ {}", fmt::hex{kernel_rsp});
    log::debugf("* syscall_frame @ {}", fmt::hex{syscall_frame});
    log::debugf("* context_frame @ {}", fmt::hex{context_frame});
    log::debugf("* k rsp saved   @ {}", fmt::hex{kernel_rsp_saved});
}

void Process::build_synthetic_context_frame(void (*entry)())
{
    // Magic numbers to help with debugging
    context_frame->r15 = 0x15151515;
    context_frame->r14 = 0x14141414;
    context_frame->r13 = 0x13131313;
    context_frame->r12 = 0x12121212;
    context_frame->rbp = 0x77777777;
    context_frame->rbx = 0x12345678;

    // Value of RIP that context_switch will RET back to
    context_frame->rip = reinterpret_cast<std::uintptr_t>(entry);

    kernel_rsp_saved = reinterpret_cast<std::uintptr_t>(context_frame);
}

void Process::copy_syscall_frame(arch::trap::SyscallFrame* frame)
{
    syscall_frame->r15 = frame->r15;
    syscall_frame->r14 = frame->r14;
    syscall_frame->r13 = frame->r13;
    syscall_frame->r12 = frame->r12;
    syscall_frame->r11 = frame->r11;
    syscall_frame->r10 = frame->r10;
    syscall_frame->r9 = frame->r9;
    syscall_frame->r8 = frame->r8;

    syscall_frame->rbp = frame->rbp;
    syscall_frame->rdi = frame->rdi;
    syscall_frame->rsi = frame->rsi;
    syscall_frame->rdx = frame->rdx;
    syscall_frame->rcx = frame->rcx;
    syscall_frame->rbx = frame->rbx;
    syscall_frame->rax = frame->rax;
    syscall_frame->rsp = frame->rsp;
}

void Process::build_stdio()
{
    fs::FileDescriptor* stdin = fs::open("/dev/tty1", fs::O_RDONLY);
    fs::FileDescriptor* stdout = fs::open("/dev/tty1", fs::O_WRONLY);
    fs::FileDescriptor* stderr = fs::open("/dev/tty1", fs::O_WRONLY);

    fd_table.push_back(stdin);
    fd_table.push_back(stdout);
    fd_table.push_back(stderr);
}

KThread::KThread(void (*func)())
    : Process{}
{
    kassert_not_null(func);

    pml4 = arch::vmm::get_kernel_pml4();
    entry = reinterpret_cast<std::uintptr_t>(func);
    context_frame = reinterpret_cast<arch::context::ContextFrame*>(kernel_rsp - sizeof(arch::context::ContextFrame));

    build_synthetic_context_frame(kthread_entry_trampoline);
}

void Process::map_elf64_header(std::uint8_t* file_buffer, const elf::Elf64_ProgramHeader& header)
{
    const auto virt = header.p_vaddr;
    const auto file_size = header.p_filesz;
    const auto mem_size = header.p_memsz;
    const auto offset = header.p_offset;

    arch::vmm::map_user_pages(pml4, virt, mem_size);

    memcpy(reinterpret_cast<void*>(virt),
        reinterpret_cast<void*>(file_buffer + offset),
        file_size);

    if (mem_size > file_size) {
        memset(reinterpret_cast<void*>(virt + file_size), 0, mem_size - file_size);
    }

    std::uintptr_t segment_end = virt + mem_size;

    if (segment_end > heap_break) {
        heap_break = (segment_end + 0xFFF) & ~0xFFF;
    }
}

void Process::build_user_stack(const kvector<kstring>& argv_strs, const kvector<kstring>& envp_strs)
{
    arch::vmm::map_user_pages(pml4, USER_STACK_BASE, USER_STACK_SIZE);

    auto* stack = reinterpret_cast<std::uintptr_t*>(USER_STACK_TOP);

    *(--stack) = 0; // padding for 16-byte alignment

    kvector<std::uintptr_t> argv_ptrs{};
    kvector<std::uintptr_t> envp_ptrs{};

    for (const kstring& envp : envp_strs) {
        auto* ptr = reinterpret_cast<std::uint8_t*>(stack);

        ptr -= envp.length() + 1;
        memcpy(ptr, envp.c_str(), envp.length());
        *(ptr + envp.length()) = '\0';
        stack = reinterpret_cast<std::uint64_t*>(ptr);
        argv_ptrs.push_back(reinterpret_cast<std::uintptr_t>(stack));
    }

    for (const kstring& argv : argv_strs) {
        auto* ptr = reinterpret_cast<std::uint8_t*>(stack);

        ptr -= argv.length() + 1;
        memcpy(ptr, argv.c_str(), argv.length());
        *(ptr + argv.length()) = '\0';
        stack = reinterpret_cast<std::uint64_t*>(ptr);
        argv_ptrs.push_back(reinterpret_cast<std::uintptr_t>(stack));
    }

    *(--stack) = 0; // AT_NULL value
    *(--stack) = 0; // AT_NULL type

    // TODO: auxv values

    *(--stack) = 0; // envp terminator (NULL)

    for (int i = envp_ptrs.size() - 1; i >= 0; i--) {
        *(--stack) = envp_ptrs[i];
    }

    *(--stack) = 0; // argv terminator (NULL)

    for (int i = argv_ptrs.size() - 1; i >= 0; i--) {
        *(--stack) = argv_ptrs[i];
    }

    *(--stack) = argv_strs.size();

    user_rsp = reinterpret_cast<std::uintptr_t>(stack);
}

void Process::exec_elf64(std::uint8_t* file_buffer, std::size_t size, kvector<kstring>& argv_strs, kvector<kstring>& envp_strs)
{
    kassert_not_null(file_buffer);

    elf::Elf64_File elf64_file = elf::parse_file(file_buffer, size);

    if (!elf64_file.is_valid_elf) {
        kpanic("attempted to load an invalid ELF64 file");
    }

    if (pml4 != nullptr) {
        arch::vmm::free_user_pml4(pml4);
    }

    pml4 = arch::vmm::create_user_pml4();

    arch::vmm::switch_pml4(pml4);
    arch::cpu::stac();

    state = ProcessState::NEW;
    wait_reason = WaitReason::NONE;
    exit_status = 0;
    heap_break = 0;
    wake_time_ns = 0;
    fs_base = 0;
    tidptr = 0;
    uheap = arch::vmm::create_user_heap(pml4);
    kernel_rsp = reinterpret_cast<std::uintptr_t>(kernel_stack + KERNEL_STACK_SIZE);
    context_frame = reinterpret_cast<arch::context::ContextFrame*>(kernel_rsp - sizeof(arch::context::ContextFrame));
    entry = elf64_file.entry;

    for (const elf::Elf64_ProgramHeader& header : elf64_file.program_headers) {
        map_elf64_header(file_buffer, header);
    }

    build_user_stack(argv_strs, envp_strs);
    build_synthetic_context_frame(userspace_entry_trampoline);

    log();

    arch::vmm::switch_kernel_pml4();
    arch::cpu::clac();
}

void Process::log_syscall_frame() const
{
    log::debugf("**** SYSCALL FRAME ****");
    log::debugf("* r15 = {}", fmt::hex{syscall_frame->r15});
    log::debugf("* r14 = {}", fmt::hex{syscall_frame->r14});
    log::debugf("* r13 = {}", fmt::hex{syscall_frame->r13});
    log::debugf("* r12 = {}", fmt::hex{syscall_frame->r12});
    log::debugf("* r11 = {}", fmt::hex{syscall_frame->r11});
    log::debugf("* r10 = {}", fmt::hex{syscall_frame->r10});
    log::debugf("* r9  = {}", fmt::hex{syscall_frame->r9});
    log::debugf("* r8  = {}", fmt::hex{syscall_frame->r8});
    log::debugf("* rpb = {}", fmt::hex{syscall_frame->rbp});
    log::debugf("* rdi = {}", fmt::hex{syscall_frame->rdi});
    log::debugf("* rsi = {}", fmt::hex{syscall_frame->rsi});
    log::debugf("* rdx = {}", fmt::hex{syscall_frame->rdx});
    log::debugf("* rcx = {}", fmt::hex{syscall_frame->rcx});
    log::debugf("* rbx = {}", fmt::hex{syscall_frame->rbx});
    log::debugf("* rax = {}", fmt::hex{syscall_frame->rax});
    log::debugf("* rsp = {}", fmt::hex{syscall_frame->rsp});
    log::debugf("***********************");
}

Process* Process::fork()
{
    arch::vmm::PML4E* cloned_pml4 = arch::vmm::clone_user_pml4(pml4);
    arch::vmm::switch_pml4(cloned_pml4);
    arch::cpu::stac();

    auto* forked = new Process{};

    forked->parent = this;
    forked->state = process::ProcessState::NEW;
    forked->wait_reason = wait_reason;
    forked->exit_status = exit_status;
    forked->heap_break = heap_break;
    forked->pml4 = cloned_pml4;
    forked->quantum_start_ns = quantum_start_ns;
    forked->wake_time_ns = wake_time_ns;
    forked->fs_base = fs_base;
    forked->tidptr = tidptr;
    forked->cwd_inode = cwd_inode;
    forked->uheap = arch::vmm::clone_user_heap(&uheap, cloned_pml4);
    forked->fd_table = fd_table;
    forked->syscall_frame = reinterpret_cast<arch::trap::SyscallFrame*>(forked->kernel_rsp - sizeof(arch::trap::SyscallFrame));
    forked->context_frame = reinterpret_cast<arch::context::ContextFrame*>(
        forked->kernel_rsp - sizeof(arch::trap::SyscallFrame) - sizeof(arch::context::ContextFrame));

    forked->copy_syscall_frame(syscall_frame);
    forked->build_synthetic_context_frame(forked_entry_context_switch);

    forked->log();
    forked->log_syscall_frame();

    arch::vmm::switch_pml4(pml4);
    arch::cpu::clac();

    return forked;
}

const char* Process::get_state_str() const
{
    switch (state) {
    case ProcessState::READY:
        return "READY";
    case ProcessState::NEW:
        return "NEW";
    case ProcessState::RUNNING:
        return "RUNNING";
    case ProcessState::DEAD:
        return "DEAD";
    case ProcessState::ZOMBIE:
        return "ZOMBIE";
    case ProcessState::BLOCKED:
        return "BLOCKED";
    default:
        kpanic("unknown process state");
    }
}

kstring Process::to_string() const
{
    kstring cwd = fs::getcwd(cwd_inode);
    kstring format = "pid = {}\n"
                     "cwd = {}\n"
                     "state = {}\n"
                     "pml4         @ {}\n"
                     "kernel stack @ {}\n"
                     "kernel rsp   @ {}\n"
                     "entry rip    @ {}\n"
                     "context switch count = {}";

    return fmt::sprintf(
        format,
        pid,
        cwd,
        get_state_str(),
        fmt::hex{pml4},
        fmt::hex{kernel_stack},
        fmt::hex{kernel_rsp},
        fmt::hex{entry},
        context_switches);
}

bool Process::is_running() const
{
    return state == ProcessState::RUNNING;
}

bool Process::is_ready() const
{
    return state == ProcessState::READY || state == ProcessState::NEW;
}

bool Process::is_zombie() const
{
    return state == ProcessState::ZOMBIE;
}

bool Process::is_dead() const
{
    return state == ProcessState::DEAD;
}

bool Process::is_blocked() const
{
    return state == ProcessState::BLOCKED;
}

bool Process::is_waiting_for(WaitReason reason) const
{
    return wait_reason == reason;
}

bool Process::is_waiting_for_child(int pid) const
{
    return is_waiting_for(WaitReason::CHILD_PROCESS) && (wait_pid == pid || wait_pid == -1);
}

bool Process::is_waiting_for_mutex(kmutex* mutex) const
{
    return is_waiting_for(WaitReason::MUTEX) && wait_mutex != nullptr && wait_mutex == mutex;
}

void Process::wake()
{
    state = ProcessState::READY;
    wait_reason = WaitReason::NONE;
    wait_pid = -1;
    wait_mutex = nullptr;
    wake_time_ns = 0;
}

/// pauses the process if it is currently running
void Process::pause()
{
    if (state == ProcessState::RUNNING) {
        state = ProcessState::READY;
    }
}

void Process::resume()
{
    state = ProcessState::RUNNING;
    wait_reason = WaitReason::NONE;
    wait_pid = -1;
    wait_mutex = nullptr;
    wake_time_ns = 0;
    quantum_start_ns = clock::get_time_ns();
}

void Process::kill()
{
    state = ProcessState::DEAD;
}

void Process::zombify()
{
    state = ProcessState::ZOMBIE;
}

void Process::wait_for(WaitReason reason)
{
    if (reason == WaitReason::SLEEP) {
        state = ProcessState::SLEEPING;
    } else {
        state = ProcessState::BLOCKED;
    }

    wait_reason = reason;
}

void Process::wait_for_child(int child_pid)
{
    wait_for(WaitReason::CHILD_PROCESS);
    wait_pid = child_pid;
}

void Process::wait_for_mutex(kmutex* mutex)
{
    wait_for(WaitReason::MUTEX);
    wait_mutex = mutex;
}

void Process::sleep_for(std::uint64_t duration_ns)
{
    wait_for(WaitReason::SLEEP);
    wake_time_ns = clock::get_time_ns() + duration_ns;
}

void Process::terminate()
{
    log::debugf("========================================");
    log::debugf("Terminating process pid={}", pid);

    const std::size_t frames_before = pmm::get_free_frames();
    const std::size_t slabs_before = slab::total_slabs();

    for (fs::FileDescriptor* fd : fd_table) {
        fd->inode->close(fd);
    }

    arch::vmm::free_user_pml4(pml4);
    delete[] kernel_stack;

    const std::size_t frames_after = pmm::get_free_frames();
    const std::size_t slabs_after = slab::total_slabs();
    const std::size_t frames_diff = frames_after - frames_before;

    log::debugf("PMM frames: {} -> {} (+{})", frames_before, frames_after, frames_diff);
    log::debugf("Slabs: {} -> {}", slabs_before, slabs_after);
    log::debugf("========================================");
}

}

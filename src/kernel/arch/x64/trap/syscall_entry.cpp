/**
 * @file syscall_entry.cpp
 * @brief SYSCALL/SYSRET configuration and syscall dispatch.
 *
 * This file configures the x86-64 SYSCALL instruction and provides the C++
 * dispatcher that routes system calls to their implementations. The actual
 * entry point is in syscall_entry.s (assembly), which handles the low-level
 * register save/restore and stack switching.
 *
 * SYSCALL vs INT 0x80:
 *
 *   SYSCALL is the modern, fast system call mechanism for x86-64. Unlike the
 *   legacy INT 0x80 (software interrupt), SYSCALL doesn't push an interrupt
 *   frame or switch stacks automatically — it just jumps to the handler with
 *   minimal overhead. This makes it ~10x faster but requires careful setup.
 *
 * MSR Configuration:
 *
 *   Four MSRs control SYSCALL behavior:
 *
 *   ┌────────────────────────────────────────────────────────────────────────┐
 *   │ MSR_EFER (0xC0000080) — Extended Feature Enable Register               │
 *   │   Bit 0 (SCE): Enable SYSCALL/SYSRET instructions                      │
 *   ├────────────────────────────────────────────────────────────────────────┤
 *   │ MSR_STAR (0xC0000081) — Segment Selectors                              │
 *   │   [63:48] SYSRET CS/SS base (add 16 for CS, add 8 for SS)              │
 *   │   [47:32] SYSCALL CS/SS (CS = value, SS = value + 8)                   │
 *   │   [31:0]  Reserved                                                     │
 *   │                                                                        │
 *   │   Our GDT layout:                                                      │
 *   │     0x08 = kernel code    0x10 = kernel data                           │
 *   │     0x18 = user data      0x20 = user code                             │
 *   │                                                                        │
 *   │   SYSRET forces RPL=3 onto the resulting CS selector, but NOT onto     │
 *   │   SS — SS gets (base+8) loaded completely raw. So the base must        │
 *   │   already encode RPL 3 for SS to come out right; we get CS "for       │
 *   │   free" either way since hardware fixes it up regardless.              │
 *   │                                                                        │
 *   │   STAR = (0x13 << 48) | (0x08 << 32)                                   │
 *   │     SYSCALL: CS=0x08, SS=0x10 (kernel)                                 │
 *   │     SYSRET:  CS=0x13+16=0x23, SS=0x13+8=0x1B (user, RPL=3 both)        │
 *   ├────────────────────────────────────────────────────────────────────────┤
 *   │ MSR_LSTAR (0xC0000082) — Long Mode SYSCALL Target                      │
 *   │   Address of syscall_entry (in syscall_entry.s)                        │
 *   ├────────────────────────────────────────────────────────────────────────┤
 *   │ MSR_SFMASK (0xC0000084) — SYSCALL Flag Mask                            │
 *   │   Bits set here are CLEARED in RFLAGS on SYSCALL entry:                │
 *   │     IF (bit 9):  Disable interrupts on entry                           │
 *   │     DF (bit 10): Clear direction flag (string ops go forward)          │
 *   │     TF (bit 8):  Disable single-stepping                               │
 *   └────────────────────────────────────────────────────────────────────────┘
 *
 * Syscall Flow:
 *
 *   Userspace                    Kernel
 *   ─────────                    ──────
 *   mov $SYS_WRITE, %rax
 *   mov fd, %rdi
 *   mov buf, %rsi
 *   mov count, %rdx
 *   syscall ─────────────────────┐
 *                                │
 *                                ▼
 *                          syscall_entry (entry.s)
 *                                │
 *                                │ 1. swapgs (get per-CPU data)
 *                                │ 2. Save user RSP, load kernel RSP
 *                                │ 3. Push all registers (SyscallFrame)
 *                                │ 4. Call syscall_dispatcher
 *                                │
 *                                ▼
 *                          syscall_dispatcher (this file)
 *                                │
 *                                │ Switch on RAX (syscall number)
 *                                │ Call sys_read/sys_write/etc.
 *                                │ Return result in RAX
 *                                │
 *                                ▼
 *                          syscall_entry (return path)
 *                                │
 *                                │ 1. Pop registers (except RAX = result)
 *                                │ 2. Restore user RSP
 *                                │ 3. swapgs (restore user GS)
 *                                │ 4. sysretq
 *                                │
 *   ◀────────────────────────────┘
 *   (RAX = return value)
 *
 * Syscall Numbers:
 *
 *   We use Linux syscall numbers where possible for familiarity. This doesn't
 *   provide binary compatibility (we'd need matching semantics), but it makes
 *   the interface recognizable to anyone who knows Linux.
 *
 * @see syscall_entry.s for the assembly entry point
 * @see percpu.hpp for per-CPU data structure used for stack switching
 */

#include "syscall_entry.hpp"

#include <arch/x64/cpu/cpu.hpp>
#include <arch/x64/percpu/percpu.hpp>
#include <fmt/fmt.hpp>
#include <linux/ioctl.hpp>
#include <linux/syscall.hpp>
#include <log/log.hpp>
#include <process/process.hpp>
#include <syscall/sys_fd.hpp>
#include <syscall/sys_mem.hpp>
#include <syscall/sys_prctl.hpp>
#include <syscall/sys_proc.hpp>
#include <syscall/sys_sleep.hpp>
#include <syscall/sys_thread.hpp>

#include <cerrno>
#include <cstdint>

extern "C" void syscall_entry();

static const char* syscall_name(std::uint64_t syscall_num)
{
    switch (syscall_num) {
    case linux::SYS_READ:
        return "read";
    case linux::SYS_WRITE:
        return "write";
    case linux::SYS_OPEN:
        return "open";
    case linux::SYS_CLOSE:
        return "close";
    case linux::SYS_STAT:
        return "stat";
    case linux::SYS_FSTAT:
        return "fstat";
    case linux::SYS_LSEEK:
        return "lseek";
    case linux::SYS_NANOSLEEP:
        return "nanosleep";
    case linux::SYS_GETPID:
        return "getpid";
    case linux::SYS_MMAP:
        return "mmap";
    case linux::SYS_MUNMAP:
        return "munmap";
    case linux::SYS_IOCTL:
        return "ioctl";
    case linux::SYS_READV:
        return "readv";
    case linux::SYS_WRITEV:
        return "writev";
    case linux::SYS_BRK:
        return "brk";
    case linux::SYS_EXIT:
        return "exit";
    case linux::SYS_EXIT_GROUP:
        return "exit_group";
    case linux::SYS_GETCWD:
        return "getcwd";
    case linux::SYS_CHDIR:
        return "chdir";
    case linux::SYS_MKDIR:
        return "mkdir";
    case linux::SYS_ARCH_PRCTL:
        return "arch_prctl";
    case linux::SYS_SET_TID_ADDR:
        return "set_tid_addr";
    case linux::SYS_FCHDIR:
        return "fchdir";
    case linux::SYS_FNCTL:
        return "fnctl";
    case linux::SYS_GETDENTS64:
        return "getdents64";
    case linux::SYS_FORK:
        return "fork";
    case linux::SYS_VFORK:
        return "vfork";
    case linux::SYS_WAIT4:
        return "wait4";
    case linux::SYS_EXECVE:
        return "execve";
    default:
        return "unknown syscall";
    }
}

/**
 * @brief Routes syscalls to their implementations based on syscall number.
 * @param frame Pointer to saved registers on kernel stack (built by syscall_entry.s).
 * @return Syscall result (placed in RAX by syscall_entry.s before sysretq).
 *
 * Called from syscall_entry.s after registers are saved. The syscall number is
 * in frame->rax, arguments in frame->rdi, rsi, rdx (matching System V ABI).
 */
extern "C" std::uint64_t syscall_dispatcher(x64::trap::SyscallFrame* frame)
{
    const std::uint64_t syscall_num = frame->rax;
    const std::uint64_t arg1 = frame->rdi;
    const std::uint64_t arg2 = frame->rsi;
    const std::uint64_t arg3 = frame->rdx;
    const std::uint64_t arg4 = frame->r10;
    const std::uint64_t arg5 = frame->r8;
    const std::uint64_t arg6 = frame->r9;

    log::debugf("**** syscall entry ****");
    log::debugf("* syscall  = {} ({})", syscall_name(syscall_num), syscall_num);
    log::debugf("* arg1 = {}", fmt::hex{arg1});
    log::debugf("* arg2 = {}", fmt::hex{arg2});
    log::debugf("* arg3 = {}", fmt::hex{arg3});
    log::debugf("* arg4 = {}", fmt::hex{arg4});
    log::debugf("* arg5 = {}", fmt::hex{arg5});
    log::debugf("* arg6 = {}", fmt::hex{arg6});

    switch (syscall_num) {
    case linux::SYS_READ:
        return syscall::sys_read(arg1, reinterpret_cast<char*>(arg2), arg3);
    case linux::SYS_WRITE:
        return syscall::sys_write(arg1, reinterpret_cast<const char*>(arg2), arg3);
    case linux::SYS_OPEN:
        return syscall::sys_open(reinterpret_cast<const char*>(arg1), arg2);
    case linux::SYS_CLOSE:
        return syscall::sys_close(arg1);
    case linux::SYS_STAT:
        return syscall::sys_stat(reinterpret_cast<const char*>(arg1), reinterpret_cast<fs::Stat*>(arg2));
    case linux::SYS_FSTAT:
        return syscall::sys_fstat(arg1, reinterpret_cast<fs::Stat*>(arg2));
    case linux::SYS_LSEEK:
        return syscall::sys_lseek(arg1, arg2, arg3);
    case linux::SYS_NANOSLEEP:
        return syscall::sys_sleep_ms(arg1);
    case linux::SYS_GETPID:
        return syscall::sys_getpid();
    case linux::SYS_MMAP:
        return syscall::sys_mmap(reinterpret_cast<void*>(arg1), arg2, arg3, arg4, arg5, arg6);
    case linux::SYS_MUNMAP:
        return syscall::sys_munmap(reinterpret_cast<void*>(arg1), arg2);
    case linux::SYS_IOCTL:
        return syscall::sys_ioctl(arg1, arg2, reinterpret_cast<void*>(arg3));
    case linux::SYS_READV:
        return syscall::sys_readv(arg1, reinterpret_cast<const linux::iovec*>(arg2), arg3);
    case linux::SYS_WRITEV:
        return syscall::sys_writev(arg1, reinterpret_cast<const linux::iovec*>(arg2), arg3);
    case linux::SYS_BRK:
        return syscall::sys_brk(reinterpret_cast<void*>(arg1));
    case linux::SYS_EXIT:
    case linux::SYS_EXIT_GROUP:
        return syscall::sys_exit(arg1);
    case linux::SYS_GETCWD:
        return syscall::sys_getcwd(reinterpret_cast<char*>(arg1), arg2);
    case linux::SYS_CHDIR:
        return syscall::sys_chdir(reinterpret_cast<const char*>(arg1));
    case linux::SYS_MKDIR:
        return syscall::sys_mkdir(reinterpret_cast<const char*>(arg1), arg2);
    case linux::SYS_ARCH_PRCTL:
        return syscall::sys_arch_prctl(arg1, arg2);
    case linux::SYS_SET_TID_ADDR:
        return syscall::sys_set_tid_address(reinterpret_cast<int*>(arg1));
    case linux::SYS_FCHDIR:
        return syscall::sys_fchdir(arg1);
    case linux::SYS_FNCTL:
        return syscall::sys_fcntl(arg1, arg2, arg3);
    case linux::SYS_GETDENTS64:
        return syscall::sys_getdents64(arg1, reinterpret_cast<void*>(arg2), arg3);
    case linux::SYS_FORK:
        return syscall::sys_fork(frame);
    case linux::SYS_VFORK:
        return syscall::sys_vfork();
    case linux::SYS_WAIT4:
        return syscall::sys_wait4(arg1, reinterpret_cast<int*>(arg2), arg3, reinterpret_cast<void*>(arg4));
    case linux::SYS_EXECVE:
        return syscall::sys_execve(reinterpret_cast<const char*>(arg1), reinterpret_cast<char**>(arg2), nullptr);
    default:
        log::error("Unsupported syscall: ", syscall_num);
        return -ENOSYS;
    }
}

namespace x64::trap {
/**
 * @brief Configures the CPU for SYSCALL/SYSRET operation.
 *
 * Programs the four MSRs that control SYSCALL behavior (see file header for
 * detailed MSR documentation). Must be called once during boot, before any
 * userspace code runs.
 */
void init()
{
    // STAR MSR encodes segment selectors for both SYSCALL and SYSRET:
    //   [63:48] = 0x13: SYSRET base. CPU computes CS=base+16=0x23, SS=base+8=0x1B
    //   [47:32] = 0x08: SYSCALL target. CS=0x08 (kernel code), SS=CS+8=0x10 (kernel data)
    // SYSRET forces RPL=3 onto CS, but loads SS raw (no RPL forcing) — base is
    // 0x13 (not 0x10) so base+8 already lands on 0x1B instead of 0x18.
    std::uint64_t star = (0x13UL << 48) | (0x08UL << 32);
    std::uint64_t lstar = reinterpret_cast<std::uint64_t>(syscall_entry);
    std::uint64_t sfmask = SFMASK_DF | SFMASK_IF | SFMASK_TF;
    std::uint64_t efer = cpu::rdmsr(MSR_EFER) | EFER_SCE;

    cpu::wrmsr(MSR_STAR, star);
    cpu::wrmsr(MSR_LSTAR, lstar);
    cpu::wrmsr(MSR_SFMASK, sfmask);
    cpu::wrmsr(MSR_EFER, efer);

    log::infof("syscall: LSTAR  @ {}", fmt::hex{lstar});
    log::infof("syscall: STAR   = {}", fmt::hex{star});
    log::infof("syscall: SFMASK = {}", fmt::bin{sfmask});
    log::infof("syscall: EFER   = {}", fmt::bin{efer});
}

}

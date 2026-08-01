# ============================================================================
# context_switch.s — Cooperative context switching for kernel threads
# ============================================================================
#
# This file implements the low-level context switch used by yield_blocked()
# when a process voluntarily gives up the CPU (e.g., waiting for keyboard
# input). This is DIFFERENT from the timer-interrupt-based preemptive
# switching in schedule() — see the comparison below.
#
# Two Scheduling Mechanisms:
#
#   1. Preemptive (schedule.cpp via timer interrupt):
#      - Timer fires, CPU pushes interrupt frame automatically
#      - schedule() modifies that frame to point to a different process
#      - iretq "returns" to the new process
#      - The interrupt frame IS the saved context
#
#   2. Cooperative (yield_blocked via context_switch):
#      - Process calls yield_blocked() from kernel code
#      - We manually save registers to the stack, swap stack pointers
#      - "ret" resumes wherever the new process left off
#      - The ContextFrame on each kernel stack IS the saved context
#
# Why Two Mechanisms?
#
#   When a process blocks (e.g., sys_read waiting for input), we're deep in
#   kernel code — there's no interrupt frame to hijack. We need to save our
#   current kernel context and switch to another process's kernel context.
#   That's what context_switch() does.
#
# The ContextFrame:
#
#   When context_switch() is called, it pushes callee-saved registers onto
#   the current stack, then the "call" instruction pushed the return address.
#   The resulting stack layout matches ContextFrame:
#
#       +0x00  r15
#       +0x08  r14
#       +0x10  r13
#       +0x18  r12
#       +0x20  rbx
#       +0x28  rbp
#       +0x30  rip  <- return address from 'call context_switch'
#
#   For an EXISTING process that previously called context_switch(), this
#   frame contains the real saved state. The "ret" will return to wherever
#   that process was in yield_blocked().
#
#   For a NEW process that has never run, we CREATE a fake ContextFrame:
#
#       frame->rip = userspace_entry_trampoline
#       frame->r15 = user entry point
#       frame->r14 = user stack pointer
#
#   When context_switch() "restores" this, it pops r15/r14 with our values,
#   then "ret" jumps to the trampoline which uses r15/r14 to enter userspace.
#   Elegant: context_switch() doesn't know it's launching a new process.
#
# Why Only Callee-Saved Registers?
#
#   The System V ABI says: rbx, rbp, r12-r15 must be preserved across calls.
#   The caller (yield_blocked) expects these to survive. Caller-saved regs
#   (rax, rcx, rdx, rsi, rdi, r8-r11) are the caller's responsibility — the
#   compiler already saved any it cared about before calling us.
#
# ============================================================================

.code64

# ============================================================================
# context_switch(old_rsp_ptr, new_rsp)
# ============================================================================
#
# Saves current context to *old_rsp_ptr, loads context from new_rsp.
#
# Arguments (System V ABI):
#   rdi = pointer to where we store the old RSP (e.g., &process->kernel_rsp)
#   rsi = the new RSP to switch to (e.g., next_process->kernel_rsp)
#
# After this returns, we're running on a completely different stack with
# different saved registers. From the new process's perspective, it just
# "returned" from its own earlier call to context_switch().

.global context_switch
.type context_switch, @function
context_switch:
    # Save callee-saved registers (building ContextFrame on current stack)
    cli
    
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15

    # Save current stack pointer to *old_rsp_ptr
    # The caller will store this in the process struct for later resumption
    mov %rsp, (%rdi)

    # Load the new process's stack pointer
    # This stack has a ContextFrame at the top (either from a previous
    # context_switch call, or a fake frame we set up for new processes)
    mov %rsi, %rsp

    # Restore callee-saved registers from the new stack
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp

    sti

    # Return to wherever the new process left off
    ret

# ============================================================================
# permanent_context_switch(new_rsp)
# ============================================================================
#
# Loads context from new_rsp.
#
# Arguments (System V ABI):
#   rdi = the new RSP to switch to (e.g., next_process->kernel_rsp)
#
# After this returns, we're running on a completely different stack with
# different saved registers. From the new process's perspective, it just
# "returned" from its own earlier call to context_switch().

.global permanent_context_switch
.type permanent_context_switch, @function
permanent_context_switch:
    cli
    
    mov %rdi, %rsp

    # Restore callee-saved registers from the new stack
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp

    sti

    # Return to wherever the new process left off
    ret

# ============================================================================
# userspace_entry_trampoline
# ============================================================================
#
# Entry point for NEW processes that have never run before.
#
# When we create a new process, we set up a fake ContextFrame with:
#   - rip = this trampoline (so context_switch "returns" here)
#   - r15 = the user program's entry point
#   - r14 = the user program's stack pointer
#
# This trampoline builds an iretq frame and jumps to userspace.
#
# Why swapgs?
#
#   The process that called yield_blocked() did so from a syscall, which
#   means syscall_entry already did swapgs (GS now points to kernel per-CPU
#   data). Before we iretq to userspace, we must swap back so userspace gets
#   its own GS value.
#
#   If we forget swapgs here, the NEW process will have kernel GS in
#   userspace. When it makes a syscall, syscall_entry does swapgs and gets
#   garbage. The symptom is usually a page fault at a low address (like 0x10)
#   when trying to save RSP to per-CPU data.

.global userspace_entry_trampoline
userspace_entry_trampoline:
    cli                     # Disable interrupts while building iretq frame
    swapgs                  # Restore user GS before returning to userspace

    # Build the iretq frame that will take us to userspace
    # Stack must contain (in push order): SS, RSP, RFLAGS, CS, RIP
    push $0x1B              # SS = user data segment (GDT index 3, RPL 3)
    push %r14               # RSP = user stack (passed via ContextFrame)
    push $0x202             # RFLAGS = IF set (interrupts enabled)
    push $0x23              # CS = user code segment (GDT index 4, RPL 3)
    push %r15               # RIP = user entry point (passed via ContextFrame)

    iretq                   # Pop all five values and jump to userspace

.global forked_entry_trampoline
forked_entry_trampoline:
    mov $0, %rax
    jmp syscall_exit

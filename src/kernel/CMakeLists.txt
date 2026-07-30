# Architecture-specific settings
set(KERNEL_ARCH "x64" CACHE STRING "Target architecture")
set(ARCH_DIR "arch/${KERNEL_ARCH}")
set(ARCH_DRIVERS_DIR "arch/${KERNEL_ARCH}/drivers")
set(LIB_DIR "lib/")
set(TEST_DIR "test/")

# Kernel source files
set(KERNEL_SOURCES
  kernel.cpp
)

# Architecture-specific kernel sources
set(ARCH_KERNEL_SOURCES
  ${ARCH_DIR}/cpu/cpu.cpp
  ${ARCH_DIR}/gdt/gdt.cpp
  ${ARCH_DIR}/gdt/gdt.s
  ${ARCH_DIR}/interrupts/idt.cpp
  ${ARCH_DIR}/interrupts/irq.cpp
  ${ARCH_DIR}/interrupts/isr.s
  ${ARCH_DIR}/trap/syscall_entry.cpp
  ${ARCH_DIR}/trap/syscall_entry.s
  ${ARCH_DIR}/percpu/percpu.cpp
  ${ARCH_DIR}/memory/vmm.cpp
  ${ARCH_DIR}/tls/tls.cpp
  ${ARCH_DIR}/context/context_switch.s
)

set(ARCH_DRIVER_KERNEL_SOURCES
  ${ARCH_DRIVERS_DIR}/pic/pic.cpp
  ${ARCH_DRIVERS_DIR}/pit/pit.cpp
  ${ARCH_DRIVERS_DIR}/apic/apic.cpp
  ${ARCH_DRIVERS_DIR}/serial/serial.cpp
  ${ARCH_DRIVERS_DIR}/keyboard/keyboard.cpp
  ${ARCH_DRIVERS_DIR}/keyboard/ps2.cpp
  ${ARCH_DRIVERS_DIR}/tsc/tsc.cpp
)

# C runtime sources
set(CRT_SOURCES
  ${LIB_DIR}/crt/crt.c
)

# General purpose kernel library sources (not arch specific)
set(LIB_KERNEL_SOURCES
  ${LIB_DIR}/boot/limine-boot.cpp
  ${LIB_DIR}/containers/kstring.cpp
  ${LIB_DIR}/containers/kstring_view.cpp
  ${LIB_DIR}/timer/timer.cpp
  ${LIB_DIR}/kpanic/kpanic.cpp
  ${LIB_DIR}/memory/memory.cpp
  ${LIB_DIR}/memory/pmm.cpp
  ${LIB_DIR}/memory/slab.cpp
  ${LIB_DIR}/memory/new.cpp
  ${LIB_DIR}/console/console.cpp
  ${LIB_DIR}/console/ansi.cpp
  ${LIB_DIR}/framebuffer/framebuffer.cpp
  ${LIB_DIR}/acpi/acpi.cpp
  ${LIB_DIR}/acpi/madt.cpp
  ${LIB_DIR}/acpi/hpet.cpp
  ${LIB_DIR}/algo/algo.cpp
  ${LIB_DIR}/fs/fs.cpp
  ${LIB_DIR}/fs/initramfs/initramfs.cpp
  ${LIB_DIR}/fs/initramfs/tar.cpp
  ${LIB_DIR}/fs/devfs/devfs.cpp
  ${LIB_DIR}/fs/devfs/dev_tty.cpp
  ${LIB_DIR}/fs/devfs/dev_null.cpp
  ${LIB_DIR}/fs/tmpfs/tmpfs.cpp
  ${LIB_DIR}/fs/procfs/procfs.cpp
  ${LIB_DIR}/fs/procfs/proc_self.cpp
  ${LIB_DIR}/process/elf.cpp
  ${LIB_DIR}/process/process.cpp
  ${LIB_DIR}/syscall/sys_fd.cpp
  ${LIB_DIR}/syscall/sys_sleep.cpp
  ${LIB_DIR}/syscall/sys_proc.cpp
  ${LIB_DIR}/syscall/sys_mem.cpp
  ${LIB_DIR}/syscall/sys_prctl.cpp
  ${LIB_DIR}/syscall/sys_thread.cpp
  ${LIB_DIR}/scheduler/mechanism/scheduler_mechanism.cpp
  ${LIB_DIR}/scheduler/policy/round_robin.cpp
  ${LIB_DIR}/gfx/gfx.cpp
)

# Test sources (only compiled when KERNEL_TESTS is ON)
option(KERNEL_TESTS "Build kernel tests" ON)
set(TEST_SOURCES "")
if(KERNEL_TESTS)
  file(GLOB_RECURSE TEST_SOURCES ${TEST_DIR}/*.cpp)
endif()

# Architecture-specific boot files
set(BOOT_SOURCES
  ${ARCH_DIR}/boot/limine_entry.cpp
)

# Linker script path — exposed to parent scope for the link command in root CMakeLists.txt
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/${ARCH_DIR}/limine.ld")
set(LINKER_SCRIPT "${LINKER_SCRIPT}" PARENT_SCOPE)

# Include directories
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}           # Allows "arch/x64/..." includes
)

# Create boot object library
add_library(boot_objs OBJECT ${BOOT_SOURCES})

# Create CRT object libraries
add_library(crti_obj OBJECT ${ARCH_DIR}/crt/crti.s)
add_library(crtn_obj OBJECT ${ARCH_DIR}/crt/crtn.s)

# Create kernel object library
add_library(kernel_objs OBJECT
  ${KERNEL_SOURCES}
  ${CRT_SOURCES}
  ${ARCH_DRIVER_KERNEL_SOURCES}
  ${ARCH_KERNEL_SOURCES}
  ${LIB_KERNEL_SOURCES}
  ${TEST_SOURCES}
)

# Kernel test compile definitions
option(KERNEL_TESTS_QUIET "Suppress passing test output" ON)
if(KERNEL_TESTS)
  target_compile_definitions(kernel_objs PRIVATE KERNEL_TESTS)
  if(KERNEL_TESTS_QUIET)
    target_compile_definitions(kernel_objs PRIVATE KERNEL_TESTS_QUIET)
  endif()
endif()

# Enable debug assertions (bounds checking, etc.)
option(KERNEL_DEBUG "Enable debug assertions" OFF)
if(KERNEL_DEBUG)
  target_compile_definitions(kernel_objs PRIVATE KERNEL_DEBUG)
  message(STATUS "Debug assertions: ENABLED")
endif()

# Print build information
message(STATUS "Kernel architecture: ${KERNEL_ARCH}")
message(STATUS "Linker script: ${CMAKE_CURRENT_SOURCE_DIR}/${ARCH_DIR}/limine.ld")
message(STATUS "Boot sources: ${BOOT_SOURCES}")

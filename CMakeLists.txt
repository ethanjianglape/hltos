cmake_minimum_required(VERSION 3.16)

# Project definition
project(hltos ASM C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Kernel code must never be position-independent; this prevents CMake from
# injecting -fPIC into OBJECT library targets, which conflicts with -mcmodel=kernel
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Override ASM compile rule to suppress unwanted default flags
set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>")

# Kernel compile flags
add_compile_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-m64>
  $<$<COMPILE_LANGUAGE:C,CXX>:-march=native>
  $<$<COMPILE_LANGUAGE:C,CXX>:-mcmodel=kernel>
  $<$<COMPILE_LANGUAGE:C,CXX>:-mabi=sysv>
  $<$<COMPILE_LANGUAGE:C,CXX>:-mno-red-zone>
  $<$<COMPILE_LANGUAGE:C,CXX>:-mgeneral-regs-only>
  $<$<COMPILE_LANGUAGE:C,CXX>:-mno-sse>
  $<$<COMPILE_LANGUAGE:C,CXX>:-ffreestanding>
  $<$<COMPILE_LANGUAGE:C,CXX>:-ffunction-sections>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-stack-protector>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-pie>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-PIC>
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wall>
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>
  $<$<COMPILE_LANGUAGE:C,CXX>:-g>
  $<$<COMPILE_LANGUAGE:C,CXX>:-O1>
)

# C++ specific flags
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
)

# Assembly specific flags
add_compile_options(
  $<$<COMPILE_LANGUAGE:ASM>:--64>  
)

# Sysroot configuration
set(SYSROOT "${CMAKE_SOURCE_DIR}/sysroot" CACHE PATH "Sysroot directory")

# Include directories (before add_subdirectory so subdirs inherit them)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add kernel subdirectory — also sets LINKER_SCRIPT in this scope via PARENT_SCOPE
add_subdirectory(src/kernel)

# Add userspace programs (compiled separately, output to initramfs/bin)
add_subdirectory(src/user)

# Create the kernel using a custom command with explicit link order
# Note: boot_objs, crti_obj, crtn_obj, and kernel_objs are all defined in src/kernel/
file(MAKE_DIRECTORY ${SYSROOT}/boot)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/kernel.elf
  BYPRODUCTS ${CMAKE_BINARY_DIR}/kernel.map
  COMMAND ${CMAKE_COMMAND} -E rm -f kernel.elf
  COMMAND ${CMAKE_CXX_COMPILER}
  -m64
  -T ${LINKER_SCRIPT}
  -nostdlib
  -static
  -no-pie
  -Wl,-Map=kernel.map
  -Wl,--build-id=none
  -z noexecstack
  -o kernel.elf
  "$<TARGET_OBJECTS:crti_obj>"
  "$<TARGET_OBJECTS:boot_objs>"
  "$<TARGET_OBJECTS:kernel_objs>"
  "$<TARGET_OBJECTS:crtn_obj>"
  -lgcc
  COMMAND ${CMAKE_COMMAND} -E remove -f ${SYSROOT}/boot/kernel.elf
  COMMAND ${CMAKE_COMMAND} -E copy kernel.elf ${SYSROOT}/boot/kernel.elf
  DEPENDS
  boot_objs kernel_objs crti_obj crtn_obj
  "$<TARGET_OBJECTS:boot_objs>"
  "$<TARGET_OBJECTS:kernel_objs>"
  "$<TARGET_OBJECTS:crti_obj>"
  "$<TARGET_OBJECTS:crtn_obj>"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Linking kernel, creating binary, and copying to sysroot"
  COMMAND_EXPAND_LISTS
  VERBATIM
)

add_custom_target(kernel ALL DEPENDS ${CMAKE_BINARY_DIR}/kernel.elf)

# ISO creation
include(FetchContent)
FetchContent_Declare(limine
  URL      https://github.com/Limine-Bootloader/Limine/releases/download/v12.3.2/limine-binary.tar.gz
  URL_HASH SHA256=433ac5933ef7918b6a6751db0a81e47fa122581dfb32430bc257be686f3906e5
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(limine)

set(LIMINE_SHARE ${limine_SOURCE_DIR})

find_program(XORRISO xorriso REQUIRED)

# Compile the limine host tool from the single-file source bundled in the release
set(LIMINE_CMD ${CMAKE_BINARY_DIR}/limine-host/limine)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/limine-host)
add_custom_command(
  OUTPUT ${LIMINE_CMD}
  COMMAND cc -o ${LIMINE_CMD} ${LIMINE_SHARE}/limine.c
  DEPENDS ${LIMINE_SHARE}/limine.c
  COMMENT "Building limine host tool"
)

set(LIMINE_CONF ${CMAKE_SOURCE_DIR}/src/bootloader/limine/limine.conf)
file(GLOB_RECURSE INITRAMFS_STATIC_FILES ${CMAKE_SOURCE_DIR}/initramfs/etc/*)

add_custom_command(
  OUTPUT ${CMAKE_SOURCE_DIR}/hltos.iso
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT}/boot/limine
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT}/EFI/BOOT
  COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_SHARE}/limine-bios.sys    ${SYSROOT}/boot/limine/
  COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_SHARE}/limine-bios-cd.bin ${SYSROOT}/boot/limine/
  COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_SHARE}/limine-uefi-cd.bin ${SYSROOT}/boot/limine/
  COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_SHARE}/BOOTX64.EFI        ${SYSROOT}/EFI/BOOT/
  COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_CONF}                     ${SYSROOT}/limine.conf
  COMMAND tar -cf ${SYSROOT}/boot/initramfs.tar -C ${CMAKE_SOURCE_DIR}/initramfs .
  COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_SOURCE_DIR}/hltos.iso
  COMMAND ${XORRISO} -as mkisofs
  -R -r -J -b boot/limine/limine-bios-cd.bin
  -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus
  -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin
  -efi-boot-part --efi-boot-image --protective-msdos-label
  -o ${CMAKE_SOURCE_DIR}/hltos.iso ${SYSROOT}/
  COMMAND ${LIMINE_CMD} bios-install ${CMAKE_SOURCE_DIR}/hltos.iso
  DEPENDS
  ${CMAKE_BINARY_DIR}/kernel.elf
  ${LIMINE_CONF}
  ${INITRAMFS_STATIC_FILES}
  ${CMAKE_SOURCE_DIR}/initramfs/bin/shell
  ${CMAKE_SOURCE_DIR}/initramfs/bin/musl
  ${LIMINE_CMD}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Creating bootable ISO"
  VERBATIM
)
add_custom_target(iso ALL DEPENDS ${CMAKE_SOURCE_DIR}/hltos.iso)
add_dependencies(iso kernel)


# Output directory for user binaries
set(USER_BIN_DIR "${CMAKE_SOURCE_DIR}/initramfs/bin")

find_program(MUSL_GCC musl-gcc REQUIRED)

set(MUSL_GCC_FLAGS -static -march=native -mabi=sysv)

include(CheckCCompilerFlag)
check_c_compiler_flag(-fno-link-libatomic HAS_NO_LINK_LIBATOMIC)

if (HAS_NO_LINK_LIBATOMIC)
  list(PREPEND MUSL_GCC_FLAGS -fno-link-libatomic)
endif()

# Function to add a musl libc program (compiled with musl-gcc, statically linked)
function(add_musl_program name source)
    add_custom_command(
        OUTPUT ${USER_BIN_DIR}/${name}
        COMMAND ${MUSL_GCC} ${MUSL_GCC_FLAGS} -o ${USER_BIN_DIR}/${name} ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
    )
    add_custom_target(${name} ALL DEPENDS ${USER_BIN_DIR}/${name})
endfunction()

# Add userspace programs
add_musl_program(shell shell.c)
add_musl_program(musl musl.c)
add_musl_program(ls ls.c)
add_musl_program(cat cat.c)
add_musl_program(mkdir mkdir.c)
add_musl_program(init init.c)

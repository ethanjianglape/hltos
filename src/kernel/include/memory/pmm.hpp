#include <arch.hpp>

#include <cstddef>
#include <cstdint>

namespace pmm {

void init();

void add_free_memory(std::size_t addr, std::size_t len);
void set_addr_free(std::size_t addr, std::size_t length);

std::size_t get_total_memory();
std::size_t get_free_frames();

void free_frame(std::uintptr_t phys);

std::uintptr_t alloc_frame();

}

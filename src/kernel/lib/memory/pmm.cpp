/**
 * @file pmm.cpp
 * @brief Physical Memory Manager — bitmap-based frame allocator.
 *
 * The PMM tracks which 4KiB physical memory frames are free or in use.
 * It uses a bitmap where each bit represents one frame:
 *   - 0 = free
 *   - 1 = used
 *
 * During boot, the bootloader (Limine) tells us which memory regions are
 * usable. We mark those as free in the bitmap, then allocate from them
 * as needed for page tables, kernel data structures, etc.
 *
 * This is a simple first-fit allocator. For contiguous allocations, it
 * scans for consecutive free frames.
 */

#include "exclusive/kspinlock_irqsave.hpp"
#include <arch.hpp>
#include <fmt/fmt.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>
#include <memory/pmm.hpp>

#include <cstddef>
#include <cstdint>

namespace pmm {

// For now, the PMM will have a hard coded upper limit of 2GiB
// attempting to access beyond 2GiB will just truncate
constexpr std::size_t MAX_MEMORY_BYTES = 2'147'483'648;
constexpr std::size_t FRAME_SIZE = arch::vmm::PAGE_SIZE;
constexpr std::size_t MAX_NUM_FRAMES = MAX_MEMORY_BYTES / FRAME_SIZE;

constexpr std::size_t FRAME_BITMAP_ENTRY_SIZE = sizeof(std::size_t) * 8;
constexpr std::size_t FRAME_BITMAP_SIZE = MAX_NUM_FRAMES / FRAME_BITMAP_ENTRY_SIZE;
constexpr std::size_t FRAME_FREE = 0;
constexpr std::size_t FRAME_USED = 1;

// Bitmap where each bit represents a 4KiB frame (0 = free, 1 = used)
static std::size_t frame_bitmap[FRAME_BITMAP_SIZE];
static std::size_t frame_bitmap_start;
static std::size_t frame_bitmap_end;

static std::size_t total_memory;
static std::size_t total_frames;
static std::size_t free_frames;

static kspinlock_irqsave g_pmm_spinlock;

bool is_frame_free(std::size_t frame)
{
    const std::size_t index = frame / FRAME_BITMAP_ENTRY_SIZE;
    const std::size_t offset = frame % FRAME_BITMAP_ENTRY_SIZE;
    const std::size_t entry = frame_bitmap[index];
    const std::size_t value = entry & (FRAME_USED << offset);

    return value == FRAME_FREE;
}

void set_frame_used(std::size_t frame)
{
    const std::size_t index = frame / FRAME_BITMAP_ENTRY_SIZE;
    const std::size_t offset = frame % FRAME_BITMAP_ENTRY_SIZE;

    frame_bitmap[index] |= (FRAME_USED << offset);
    free_frames--;
}

void set_frame_free(std::size_t frame)
{
    const std::size_t index = frame / FRAME_BITMAP_ENTRY_SIZE;
    const std::size_t offset = frame % FRAME_BITMAP_ENTRY_SIZE;
    const std::size_t mask = ~(FRAME_USED << offset);

    frame_bitmap[index] &= mask;
    free_frames++;
}

void init()
{
    g_pmm_spinlock.lock();

    // all pages set to used by default
    for (std::size_t i = 0; i < FRAME_BITMAP_SIZE; i++) {
        frame_bitmap[i] = 0xFFFFFFFFFFFFFFFF;
    }

    total_memory = 0;
    total_frames = 0;
    free_frames = 0;

    g_pmm_spinlock.unlock();
}

/**
 * @brief Registers a region of physical memory as available for allocation.
 *
 * Called during boot for each usable memory region reported by Limine.
 * Regions beyond MAX_MEMORY_BYTES are truncated or ignored.
 *
 * @param addr Physical start address of the region.
 * @param len Length of the region in bytes.
 */
void add_free_memory(std::size_t addr, std::size_t len)
{
    g_pmm_spinlock.lock();

    std::uint64_t end = addr + len;

    if (end >= MAX_MEMORY_BYTES) {
        if (addr >= MAX_MEMORY_BYTES) {
            log::warn("Ignoring memory region at ", fmt::hex{addr}, " (beyond max)");
            goto cleanup;
        }

        log::warn("Truncating memory region from ", fmt::hex{end}, " to ", fmt::hex{MAX_MEMORY_BYTES});
        len = MAX_MEMORY_BYTES - addr;
    }

    total_memory += len;
    total_frames += (len / FRAME_SIZE) + 1;
    free_frames += total_frames;

    set_addr_free(addr, len);
    set_frame_used(0);

cleanup:
    g_pmm_spinlock.unlock();
}

std::size_t get_total_memory()
{
    return total_memory;
}

std::size_t get_free_frames()
{
    return free_frames;
}

void set_addr_free(std::size_t addr, std::size_t length)
{
    std::size_t frame_start = addr / FRAME_SIZE;
    std::size_t frame_end = (addr + length) / FRAME_SIZE;

    frame_bitmap_start = frame_start;
    frame_bitmap_end = frame_end;

    while (frame_start <= frame_end) {
        set_frame_free(frame_start++);
    }
}

void set_addr_used(std::size_t addr, std::size_t length)
{
    std::size_t frame_start = addr / FRAME_SIZE;
    std::size_t frame_end = (addr + length) / FRAME_SIZE;

    while (frame_start <= frame_end) {
        set_frame_used(frame_start++);
    }
}

void free_frame(std::uintptr_t phys)
{
    g_pmm_spinlock.lock();

    std::size_t frame = phys / FRAME_SIZE;
    set_frame_free(frame);

    g_pmm_spinlock.unlock();
}

/**
 * @brief Allocates a single 4KiB physical frame.
 *
 * Uses first-fit search starting from the last allocation point.
 *
 * @return Physical address of the allocated frame.
 * @throws Panics if no free frames are available.
 */
std::uintptr_t alloc_frame()
{
    g_pmm_spinlock.lock();

    std::uintptr_t frame = frame_bitmap_start;

    while (frame < frame_bitmap_end) {
        if (is_frame_free(frame)) {
            set_frame_used(frame);
            frame_bitmap_start = frame + 1;

            g_pmm_spinlock.unlock();

            return frame * FRAME_SIZE;
        }

        frame++;
    }

    kpanic("PMM: Out of physical memory");
}

}

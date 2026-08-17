/**
 * @file vmm.cpp
 * @brief Virtual Memory Manager — x86-64 paging and address space management.
 *
 * This module manages virtual memory through x86-64's 4-level page tables.
 * It provides functions to map/unmap pages, allocate kernel memory, and
 * create/switch address spaces for user processes.
 *
 * Page Table Hierarchy (4-level paging):
 *
 *   Virtual Address (48 bits used):
 *   ┌────────┬────────┬────────┬────────┬──────────────┐
 *   │ PML4   │ PDPT   │  PD    │  PT    │    Offset    │
 *   │ [47:39]│ [38:30]│ [29:21]│ [20:12]│    [11:0]    │
 *   └────────┴────────┴────────┴────────┴──────────────┘
 *      9 bits   9 bits   9 bits   9 bits    12 bits
 *
 *   CR3 → PML4 → PDPT → PD → PT → Physical Page
 *
 * Higher Half Direct Map (HHDM):
 *
 *   Limine maps all physical memory at a fixed virtual offset (typically
 *   0xFFFF800000000000). This lets us access any physical address by simply
 *   adding the HHDM offset. We use this to access page tables themselves,
 *   since they're stored in physical memory.
 *
 *   phys_to_virt(phys) = phys + hhdm_offset
 *   virt_to_phys(virt) = virt - hhdm_offset  (for HHDM addresses)
 */

#include "vmm.hpp"
#include "arch/x64/cpu/cpu.hpp"

#include <exclusive/kspinlock_irqsave.hpp>
#include <fmt/fmt.hpp>
#include <kassert/kassert.hpp>
#include <log/log.hpp>
#include <memory/memory.hpp>
#include <memory/pmm.hpp>

#include <cstddef>
#include <cstdint>

namespace x64::vmm {

static PML4E* kernel_pml4;

static std::uintptr_t hhdm_offset;

static Heap kheap{};

static kspinlock_irqsave g_vmm_lock{};

template <typename T>
static T hhdm_ptov(std::uintptr_t phys) { return reinterpret_cast<T>(phys + hhdm_offset); }

template <typename T>
static std::uintptr_t hhdm_vtop(T virt) { return reinterpret_cast<std::uintptr_t>(virt) - hhdm_offset; }

static std::size_t get_kernel_pml4_index() { return (hhdm_offset >> 39) & 0x1FF; }

static PDPTE* get_pdpt(const PML4E& pml4e)
{
    return hhdm_ptov<PDPTE*>(pml4e.addr << 12);
}

static PDE* get_pd(const PDPTE& pdpte)
{
    return hhdm_ptov<PDE*>(pdpte.addr << 12);
}

static PTE* get_pt(const PDE& pde)
{
    return hhdm_ptov<PTE*>(pde.addr << 12);
}

static std::uintptr_t get_pte_phys_frame(const PTE& pte)
{
    return pte.addr << 12;
}

static bool is_page_aligned(std::uintptr_t addr)
{
    return (addr & PAGE_MASK) == 0;
}

static std::uintptr_t page_align(std::uintptr_t addr)
{
    return addr & ~PAGE_MASK;
}

static std::uintptr_t page_align(void* virt_addr)
{
    return page_align(reinterpret_cast<std::uintptr_t>(virt_addr));
}

static std::uintptr_t get_next_heap_virt_page(Heap* heap)
{
    std::uintptr_t virt_page = 0;

    virt_page |= ((heap->pml4_idx & 0x1FF) << 39);
    virt_page |= ((heap->pdpt_idx & 0x1FF) << 30);
    virt_page |= ((heap->pd_idx & 0x1FF) << 21);
    virt_page |= ((heap->pt_idx & 0x1FF) << 12);

    const std::uintptr_t bit_47 = virt_page & (1ULL << 47);

    if (bit_47) {
        virt_page |= (0xFFFFUL << 48);
    }

    kassert(is_page_aligned(virt_page));

    return virt_page;
}

/// @brief advances the heap pml4 indexes to the next available one
///
static void advance_heap(Heap* heap)
{
    heap->pt_idx++;

    if (heap->pt_idx < NUM_PT_ENTRIES) {
        return;
    }

    heap->pt_idx = 0;
    heap->pd_idx++;

    if (heap->pd_idx < NUM_PT_ENTRIES) {
        return;
    }

    heap->pd_idx = 0;
    heap->pdpt_idx++;

    if (heap->pdpt_idx < NUM_PT_ENTRIES) {
        return;
    }

    heap->pdpt_idx = 0;
    heap->pml4_idx++;
}

/**
 * @brief Walks the page table hierarchy to find the PTE for a virtual address.
 * @param pml4 The top-level page table to walk.
 * @param virt The virtual address to look up.
 * @return Pointer to the page table entry, or nullptr if not mapped.
 */
static PTE* find_pte(PML4E* pml4, std::uintptr_t virt)
{
    const std::size_t pml4_idx = (virt >> 39) & 0x1FF;
    const std::size_t pdpt_idx = (virt >> 30) & 0x1FF;
    const std::size_t pd_idx = (virt >> 21) & 0x1FF;
    const std::size_t pt_idx = (virt >> 12) & 0x1FF;

    if (!pml4[pml4_idx].p) {
        return nullptr;
    }

    PDPTE* pdpt = get_pdpt(pml4[pml4_idx]);

    if (!pdpt[pdpt_idx].p) {
        return nullptr;
    }

    PDE* pd = get_pd(pdpt[pdpt_idx]);

    if (!pd[pd_idx].p) {
        return nullptr;
    }

    PTE* pt = get_pt(pd[pd_idx]);

    if (!pt[pt_idx].p) {
        return nullptr;
    }

    return &pt[pt_idx];
}

static void populate_pml4e(PML4E& pml4e, std::uint64_t phys_frame, int flags)
{
    kassert(is_page_aligned(phys_frame));
    pml4e.p = 1;
    pml4e.rw = 1;
    pml4e.us = (flags & PAGE_USER) ? 1 : 0;
    pml4e.pwt = 0;
    pml4e.pcd = 0;
    pml4e.a = 0;
    pml4e.addr = phys_frame >> 12;
}

static void populate_pdpte(PDPTE& pdpte, std::uint64_t phys_frame, int flags)
{
    kassert(is_page_aligned(phys_frame));
    pdpte.p = 1;
    pdpte.rw = 1;
    pdpte.us = (flags & PAGE_USER) ? 1 : 0;
    pdpte.pwt = 0;
    pdpte.pcd = 0;
    pdpte.a = 0;
    pdpte.ps = 0;
    pdpte.addr = phys_frame >> 12;
}

static void populate_pde(PDE& pde, std::uint64_t phys_frame, int flags)
{
    kassert(is_page_aligned(phys_frame));
    pde.p = 1;
    pde.rw = 1;
    pde.us = (flags & PAGE_USER) ? 1 : 0;
    pde.pwt = 0;
    pde.pcd = 0;
    pde.a = 0;
    pde.ps = 0;
    pde.addr = phys_frame >> 12;
}

static void populate_pte(PTE& pte, std::uint64_t phys_frame, int flags)
{
    kassert(is_page_aligned(phys_frame));
    pte.p = 1;
    pte.rw = (flags & PAGE_WRITE) ? 1 : 0;
    pte.us = (flags & PAGE_USER) ? 1 : 0;
    pte.pwt = 0;
    pte.pcd = (flags & PAGE_CACHE_DISABLE) ? 1 : 0;
    pte.a = 0;
    pte.d = 0;
    pte.pat = 0;
    pte.g = (flags & PAGE_GLOBAL) ? 1 : 0;
    pte.addr = phys_frame >> 12;
    pte.nx = (flags & PAGE_NX) ? 1 : 0;
}

static void free_pml4e(PML4E& pml4e)
{
    pmm::free_frame(pml4e.addr << 12);
    pml4e.p = 0;
}

static void free_pdpte(PDPTE& pdpte)
{
    pmm::free_frame(pdpte.addr << 12);
    pdpte.p = 0;
}

static void free_pde(PDE& pde)
{
    pmm::free_frame(pde.addr << 12);
    pde.p = 0;
}

static void free_pte(PTE& pte)
{
    pmm::free_frame(pte.addr << 12);
    pte.p = 0;
}

static void zero_page(std::uintptr_t* virt)
{
    kassert_not_null(virt);

    for (std::size_t i = 0; i < NUM_PT_ENTRIES; i++) {
        virt[i] = 0;
    }
}

static PDPTE* ensure_pdpte_present(PML4E& pml4e, int flags)
{
    if (!pml4e.p) {
        std::uintptr_t phys_frame = pmm::alloc_frame();
        populate_pml4e(pml4e, phys_frame, flags);
        zero_page(hhdm_ptov<std::uintptr_t*>(phys_frame));
    }

    return hhdm_ptov<PDPTE*>(pml4e.addr << 12);
}

static PDE* ensure_pde_present(PDPTE& pdpte, int flags)
{
    if (!pdpte.p) {
        std::uintptr_t phys_frame = pmm::alloc_frame();
        populate_pdpte(pdpte, phys_frame, flags);
        zero_page(hhdm_ptov<std::uintptr_t*>(phys_frame));
    }

    return hhdm_ptov<PDE*>(pdpte.addr << 12);
}

static PTE* ensure_pte_present(PDE& pde, int flags)
{
    if (!pde.p) {
        std::uintptr_t phys_frame = pmm::alloc_frame();
        populate_pde(pde, phys_frame, flags);
        zero_page(hhdm_ptov<std::uintptr_t*>(phys_frame));
    }

    return hhdm_ptov<PTE*>(pde.addr << 12);
}

static std::uintptr_t map_heap_page(PML4E* pml4, Heap* heap, std::uintptr_t phys_addr, int flags)
{
    kassert_not_null(pml4);
    kassert_not_null(heap);

    std::uintptr_t phys_frame = page_align(phys_addr);
    std::uintptr_t phys_offset = phys_addr & PAGE_MASK;
    std::uintptr_t virt_page = get_next_heap_virt_page(heap);

    std::size_t pml4_idx = heap->pml4_idx;
    std::size_t pdpt_idx = heap->pdpt_idx;
    std::size_t pd_idx = heap->pd_idx;
    std::size_t pt_idx = heap->pt_idx;

    PDPTE* pdpt = ensure_pdpte_present(pml4[pml4_idx], flags);
    PDE* pd = ensure_pde_present(pdpt[pdpt_idx], flags);
    PTE* pt = ensure_pte_present(pd[pd_idx], flags);

    populate_pte(pt[pt_idx], phys_frame, flags);
    advance_heap(heap);

    asm volatile("invlpg (%0)" : : "r"(virt_page) : "memory");

    return virt_page + phys_offset;
}

/// @brief Maps a physical memory address to a single page in the kernel heap
///
/// @param phys_addr the physical address
///
/// @return the mapped virtual address (page aligned)
///
std::uintptr_t map_phys(std::uintptr_t phys_addr, int flags)
{
    return map_heap_page(kernel_pml4, &kheap, phys_addr, flags | PAGE_WRITE | PAGE_GLOBAL);
}

/**
 * @brief Maps a virtual page to a physical frame in the given page table.
 *
 * Walks the 4-level page table, creating intermediate tables as needed,
 * then creates the final mapping. Invalidates the TLB entry for this address.
 *
 * @param pml4 The top-level page table (PML4) to modify.
 * @param virt Virtual address to map (must be page-aligned).
 * @param phys Physical address to map to (must be page-aligned).
 * @param flags Page flags (PAGE_PRESENT, PAGE_WRITE, PAGE_USER, etc.).
 */
static void map_page_to_frame(PML4E* pml4, std::uintptr_t virt_page, std::uintptr_t phys_frame, int flags)
{
    kassert_not_null(pml4);
    kassert(is_page_aligned(virt_page));
    kassert(is_page_aligned(phys_frame));

    const std::size_t pml4_idx = (virt_page >> 39) & 0x1FF;
    const std::size_t pdpt_idx = (virt_page >> 30) & 0x1FF;
    const std::size_t pd_idx = (virt_page >> 21) & 0x1FF;
    const std::size_t pt_idx = (virt_page >> 12) & 0x1FF;

    PDPTE* pdpt = ensure_pdpte_present(pml4[pml4_idx], flags);
    PDE* pd = ensure_pde_present(pdpt[pdpt_idx], flags);
    PTE* pt = ensure_pte_present(pd[pd_idx], flags);

    populate_pte(pt[pt_idx], phys_frame, flags);

    asm volatile("invlpg (%0)" : : "r"(virt_page) : "memory");
}

/**
 * @brief Maps pages at a specific virtual address, allocating physical frames.
 *
 *
 * @param pml4 Page table to modify.
 * @param virt Starting virtual address (may be unaligned; page-aligns down).
 * @param bytes Number of bytes to map.
 * @param flags Page flags (PAGE_USER, PAGE_WRITE, etc.).
 * @return Number of pages mapped (needed for unmap_mem_at).
 */
void map_pages(PML4E* pml4, std::uintptr_t virt_addr, std::size_t bytes, int flags)
{
    kassert_not_null(pml4);
    kassert(bytes > 0);

    g_vmm_lock.lock();

    std::uintptr_t page_start = virt_addr & ~(PAGE_SIZE - 1);
    std::uintptr_t page_end = (virt_addr + bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    std::size_t num_pages = (page_end - page_start) / PAGE_SIZE;

    log::infof("VMM: mapping pages @ [{} - {}] flags = {} ({} pages, {} bytes)",
        fmt::hex{virt_addr},
        fmt::hex{virt_addr + bytes},
        fmt::bin{flags},
        num_pages,
        bytes);

    kassert(is_page_aligned(page_start));
    kassert(is_page_aligned(page_end));
    kassert(page_start < page_end);
    kassert(num_pages > 0);

    for (std::size_t page = 0; page < num_pages; page++) {
        std::uintptr_t phys_frame = pmm::alloc_frame();

        map_page_to_frame(pml4, page_start + (page * PAGE_SIZE), phys_frame, flags);
    }

    g_vmm_lock.unlock();
}

void map_user_pages(PML4E* pml4, std::uintptr_t virt, std::size_t bytes)
{
    map_pages(pml4, virt, bytes, PAGE_USER | PAGE_WRITE);
}

/**
 * @brief Unmaps a virtual address and frees its physical frame.
 *  
 * @param pml4 The page table to modify.
 * @param virt Virtual address to unmap.
 */
static void unmap_page(PML4E* pml4, std::uintptr_t virt_page)
{
    kassert_not_null(pml4);
    kassert(is_page_aligned(virt_page));

    PTE* pte = find_pte(pml4, virt_page);

    if (pte == nullptr) {
        log::warn("Attempt to unmap virt addr that is not mapped: ", fmt::hex{virt_page});
        return;
    }

    pmm::free_frame(get_pte_phys_frame(*pte));
    *pte = {};
    asm volatile("invlpg (%0)" : : "r"(virt_page) : "memory");
}

/**
 * @brief Allocates a single raw 4KB page.
 *
 * Returns a page with no header or metadata — the caller gets the full 4KB.
 * Use free_kernel_page() to release. For allocations that need automatic size
 * tracking, use alloc_kernel() instead.
 */
void* alloc_kernel_page()
{
    g_vmm_lock.lock();

    std::uintptr_t phys_frame = pmm::alloc_frame();
    std::uintptr_t virt_page = map_phys(phys_frame);

    void* addr = reinterpret_cast<void*>(virt_page);

    g_vmm_lock.unlock();

    return addr;
}

void free_kernel_page(void* virt_addr)
{
    if (virt_addr == nullptr) {
        return;
    }

    g_vmm_lock.lock();

    std::uintptr_t virt_page = page_align(virt_addr);
    unmap_page(kernel_pml4, virt_page);

    g_vmm_lock.unlock();
}

void* map_heap_pages(PML4E* pml4, Heap* heap, std::size_t bytes, int flags)
{
    kassert_not_null(pml4);
    kassert_not_null(heap);

    g_vmm_lock.lock();
    arch::cpu::stac();

    std::size_t total = bytes + sizeof(std::size_t);
    std::size_t num_pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    std::size_t* first_page = nullptr;

    for (std::size_t page = 0; page < num_pages; page++) {
        std::uintptr_t phys_frame = pmm::alloc_frame();
        std::uintptr_t virt_page = map_heap_page(pml4, heap, phys_frame, flags);

        if (first_page == nullptr) {
            first_page = reinterpret_cast<std::size_t*>(virt_page);
        }
    }

    kassert_not_null(first_page);

    *first_page = num_pages;

    arch::cpu::clac();
    g_vmm_lock.unlock();

    return first_page + 1;
}

/**
 * @brief Allocates contiguous kernel memory with embedded size tracking.
 *
 * Unlike alloc_kernel_page() which returns raw pages, this function stores the
 * allocation size in a hidden header so free_kernel() can free
 * the correct number of pages without the caller tracking it.
 *
 * Memory layout:
 *   ┌──────────────┬─────────────────────────────────────┐
 *   │  num_pages   │         usable memory               │
 *   │  (8 bytes)   │         (bytes requested)           │
 *   └──────────────┴─────────────────────────────────────┘
 *   ^               ^
 *   actual alloc    returned pointer
 *
 * @param bytes Number of bytes to allocate.
 * @return Pointer to usable memory (after the hidden header).
 */
void* alloc_kernel(std::size_t bytes)
{
    return map_heap_pages(kernel_pml4, &kheap, bytes, PAGE_WRITE | PAGE_GLOBAL);
}

/**
 * @brief Frees memory allocated by alloc_kernel().
 *
 * Reads the page count from the hidden header before the pointer, then
 * frees that many contiguous frames.
 */
void free_kernel(void* virt_addr)
{
    if (virt_addr == nullptr) {
        return;
    }

    std::uintptr_t virt_page = page_align(virt_addr);
    auto num_pages = *reinterpret_cast<std::size_t*>(virt_page);

    if (num_pages == 0) {
        return;
    }

    g_vmm_lock.lock();

    for (std::size_t page = 0; page < num_pages; page++) {
        unmap_page(kernel_pml4, virt_page + (page * PAGE_SIZE));
    }

    g_vmm_lock.unlock();
}

/**
 * @brief Unmaps pages and frees their physical frames.
 * @param pml4 The page table to modify.
 * @param virt Starting virtual address.
 * @param num_pages Number of pages to unmap (from map_mem_at return value).
 */
void unmap_mem_at(PML4E* pml4, std::uintptr_t virt_page, std::size_t num_pages)
{
    kassert_not_null(pml4);

    g_vmm_lock.lock();

    for (std::size_t page = 0; page < num_pages; page++) {
        unmap_page(pml4, virt_page + (page * PAGE_SIZE));
    }

    g_vmm_lock.unlock();
}

// Set our local pml4 to point to the pml4 created by Limine which
// is stored in cr3 as a physical address
static void init_pml4()
{
    std::uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));

    kernel_pml4 = hhdm_ptov<PML4E*>(page_align(cr3));
}

static void init_kheap()
{
    const std::size_t kernel_pml4_idx = get_kernel_pml4_index();

    kheap.pml4_idx = kernel_pml4_idx + 1;
    kheap.pdpt_idx = 0;
    kheap.pd_idx = 0;
    kheap.pt_idx = 0;

    PDPTE* pdpt = ensure_pdpte_present(kernel_pml4[kheap.pml4_idx], 0);
    PDE* pd = ensure_pde_present(pdpt[kheap.pdpt_idx], 0);
    PTE* pt = ensure_pte_present(pd[kheap.pd_idx], 0);

    kassert_not_null(pt);

    std::uintptr_t virt_page = 0;

    virt_page |= ((kheap.pml4_idx & 0x1FF) << 39);
    virt_page |= ((kheap.pdpt_idx & 0x1FF) << 30);
    virt_page |= ((kheap.pd_idx & 0x1FF) << 21);
    virt_page |= ((kheap.pt_idx & 0x1FF) << 12);

    const std::uintptr_t bit_47 = virt_page & (1ULL << 47);

    if (bit_47) {
        virt_page |= (0xFFFFUL << 48);
    }
}

/**
 * @brief Creates a new PML4 for a user process.
 *
 * Allocates a new page table and copies the kernel's higher-half mappings
 * (from HHDM index onwards) so the kernel is accessible from user space.
 * The lower half is left empty for user mappings.
 *
 * @return Pointer to the new PML4 (virtual address).
 */
PML4E* create_user_pml4()
{
    std::uintptr_t phys = pmm::alloc_frame();
    auto* virt = hhdm_ptov<std::uintptr_t*>(phys);
    auto* new_pml4 = reinterpret_cast<PML4E*>(virt);

    kassert_not_null(new_pml4);
    zero_page(virt);

    std::size_t kernel_start = get_kernel_pml4_index();

    for (std::size_t i = kernel_start; i < NUM_PT_ENTRIES; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_pml4;
}

void free_user_pml4(PML4E* pml4)
{
    if (pml4 == kernel_pml4) {
        return;
    }

    g_vmm_lock.lock();

    std::size_t kernel_start = get_kernel_pml4_index();

    for (std::size_t pml4_idx = 0; pml4_idx < kernel_start; pml4_idx++) {
        if (!pml4[pml4_idx].p) {
            continue;
        }

        PDPTE* pdpt = get_pdpt(pml4[pml4_idx]);

        for (std::size_t pdpt_idx = 0; pdpt_idx < NUM_PT_ENTRIES; pdpt_idx++) {
            if (!pdpt[pdpt_idx].p) {
                continue;
            }

            PDE* pd = get_pd(pdpt[pdpt_idx]);

            for (std::size_t pd_idx = 0; pd_idx < NUM_PT_ENTRIES; pd_idx++) {
                if (!pd[pd_idx].p) {
                    continue;
                }

                PTE* pt = get_pt(pd[pd_idx]);

                for (std::size_t pt_idx = 0; pt_idx < NUM_PT_ENTRIES; pt_idx++) {
                    if (!pt[pt_idx].p) {
                        continue;
                    }

                    log::debugf("freeing PTE at pml4={} pdpt={} pd={} pt={}", pml4_idx, pdpt_idx, pd_idx, pt_idx);

                    free_pte(pt[pt_idx]);
                }

                free_pde(pd[pd_idx]);
            }

            free_pdpte(pdpt[pdpt_idx]);
        }

        free_pml4e(pml4[pml4_idx]);
    }

    zero_page(reinterpret_cast<std::uintptr_t*>(pml4));
    pmm::free_frame(hhdm_vtop(pml4));

    g_vmm_lock.unlock();
}

PML4E* clone_user_pml4(PML4E* pml4)
{
    g_vmm_lock.lock();

    std::uintptr_t phys = pmm::alloc_frame();
    auto* new_pml4 = hhdm_ptov<PML4E*>(phys);

    kassert_not_null(new_pml4);
    zero_page(reinterpret_cast<std::uintptr_t*>(new_pml4));

    std::size_t kernel_start = get_kernel_pml4_index();

    for (std::size_t i = kernel_start; i < NUM_PT_ENTRIES; i++) {
        new_pml4[i] = pml4[i];
    }

    for (std::size_t pml4_idx = 0; pml4_idx < kernel_start; pml4_idx++) {
        if (!pml4[pml4_idx].p) {
            continue;
        }

        PDPTE* new_pdpt = ensure_pdpte_present(new_pml4[pml4_idx], PAGE_USER);
        PDPTE* pdpt = get_pdpt(pml4[pml4_idx]);

        for (std::size_t pdpt_idx = 0; pdpt_idx < NUM_PT_ENTRIES; pdpt_idx++) {
            if (!pdpt[pdpt_idx].p) {
                continue;
            }

            PDE* new_pd = ensure_pde_present(new_pdpt[pdpt_idx], PAGE_USER);
            PDE* pd = get_pd(pdpt[pdpt_idx]);

            for (std::size_t pd_idx = 0; pd_idx < NUM_PT_ENTRIES; pd_idx++) {
                if (!pd[pd_idx].p) {
                    continue;
                }

                PTE* new_pt = ensure_pte_present(new_pd[pd_idx], PAGE_USER);
                PTE* pt = get_pt(pd[pd_idx]);

                for (std::size_t pt_idx = 0; pt_idx < NUM_PT_ENTRIES; pt_idx++) {
                    if (!pt[pt_idx].p) {
                        continue;
                    }

                    std::uintptr_t phys_frame = pmm::alloc_frame();

                    new_pt[pt_idx] = pt[pt_idx];
                    new_pt[pt_idx].addr = phys_frame >> 12;

                    auto* from = hhdm_ptov<std::uint8_t*>(pt[pt_idx].addr << 12);
                    auto* to = hhdm_ptov<std::uint8_t*>(new_pt[pt_idx].addr << 12);

                    memcpy(to, from, PAGE_SIZE);
                }
            }
        }
    }

    g_vmm_lock.unlock();

    return new_pml4;
}

Heap create_user_heap(PML4E* pml4)
{
    Heap heap;

    heap.pml4_idx = 1;
    heap.pdpt_idx = 0;
    heap.pd_idx = 0;
    heap.pt_idx = 0;

    PDPTE* pdpt = ensure_pdpte_present(pml4[heap.pml4_idx], PAGE_USER);
    PDE* pd = ensure_pde_present(pdpt[heap.pdpt_idx], PAGE_USER);
    PTE* pt = ensure_pte_present(pd[heap.pd_idx], PAGE_USER);

    kassert_not_null(pt);

    return heap;
}

Heap clone_user_heap(Heap* existing, PML4E* pml4)
{
    Heap heap;

    heap.pml4_idx = existing->pml4_idx;
    heap.pdpt_idx = existing->pdpt_idx;
    heap.pd_idx = existing->pd_idx;
    heap.pt_idx = existing->pt_idx;

    PDPTE* pdpt = ensure_pdpte_present(pml4[heap.pml4_idx], PAGE_USER);
    PDE* pd = ensure_pde_present(pdpt[heap.pdpt_idx], PAGE_USER);
    PTE* pt = ensure_pte_present(pd[heap.pd_idx], PAGE_USER);

    kassert_not_null(pt);

    return heap;
}

void switch_pml4(PML4E* pml4)
{
    kassert_not_null(pml4);

    // the value in cr3 must be a physical address
    asm volatile("mov %0, %%cr3" : : "r"(hhdm_vtop(pml4)) : "memory");
}

PML4E* get_kernel_pml4() { return kernel_pml4; }

void switch_kernel_pml4() { switch_pml4(kernel_pml4); }

constexpr std::uintptr_t user_max_addr = 0x0000800000000000ULL;

bool is_user_addr(void* addr)
{
    return reinterpret_cast<std::uintptr_t>(addr) < user_max_addr;
}

bool is_user_addr(const void* addr)
{
    return reinterpret_cast<std::uintptr_t>(addr) < user_max_addr;
}

bool is_user_addr(std::uintptr_t addr, std::size_t size)
{
    return addr < user_max_addr && size <= user_max_addr - addr;
}

static void init_cr4()
{
    /// Page Global Enable: enables global pages which are not flushed from the TLB
    /// during a context switch (when cr3 is updated)
    constexpr std::uint64_t CR4_PGE = (1ULL << 7);

    /// User Mode Instruction Protection: prevents usage of certain cpu instruction
    /// whle in user mode
    constexpr std::uint64_t CR4_UMIP = (1ULL << 11);

    /// Supervisor Mode Execution Prevention: kernel cannot execute code from
    /// userspace mapped pages
    constexpr std::uint64_t CR4_SMEP = (1ULL << 20);

    /// Supervisor Mode Access Prevention: kernel cannot read/write data from
    /// userspace mapped pages unless explicitly allowed (stac + clac)
    constexpr std::uint64_t CR4_SMAP = (1ULL << 21);

    std::uint64_t cr4 = cpu::read_cr4();
    cr4 |= CR4_PGE | CR4_UMIP | CR4_SMEP | CR4_SMAP;
    cpu::write_cr4(cr4);

    // we are already in kernel mode, but reload the kernel pml4 to
    // flush the TLB
    switch_kernel_pml4();
}

/**
 * @brief Initializes the Virtual Memory Manager.
 * @param offset The HHDM offset provided by Limine.
 */
void init(std::uintptr_t offset)
{
    hhdm_offset = offset;

    init_pml4();
    init_kheap();
    init_cr4();

    log::infof("VMM: Kernel HHDM @ {}", fmt::hex{hhdm_offset});
    log::infof("VMM: Kernel PML4 @ {}", fmt::hex{kernel_pml4});
    log::infof("VMM: cr4 = {} ({})", fmt::hex{cpu::read_cr4()}, fmt::bin{cpu::read_cr4()});
}

} // namespace x64::vmm

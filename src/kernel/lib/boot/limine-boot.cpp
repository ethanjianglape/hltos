#include <acpi/acpi.hpp>
#include <arch.hpp>
#include <boot/boot.hpp>
#include <boot/limine.h>
#include <fmt/fmt.hpp>
#include <framebuffer/framebuffer.hpp>
#include <fs/devfs/devfs.hpp>
#include <fs/fs.hpp>
#include <fs/initramfs/initramfs.hpp>
#include <fs/tmpfs/tmpfs.hpp>
#include <kassert/kassert.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>
#include <memory/pmm.hpp>
#include <scheduler/scheduler.hpp>

#include <cstddef>
#include <cstdint>

// ============================================================================
// Limine Request Markers and Structures
// ============================================================================
//
// These structures are placed in special linker sections that Limine scans
// at boot. Limine populates the 'response' pointers before jumping to _start.
//
// The [[gnu::used]] attribute prevents the compiler from optimizing these away,
// and [[gnu::section]] places them in the correct linker sections.
// ============================================================================

[[gnu::used, gnu::section(".limine_requests_start")]]
static volatile std::uint64_t limine_requests_start_marker[]
    = LIMINE_REQUESTS_START_MARKER;

[[gnu::used, gnu::section(".limine_requests")]]
static volatile std::uint64_t limine_base_revision[]
    = LIMINE_BASE_REVISION(4);

[[gnu::used, gnu::section(".limine_requests")]]
static volatile limine_framebuffer_request framebuffer_request
    = {
        .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
        .revision = 0,
        .response = nullptr};

[[gnu::used, gnu::section(".limine_requests")]]
static volatile limine_memmap_request memmap_request
    = {
        .id = LIMINE_MEMMAP_REQUEST_ID,
        .revision = 0,
        .response = nullptr};

[[gnu::used, gnu::section(".limine_requests")]]
static volatile limine_hhdm_request hhdm_request
    = {
        .id = LIMINE_HHDM_REQUEST_ID,
        .revision = 0,
        .response = nullptr};

[[gnu::used, gnu::section(".limine_requests")]]
static volatile limine_rsdp_request rsdp_request
    = {
        .id = LIMINE_RSDP_REQUEST_ID,
        .revision = 0,
        .response = nullptr};

[[gnu::used, gnu::section(".limine_requests")]]
static volatile limine_module_request module_request
    = {
        .id = LIMINE_MODULE_REQUEST_ID,
        .revision = 0,
        .response = nullptr,
        .internal_module_count = 0,
        .internal_modules = nullptr};

[[gnu::used, gnu::section(".limine_requests_end")]]
static volatile std::uint64_t limine_requests_end_marker[]
    = LIMINE_REQUESTS_END_MARKER;

const char* memmap_type_to_string(std::uint64_t type)
{
    switch (type) {
    case LIMINE_MEMMAP_USABLE:
        return "Usable";
    case LIMINE_MEMMAP_RESERVED:
        return "Reserved";
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        return "ACPI Reclaimable";
    case LIMINE_MEMMAP_ACPI_NVS:
        return "ACPI NVS";
    case LIMINE_MEMMAP_BAD_MEMORY:
        return "Bad Memory";
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        return "Bootloader Reclaimable";
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
        return "Kernel and Modules";
    case LIMINE_MEMMAP_FRAMEBUFFER:
        return "Framebuffer";
    default:
        return "Unknown";
    }
}

static void validate_limine_responses()
{
    kassert_not_null(framebuffer_request.response);
    kassert_not_null(memmap_request.response);
    kassert_not_null(hhdm_request.response);
    kassert_not_null(rsdp_request.response);
}

static void init_memory()
{
    const std::uint64_t entry_count = memmap_request.response->entry_count;
    limine_memmap_entry** entries = memmap_request.response->entries;

    log::infof("Physical RAM map contains {} entries:", entry_count);

    // Initialize PMM before adding free regions
    pmm::init();

    std::uint64_t total_usable = 0;

    for (std::size_t i = 0; i < entry_count; i++) {
        limine_memmap_entry* entry = entries[i];

        const std::uint64_t base = entry->base;
        const std::uint64_t length = entry->length;
        const std::uint64_t type = entry->type;

        log::infof(
            "  [{} - {}] ({} bytes) {}",
            fmt::hex{base},
            fmt::hex{base + length},
            fmt::hex{length},
            memmap_type_to_string(type));

        if (type == LIMINE_MEMMAP_USABLE) {
            pmm::add_free_memory(base, length);
            total_usable += length;
        }
    }

    log::infof("Total usable memory: {} MiB", total_usable / 1024 / 1024);

    // Initialize VMM with the Higher Half Direct Map offset
    std::uint64_t hhdm_offset = hhdm_request.response->offset;
    arch::vmm::init(hhdm_offset);
    scheduler::init();
}

static void init_acpi()
{
    void* rsdp_address = rsdp_request.response->address;

    acpi::init(rsdp_address);
}

static void init_modules()
{
    limine_module_response* modules = module_request.response;

    if (modules == nullptr || modules->module_count == 0) {
        log::warn("No Limine modules loaded - initramfs will be unavailable");
        return;
    }

    log::infof("Loading {} module(s)", modules->module_count);

    for (std::size_t i = 0; i < modules->module_count; i++) {
        limine_file* mod = modules->modules[i];

        std::uint8_t* addr = static_cast<std::uint8_t*>(mod->address);
        std::uint64_t size = mod->size;
        const char* path = mod->path;

        log::infof("  [{}] {} ({} bytes)", i, path, size);

        // TODO: Check module type/extension before assuming initramfs
        // For now, we assume all modules are TAR archives for initramfs

        auto* mp = new fs::initramfs::InitramfsMountPoint{addr, size};
        fs::register_mount("/", mp);
    }
}

static void init_framebuffer()
{
    limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];

    kassert_not_null(fb);

    auto fb_info = framebuffer::FrameBufferInfo{
        .width = fb->width,
        .height = fb->height,
        .pitch = fb->pitch,
        .bpp = fb->bpp,
        .vram = static_cast<std::uint8_t*>(fb->address)};

    framebuffer::init(fb_info);
}

namespace boot {

void init()
{
    log::info("limine: parsing headers");

    validate_limine_responses();
    init_memory();
    init_acpi();
    init_modules();
    init_framebuffer();
}

}

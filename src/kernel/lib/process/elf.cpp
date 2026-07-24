#include <fmt/fmt.hpp>
#include <kassert/kassert.hpp>
#include <log/log.hpp>
#include <process/elf.hpp>

namespace process::elf {
bool validate_magic(Elf64_Header* header)
{
    return header->e_ident[EI_MAG0] == E_MAG0 && header->e_ident[EI_MAG1] == E_MAG1 && header->e_ident[EI_MAG2] == E_MAG2 && header->e_ident[EI_MAG3] == E_MAG3;
}

bool validate_class(Elf64_Header* header)
{
    return header->e_ident[EI_CLASS] == ELFCLASS64;
}

bool validate_machine(Elf64_Header* header)
{
    return header->e_machine == EM_X86_64;
}

bool validate_type(Elf64_Header* header)
{
    return header->e_type == ET_EXEC;
}

Elf64_File invalid_file()
{
    return Elf64_File{
        .is_valid_elf = false,
        .entry = 0,
        .program_headers = {}};
}

Elf64_File parse_file(std::uint8_t* buffer, [[maybe_unused]] std::size_t size)
{
    kassert_not_null(buffer);

    log::infof("ELF64: validating file");

    auto* header = reinterpret_cast<Elf64_Header*>(buffer);

    if (!validate_magic(header)) {
        log::warn("Invalid ELF magic found, not an ELF file");
        return invalid_file();
    }

    if (!validate_class(header)) {
        log::warn("Invalid ELF class, expected 64-bit");
        return invalid_file();
    }

    if (!validate_machine(header)) {
        log::warn("Invalid ELF machine, expected x86-64");
        return invalid_file();
    }

    if (!validate_type(header)) {
        log::warn("Invalid ELF type, exepcted executable");
        return invalid_file();
    }

    Elf64_File file{
        .is_valid_elf = false,
        .entry = header->e_entry,
        .program_headers = {}};

    for (std::size_t i = 0; i < header->e_phnum; i++) {
        auto* addr = buffer + header->e_phoff + (i * header->e_phentsize);
        auto* phdr = reinterpret_cast<Elf64_ProgramHeader*>(addr);

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        file.is_valid_elf = true;
        file.program_headers.push_back(*phdr);
    }

    for (const auto& header : file.program_headers) {
        log::infof("ELF64: program header");
        log::infof("  addr @ {}", fmt::hex{header.p_vaddr});
        log::infof("  file size = {} bytes", header.p_filesz);
        log::infof("  mem size  = {} bytes", header.p_memsz);
        log::infof("  flags = {}", fmt::bin{header.p_flags});
    }

    return file;
}

}

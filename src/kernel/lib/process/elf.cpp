#include <fmt/fmt.hpp>
#include <kassert/kassert.hpp>
#include <log/log.hpp>
#include <process/elf.hpp>

namespace process::elf {

Elf64_File::Elf64_File(Elf64_Addr entry)
    : is_valid_elf{false}
    , entry{entry}
{
}

static bool validate_magic(Elf64_Header* header)
{
    return header->e_ident[EI_MAG0] == E_MAG0 && header->e_ident[EI_MAG1] == E_MAG1 && header->e_ident[EI_MAG2] == E_MAG2 && header->e_ident[EI_MAG3] == E_MAG3;
}

static bool validate_class(Elf64_Header* header)
{
    return header->e_ident[EI_CLASS] == ELFCLASS64;
}

static bool validate_machine(Elf64_Header* header)
{
    return header->e_machine == EM_X86_64;
}

static bool validate_type(Elf64_Header* header)
{
    return header->e_type == ET_EXEC;
}

Elf64_File parse_file(std::uint8_t* buffer, [[maybe_unused]] std::size_t size)
{
    kassert_not_null(buffer);

    auto* header = reinterpret_cast<Elf64_Header*>(buffer);

    if (!validate_magic(header)) {
        kpanic("Invalid ELF magic found, not an ELF file");
    }

    if (!validate_class(header)) {
        kpanic("Invalid ELF class, expected 64-bit");
    }

    if (!validate_machine(header)) {
        kpanic("Invalid ELF machine, expected x86-64");
    }

    if (!validate_type(header)) {
        kpanic("Invalid ELF type, exepcted executable");
    }

    Elf64_File file{header->e_entry};

    for (std::size_t i = 0; i < header->e_phnum; i++) {
        auto* addr = buffer + header->e_phoff + (i * header->e_phentsize);
        auto* phdr = reinterpret_cast<Elf64_ProgramHeader*>(addr);

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        file.is_valid_elf = true;
        file.program_headers.push_back(*phdr);
    }

    for (const Elf64_ProgramHeader& pheader : file.program_headers) {
        log::infof("ELF64: program header @ [{}] flags = {} (filesz={}, memsz={})",
            fmt::hex{pheader.p_vaddr},
            fmt::bin{pheader.p_flags},
            pheader.p_filesz,
            pheader.p_memsz);
    }

    return file;
}

}

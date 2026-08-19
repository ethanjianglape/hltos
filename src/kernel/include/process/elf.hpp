#pragma once

/**
 * See: https://uclibc.org/docs/elf-64-gen.pdf
 */

#include <containers/kvector.hpp>

#include <cstddef>
#include <cstdint>

namespace process::elf {
using Elf64_Addr = std::uintptr_t; // Unsigned program address
using Elf64_Off = std::size_t;     // Unsigned file offset
using Elf64_Byte = unsigned char;
using Elf64_Half = std::uint16_t;
using Elf64_Word = std::uint32_t;
using Elf64_Sword = std::int32_t;
using Elf64_Xword = std::uint64_t;
using Elf64_Sxword = std::int64_t;

// **************************************************
// Elf header e_ident[] indices
// **************************************************
constexpr Elf64_Off EI_MAG0 = 0;       // 0x7F
constexpr Elf64_Off EI_MAG1 = 1;       // 'E'
constexpr Elf64_Off EI_MAG2 = 2;       // 'L'
constexpr Elf64_Off EI_MAG3 = 3;       // 'F'
constexpr Elf64_Off EI_CLASS = 4;      // File class (32 vs 64 bit)
constexpr Elf64_Off EI_DATA = 5;       // Data encoding (endianness)
constexpr Elf64_Off EI_VERSION = 6;    // Object file format version (always 1)
constexpr Elf64_Off EI_OSABI = 7;      // OS/ABI identification (0 for System V)
constexpr Elf64_Off EI_ABIVERSION = 8; // ABI Version (0 for System V)
constexpr Elf64_Off EI_PAD = 9;        // Padding (unused)
constexpr Elf64_Off EI_NIDENT = 16;    // Size of e_ident[]

constexpr Elf64_Byte E_MAG0 = 0x7F;
constexpr Elf64_Byte E_MAG1 = 'E';
constexpr Elf64_Byte E_MAG2 = 'L';
constexpr Elf64_Byte E_MAG3 = 'F';

constexpr Elf64_Byte ELFCLASS32 = 1;            // 32-bit
constexpr Elf64_Byte ELFCLASS64 = 2;            // 64-bit

constexpr Elf64_Byte ELFDATA2LSB = 1;           // little-endian (x86_64 uses this)
constexpr Elf64_Byte ELFDATA2MSB = 2;           // big-endian

constexpr Elf64_Byte ELFOSABI_SYSV = 0;         // System V (our target)
constexpr Elf64_Byte ELFOSABI_HPUX = 2;         // HP-UX
constexpr Elf64_Byte ELFOSABI_STANDALONE = 255; // Standalone (embedded) application

constexpr Elf64_Half EM_X86_64 = 0x3E;

// **************************************************
// Object file types (Elf64_Header.e_type)
// **************************************************
constexpr Elf64_Half ET_NONE = 0x0000;    // No file type
constexpr Elf64_Half ET_REL = 0x0001;     // Relocatable object file
constexpr Elf64_Half ET_EXEC = 0x0002;    // Executable file
constexpr Elf64_Half ET_DYN = 0x0003;     // shared object file
constexpr Elf64_Half ET_CORE = 0x0004;    // Core file
constexpr Elf64_Half ET_LOOS = 0xFE00;    // Environment-specific use
constexpr Elf64_Half ET_HIOS = 0xFEFF;
constexpr Elf64_Half ET_LOPROC = 0xFF00;  // Processor-specific use
constexpr Elf64_Half ET_HIPROC = 0xFFFF;

constexpr Elf64_Half SHN_UNDEF = 0x0000;  // Undefined/invalid section reference
constexpr Elf64_Half SHN_LOPROC = 0xFF00; // Processor-specific use
constexpr Elf64_Half SHN_HIPROC = 0xFF1F;
constexpr Elf64_Half SHN_LOOS = 0xFF20;   // Environment-specific use
constexpr Elf64_Half SHN_HIOS = 0xFF3F;
constexpr Elf64_Half SHN_ABS = 0xFFF1;    // Reference is an absoluate value
constexpr Elf64_Half SHN_COMMON = 0xFFF2; // Common block (C tentative declaration)

// **************************************************
// Section header types (Elf64_SectionHeader.sh_type)
// **************************************************
constexpr Elf64_Word SHT_NULL = 0;            // Unused section header
constexpr Elf64_Word SHT_PROGBITS = 1;        // Contains info found in program
constexpr Elf64_Word SHT_SYMTAB = 2;          // Contains a linker symbol table
constexpr Elf64_Word SHT_STRTAB = 3;          // Contains a string table
constexpr Elf64_Word SHT_RELA = 4;            // Contains "Rela" type relocation entries
constexpr Elf64_Word SHT_HASH = 5;            // Contains a symbol hash table
constexpr Elf64_Word SHT_DYNAMIC = 6;         // Contains dynamic linking tables
constexpr Elf64_Word SHT_NOTE = 7;            // Contains note info
constexpr Elf64_Word SHT_NOBITS = 8;          // Contains uninitialized space (does not occupy space in file)
constexpr Elf64_Word SHT_REL = 9;             // Contains "Rel" type relocation entries
constexpr Elf64_Word SHT_SHLIB = 10;          // Reserved
constexpr Elf64_Word SHT_DYNSYM = 11;         // Contains dynamic loader symbol table
constexpr Elf64_Word SHT_LOOS = 0x60000000;   // Environment-specific user
constexpr Elf64_Word SHT_HIOS = 0x6FFFFFFF;
constexpr Elf64_Word SHT_LOPROC = 0x70000000; // Processor-specific user
constexpr Elf64_Word SHT_HIPROC = 0x7FFFFFFF;

// **************************************************
// Section header flags (Elf64_SectionHeader.sh_flags)
// **************************************************
constexpr Elf64_Xword SHF_WRITE = 0x1;
constexpr Elf64_Xword SHF_ALLOC = 0x2;
constexpr Elf64_Xword SHF_EXECINSTR = 0x4;
constexpr Elf64_Xword SHF_MASKOS = 0x0F000000;
constexpr Elf64_Xword SHF_MASKPROC = 0xF00000000;

// **************************************************
// Symbol table bindings/types (Elf64_SymbolTable.sh_info)
// **************************************************
constexpr Elf64_Byte STB_LOCAL = 0;
constexpr Elf64_Byte STB_GLOBAL = 1;
constexpr Elf64_Byte STB_WEAK = 2;
constexpr Elf64_Byte STB_LOOS = 10;
constexpr Elf64_Byte STB_HIOS = 12;
constexpr Elf64_Byte STB_LOPROC = 13;
constexpr Elf64_Byte STB_HIPROC = 15;
constexpr Elf64_Byte STT_NOTYPE = 0;
constexpr Elf64_Byte STT_OBJECT = 1;
constexpr Elf64_Byte STT_FUNC = 2;
constexpr Elf64_Byte STT_SECTION = 3;
constexpr Elf64_Byte STT_FILE = 4;
constexpr Elf64_Byte STT_LOOS = 10;
constexpr Elf64_Byte STT_HIOS = 12;
constexpr Elf64_Byte STT_LOPROC = 13;
constexpr Elf64_Byte STT_HIPROC = 15;

// **************************************************
// Segment types (Elf64_ProgramHeader.p_type)
// **************************************************
constexpr Elf64_Word PT_NULL = 0;            // Unused
constexpr Elf64_Word PT_LOAD = 1;            // Loadable segment
constexpr Elf64_Word PT_DYNAMIC = 2;         // Dynamic linking tables
constexpr Elf64_Word PT_INTERP = 3;          // Program interpreter path name
constexpr Elf64_Word PT_NOTE = 4;            // Notes sections
constexpr Elf64_Word PT_SHLIB = 5;           // Reserved
constexpr Elf64_Word PT_PHDR = 6;            // Program header table
constexpr Elf64_Word PT_LOOS = 0x60000000;   // Environment-specific use
constexpr Elf64_Word PT_HIOS = 0x6FFFFFFF;
constexpr Elf64_Word PT_LOPROC = 0x70000000; // Processor-specific use
constexpr Elf64_Word PT_HIPROC = 0x7FFFFFFF;

// **************************************************
// Segment flags (Elf64_ProgramHeader.p_flags)
// **************************************************
constexpr Elf64_Word PF_X = 0x1;               // Execute
constexpr Elf64_Word PF_W = 0x2;               // Write
constexpr Elf64_Word PF_R = 0x4;               // Read
constexpr Elf64_Word PF_MASKOS = 0x00FF0000;   // Environment-specific use
constexpr Elf64_Word PF_MASKPROC = 0xFF000000; // Processor-specific use

struct [[gnu::packed]] Elf64_Header {
    Elf64_Byte e_ident[EI_NIDENT];             // ELF identification
    Elf64_Half e_type;                         // Object file type
    Elf64_Half e_machine;                      // Machine type
    Elf64_Word e_version;                      // Object file version
    Elf64_Addr e_entry;                        // Entry point virtual address
    Elf64_Off e_phoff;                         // Program header file offset
    Elf64_Off e_shoff;                         // Section header file offset
    Elf64_Word e_flags;                        // Processor-specific flags
    Elf64_Half e_ehsize;                       // ELF header size
    Elf64_Half e_phentsize;                    // Size of program header entry
    Elf64_Half e_phnum;                        // Number of program header entries
    Elf64_Half e_shentsize;                    // Size of section header entry
    Elf64_Half e_shnum;                        // Number of sesction header entries
    Elf64_Half e_shstrndx;                     // Section name string table index
};

static_assert(sizeof(Elf64_Header) == 64);

struct [[gnu::packed]] Elf64_SectionHeader {
    Elf64_Word sh_name;       // Section name
    Elf64_Word sh_type;       // Section type
    Elf64_Xword sh_flags;     // Section attributes
    Elf64_Addr sh_addr;       // Virtual address in memory
    Elf64_Off sh_offset;      // Offset in file
    Elf64_Xword sh_size;      // Size of section
    Elf64_Word sh_link;       // Link to other section
    Elf64_Word sh_info;       // Misc. info
    Elf64_Xword sh_addralign; // Address alignment boundary
    Elf64_Xword sh_entsize;   // Size of entries, if section has table
};

static_assert(sizeof(Elf64_SectionHeader) == 64);

struct [[gnu::packed]] Elf64_SymbolTable {
    Elf64_Word st_name;  // Symbol name
    Elf64_Byte st_info;  // Type and binding attrs
    Elf64_Byte st_other; // Reserved
    Elf64_Half st_shndx; // Section table index
    Elf64_Addr st_value; // Symbol value
    Elf64_Xword st_size; // Size of object
};

static_assert(sizeof(Elf64_SymbolTable) == 24);

struct [[gnu::packed]] Elf64_Rel {
    Elf64_Addr r_offset; // Address of reference
    Elf64_Xword r_info;  // Symbol index and type of relocation
};

struct [[gnu::packed]] Elf64_Rela {
    Elf64_Addr r_offset;   // Address of reference
    Elf64_Xword r_info;    // Symbol index and type of relocation
    Elf64_Sxword r_addend; // Constant part of expression
};

static_assert(sizeof(Elf64_Rel) == 16);
static_assert(sizeof(Elf64_Rela) == 24);

struct [[gnu::packed]] Elf64_ProgramHeader {
    Elf64_Word p_type;    // Type of segment
    Elf64_Word p_flags;   // Segment attrs
    Elf64_Off p_offset;   // Offset in file
    Elf64_Addr p_vaddr;   // Virtual address in memory
    Elf64_Addr p_paddr;   // Reserved
    Elf64_Xword p_filesz; // Size of segment in file
    Elf64_Xword p_memsz;  // Size of segment in memory
    Elf64_Xword p_align;  // Alignment of Segment
};

static_assert(sizeof(Elf64_ProgramHeader) == 56);

struct Elf64_File {
    bool is_valid_elf;
    Elf64_Addr entry;
    kvector<Elf64_ProgramHeader> program_headers;

    Elf64_File(Elf64_Addr entry);
};

Elf64_File parse_file(std::uint8_t* buffer, std::size_t size);
}

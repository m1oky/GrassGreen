#pragma once
#ifndef GRASS_ELF_WRITER_H
#define GRASS_ELF_WRITER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <cstring>
#include <algorithm>

#include "native/GrassBackendTypes.h"
#include "native/GrassObjWriter.h"

namespace grass::backend {

    // =========================================================================
    // ELF Executable and Segment Constants (System V AMD64 ABI)
    // =========================================================================

    constexpr uint16_t ELF_ET_EXEC = 2;

    // Segment Types (p_type)
    constexpr uint32_t ELF_PT_NULL    = 0;
    constexpr uint32_t ELF_PT_LOAD    = 1;
    constexpr uint32_t ELF_PT_DYNAMIC = 2;
    constexpr uint32_t ELF_PT_INTERP  = 3;
    constexpr uint32_t ELF_PT_NOTE    = 4;
    constexpr uint32_t ELF_PT_SHLIB   = 5;
    constexpr uint32_t ELF_PT_PHDR    = 6;

    // Segment Flags (p_flags)
    constexpr uint32_t ELF_PF_X = 0x1;
    constexpr uint32_t ELF_PF_W = 0x2;
    constexpr uint32_t ELF_PF_R = 0x4;

    // Linux x86_64 Syscall Numbers
    enum class LinuxSyscall : uint32_t {
        SysWrite  = 1,
        SysOpen   = 2,
        SysClose  = 3,
        SysMmap   = 9,
        SysMunmap = 11,
        SysAccess = 21,
        SysFork   = 57,
        SysExecve = 59,
        SysExit   = 60,
        SysWait4  = 61
    };

#pragma pack(push, 1)

    // 64-bit ELF Program Header
    struct Elf64_Phdr {
        uint32_t p_type;
        uint32_t p_flags;
        uint64_t p_offset;
        uint64_t p_vaddr;
        uint64_t p_paddr;
        uint64_t p_filesz;
        uint64_t p_memsz;
        uint64_t p_align;
    };
    static_assert(sizeof(Elf64_Phdr) == 56, "Elf64_Phdr must be 56 bytes");

#pragma pack(pop)

    // Configuration for ELF Executable generation
    struct ElfWriterConfig {
        uint64_t base_address = 0x400000;
        uint64_t page_size = 0x1000;
        std::string entry_symbol = "_start";
        bool auto_inject_syscalls = true;
    };

    // Information about computed layout and resolved symbols
    struct ElfLayoutInfo {
        uint64_t base_address = 0x400000;
        uint64_t page_size = 0x1000;

        uint64_t text_vaddr = 0;
        uint64_t text_offset = 0;
        uint64_t text_size = 0;

        uint64_t rodata_vaddr = 0;
        uint64_t rodata_offset = 0;
        uint64_t rodata_size = 0;

        uint64_t data_vaddr = 0;
        uint64_t data_offset = 0;
        uint64_t data_size = 0;

        uint64_t bss_vaddr = 0;
        uint64_t bss_size = 0;

        uint64_t shstrtab_offset = 0;
        uint64_t shstrtab_size = 0;

        uint64_t shoff = 0;
        uint64_t shnum = 0;
        uint64_t shstrndx = 0;

        uint64_t entry_point = 0;

        uint64_t code_seg_offset = 0;
        uint64_t code_seg_vaddr = 0;
        uint64_t code_seg_filesz = 0;
        uint64_t code_seg_memsz = 0;

        uint64_t data_seg_offset = 0;
        uint64_t data_seg_vaddr = 0;
        uint64_t data_seg_filesz = 0;
        uint64_t data_seg_memsz = 0;

        std::unordered_map<std::string, uint64_t> symbol_addresses;
    };

    // GrassElfWriter - Direct Linux ELF64 Executable Writer
    class GrassElfWriter {
    public:
        // Write ELF64 executable into a byte vector
        static bool write_elf_exe(const CompiledUnit& unit,
                                  std::vector<uint8_t>& out_bytes,
                                  const ElfWriterConfig& config = {},
                                  ElfLayoutInfo* out_layout = nullptr,
                                  std::string* out_error = nullptr);

        // Write ELF64 executable to file
        static bool write_elf_exe_file(const CompiledUnit& unit,
                                       const std::string& filename,
                                       const ElfWriterConfig& config = {},
                                       ElfLayoutInfo* out_layout = nullptr,
                                       std::string* out_error = nullptr);

        // Overload for BinaryBuffer compatibility
        static bool write_elf_exe(const CompiledUnit& unit,
                                  BinaryBuffer& out,
                                  const ElfWriterConfig& config = {},
                                  ElfLayoutInfo* out_layout = nullptr,
                                  std::string* out_error = nullptr) {
            std::vector<uint8_t> bytes;
            if (!write_elf_exe(unit, bytes, config, out_layout, out_error)) {
                return false;
            }
            out.emit_bytes(bytes.data(), bytes.size());
            return true;
        }

        // Layout computation and relocation resolution
        static bool compute_layout(const CompiledUnit& unit,
                                   const ElfWriterConfig& config,
                                   ElfLayoutInfo& out_layout,
                                   std::string* out_error = nullptr);

        static bool resolve_relocations(CompiledUnit& mutable_unit,
                                        const ElfLayoutInfo& layout,
                                        std::string* out_error = nullptr);

        // Linux Syscall Wrappers
        static std::vector<uint8_t> generate_syscall_wrapper(LinuxSyscall sc);
        static bool inject_syscall_wrapper(CompiledUnit& unit, LinuxSyscall sc, const std::string& name = "");
        static bool inject_missing_syscalls(CompiledUnit& unit);

        // Header creation helpers
        static Elf64_Ehdr create_elf64_ehdr(uint64_t entry_point,
                                            uint64_t phoff,
                                            uint64_t shoff,
                                            uint16_t phnum,
                                            uint16_t shnum,
                                            uint16_t shstrndx);

        static Elf64_Phdr create_elf64_phdr(uint32_t type,
                                            uint32_t flags,
                                            uint64_t offset,
                                            uint64_t vaddr,
                                            uint64_t filesz,
                                            uint64_t memsz,
                                            uint64_t align);

        static Elf64_Shdr create_elf64_shdr(uint32_t name,
                                            uint32_t type,
                                            uint64_t flags,
                                            uint64_t addr,
                                            uint64_t offset,
                                            uint64_t size,
                                            uint32_t link,
                                            uint32_t info,
                                            uint64_t addralign,
                                            uint64_t entsize);
    };

} // namespace grass::backend

#endif // GRASS_ELF_WRITER_H

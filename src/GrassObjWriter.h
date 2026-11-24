#pragma once
#ifndef GRASS_OBJ_WRITER_H
#define GRASS_OBJ_WRITER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <memory>

#include "GrassBackendTypes.h"

namespace grass::backend {

    // =========================================================================
    // PE/COFF Constants and Binary Structures (Microsoft Specification)
    // =========================================================================

    constexpr uint16_t COFF_MACHINE_AMD64 = 0x8664;
    constexpr uint16_t COFF_MACHINE_I386  = 0x014C;

    constexpr uint32_t COFF_SCN_CNT_CODE               = 0x00000020;
    constexpr uint32_t COFF_SCN_CNT_INITIALIZED_DATA   = 0x00000040;
    constexpr uint32_t COFF_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
    constexpr uint32_t COFF_SCN_ALIGN_16BYTES          = 0x00500000;
    constexpr uint32_t COFF_SCN_MEM_EXECUTE            = 0x20000000;
    constexpr uint32_t COFF_SCN_MEM_READ               = 0x40000000;
    constexpr uint32_t COFF_SCN_MEM_WRITE              = 0x80000000;

    constexpr uint16_t COFF_REL_AMD64_ADDR64   = 0x0001;
    constexpr uint16_t COFF_REL_AMD64_ADDR32   = 0x0002;
    constexpr uint16_t COFF_REL_AMD64_ADDR32NB = 0x0003;
    constexpr uint16_t COFF_REL_AMD64_REL32    = 0x0004;

    constexpr uint16_t COFF_REL_I386_DIR32 = 0x0006;
    constexpr uint16_t COFF_REL_I386_REL32 = 0x0014;

    constexpr uint8_t  COFF_SYM_CLASS_EXTERNAL = 2;
    constexpr uint8_t  COFF_SYM_CLASS_STATIC   = 3;
    constexpr uint16_t COFF_SYM_TYPE_NULL      = 0x0000;
    constexpr uint16_t COFF_SYM_TYPE_FUNC      = 0x0020;

#pragma pack(push, 1)

    struct COFF_FileHeader {
        uint16_t machine;
        uint16_t number_of_sections;
        uint32_t time_date_stamp;
        uint32_t pointer_to_symbol_table;
        uint32_t number_of_symbols;
        uint16_t size_of_optional_header;
        uint16_t characteristics;
    };
    static_assert(sizeof(COFF_FileHeader) == 20, "COFF_FileHeader must be 20 bytes");

    struct COFF_SectionHeader {
        char     name[8];
        uint32_t virtual_size;
        uint32_t virtual_address;
        uint32_t size_of_raw_data;
        uint32_t pointer_to_raw_data;
        uint32_t pointer_to_relocations;
        uint32_t pointer_to_linenumbers;
        uint16_t number_of_relocations;
        uint16_t number_of_linenumbers;
        uint32_t characteristics;
    };
    static_assert(sizeof(COFF_SectionHeader) == 40, "COFF_SectionHeader must be 40 bytes");

    struct COFF_Relocation {
        uint32_t virtual_address;
        uint32_t symbol_table_index;
        uint16_t type;
    };
    static_assert(sizeof(COFF_Relocation) == 10, "COFF_Relocation must be 10 bytes");

    struct COFF_SymbolRecord {
        union {
            char short_name[8];
            struct {
                uint32_t zeroes;
                uint32_t offset;
            } long_name;
        } name;
        uint32_t value;
        int16_t  section_number;
        uint16_t type;
        uint8_t  storage_class;
        uint8_t  number_of_aux_symbols;
    };
    static_assert(sizeof(COFF_SymbolRecord) == 18, "COFF_SymbolRecord must be 18 bytes");

    // =========================================================================
    // PE32+ (Windows x64 Executable) Binary Structures
    // =========================================================================

    constexpr uint16_t PE_DOS_MAGIC                     = 0x5A4D;     // "MZ"
    constexpr uint32_t PE_SIGNATURE                     = 0x00004550; // "PE\0\0"
    constexpr uint16_t PE_OPTIONAL_HDR64_MAGIC          = 0x020B;     // PE32+
    constexpr uint16_t PE_SUBSYSTEM_WINDOWS_GUI         = 2;
    constexpr uint16_t PE_SUBSYSTEM_WINDOWS_CUI         = 3;          // Console
    constexpr uint64_t PE_DEFAULT_IMAGE_BASE            = 0x140000000ULL;
    constexpr uint32_t PE_DEFAULT_SECTION_ALIGNMENT     = 0x1000;
    constexpr uint32_t PE_DEFAULT_FILE_ALIGNMENT        = 0x200;

    struct PE_DosHeader {
        uint16_t e_magic;    // 0x5A4D ("MZ")
        uint16_t e_cblp;
        uint16_t e_cp;
        uint16_t e_crlc;
        uint16_t e_cparhdr;
        uint16_t e_minalloc;
        uint16_t e_maxalloc;
        uint16_t e_ss;
        uint16_t e_sp;
        uint16_t e_csum;
        uint16_t e_ip;
        uint16_t e_cs;
        uint16_t e_lfarlc;
        uint16_t e_ovno;
        uint16_t e_res[4];
        uint16_t e_oemid;
        uint16_t e_oeminfo;
        uint16_t e_res2[10];
        uint32_t e_lfanew;   // Offset to PE signature
    };
    static_assert(sizeof(PE_DosHeader) == 64, "PE_DosHeader must be 64 bytes");

    struct PE_DataDirectory {
        uint32_t virtual_address;
        uint32_t size;
    };
    static_assert(sizeof(PE_DataDirectory) == 8, "PE_DataDirectory must be 8 bytes");

    struct PE32Plus_OptionalHeader {
        uint16_t magic;
        uint8_t  major_linker_version;
        uint8_t  minor_linker_version;
        uint32_t size_of_code;
        uint32_t size_of_initialized_data;
        uint32_t size_of_uninitialized_data;
        uint32_t address_of_entry_point;
        uint32_t base_of_code;

        uint64_t image_base;
        uint32_t section_alignment;
        uint32_t file_alignment;
        uint16_t major_operating_system_version;
        uint16_t minor_operating_system_version;
        uint16_t major_image_version;
        uint16_t minor_image_version;
        uint16_t major_subsystem_version;
        uint16_t minor_subsystem_version;
        uint32_t win32_version_value;
        uint32_t size_of_image;
        uint32_t size_of_headers;
        uint32_t check_sum;
        uint16_t subsystem;
        uint16_t dll_characteristics;
        uint64_t size_of_stack_reserve;
        uint64_t size_of_stack_commit;
        uint64_t size_of_heap_reserve;
        uint64_t size_of_heap_commit;
        uint32_t loader_flags;
        uint32_t number_of_rva_and_sizes;

        PE_DataDirectory data_directories[16];
    };
    static_assert(sizeof(PE32Plus_OptionalHeader) == 240, "PE32Plus_OptionalHeader must be 240 bytes");

    struct PE_ImportDirectoryEntry {
        uint32_t original_first_thunk; // ILT RVA
        uint32_t time_date_stamp;
        uint32_t forwarder_chain;
        uint32_t name_rva;             // DLL name RVA
        uint32_t first_thunk;          // IAT RVA
    };
    static_assert(sizeof(PE_ImportDirectoryEntry) == 20, "PE_ImportDirectoryEntry must be 20 bytes");

    // =========================================================================
    // ELF Constants and Binary Structures (System V ELF Specification)
    // =========================================================================

    constexpr uint8_t  ELF_CLASS_32 = 1;
    constexpr uint8_t  ELF_CLASS_64 = 2;
    constexpr uint8_t  ELF_DATA_2LSB = 1;
    constexpr uint8_t  ELF_EV_CURRENT = 1;
    constexpr uint8_t  ELF_OSABI_SYSV = 0;

    constexpr uint16_t ELF_ET_REL = 1;
    constexpr uint16_t ELF_EM_386    = 3;
    constexpr uint16_t ELF_EM_X86_64 = 62;

    constexpr uint32_t ELF_SHT_NULL     = 0;
    constexpr uint32_t ELF_SHT_PROGBITS = 1;
    constexpr uint32_t ELF_SHT_SYMTAB   = 2;
    constexpr uint32_t ELF_SHT_STRTAB   = 3;
    constexpr uint32_t ELF_SHT_RELA     = 4;
    constexpr uint32_t ELF_SHT_NOBITS   = 8;
    constexpr uint32_t ELF_SHT_REL      = 9;

    constexpr uint64_t ELF_SHF_WRITE     = 0x1;
    constexpr uint64_t ELF_SHF_ALLOC     = 0x2;
    constexpr uint64_t ELF_SHF_EXECINSTR = 0x4;
    constexpr uint64_t ELF_SHF_INFO_LINK = 0x40;

    constexpr uint8_t  ELF_STB_LOCAL  = 0;
    constexpr uint8_t  ELF_STB_GLOBAL = 1;
    constexpr uint8_t  ELF_STB_WEAK   = 2;

    constexpr uint8_t  ELF_STT_NOTYPE  = 0;
    constexpr uint8_t  ELF_STT_OBJECT  = 1;
    constexpr uint8_t  ELF_STT_FUNC    = 2;
    constexpr uint8_t  ELF_STT_SECTION = 3;
    constexpr uint8_t  ELF_STT_FILE    = 4;

    constexpr uint32_t ELF_R_X86_64_NONE  = 0;
    constexpr uint32_t ELF_R_X86_64_64    = 1;
    constexpr uint32_t ELF_R_X86_64_PC32  = 2;
    constexpr uint32_t ELF_R_X86_64_PLT32 = 4;
    constexpr uint32_t ELF_R_X86_64_32    = 10;

    constexpr uint32_t ELF_R_386_NONE = 0;
    constexpr uint32_t ELF_R_386_32   = 1;
    constexpr uint32_t ELF_R_386_PC32 = 2;

    constexpr uint16_t ELF_SHN_UNDEF  = 0;
    constexpr uint16_t ELF_SHN_ABS    = 0xFFF1;
    constexpr uint16_t ELF_SHN_COMMON = 0xFFF2;

    struct Elf64_Ehdr {
        uint8_t  e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint64_t e_entry;
        uint64_t e_phoff;
        uint64_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    };
    static_assert(sizeof(Elf64_Ehdr) == 64, "Elf64_Ehdr must be 64 bytes");

    struct Elf64_Shdr {
        uint32_t sh_name;
        uint32_t sh_type;
        uint64_t sh_flags;
        uint64_t sh_addr;
        uint64_t sh_offset;
        uint64_t sh_size;
        uint32_t sh_link;
        uint32_t sh_info;
        uint64_t sh_addralign;
        uint64_t sh_entsize;
    };
    static_assert(sizeof(Elf64_Shdr) == 64, "Elf64_Shdr must be 64 bytes");

    struct Elf64_Sym {
        uint32_t st_name;
        uint8_t  st_info;
        uint8_t  st_other;
        uint16_t st_shndx;
        uint64_t st_value;
        uint64_t st_size;
    };
    static_assert(sizeof(Elf64_Sym) == 24, "Elf64_Sym must be 24 bytes");

    struct Elf64_Rela {
        uint64_t r_offset;
        uint64_t r_info;
        int64_t  r_addend;
    };
    static_assert(sizeof(Elf64_Rela) == 24, "Elf64_Rela must be 24 bytes");

    struct Elf32_Ehdr {
        uint8_t  e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint32_t e_entry;
        uint32_t e_phoff;
        uint32_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    };
    static_assert(sizeof(Elf32_Ehdr) == 52, "Elf32_Ehdr must be 52 bytes");

    struct Elf32_Shdr {
        uint32_t sh_name;
        uint32_t sh_type;
        uint32_t sh_flags;
        uint32_t sh_addr;
        uint32_t sh_offset;
        uint32_t sh_size;
        uint32_t sh_link;
        uint32_t sh_info;
        uint32_t sh_addralign;
        uint32_t sh_entsize;
    };
    static_assert(sizeof(Elf32_Shdr) == 40, "Elf32_Shdr must be 40 bytes");

    struct Elf32_Sym {
        uint32_t st_name;
        uint32_t st_value;
        uint32_t st_size;
        uint8_t  st_info;
        uint8_t  st_other;
        uint16_t st_shndx;
    };
    static_assert(sizeof(Elf32_Sym) == 16, "Elf32_Sym must be 16 bytes");

    struct Elf32_Rel {
        uint32_t r_offset;
        uint32_t r_info;
    };
    static_assert(sizeof(Elf32_Rel) == 8, "Elf32_Rel must be 8 bytes");

#pragma pack(pop)

    // =========================================================================
    // 1. Binary Buffer Utilities (BinaryBuffer)
    // =========================================================================

    class BinaryBuffer {
    public:
        BinaryBuffer() = default;
        explicit BinaryBuffer(size_t initial_capacity);

        void emit_u8(uint8_t val);
        void emit_u16(uint16_t val);
        void emit_u32(uint32_t val);
        void emit_u64(uint64_t val);

        void emit_i8(int8_t val);
        void emit_i16(int16_t val);
        void emit_i32(int32_t val);
        void emit_i64(int64_t val);

        void emit_bytes(const void* src, size_t count);
        void emit_bytes(const std::vector<uint8_t>& bytes);
        void emit_string(const std::string& str);
        void emit_cstring(const std::string& str);
        void emit_zeros(size_t count);

        void align_to(size_t alignment, uint8_t pad_byte = 0);

        void patch_u8(size_t offset, uint8_t val);
        void patch_u16(size_t offset, uint16_t val);
        void patch_u32(size_t offset, uint32_t val);
        void patch_u64(size_t offset, uint64_t val);
        void patch_i32(size_t offset, int32_t val);
        void patch_i64(size_t offset, int64_t val);
        void patch_bytes(size_t offset, const void* src, size_t count);

        uint8_t  read_u8(size_t offset) const;
        uint16_t read_u16(size_t offset) const;
        uint32_t read_u32(size_t offset) const;
        uint64_t read_u64(size_t offset) const;
        int32_t  read_i32(size_t offset) const;
        int64_t  read_i64(size_t offset) const;

        size_t size() const noexcept;
        bool empty() const noexcept;
        void clear() noexcept;
        void reserve(size_t capacity);

        const uint8_t* data() const noexcept;
        uint8_t* data() noexcept;

        const std::vector<uint8_t>& get_bytes() const noexcept;
        std::vector<uint8_t>& get_bytes() noexcept;

        bool write_to_file(const std::string& path) const;
        bool read_from_file(const std::string& path);

    private:
        std::vector<uint8_t> buffer_;
    };

    // =========================================================================
    // 2. String Table Builder (COFF & ELF string pools)
    // =========================================================================

    class StringTableBuilder {
    public:
        StringTableBuilder() = default;

        uint32_t add_coff(const std::string& str);
        uint32_t add_elf(const std::string& str);

        uint32_t get_offset(const std::string& str) const;
        uint32_t get_offset_coff(const std::string& str) const;
        uint32_t get_offset_elf(const std::string& str) const;
        bool contains(const std::string& str) const;

        void build_coff(BinaryBuffer& out) const;
        void build_elf(BinaryBuffer& out) const;

        size_t total_bytes_coff() const noexcept;
        size_t total_bytes_elf() const noexcept;

        void clear() noexcept;
        size_t count() const noexcept;
        const std::vector<std::string>& get_strings() const noexcept;

    private:
        std::vector<std::string> strings_;
        std::vector<std::string> coff_strings_;
        std::vector<std::string> elf_strings_;
        std::unordered_map<std::string, uint32_t> coff_offsets_;
        std::unordered_map<std::string, uint32_t> elf_offsets_;
        size_t coff_total_size_ = 4;
        size_t elf_total_size_ = 1;
    };

    // =========================================================================
    // 3. Register Enums and ABI Helper (ABISpec)
    // =========================================================================

    struct StackFrameRequest {
        uint32_t local_vars_size = 0;
        std::vector<Reg> callee_saved_regs;
        uint32_t max_outgoing_args_size = 0;
        bool has_calls = false;
        bool use_frame_pointer = true;
    };

    struct StackFrameInfo {
        uint32_t local_vars_size = 0;
        std::vector<Reg> callee_saved_regs;
        uint32_t shadow_space_size = 0;
        uint32_t outgoing_args_size = 0;
        uint32_t alignment_padding = 0;
        uint32_t total_frame_size = 0;
        uint32_t pushed_regs_size = 0;
        bool use_frame_pointer = true;
        TargetABI abi = TargetABI::Win64;
        TargetArch arch = TargetArch::X86_64;
    };

    class ABISpec {
    public:
        static ABISpec for_abi(TargetABI abi);

        TargetABI get_abi() const noexcept;
        TargetArch get_arch() const noexcept;

        const std::vector<Reg>& get_integer_arg_regs() const noexcept;
        const std::vector<Reg>& get_float_arg_regs() const noexcept;
        const std::vector<Reg>& get_callee_saved_regs() const noexcept;
        const std::vector<Reg>& get_caller_saved_regs() const noexcept;

        Reg get_return_reg() const noexcept;
        Reg get_float_return_reg() const noexcept;

        uint32_t get_shadow_space_size() const noexcept;
        uint32_t get_red_zone_size() const noexcept;
        uint32_t get_stack_alignment() const noexcept;

        bool is_callee_saved(Reg reg) const;
        bool is_caller_saved(Reg reg) const;
        static const char* reg_name(Reg reg);

        StackFrameInfo compute_stack_frame(const StackFrameRequest& req) const;

        void emit_prologue_bytes(const StackFrameInfo& frame, BinaryBuffer& out) const;
        void emit_epilogue_bytes(const StackFrameInfo& frame, BinaryBuffer& out) const;

    private:
        TargetABI abi_ = TargetABI::Win64;
        TargetArch arch_ = TargetArch::X86_64;
        std::vector<Reg> int_arg_regs_;
        std::vector<Reg> float_arg_regs_;
        std::vector<Reg> callee_saved_;
        std::vector<Reg> caller_saved_;
        Reg ret_reg_ = Reg::RAX;
        Reg float_ret_reg_ = Reg::XMM0;
        uint32_t shadow_space_ = 0;
        uint32_t red_zone_ = 0;
        uint32_t stack_align_ = 16;
    };

    // =========================================================================
    // 4. Link Map Generator for Bare-Metal (LinkMap)
    // =========================================================================

    struct SectionMapEntry {
        std::string name;
        uint64_t start_address = 0;
        uint64_t end_address = 0;
        uint64_t size = 0;
        uint32_t alignment = 0;
    };

    struct SymbolMapEntry {
        std::string name;
        uint64_t address = 0;
        uint64_t size = 0;
        SectionKind section = SectionKind::Text;
        bool is_global = true;
    };

    struct RelocMapEntry {
        SectionKind section = SectionKind::Text;
        uint64_t section_offset = 0;
        uint64_t site_address = 0;
        std::string target_symbol;
        RelocType type = RelocType::Rel32;
        int64_t addend = 0;
        int64_t computed_value = 0;
    };

    class LinkMap {
    public:
        LinkMap() = default;

        void set_base_address(uint64_t addr) noexcept;
        uint64_t get_base_address() const noexcept;

        void add_section_entry(const std::string& name, uint64_t start_addr, uint64_t size, uint32_t align);
        void add_symbol_entry(const std::string& name, uint64_t addr, uint64_t size, SectionKind sec, bool is_global);
        void add_reloc_entry(SectionKind sec, uint64_t offset, uint64_t site_addr, const std::string& sym, RelocType type, int64_t addend, int64_t computed_value);

        const std::vector<SectionMapEntry>& get_sections() const noexcept;
        const std::vector<SymbolMapEntry>& get_symbols() const noexcept;
        const std::vector<RelocMapEntry>& get_relocations() const noexcept;

        std::string generate_report() const;
        std::string generate_link_map() const;
        void clear() noexcept;
        bool write_to_file(const std::string& path) const;

    private:
        uint64_t base_address_ = 0;
        std::vector<SectionMapEntry> sections_;
        std::vector<SymbolMapEntry> symbols_;
        std::vector<RelocMapEntry> relocations_;
    };

    // =========================================================================
    // 5. GrassObjWriter - Container Emission Engine
    // =========================================================================

    struct WinLibraryImport {
        std::string dll_name; // e.g. "KERNEL32.dll", "WS2_32.dll", "USER32.dll"
        std::vector<std::string> symbols;
    };

    class GrassObjWriter {
    public:
        static bool validate_compiled_unit(const CompiledUnit& unit, std::string* out_error = nullptr);

        static bool write_coff_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error = nullptr, bool include_directives = false);
        static bool write_coff_obj_file(const CompiledUnit& unit, const std::string& filename, std::string* out_error = nullptr, bool include_directives = false);

        static bool write_elf_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error = nullptr);
        static bool write_elf_obj_file(const CompiledUnit& unit, const std::string& filename, std::string* out_error = nullptr);

        static bool write_flat_binary(const CompiledUnit& unit, uint64_t base_address, BinaryBuffer& out, LinkMap* out_map = nullptr, std::string* out_error = nullptr);
        static bool write_flat_binary_file(const CompiledUnit& unit, uint64_t base_address, const std::string& filename, const std::string& map_filename = "", std::string* out_error = nullptr);
        static std::string generate_link_map(const LinkMap& map);

        static bool write_pe_executable(const CompiledUnit& unit,
                                        BinaryBuffer& out,
                                        std::string* out_error = nullptr,
                                        const std::string& entry_point_name = "main");
        static bool write_pe_executable(const CompiledUnit& unit,
                                        const std::vector<WinLibraryImport>& custom_imports,
                                        BinaryBuffer& out,
                                        std::string* out_error = nullptr,
                                        const std::string& entry_point_name = "main");
        static bool write_pe_executable_file(const CompiledUnit& unit,
                                             const std::string& filename,
                                             std::string* out_error = nullptr,
                                             const std::string& entry_point_name = "main");
        static bool write_pe_executable_file(const CompiledUnit& unit,
                                             const std::vector<WinLibraryImport>& custom_imports,
                                             const std::string& filename,
                                             std::string* out_error = nullptr,
                                             const std::string& entry_point_name = "main");

        static bool inject_missing_win64_runtime(CompiledUnit& unit);

        static bool generate_import_table(const std::vector<WinLibraryImport>& imports,
                                          uint32_t idata_rva,
                                          BinaryBuffer& out_idata,
                                          std::unordered_map<std::string, uint32_t>& out_iat_offsets,
                                          std::string* out_error = nullptr,
                                          uint32_t* out_iat_rva = nullptr,
                                          uint32_t* out_iat_size = nullptr);
        static std::vector<WinLibraryImport> get_default_windows_imports();

        static uint16_t coff_machine_for_arch(TargetArch arch);
        static uint16_t coff_reloc_type(TargetArch arch, RelocType type);

        static uint16_t elf_machine_for_arch(TargetArch arch);
        static uint32_t elf_reloc_type(TargetArch arch, RelocType type);

    private:
        static bool write_elf64_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error);
        static bool write_elf32_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error);
    };

} // namespace grass::backend

#endif // GRASS_OBJ_WRITER_H

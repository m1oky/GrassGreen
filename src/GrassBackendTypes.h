#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace grass::backend {

    enum class TargetArch : uint8_t {
        X86_64,
        X86_32
    };

    enum class TargetABI : uint8_t {
        Win64,
        SysV64,
        Cdecl32,
        Baremetal64,
        Baremetal32
    };

    enum class SectionKind : uint8_t {
        Text,
        RoData,
        Data,
        Bss
    };

    enum class RelocType : uint8_t {
        Rel32,
        Abs64,
        Abs32
    };

    struct RelocEntry {
        SectionKind section = SectionKind::Text;
        uint64_t offset = 0;
        std::string symbol_name;
        RelocType type = RelocType::Rel32;
        int64_t addend = 0;
    };

    struct SymbolDef {
        std::string name;
        SectionKind section = SectionKind::Text;
        uint64_t offset = 0;
        uint64_t size = 0;
        bool is_global = true;
        bool is_external = false;
    };

    struct CompiledUnit {
        TargetArch arch = TargetArch::X86_64;
        TargetABI abi = TargetABI::Win64;
        std::vector<uint8_t> text;
        std::vector<uint8_t> rodata;
        std::vector<uint8_t> data;
        uint64_t bss_size = 0;
        std::vector<SymbolDef> symbols;
        std::vector<RelocEntry> relocations;
    };

} // namespace grass::backend
#include "native/GrassObjRuntime.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace grass::backend {

    // =========================================================================
    // 5. GrassObjWriter Implementation
    // =========================================================================

    uint16_t GrassObjWriter::coff_machine_for_arch(TargetArch arch) {
        return (arch == TargetArch::X86_64) ? COFF_MACHINE_AMD64 : COFF_MACHINE_I386;
    }

    uint16_t GrassObjWriter::coff_reloc_type(TargetArch arch, RelocType type) {
        if (arch == TargetArch::X86_64) {
            switch (type) {
                case RelocType::Rel32: return COFF_REL_AMD64_REL32;
                case RelocType::Abs64: return COFF_REL_AMD64_ADDR64;
                case RelocType::Abs32: return COFF_REL_AMD64_ADDR32;
            }
        } else {
            switch (type) {
                case RelocType::Rel32: return COFF_REL_I386_REL32;
                case RelocType::Abs32: return COFF_REL_I386_DIR32;
                case RelocType::Abs64: return COFF_REL_I386_DIR32;
            }
        }
        return 0;
    }

    uint16_t GrassObjWriter::elf_machine_for_arch(TargetArch arch) {
        return (arch == TargetArch::X86_64) ? ELF_EM_X86_64 : ELF_EM_386;
    }

    uint32_t GrassObjWriter::elf_reloc_type(TargetArch arch, RelocType type) {
        if (arch == TargetArch::X86_64) {
            switch (type) {
                case RelocType::Rel32: return ELF_R_X86_64_PC32;
                case RelocType::Abs64: return ELF_R_X86_64_64;
                case RelocType::Abs32: return ELF_R_X86_64_32;
            }
        } else {
            switch (type) {
                case RelocType::Rel32: return ELF_R_386_PC32;
                case RelocType::Abs32: return ELF_R_386_32;
                case RelocType::Abs64: return ELF_R_386_32;
            }
        }
        return 0;
    }

    bool GrassObjWriter::validate_compiled_unit(const CompiledUnit& unit, std::string* out_error) {
        for (const auto& sym : unit.symbols) {
            if (!sym.is_external) {
                if (sym.offset + sym.size < sym.offset) {
                    if (out_error) *out_error = "Symbol " + sym.name + " offset + size overflow";
                    return false;
                }
                if (sym.section == SectionKind::Text && sym.offset + sym.size > unit.text.size()) {
                    if (out_error) *out_error = "Symbol " + sym.name + " exceeds text section bounds";
                    return false;
                }
                if (sym.section == SectionKind::RoData && sym.offset + sym.size > unit.rodata.size()) {
                    if (out_error) *out_error = "Symbol " + sym.name + " exceeds rodata section bounds";
                    return false;
                }
                if (sym.section == SectionKind::Data && sym.offset + sym.size > unit.data.size()) {
                    if (out_error) *out_error = "Symbol " + sym.name + " exceeds data section bounds";
                    return false;
                }
                if (sym.section == SectionKind::Bss && sym.offset + sym.size > unit.bss_size) {
                    if (out_error) *out_error = "Symbol " + sym.name + " exceeds bss section bounds";
                    return false;
                }
            }
        }

        for (const auto& rel : unit.relocations) {
            if (rel.section == SectionKind::Bss) {
                if (out_error) *out_error = "Relocations cannot be placed in BSS section";
                return false;
            }
            uint64_t reloc_width = (rel.type == RelocType::Abs64) ? 8 : 4;
            if (rel.offset + reloc_width < rel.offset) {
                if (out_error) *out_error = "Relocation offset overflow";
                return false;
            }
            if (rel.section == SectionKind::Text && rel.offset + reloc_width > unit.text.size()) {
                if (out_error) *out_error = "Relocation offset exceeds text section bounds";
                return false;
            }
            if (rel.section == SectionKind::RoData && rel.offset + reloc_width > unit.rodata.size()) {
                if (out_error) *out_error = "Relocation offset exceeds rodata section bounds";
                return false;
            }
            if (rel.section == SectionKind::Data && rel.offset + reloc_width > unit.data.size()) {
                if (out_error) *out_error = "Relocation offset exceeds data section bounds";
                return false;
            }
            if (unit.arch == TargetArch::X86_32 && rel.type == RelocType::Abs64) {
                if (out_error) *out_error = "Abs64 relocation requested for 32-bit architecture";
                return false;
            }
        }

        return true;
    }

    bool GrassObjWriter::write_coff_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error, bool include_directives) {
        if (!validate_compiled_unit(unit, out_error)) {
            return false;
        }

        struct SectionData {
            SectionKind kind;
            std::string name;
            uint32_t characteristics = 0;
            std::vector<uint8_t> raw_bytes;
            std::vector<RelocEntry> relocs;
            int16_t section_number = 0;
            uint32_t ptr_raw_data = 0;
            uint32_t ptr_relocs = 0;
        };

        std::vector<SectionData> sections;
        int16_t next_sec_num = 1;

        if (!unit.text.empty() || (unit.rodata.empty() && unit.data.empty() && unit.bss_size == 0)) {
            SectionData sec;
            sec.kind = SectionKind::Text;
            sec.name = ".text";
            sec.characteristics = COFF_SCN_CNT_CODE | COFF_SCN_MEM_EXECUTE | COFF_SCN_MEM_READ | COFF_SCN_ALIGN_16BYTES;
            sec.raw_bytes = unit.text;
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        if (!unit.rodata.empty()) {
            SectionData sec;
            sec.kind = SectionKind::RoData;
            sec.name = ".rdata";
            sec.characteristics = COFF_SCN_CNT_INITIALIZED_DATA | COFF_SCN_MEM_READ | COFF_SCN_ALIGN_16BYTES;
            sec.raw_bytes = unit.rodata;
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        if (!unit.data.empty()) {
            SectionData sec;
            sec.kind = SectionKind::Data;
            sec.name = ".data";
            sec.characteristics = COFF_SCN_CNT_INITIALIZED_DATA | COFF_SCN_MEM_READ | COFF_SCN_MEM_WRITE | COFF_SCN_ALIGN_16BYTES;
            sec.raw_bytes = unit.data;
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        if (unit.bss_size > 0) {
            SectionData sec;
            sec.kind = SectionKind::Bss;
            sec.name = ".bss";
            sec.characteristics = COFF_SCN_CNT_UNINITIALIZED_DATA | COFF_SCN_MEM_READ | COFF_SCN_MEM_WRITE | COFF_SCN_ALIGN_16BYTES;
            sec.raw_bytes.clear();
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        // Add .drectve section to automatically pull in system libraries when linking
        if (include_directives) {
            SectionData sec;
            sec.kind = SectionKind::RoData;
            sec.name = ".drectve";
            sec.characteristics = 0x00100A00; // IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE | ALIGN_1BYTE
            std::string drectve_str = " -defaultlib:kernel32.lib -defaultlib:ws2_32.lib -defaultlib:user32.lib ";
            sec.raw_bytes.assign(drectve_str.begin(), drectve_str.end());
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        std::unordered_map<SectionKind, int16_t> sec_kind_to_num;
        for (const auto& sec : sections) {
            sec_kind_to_num[sec.kind] = sec.section_number;
        }

        for (const auto& rel : unit.relocations) {
            for (auto& sec : sections) {
                if (sec.kind == rel.section) {
                    sec.relocs.push_back(rel);
                    break;
                }
            }
        }

        for (auto& sec : sections) {
            for (const auto& rel : sec.relocs) {
                size_t off = static_cast<size_t>(rel.offset);
                if (rel.type == RelocType::Rel32) {
                    int32_t inline_addend = static_cast<int32_t>(rel.addend + 4);
                    if (inline_addend != 0 && off + 4 <= sec.raw_bytes.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sec.raw_bytes.data() + off, 4);
                        curr += inline_addend;
                        std::memcpy(sec.raw_bytes.data() + off, &curr, 4);
                    }
                } else if (rel.type == RelocType::Abs32) {
                    if (rel.addend != 0 && off + 4 <= sec.raw_bytes.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sec.raw_bytes.data() + off, 4);
                        curr += static_cast<int32_t>(rel.addend);
                        std::memcpy(sec.raw_bytes.data() + off, &curr, 4);
                    }
                } else if (rel.type == RelocType::Abs64) {
                    if (rel.addend != 0 && off + 8 <= sec.raw_bytes.size()) {
                        int64_t curr = 0;
                        std::memcpy(&curr, sec.raw_bytes.data() + off, 8);
                        curr += rel.addend;
                        std::memcpy(sec.raw_bytes.data() + off, &curr, 8);
                    }
                }
            }
        }

        StringTableBuilder coff_strtab;
        std::vector<COFF_SymbolRecord> symbols;
        std::unordered_map<std::string, uint32_t> sym_to_index;

        for (const auto& sec : sections) {
            COFF_SymbolRecord sym{};
            size_t copy_len = std::min<size_t>(sec.name.size(), 8);
            std::memcpy(sym.name.short_name, sec.name.data(), copy_len);
            sym.value = 0;
            sym.section_number = sec.section_number;
            sym.type = COFF_SYM_TYPE_NULL;
            sym.storage_class = COFF_SYM_CLASS_STATIC;
            sym.number_of_aux_symbols = 0;
            sym_to_index[sec.name] = static_cast<uint32_t>(symbols.size());
            symbols.push_back(sym);
        }

        for (const auto& s : unit.symbols) {
            COFF_SymbolRecord sym{};

            if (s.name.size() <= 8) {
                std::memcpy(sym.name.short_name, s.name.data(), s.name.size());
            } else {
                uint32_t str_off = coff_strtab.add_coff(s.name);
                sym.name.long_name.zeroes = 0;
                sym.name.long_name.offset = str_off;
            }

            if (s.is_external) {
                sym.value = 0;
                sym.section_number = 0;
                sym.storage_class = COFF_SYM_CLASS_EXTERNAL;
                sym.type = (s.section == SectionKind::Text) ? COFF_SYM_TYPE_FUNC : COFF_SYM_TYPE_NULL;
            } else {
                sym.value = static_cast<uint32_t>(s.offset);
                auto it = sec_kind_to_num.find(s.section);
                sym.section_number = (it != sec_kind_to_num.end()) ? it->second : 0;
                sym.storage_class = s.is_global ? COFF_SYM_CLASS_EXTERNAL : COFF_SYM_CLASS_STATIC;
                sym.type = (s.section == SectionKind::Text) ? COFF_SYM_TYPE_FUNC : COFF_SYM_TYPE_NULL;
            }

            sym.number_of_aux_symbols = 0;
            sym_to_index[s.name] = static_cast<uint32_t>(symbols.size());
            symbols.push_back(sym);
        }

        for (const auto& rel : unit.relocations) {
            if (sym_to_index.find(rel.symbol_name) == sym_to_index.end()) {
                COFF_SymbolRecord sym{};

                if (rel.symbol_name.size() <= 8) {
                    std::memcpy(sym.name.short_name, rel.symbol_name.data(), rel.symbol_name.size());
                } else {
                    uint32_t str_off = coff_strtab.add_coff(rel.symbol_name);
                    sym.name.long_name.zeroes = 0;
                    sym.name.long_name.offset = str_off;
                }

                sym.value = 0;
                sym.section_number = 0;
                sym.storage_class = COFF_SYM_CLASS_EXTERNAL;
                sym.type = (rel.type == RelocType::Rel32) ? COFF_SYM_TYPE_FUNC : COFF_SYM_TYPE_NULL;
                sym.number_of_aux_symbols = 0;
                sym_to_index[rel.symbol_name] = static_cast<uint32_t>(symbols.size());
                symbols.push_back(sym);
            }
        }

        uint32_t file_offset = static_cast<uint32_t>(sizeof(COFF_FileHeader) + sections.size() * sizeof(COFF_SectionHeader));

        for (auto& sec : sections) {
            if (!sec.raw_bytes.empty()) {
                uint32_t rem = file_offset % 4;
                if (rem != 0) file_offset += (4 - rem);
                sec.ptr_raw_data = file_offset;
                file_offset += static_cast<uint32_t>(sec.raw_bytes.size());
            } else {
                sec.ptr_raw_data = 0;
            }
        }

        for (auto& sec : sections) {
            if (!sec.relocs.empty()) {
                uint32_t rem = file_offset % 2;
                if (rem != 0) file_offset += (2 - rem);
                sec.ptr_relocs = file_offset;
                file_offset += static_cast<uint32_t>(sec.relocs.size() * sizeof(COFF_Relocation));
            } else {
                sec.ptr_relocs = 0;
            }
        }

        uint32_t rem_sym = file_offset % 4;
        if (rem_sym != 0) file_offset += (4 - rem_sym);
        uint32_t ptr_symbol_table = file_offset;

        out.clear();

        COFF_FileHeader file_hdr{};
        file_hdr.machine = coff_machine_for_arch(unit.arch);
        file_hdr.number_of_sections = static_cast<uint16_t>(sections.size());
        file_hdr.time_date_stamp = 0;
        file_hdr.pointer_to_symbol_table = ptr_symbol_table;
        file_hdr.number_of_symbols = static_cast<uint32_t>(symbols.size());
        file_hdr.size_of_optional_header = 0;
        file_hdr.characteristics = 0;
        out.emit_bytes(&file_hdr, sizeof(file_hdr));

        for (const auto& sec : sections) {
            COFF_SectionHeader shdr{};
            size_t copy_len = std::min<size_t>(sec.name.size(), 8);
            std::memcpy(shdr.name, sec.name.data(), copy_len);
            shdr.virtual_size = 0;
            shdr.virtual_address = 0;
            shdr.size_of_raw_data = static_cast<uint32_t>(sec.raw_bytes.size());
            shdr.pointer_to_raw_data = sec.ptr_raw_data;
            shdr.pointer_to_relocations = sec.ptr_relocs;
            shdr.pointer_to_linenumbers = 0;
            shdr.number_of_relocations = static_cast<uint16_t>(sec.relocs.size());
            shdr.number_of_linenumbers = 0;
            shdr.characteristics = sec.characteristics;
            out.emit_bytes(&shdr, sizeof(shdr));
        }

        for (const auto& sec : sections) {
            if (sec.ptr_raw_data > 0) {
                out.align_to(4, 0);
                out.emit_bytes(sec.raw_bytes.data(), sec.raw_bytes.size());
            }
        }

        for (const auto& sec : sections) {
            if (sec.ptr_relocs > 0) {
                out.align_to(2, 0);
                for (const auto& rel : sec.relocs) {
                    COFF_Relocation crel{};
                    crel.virtual_address = static_cast<uint32_t>(rel.offset);
                    crel.symbol_table_index = sym_to_index[rel.symbol_name];
                    crel.type = coff_reloc_type(unit.arch, rel.type);
                    out.emit_bytes(&crel, sizeof(crel));
                }
            }
        }

        out.align_to(4, 0);
        for (const auto& sym : symbols) {
            out.emit_bytes(&sym, sizeof(sym));
        }

        coff_strtab.build_coff(out);
        return true;
    }

    bool GrassObjWriter::write_coff_obj_file(const CompiledUnit& unit, const std::string& filename, std::string* out_error, bool include_directives) {
        BinaryBuffer buf;
        if (!write_coff_obj(unit, buf, out_error, include_directives)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write COFF object file to " + filename;
            return false;
        }
        return true;
    }

    bool GrassObjWriter::write_elf_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error) {
        if (unit.arch == TargetArch::X86_64) {
            return write_elf64_obj(unit, out, out_error);
        } else {
            return write_elf32_obj(unit, out, out_error);
        }
    }

    bool GrassObjWriter::write_elf_obj_file(const CompiledUnit& unit, const std::string& filename, std::string* out_error) {
        BinaryBuffer buf;
        if (!write_elf_obj(unit, buf, out_error)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write ELF object file to " + filename;
            return false;
        }
        return true;
    }

    bool GrassObjWriter::write_flat_binary(const CompiledUnit& unit, uint64_t base_address, BinaryBuffer& out, LinkMap* out_map, std::string* out_error) {
        if (!validate_compiled_unit(unit, out_error)) {
            return false;
        }

        uint64_t text_addr = base_address;
        uint64_t text_size = unit.text.size();
        uint64_t text_end = text_addr + text_size;

        uint64_t rodata_addr = (text_size > 0) ? ((text_end + 15) / 16 * 16) : text_addr;
        uint64_t rodata_size = unit.rodata.size();
        uint64_t rodata_end = rodata_addr + rodata_size;

        uint64_t data_addr = (rodata_size > 0) ? ((rodata_end + 15) / 16 * 16) : rodata_end;
        uint64_t data_size = unit.data.size();
        uint64_t data_end = data_addr + data_size;

        uint64_t bss_addr = (data_size > 0) ? ((data_end + 15) / 16 * 16) : data_end;
        uint64_t bss_size = unit.bss_size;

        if (out_map) {
            out_map->clear();
            out_map->set_base_address(base_address);
            if (text_size > 0) out_map->add_section_entry(".text", text_addr, text_size, 16);
            if (rodata_size > 0) out_map->add_section_entry(".rodata", rodata_addr, rodata_size, 16);
            if (data_size > 0) out_map->add_section_entry(".data", data_addr, data_size, 16);
            if (bss_size > 0) out_map->add_section_entry(".bss", bss_addr, bss_size, 16);
        }

        std::unordered_map<std::string, uint64_t> sym_addrs;
        for (const auto& s : unit.symbols) {
            if (s.is_external) continue;
            uint64_t addr = 0;
            if (s.section == SectionKind::Text) addr = text_addr + s.offset;
            else if (s.section == SectionKind::RoData) addr = rodata_addr + s.offset;
            else if (s.section == SectionKind::Data) addr = data_addr + s.offset;
            else if (s.section == SectionKind::Bss) addr = bss_addr + s.offset;

            sym_addrs[s.name] = addr;
            if (out_map) {
                out_map->add_symbol_entry(s.name, addr, s.size, s.section, s.is_global);
            }
        }

        out.clear();
        out.emit_bytes(unit.text.data(), unit.text.size());

        if (rodata_size > 0) {
            size_t target_off = static_cast<size_t>(rodata_addr - base_address);
            if (out.size() < target_off) {
                out.emit_zeros(target_off - out.size());
            }
            out.emit_bytes(unit.rodata.data(), unit.rodata.size());
        }

        if (data_size > 0) {
            size_t target_off = static_cast<size_t>(data_addr - base_address);
            if (out.size() < target_off) {
                out.emit_zeros(target_off - out.size());
            }
            out.emit_bytes(unit.data.data(), unit.data.size());
        }

        for (const auto& rel : unit.relocations) {
            uint64_t sec_base = 0;
            size_t sec_buf_off = 0;
            if (rel.section == SectionKind::Text) {
                sec_base = text_addr;
                sec_buf_off = 0;
            } else if (rel.section == SectionKind::RoData) {
                sec_base = rodata_addr;
                sec_buf_off = static_cast<size_t>(rodata_addr - base_address);
            } else if (rel.section == SectionKind::Data) {
                sec_base = data_addr;
                sec_buf_off = static_cast<size_t>(data_addr - base_address);
            } else {
                if (out_error) *out_error = "Relocation in unsupported section for flat binary";
                return false;
            }

            uint64_t site_addr = sec_base + rel.offset;
            size_t patch_off = sec_buf_off + static_cast<size_t>(rel.offset);

            auto it = sym_addrs.find(rel.symbol_name);
            if (it == sym_addrs.end()) {
                if (out_error) *out_error = "Unresolved symbol in flat binary: " + rel.symbol_name;
                return false;
            }
            uint64_t sym_addr = it->second;
            int64_t computed = 0;

            if (rel.type == RelocType::Rel32) {
                int64_t disp = static_cast<int64_t>(sym_addr + rel.addend) - static_cast<int64_t>(site_addr);
                computed = disp;
                out.patch_i32(patch_off, static_cast<int32_t>(disp));
            } else if (rel.type == RelocType::Abs64) {
                uint64_t abs_val = sym_addr + rel.addend;
                computed = static_cast<int64_t>(abs_val);
                out.patch_u64(patch_off, abs_val);
            } else if (rel.type == RelocType::Abs32) {
                uint32_t abs_val = static_cast<uint32_t>(sym_addr + rel.addend);
                computed = static_cast<int64_t>(abs_val);
                out.patch_u32(patch_off, abs_val);
            }

            if (out_map) {
                out_map->add_reloc_entry(rel.section, rel.offset, site_addr, rel.symbol_name, rel.type, rel.addend, computed);
            }
        }

        return true;
    }

    bool GrassObjWriter::write_flat_binary_file(const CompiledUnit& unit, uint64_t base_address, const std::string& filename, const std::string& map_filename, std::string* out_error) {
        BinaryBuffer buf;
        LinkMap lmap;
        if (!write_flat_binary(unit, base_address, buf, &lmap, out_error)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write flat binary to " + filename;
            return false;
        }
        if (!map_filename.empty()) {
            if (!lmap.write_to_file(map_filename)) {
                if (out_error) *out_error = "Failed to write link map to " + map_filename;
                return false;
            }
        }
        return true;
    }

    std::string GrassObjWriter::generate_link_map(const LinkMap& map) {
        return map.generate_report();
    }

    void Win64RuntimeCtx::add_rodata_str(const std::string& sym_name, const std::string& text) {
        for (const auto& s : unit.symbols) {
            if (s.name == sym_name && !s.is_external) return;
        }
        while (unit.rodata.size() % 16 != 0) unit.rodata.push_back(0);
        uint64_t off = unit.rodata.size();
        unit.rodata.insert(unit.rodata.end(), text.begin(), text.end());
        unit.rodata.push_back(0);
        SymbolDef sym;
        sym.name = sym_name;
        sym.section = SectionKind::RoData;
        sym.offset = off;
        sym.size = text.size() + 1;
        sym.is_global = true;
        sym.is_external = false;
        unit.symbols.push_back(sym);
        defined_symbols.insert(sym_name);
        any_injected = true;
    }

    void Win64RuntimeCtx::add_data_buffer(const std::string& sym_name, size_t size_bytes) {
        for (const auto& s : unit.symbols) {
            if (s.name == sym_name && !s.is_external) return;
        }
        while (unit.data.size() % 16 != 0) unit.data.push_back(0);
        uint64_t off = unit.data.size();
        unit.data.resize(off + size_bytes, 0);
        SymbolDef sym;
        sym.name = sym_name;
        sym.section = SectionKind::Data;
        sym.offset = off;
        sym.size = size_bytes;
        sym.is_global = true;
        sym.is_external = false;
        unit.symbols.push_back(sym);
        defined_symbols.insert(sym_name);
        any_injected = true;
    }

    void Win64RuntimeCtx::add_bss_buffer(const std::string& sym_name, size_t size_bytes) {
        for (const auto& s : unit.symbols) {
            if (s.name == sym_name && !s.is_external) return;
        }
        unit.bss_size = (unit.bss_size + 15) & ~static_cast<uint64_t>(15);
        uint64_t off = unit.bss_size;
        unit.bss_size += size_bytes;
        SymbolDef sym;
        sym.name = sym_name;
        sym.section = SectionKind::Bss;
        sym.offset = off;
        sym.size = size_bytes;
        sym.is_global = true;
        sym.is_external = false;
        unit.symbols.push_back(sym);
        defined_symbols.insert(sym_name);
        any_injected = true;
    }

    void Win64RuntimeCtx::append_function(const std::string& name,
                                          const std::vector<uint8_t>& code,
                                          const std::vector<RelocEntry>& rels) {
        while (unit.text.size() % 16 != 0) {
            unit.text.push_back(0x90);
        }
        uint64_t fn_offset = unit.text.size();
        unit.text.insert(unit.text.end(), code.begin(), code.end());

        unit.symbols.erase(
            std::remove_if(unit.symbols.begin(), unit.symbols.end(),
                [&](const SymbolDef& s) { return s.name == name && s.is_external; }),
            unit.symbols.end());

        SymbolDef sym;
        sym.name = name;
        sym.section = SectionKind::Text;
        sym.offset = fn_offset;
        sym.size = code.size();
        sym.is_global = true;
        sym.is_external = false;
        unit.symbols.push_back(sym);

        for (auto r : rels) {
            r.offset += fn_offset;
            unit.relocations.push_back(r);
        }
        defined_symbols.insert(name);
        any_injected = true;
    }

    bool GrassObjWriter::inject_missing_win64_runtime(CompiledUnit& unit) {
        for (int pass = 0; pass < 8; ++pass) {
            bool any_injected = false;
            std::unordered_set<std::string> defined_symbols;
            for (const auto& s : unit.symbols) {
                if (!s.is_external) {
                    defined_symbols.insert(s.name);
                }
            }

            std::unordered_set<std::string> needed_symbols;
            for (const auto& r : unit.relocations) {
                if (defined_symbols.find(r.symbol_name) == defined_symbols.end()) {
                    needed_symbols.insert(r.symbol_name);
                }
            }

            Win64RuntimeCtx ctx{ unit, defined_symbols, needed_symbols, any_injected };
            inject_win64_rt_io(ctx);
            inject_win64_rt_sys(ctx);

            if (!any_injected) {
                break;
            }
        }
        return true;
    }

} // namespace grass::backend

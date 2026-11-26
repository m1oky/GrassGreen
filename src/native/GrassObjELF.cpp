#include "native/GrassObjWriter.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace grass::backend {

    bool GrassObjWriter::write_elf64_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error) {
        if (!validate_compiled_unit(unit, out_error)) {
            return false;
        }

        struct ElfSecDesc {
            std::string name;
            uint32_t type = 0;
            uint64_t flags = 0;
            uint64_t addr = 0;
            uint64_t offset = 0;
            uint64_t size = 0;
            uint32_t link = 0;
            uint32_t info = 0;
            uint64_t addralign = 1;
            uint64_t entsize = 0;
            std::vector<uint8_t> data;
            SectionKind sec_kind = SectionKind::Text;
            bool is_sec_kind = false;
        };

        std::vector<ElfSecDesc> sections;

        ElfSecDesc null_sec;
        null_sec.name = "";
        null_sec.type = ELF_SHT_NULL;
        sections.push_back(null_sec);

        uint32_t text_idx = 0;
        uint32_t rodata_idx = 0;
        uint32_t data_idx = 0;
        uint32_t bss_idx = 0;

        if (!unit.text.empty() || (unit.rodata.empty() && unit.data.empty() && unit.bss_size == 0)) {
            ElfSecDesc s;
            s.name = ".text";
            s.type = ELF_SHT_PROGBITS;
            s.flags = ELF_SHF_ALLOC | ELF_SHF_EXECINSTR;
            s.addralign = 16;
            s.data = unit.text;
            s.size = unit.text.size();
            s.sec_kind = SectionKind::Text;
            s.is_sec_kind = true;
            text_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (!unit.rodata.empty()) {
            ElfSecDesc s;
            s.name = ".rodata";
            s.type = ELF_SHT_PROGBITS;
            s.flags = ELF_SHF_ALLOC;
            s.addralign = 16;
            s.data = unit.rodata;
            s.size = unit.rodata.size();
            s.sec_kind = SectionKind::RoData;
            s.is_sec_kind = true;
            rodata_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (!unit.data.empty()) {
            ElfSecDesc s;
            s.name = ".data";
            s.type = ELF_SHT_PROGBITS;
            s.flags = ELF_SHF_ALLOC | ELF_SHF_WRITE;
            s.addralign = 16;
            s.data = unit.data;
            s.size = unit.data.size();
            s.sec_kind = SectionKind::Data;
            s.is_sec_kind = true;
            data_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (unit.bss_size > 0) {
            ElfSecDesc s;
            s.name = ".bss";
            s.type = ELF_SHT_NOBITS;
            s.flags = ELF_SHF_ALLOC | ELF_SHF_WRITE;
            s.addralign = 16;
            s.size = unit.bss_size;
            s.sec_kind = SectionKind::Bss;
            s.is_sec_kind = true;
            bss_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        std::vector<RelocEntry> text_relocs;
        std::vector<RelocEntry> rodata_relocs;
        std::vector<RelocEntry> data_relocs;
        for (const auto& rel : unit.relocations) {
            if (rel.section == SectionKind::Text) {
                text_relocs.push_back(rel);
            } else if (rel.section == SectionKind::RoData) {
                rodata_relocs.push_back(rel);
            } else if (rel.section == SectionKind::Data) {
                data_relocs.push_back(rel);
            }
        }

        uint32_t rela_text_idx = 0;
        if (!text_relocs.empty()) {
            ElfSecDesc s;
            s.name = ".rela.text";
            s.type = ELF_SHT_RELA;
            s.flags = ELF_SHF_INFO_LINK;
            s.addralign = 8;
            s.entsize = sizeof(Elf64_Rela);
            s.info = text_idx;
            rela_text_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        uint32_t rela_rodata_idx = 0;
        if (!rodata_relocs.empty()) {
            ElfSecDesc s;
            s.name = ".rela.rodata";
            s.type = ELF_SHT_RELA;
            s.flags = ELF_SHF_INFO_LINK;
            s.addralign = 8;
            s.entsize = sizeof(Elf64_Rela);
            s.info = rodata_idx;
            rela_rodata_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        uint32_t rela_data_idx = 0;
        if (!data_relocs.empty()) {
            ElfSecDesc s;
            s.name = ".rela.data";
            s.type = ELF_SHT_RELA;
            s.flags = ELF_SHF_INFO_LINK;
            s.addralign = 8;
            s.entsize = sizeof(Elf64_Rela);
            s.info = data_idx;
            rela_data_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        ElfSecDesc symtab_sec;
        symtab_sec.name = ".symtab";
        symtab_sec.type = ELF_SHT_SYMTAB;
        symtab_sec.flags = 0;
        symtab_sec.addralign = 8;
        symtab_sec.entsize = sizeof(Elf64_Sym);
        uint32_t symtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(symtab_sec);

        ElfSecDesc strtab_sec;
        strtab_sec.name = ".strtab";
        strtab_sec.type = ELF_SHT_STRTAB;
        strtab_sec.flags = 0;
        strtab_sec.addralign = 1;
        uint32_t strtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(strtab_sec);

        ElfSecDesc shstrtab_sec;
        shstrtab_sec.name = ".shstrtab";
        shstrtab_sec.type = ELF_SHT_STRTAB;
        shstrtab_sec.flags = 0;
        shstrtab_sec.addralign = 1;
        uint32_t shstrtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(shstrtab_sec);

        sections[symtab_idx].link = strtab_idx;
        if (rela_text_idx > 0) sections[rela_text_idx].link = symtab_idx;
        if (rela_rodata_idx > 0) sections[rela_rodata_idx].link = symtab_idx;
        if (rela_data_idx > 0) sections[rela_data_idx].link = symtab_idx;

        StringTableBuilder strtab;
        std::vector<Elf64_Sym> local_symbols;
        std::vector<Elf64_Sym> global_symbols;
        std::unordered_map<std::string, uint32_t> sym_index_map;

        Elf64_Sym sym0{};
        local_symbols.push_back(sym0);

        Elf64_Sym sym_file{};
        sym_file.st_name = strtab.add_elf("grass_source");
        sym_file.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_FILE;
        sym_file.st_shndx = ELF_SHN_ABS;
        local_symbols.push_back(sym_file);

        if (text_idx > 0) {
            Elf64_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(text_idx);
            sym_index_map[".text"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (rodata_idx > 0) {
            Elf64_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(rodata_idx);
            sym_index_map[".rodata"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (data_idx > 0) {
            Elf64_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(data_idx);
            sym_index_map[".data"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (bss_idx > 0) {
            Elf64_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(bss_idx);
            sym_index_map[".bss"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }

        auto get_section_shndx = [&](SectionKind k) -> uint16_t {
            if (k == SectionKind::Text) return static_cast<uint16_t>(text_idx);
            if (k == SectionKind::RoData) return static_cast<uint16_t>(rodata_idx);
            if (k == SectionKind::Data) return static_cast<uint16_t>(data_idx);
            if (k == SectionKind::Bss) return static_cast<uint16_t>(bss_idx);
            return 0;
        };

        for (const auto& s : unit.symbols) {
            if (!s.is_global && !s.is_external) {
                Elf64_Sym sym{};
                sym.st_name = strtab.add_elf(s.name);
                sym.st_info = (ELF_STB_LOCAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_OBJECT);
                sym.st_shndx = get_section_shndx(s.section);
                sym.st_value = s.offset;
                sym.st_size = s.size;
                sym_index_map[s.name] = static_cast<uint32_t>(local_symbols.size());
                local_symbols.push_back(sym);
            }
        }

        uint32_t first_global_idx = static_cast<uint32_t>(local_symbols.size());
        sections[symtab_idx].info = first_global_idx;

        for (const auto& s : unit.symbols) {
            if (s.is_global && !s.is_external) {
                Elf64_Sym sym{};
                sym.st_name = strtab.add_elf(s.name);
                sym.st_info = (ELF_STB_GLOBAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_OBJECT);
                sym.st_shndx = get_section_shndx(s.section);
                sym.st_value = s.offset;
                sym.st_size = s.size;
                sym_index_map[s.name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                global_symbols.push_back(sym);
            }
        }

        for (const auto& s : unit.symbols) {
            if (s.is_external) {
                if (sym_index_map.find(s.name) == sym_index_map.end()) {
                    Elf64_Sym sym{};
                    sym.st_name = strtab.add_elf(s.name);
                    sym.st_info = (ELF_STB_GLOBAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_NOTYPE);
                    sym.st_shndx = 0;
                    sym.st_value = 0;
                    sym.st_size = 0;
                    sym_index_map[s.name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                    global_symbols.push_back(sym);
                }
            }
        }

        for (const auto& rel : unit.relocations) {
            if (sym_index_map.find(rel.symbol_name) == sym_index_map.end()) {
                Elf64_Sym sym{};
                sym.st_name = strtab.add_elf(rel.symbol_name);
                sym.st_info = (ELF_STB_GLOBAL << 4) | ((rel.type == RelocType::Rel32) ? ELF_STT_FUNC : ELF_STT_NOTYPE);
                sym.st_shndx = 0;
                sym.st_value = 0;
                sym.st_size = 0;
                sym_index_map[rel.symbol_name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                global_symbols.push_back(sym);
            }
        }

        std::vector<Elf64_Sym> all_symbols = local_symbols;
        all_symbols.insert(all_symbols.end(), global_symbols.begin(), global_symbols.end());

        BinaryBuffer symtab_buf;
        for (const auto& s : all_symbols) {
            symtab_buf.emit_bytes(&s, sizeof(s));
        }
        sections[symtab_idx].data = symtab_buf.get_bytes();
        sections[symtab_idx].size = symtab_buf.size();

        BinaryBuffer strtab_buf;
        strtab.build_elf(strtab_buf);
        sections[strtab_idx].data = strtab_buf.get_bytes();
        sections[strtab_idx].size = strtab_buf.size();

        if (rela_text_idx > 0) {
            BinaryBuffer rela_buf;
            for (const auto& rel : text_relocs) {
                Elf64_Rela r{};
                r.r_offset = rel.offset;
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_64, rel.type);
                r.r_info = (static_cast<uint64_t>(sym_id) << 32) | (static_cast<uint64_t>(r_type) & 0xFFFFFFFFULL);
                r.r_addend = rel.addend;
                rela_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rela_text_idx].data = rela_buf.get_bytes();
            sections[rela_text_idx].size = rela_buf.size();
        }

        if (rela_rodata_idx > 0) {
            BinaryBuffer rela_buf;
            for (const auto& rel : rodata_relocs) {
                Elf64_Rela r{};
                r.r_offset = rel.offset;
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_64, rel.type);
                r.r_info = (static_cast<uint64_t>(sym_id) << 32) | (static_cast<uint64_t>(r_type) & 0xFFFFFFFFULL);
                r.r_addend = rel.addend;
                rela_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rela_rodata_idx].data = rela_buf.get_bytes();
            sections[rela_rodata_idx].size = rela_buf.size();
        }

        if (rela_data_idx > 0) {
            BinaryBuffer rela_buf;
            for (const auto& rel : data_relocs) {
                Elf64_Rela r{};
                r.r_offset = rel.offset;
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_64, rel.type);
                r.r_info = (static_cast<uint64_t>(sym_id) << 32) | (static_cast<uint64_t>(r_type) & 0xFFFFFFFFULL);
                r.r_addend = rel.addend;
                rela_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rela_data_idx].data = rela_buf.get_bytes();
            sections[rela_data_idx].size = rela_buf.size();
        }

        StringTableBuilder shstrtab;
        for (const auto& sec : sections) {
            shstrtab.add_elf(sec.name);
        }
        BinaryBuffer shstr_buf;
        shstrtab.build_elf(shstr_buf);
        sections[shstrtab_idx].data = shstr_buf.get_bytes();
        sections[shstrtab_idx].size = shstr_buf.size();

        out.clear();

        Elf64_Ehdr ehdr{};
        ehdr.e_ident[0] = 0x7F;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = ELF_CLASS_64;
        ehdr.e_ident[5] = ELF_DATA_2LSB;
        ehdr.e_ident[6] = ELF_EV_CURRENT;
        ehdr.e_ident[7] = ELF_OSABI_SYSV;
        ehdr.e_type = ELF_ET_REL;
        ehdr.e_machine = ELF_EM_X86_64;
        ehdr.e_version = ELF_EV_CURRENT;
        ehdr.e_shoff = 0; // patched below
        ehdr.e_ehsize = sizeof(Elf64_Ehdr);
        ehdr.e_shentsize = sizeof(Elf64_Shdr);
        ehdr.e_shnum = static_cast<uint16_t>(sections.size());
        ehdr.e_shstrndx = static_cast<uint16_t>(shstrtab_idx);
        out.emit_bytes(&ehdr, sizeof(ehdr));

        for (size_t i = 1; i < sections.size(); ++i) {
            auto& sec = sections[i];
            if (sec.type == ELF_SHT_NOBITS) {
                sec.offset = out.size();
            } else if (sec.size > 0) {
                out.align_to(sec.addralign > 0 ? static_cast<size_t>(sec.addralign) : 1, 0);
                sec.offset = out.size();
                out.emit_bytes(sec.data.data(), sec.data.size());
            } else {
                sec.offset = out.size();
            }
        }

        out.align_to(8, 0);
        uint64_t shoff = out.size();
        out.patch_u64(40, shoff);

        for (const auto& sec : sections) {
            Elf64_Shdr shdr{};
            shdr.sh_name = shstrtab.get_offset(sec.name);
            shdr.sh_type = sec.type;
            shdr.sh_flags = sec.flags;
            shdr.sh_addr = sec.addr;
            shdr.sh_offset = sec.offset;
            shdr.sh_size = sec.size;
            shdr.sh_link = sec.link;
            shdr.sh_info = sec.info;
            shdr.sh_addralign = sec.addralign;
            shdr.sh_entsize = sec.entsize;
            out.emit_bytes(&shdr, sizeof(shdr));
        }

        return true;
    }

    bool GrassObjWriter::write_elf32_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error) {
        if (!validate_compiled_unit(unit, out_error)) {
            return false;
        }

        struct ElfSecDesc32 {
            std::string name;
            uint32_t type = 0;
            uint32_t flags = 0;
            uint32_t addr = 0;
            uint32_t offset = 0;
            uint32_t size = 0;
            uint32_t link = 0;
            uint32_t info = 0;
            uint32_t addralign = 1;
            uint32_t entsize = 0;
            std::vector<uint8_t> data;
            SectionKind sec_kind = SectionKind::Text;
            bool is_sec_kind = false;
        };

        std::vector<ElfSecDesc32> sections;

        ElfSecDesc32 null_sec;
        null_sec.name = "";
        null_sec.type = ELF_SHT_NULL;
        sections.push_back(null_sec);

        uint32_t text_idx = 0;
        uint32_t rodata_idx = 0;
        uint32_t data_idx = 0;
        uint32_t bss_idx = 0;

        if (!unit.text.empty() || (unit.rodata.empty() && unit.data.empty() && unit.bss_size == 0)) {
            ElfSecDesc32 s;
            s.name = ".text";
            s.type = ELF_SHT_PROGBITS;
            s.flags = static_cast<uint32_t>(ELF_SHF_ALLOC | ELF_SHF_EXECINSTR);
            s.addralign = 16;
            s.data = unit.text;
            s.size = static_cast<uint32_t>(unit.text.size());
            s.sec_kind = SectionKind::Text;
            s.is_sec_kind = true;
            text_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (!unit.rodata.empty()) {
            ElfSecDesc32 s;
            s.name = ".rodata";
            s.type = ELF_SHT_PROGBITS;
            s.flags = static_cast<uint32_t>(ELF_SHF_ALLOC);
            s.addralign = 16;
            s.data = unit.rodata;
            s.size = static_cast<uint32_t>(unit.rodata.size());
            s.sec_kind = SectionKind::RoData;
            s.is_sec_kind = true;
            rodata_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (!unit.data.empty()) {
            ElfSecDesc32 s;
            s.name = ".data";
            s.type = ELF_SHT_PROGBITS;
            s.flags = static_cast<uint32_t>(ELF_SHF_ALLOC | ELF_SHF_WRITE);
            s.addralign = 16;
            s.data = unit.data;
            s.size = static_cast<uint32_t>(unit.data.size());
            s.sec_kind = SectionKind::Data;
            s.is_sec_kind = true;
            data_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (unit.bss_size > 0) {
            ElfSecDesc32 s;
            s.name = ".bss";
            s.type = ELF_SHT_NOBITS;
            s.flags = static_cast<uint32_t>(ELF_SHF_ALLOC | ELF_SHF_WRITE);
            s.addralign = 16;
            s.size = static_cast<uint32_t>(unit.bss_size);
            s.sec_kind = SectionKind::Bss;
            s.is_sec_kind = true;
            bss_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        std::vector<RelocEntry> text_relocs;
        std::vector<RelocEntry> rodata_relocs;
        std::vector<RelocEntry> data_relocs;
        for (const auto& rel : unit.relocations) {
            if (rel.section == SectionKind::Text) {
                text_relocs.push_back(rel);
            } else if (rel.section == SectionKind::RoData) {
                rodata_relocs.push_back(rel);
            } else if (rel.section == SectionKind::Data) {
                data_relocs.push_back(rel);
            }
        }

        if (text_idx > 0) {
            for (const auto& rel : text_relocs) {
                if (rel.addend != 0) {
                    size_t off = static_cast<size_t>(rel.offset);
                    if (off + 4 <= sections[text_idx].data.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sections[text_idx].data.data() + off, 4);
                        curr += static_cast<int32_t>(rel.addend);
                        std::memcpy(sections[text_idx].data.data() + off, &curr, 4);
                    }
                }
            }
        }
        if (rodata_idx > 0) {
            for (const auto& rel : rodata_relocs) {
                if (rel.addend != 0) {
                    size_t off = static_cast<size_t>(rel.offset);
                    if (off + 4 <= sections[rodata_idx].data.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sections[rodata_idx].data.data() + off, 4);
                        curr += static_cast<int32_t>(rel.addend);
                        std::memcpy(sections[rodata_idx].data.data() + off, &curr, 4);
                    }
                }
            }
        }
        if (data_idx > 0) {
            for (const auto& rel : data_relocs) {
                if (rel.addend != 0) {
                    size_t off = static_cast<size_t>(rel.offset);
                    if (off + 4 <= sections[data_idx].data.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sections[data_idx].data.data() + off, 4);
                        curr += static_cast<int32_t>(rel.addend);
                        std::memcpy(sections[data_idx].data.data() + off, &curr, 4);
                    }
                }
            }
        }

        uint32_t rel_text_idx = 0;
        if (!text_relocs.empty()) {
            ElfSecDesc32 s;
            s.name = ".rel.text";
            s.type = ELF_SHT_REL;
            s.flags = static_cast<uint32_t>(ELF_SHF_INFO_LINK);
            s.addralign = 4;
            s.entsize = sizeof(Elf32_Rel);
            s.info = text_idx;
            rel_text_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        uint32_t rel_rodata_idx = 0;
        if (!rodata_relocs.empty()) {
            ElfSecDesc32 s;
            s.name = ".rel.rodata";
            s.type = ELF_SHT_REL;
            s.flags = static_cast<uint32_t>(ELF_SHF_INFO_LINK);
            s.addralign = 4;
            s.entsize = sizeof(Elf32_Rel);
            s.info = rodata_idx;
            rel_rodata_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        uint32_t rel_data_idx = 0;
        if (!data_relocs.empty()) {
            ElfSecDesc32 s;
            s.name = ".rel.data";
            s.type = ELF_SHT_REL;
            s.flags = static_cast<uint32_t>(ELF_SHF_INFO_LINK);
            s.addralign = 4;
            s.entsize = sizeof(Elf32_Rel);
            s.info = data_idx;
            rel_data_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        ElfSecDesc32 symtab_sec;
        symtab_sec.name = ".symtab";
        symtab_sec.type = ELF_SHT_SYMTAB;
        symtab_sec.flags = 0;
        symtab_sec.addralign = 4;
        symtab_sec.entsize = sizeof(Elf32_Sym);
        uint32_t symtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(symtab_sec);

        ElfSecDesc32 strtab_sec;
        strtab_sec.name = ".strtab";
        strtab_sec.type = ELF_SHT_STRTAB;
        strtab_sec.flags = 0;
        strtab_sec.addralign = 1;
        uint32_t strtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(strtab_sec);

        ElfSecDesc32 shstrtab_sec;
        shstrtab_sec.name = ".shstrtab";
        shstrtab_sec.type = ELF_SHT_STRTAB;
        shstrtab_sec.flags = 0;
        shstrtab_sec.addralign = 1;
        uint32_t shstrtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(shstrtab_sec);

        sections[symtab_idx].link = strtab_idx;
        if (rel_text_idx > 0) sections[rel_text_idx].link = symtab_idx;
        if (rel_rodata_idx > 0) sections[rel_rodata_idx].link = symtab_idx;
        if (rel_data_idx > 0) sections[rel_data_idx].link = symtab_idx;

        StringTableBuilder strtab;
        std::vector<Elf32_Sym> local_symbols;
        std::vector<Elf32_Sym> global_symbols;
        std::unordered_map<std::string, uint32_t> sym_index_map;

        Elf32_Sym sym0{};
        local_symbols.push_back(sym0);

        Elf32_Sym sym_file{};
        sym_file.st_name = strtab.add_elf("grass_source");
        sym_file.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_FILE;
        sym_file.st_shndx = ELF_SHN_ABS;
        local_symbols.push_back(sym_file);

        if (text_idx > 0) {
            Elf32_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(text_idx);
            sym_index_map[".text"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (rodata_idx > 0) {
            Elf32_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(rodata_idx);
            sym_index_map[".rodata"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (data_idx > 0) {
            Elf32_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(data_idx);
            sym_index_map[".data"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (bss_idx > 0) {
            Elf32_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(bss_idx);
            sym_index_map[".bss"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }

        auto get_section_shndx = [&](SectionKind k) -> uint16_t {
            if (k == SectionKind::Text) return static_cast<uint16_t>(text_idx);
            if (k == SectionKind::RoData) return static_cast<uint16_t>(rodata_idx);
            if (k == SectionKind::Data) return static_cast<uint16_t>(data_idx);
            if (k == SectionKind::Bss) return static_cast<uint16_t>(bss_idx);
            return 0;
        };

        for (const auto& s : unit.symbols) {
            if (!s.is_global && !s.is_external) {
                Elf32_Sym sym{};
                sym.st_name = strtab.add_elf(s.name);
                sym.st_value = static_cast<uint32_t>(s.offset);
                sym.st_size = static_cast<uint32_t>(s.size);
                sym.st_info = (ELF_STB_LOCAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_OBJECT);
                sym.st_shndx = get_section_shndx(s.section);
                sym_index_map[s.name] = static_cast<uint32_t>(local_symbols.size());
                local_symbols.push_back(sym);
            }
        }

        uint32_t first_global_idx = static_cast<uint32_t>(local_symbols.size());
        sections[symtab_idx].info = first_global_idx;

        for (const auto& s : unit.symbols) {
            if (s.is_global && !s.is_external) {
                Elf32_Sym sym{};
                sym.st_name = strtab.add_elf(s.name);
                sym.st_value = static_cast<uint32_t>(s.offset);
                sym.st_size = static_cast<uint32_t>(s.size);
                sym.st_info = (ELF_STB_GLOBAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_OBJECT);
                sym.st_shndx = get_section_shndx(s.section);
                sym_index_map[s.name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                global_symbols.push_back(sym);
            }
        }

        for (const auto& s : unit.symbols) {
            if (s.is_external) {
                if (sym_index_map.find(s.name) == sym_index_map.end()) {
                    Elf32_Sym sym{};
                    sym.st_name = strtab.add_elf(s.name);
                    sym.st_value = 0;
                    sym.st_size = 0;
                    sym.st_info = (ELF_STB_GLOBAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_NOTYPE);
                    sym.st_shndx = 0;
                    sym_index_map[s.name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                    global_symbols.push_back(sym);
                }
            }
        }

        for (const auto& rel : unit.relocations) {
            if (sym_index_map.find(rel.symbol_name) == sym_index_map.end()) {
                Elf32_Sym sym{};
                sym.st_name = strtab.add_elf(rel.symbol_name);
                sym.st_value = 0;
                sym.st_size = 0;
                sym.st_info = (ELF_STB_GLOBAL << 4) | ((rel.type == RelocType::Rel32) ? ELF_STT_FUNC : ELF_STT_NOTYPE);
                sym.st_shndx = 0;
                sym_index_map[rel.symbol_name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                global_symbols.push_back(sym);
            }
        }

        std::vector<Elf32_Sym> all_symbols = local_symbols;
        all_symbols.insert(all_symbols.end(), global_symbols.begin(), global_symbols.end());

        BinaryBuffer symtab_buf;
        for (const auto& s : all_symbols) {
            symtab_buf.emit_bytes(&s, sizeof(s));
        }
        sections[symtab_idx].data = symtab_buf.get_bytes();
        sections[symtab_idx].size = static_cast<uint32_t>(symtab_buf.size());

        BinaryBuffer strtab_buf;
        strtab.build_elf(strtab_buf);
        sections[strtab_idx].data = strtab_buf.get_bytes();
        sections[strtab_idx].size = static_cast<uint32_t>(strtab_buf.size());

        if (rel_text_idx > 0) {
            BinaryBuffer rel_buf;
            for (const auto& rel : text_relocs) {
                Elf32_Rel r{};
                r.r_offset = static_cast<uint32_t>(rel.offset);
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_32, rel.type);
                r.r_info = (sym_id << 8) | (r_type & 0xFF);
                rel_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rel_text_idx].data = rel_buf.get_bytes();
            sections[rel_text_idx].size = static_cast<uint32_t>(rel_buf.size());
        }

        if (rel_rodata_idx > 0) {
            BinaryBuffer rel_buf;
            for (const auto& rel : rodata_relocs) {
                Elf32_Rel r{};
                r.r_offset = static_cast<uint32_t>(rel.offset);
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_32, rel.type);
                r.r_info = (sym_id << 8) | (r_type & 0xFF);
                rel_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rel_rodata_idx].data = rel_buf.get_bytes();
            sections[rel_rodata_idx].size = static_cast<uint32_t>(rel_buf.size());
        }

        if (rel_data_idx > 0) {
            BinaryBuffer rel_buf;
            for (const auto& rel : data_relocs) {
                Elf32_Rel r{};
                r.r_offset = static_cast<uint32_t>(rel.offset);
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_32, rel.type);
                r.r_info = (sym_id << 8) | (r_type & 0xFF);
                rel_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rel_data_idx].data = rel_buf.get_bytes();
            sections[rel_data_idx].size = static_cast<uint32_t>(rel_buf.size());
        }

        StringTableBuilder shstrtab;
        for (const auto& sec : sections) {
            shstrtab.add_elf(sec.name);
        }
        BinaryBuffer shstr_buf;
        shstrtab.build_elf(shstr_buf);
        sections[shstrtab_idx].data = shstr_buf.get_bytes();
        sections[shstrtab_idx].size = static_cast<uint32_t>(shstr_buf.size());

        out.clear();

        Elf32_Ehdr ehdr{};
        ehdr.e_ident[0] = 0x7F;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = ELF_CLASS_32;
        ehdr.e_ident[5] = ELF_DATA_2LSB;
        ehdr.e_ident[6] = ELF_EV_CURRENT;
        ehdr.e_ident[7] = ELF_OSABI_SYSV;
        ehdr.e_type = ELF_ET_REL;
        ehdr.e_machine = ELF_EM_386;
        ehdr.e_version = ELF_EV_CURRENT;
        ehdr.e_shoff = 0; // patched below
        ehdr.e_ehsize = sizeof(Elf32_Ehdr);
        ehdr.e_shentsize = sizeof(Elf32_Shdr);
        ehdr.e_shnum = static_cast<uint16_t>(sections.size());
        ehdr.e_shstrndx = static_cast<uint16_t>(shstrtab_idx);
        out.emit_bytes(&ehdr, sizeof(ehdr));

        for (size_t i = 1; i < sections.size(); ++i) {
            auto& sec = sections[i];
            if (sec.type == ELF_SHT_NOBITS) {
                sec.offset = static_cast<uint32_t>(out.size());
            } else if (sec.size > 0) {
                out.align_to(sec.addralign > 0 ? static_cast<size_t>(sec.addralign) : 1, 0);
                sec.offset = static_cast<uint32_t>(out.size());
                out.emit_bytes(sec.data.data(), sec.data.size());
            } else {
                sec.offset = static_cast<uint32_t>(out.size());
            }
        }

        out.align_to(4, 0);
        uint32_t shoff = static_cast<uint32_t>(out.size());
        out.patch_u32(32, shoff);

        for (const auto& sec : sections) {
            Elf32_Shdr shdr{};
            shdr.sh_name = shstrtab.get_offset(sec.name);
            shdr.sh_type = sec.type;
            shdr.sh_flags = sec.flags;
            shdr.sh_addr = sec.addr;
            shdr.sh_offset = sec.offset;
            shdr.sh_size = sec.size;
            shdr.sh_link = sec.link;
            shdr.sh_info = sec.info;
            shdr.sh_addralign = sec.addralign;
            shdr.sh_entsize = sec.entsize;
            out.emit_bytes(&shdr, sizeof(shdr));
        }

        return true;
    }


} // namespace grass::backend

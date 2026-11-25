#include "native/GrassElfWriter.h"

namespace grass::backend {

    std::vector<uint8_t> GrassElfWriter::generate_syscall_wrapper(LinuxSyscall sc) {
        std::vector<uint8_t> code;
        uint32_t num = static_cast<uint32_t>(sc);

        switch (sc) {
            case LinuxSyscall::SysWrite:
            case LinuxSyscall::SysOpen:
            case LinuxSyscall::SysClose:
            case LinuxSyscall::SysMunmap:
            case LinuxSyscall::SysExit: {
                // mov eax, num (B8 imm32)
                code.push_back(0xB8);
                code.push_back(static_cast<uint8_t>(num & 0xFF));
                code.push_back(static_cast<uint8_t>((num >> 8) & 0xFF));
                code.push_back(static_cast<uint8_t>((num >> 16) & 0xFF));
                code.push_back(static_cast<uint8_t>((num >> 24) & 0xFF));
                // syscall (0F 05)
                code.push_back(0x0F);
                code.push_back(0x05);
                // ret (C3)
                code.push_back(0xC3);
                break;
            }
            case LinuxSyscall::SysMmap: {
                // SysV 4th argument is in RCX, while Linux syscall expects R10
                // mov r10, rcx (49 89 CA)
                code.push_back(0x49);
                code.push_back(0x89);
                code.push_back(0xCA);
                // mov eax, 9 (B8 09 00 00 00)
                code.push_back(0xB8);
                code.push_back(static_cast<uint8_t>(num & 0xFF));
                code.push_back(static_cast<uint8_t>((num >> 8) & 0xFF));
                code.push_back(static_cast<uint8_t>((num >> 16) & 0xFF));
                code.push_back(static_cast<uint8_t>((num >> 24) & 0xFF));
                // syscall (0F 05)
                code.push_back(0x0F);
                code.push_back(0x05);
                // ret (C3)
                code.push_back(0xC3);
                break;
            }
            default:
                break;
        }
        return code;
    }

    bool GrassElfWriter::inject_syscall_wrapper(CompiledUnit& unit, LinuxSyscall sc, const std::string& name) {
        std::string sym_name = name;
        if (sym_name.empty()) {
            switch (sc) {
                case LinuxSyscall::SysWrite:  sym_name = "sys_write"; break;
                case LinuxSyscall::SysOpen:   sym_name = "sys_open"; break;
                case LinuxSyscall::SysClose:  sym_name = "sys_close"; break;
                case LinuxSyscall::SysMmap:   sym_name = "sys_mmap"; break;
                case LinuxSyscall::SysMunmap: sym_name = "sys_munmap"; break;
                case LinuxSyscall::SysExit:   sym_name = "sys_exit"; break;
            }
        }

        for (const auto& s : unit.symbols) {
            if (s.name == sym_name && s.section == SectionKind::Text) {
                return true;
            }
        }

        std::vector<uint8_t> wrapper = generate_syscall_wrapper(sc);
        if (wrapper.empty()) {
            return false;
        }

        // Align code to 16 bytes using NOP (0x90)
        while (unit.text.size() % 16 != 0) {
            unit.text.push_back(0x90);
        }

        uint64_t offset = unit.text.size();
        unit.text.insert(unit.text.end(), wrapper.begin(), wrapper.end());

        SymbolDef sym;
        sym.name = sym_name;
        sym.section = SectionKind::Text;
        sym.offset = offset;
        sym.size = wrapper.size();
        sym.is_global = true;
        sym.is_external = false;
        unit.symbols.push_back(sym);

        return true;
    }

    bool GrassElfWriter::inject_missing_syscalls(CompiledUnit& unit) {
        for (const auto& rel : unit.relocations) {
            bool defined = false;
            for (const auto& s : unit.symbols) {
                if (s.name == rel.symbol_name) {
                    defined = true;
                    break;
                }
            }
            if (!defined) {
                if (rel.symbol_name == "sys_write" || rel.symbol_name == "write") {
                    inject_syscall_wrapper(unit, LinuxSyscall::SysWrite, rel.symbol_name);
                } else if (rel.symbol_name == "sys_open" || rel.symbol_name == "open") {
                    inject_syscall_wrapper(unit, LinuxSyscall::SysOpen, rel.symbol_name);
                } else if (rel.symbol_name == "sys_close" || rel.symbol_name == "close") {
                    inject_syscall_wrapper(unit, LinuxSyscall::SysClose, rel.symbol_name);
                } else if (rel.symbol_name == "sys_mmap" || rel.symbol_name == "mmap") {
                    inject_syscall_wrapper(unit, LinuxSyscall::SysMmap, rel.symbol_name);
                } else if (rel.symbol_name == "sys_munmap" || rel.symbol_name == "munmap") {
                    inject_syscall_wrapper(unit, LinuxSyscall::SysMunmap, rel.symbol_name);
                } else if (rel.symbol_name == "sys_exit" || rel.symbol_name == "exit") {
                    inject_syscall_wrapper(unit, LinuxSyscall::SysExit, rel.symbol_name);
                }
            }
        }
        return true;
    }

    Elf64_Ehdr GrassElfWriter::create_elf64_ehdr(uint64_t entry_point,
                                                uint64_t phoff,
                                                uint64_t shoff,
                                                uint16_t phnum,
                                                uint16_t shnum,
                                                uint16_t shstrndx) {
        Elf64_Ehdr ehdr{};
        ehdr.e_ident[0] = 0x7F;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = ELF_CLASS_64;
        ehdr.e_ident[5] = ELF_DATA_2LSB;
        ehdr.e_ident[6] = ELF_EV_CURRENT;
        ehdr.e_ident[7] = ELF_OSABI_SYSV;
        ehdr.e_ident[8] = 0;

        ehdr.e_type = ELF_ET_EXEC;
        ehdr.e_machine = ELF_EM_X86_64;
        ehdr.e_version = ELF_EV_CURRENT;
        ehdr.e_entry = entry_point;
        ehdr.e_phoff = phoff;
        ehdr.e_shoff = shoff;
        ehdr.e_flags = 0;
        ehdr.e_ehsize = sizeof(Elf64_Ehdr);
        ehdr.e_phentsize = sizeof(Elf64_Phdr);
        ehdr.e_phnum = phnum;
        ehdr.e_shentsize = sizeof(Elf64_Shdr);
        ehdr.e_shnum = shnum;
        ehdr.e_shstrndx = shstrndx;
        return ehdr;
    }

    Elf64_Phdr GrassElfWriter::create_elf64_phdr(uint32_t type,
                                                uint32_t flags,
                                                uint64_t offset,
                                                uint64_t vaddr,
                                                uint64_t filesz,
                                                uint64_t memsz,
                                                uint64_t align) {
        Elf64_Phdr phdr{};
        phdr.p_type = type;
        phdr.p_flags = flags;
        phdr.p_offset = offset;
        phdr.p_vaddr = vaddr;
        phdr.p_paddr = vaddr;
        phdr.p_filesz = filesz;
        phdr.p_memsz = memsz;
        phdr.p_align = align;
        return phdr;
    }

    Elf64_Shdr GrassElfWriter::create_elf64_shdr(uint32_t name,
                                                uint32_t type,
                                                uint64_t flags,
                                                uint64_t addr,
                                                uint64_t offset,
                                                uint64_t size,
                                                uint32_t link,
                                                uint32_t info,
                                                uint64_t addralign,
                                                uint64_t entsize) {
        Elf64_Shdr shdr{};
        shdr.sh_name = name;
        shdr.sh_type = type;
        shdr.sh_flags = flags;
        shdr.sh_addr = addr;
        shdr.sh_offset = offset;
        shdr.sh_size = size;
        shdr.sh_link = link;
        shdr.sh_info = info;
        shdr.sh_addralign = addralign;
        shdr.sh_entsize = entsize;
        return shdr;
    }

    bool GrassElfWriter::compute_layout(const CompiledUnit& unit,
                                       const ElfWriterConfig& config,
                                       ElfLayoutInfo& out_layout,
                                       std::string* /*out_error*/) {
        out_layout.base_address = config.base_address;
        out_layout.page_size = config.page_size > 0 ? config.page_size : 0x1000;

        // Code segment starts at file offset 0 and virtual address base_address
        out_layout.code_seg_offset = 0;
        out_layout.code_seg_vaddr = out_layout.base_address;

        // .text section is placed at page_size in file and base_address + page_size in memory
        out_layout.text_offset = out_layout.page_size;
        out_layout.text_vaddr = out_layout.base_address + out_layout.text_offset;
        out_layout.text_size = unit.text.size();

        out_layout.code_seg_filesz = out_layout.text_offset + out_layout.text_size;
        out_layout.code_seg_memsz = out_layout.code_seg_filesz;

        // Data segment starts at the next page boundary
        uint64_t data_off = ((out_layout.code_seg_filesz + out_layout.page_size - 1) / out_layout.page_size) * out_layout.page_size;
        if (data_off < out_layout.text_offset + out_layout.page_size) {
            data_off = out_layout.text_offset + out_layout.page_size;
        }

        out_layout.data_seg_offset = data_off;
        out_layout.data_seg_vaddr = out_layout.base_address + data_off;

        // .rodata section
        out_layout.rodata_offset = data_off;
        out_layout.rodata_vaddr = out_layout.data_seg_vaddr;
        out_layout.rodata_size = unit.rodata.size();

        // .data section
        out_layout.data_offset = out_layout.rodata_offset + out_layout.rodata_size;
        out_layout.data_vaddr = out_layout.rodata_vaddr + out_layout.rodata_size;
        out_layout.data_size = unit.data.size();

        // .bss section
        out_layout.bss_vaddr = out_layout.data_vaddr + out_layout.data_size;
        out_layout.bss_size = unit.bss_size;

        out_layout.data_seg_filesz = out_layout.rodata_size + out_layout.data_size;
        out_layout.data_seg_memsz = out_layout.rodata_size + out_layout.data_size + out_layout.bss_size;

        // .shstrtab section offset
        out_layout.shstrtab_offset = out_layout.data_offset + out_layout.data_size;

        // Map symbols to virtual addresses
        out_layout.symbol_addresses.clear();
        for (const auto& sym : unit.symbols) {
            uint64_t addr = 0;
            switch (sym.section) {
                case SectionKind::Text:
                    addr = out_layout.text_vaddr + sym.offset;
                    break;
                case SectionKind::RoData:
                    addr = out_layout.rodata_vaddr + sym.offset;
                    break;
                case SectionKind::Data:
                    addr = out_layout.data_vaddr + sym.offset;
                    break;
                case SectionKind::Bss:
                    addr = out_layout.bss_vaddr + sym.offset;
                    break;
            }
            out_layout.symbol_addresses[sym.name] = addr;
        }

        // Determine entry point
        if (out_layout.symbol_addresses.find(config.entry_symbol) != out_layout.symbol_addresses.end()) {
            out_layout.entry_point = out_layout.symbol_addresses[config.entry_symbol];
        } else if (out_layout.symbol_addresses.find("_start") != out_layout.symbol_addresses.end()) {
            out_layout.entry_point = out_layout.symbol_addresses["_start"];
        } else if (out_layout.symbol_addresses.find("main") != out_layout.symbol_addresses.end()) {
            out_layout.entry_point = out_layout.symbol_addresses["main"];
        } else {
            out_layout.entry_point = out_layout.text_vaddr;
        }

        return true;
    }

    bool GrassElfWriter::resolve_relocations(CompiledUnit& mutable_unit,
                                            const ElfLayoutInfo& layout,
                                            std::string* out_error) {
        for (const auto& reloc : mutable_unit.relocations) {
            std::vector<uint8_t>* target_buffer = nullptr;
            uint64_t section_vaddr = 0;

            switch (reloc.section) {
                case SectionKind::Text:
                    target_buffer = &mutable_unit.text;
                    section_vaddr = layout.text_vaddr;
                    break;
                case SectionKind::Data:
                    target_buffer = &mutable_unit.data;
                    section_vaddr = layout.data_vaddr;
                    break;
                case SectionKind::RoData:
                    target_buffer = &mutable_unit.rodata;
                    section_vaddr = layout.rodata_vaddr;
                    break;
                default:
                    if (out_error) {
                        *out_error = "Unsupported relocation section kind";
                    }
                    return false;
            }

            auto sym_it = layout.symbol_addresses.find(reloc.symbol_name);
            if (sym_it == layout.symbol_addresses.end()) {
                if (out_error) {
                    *out_error = "Unresolved symbol during relocation: " + reloc.symbol_name;
                }
                return false;
            }

            uint64_t sym_vaddr = sym_it->second;
            uint64_t site_vaddr = section_vaddr + reloc.offset;

            bool is_rel32 = (reloc.type == RelocType::Rel32 ||
                             static_cast<uint8_t>(reloc.type) == ELF_R_X86_64_PLT32);
            bool is_abs64 = (reloc.type == RelocType::Abs64);
            bool is_abs32 = (reloc.type == RelocType::Abs32 ||
                             static_cast<uint8_t>(reloc.type) == ELF_R_X86_64_32 ||
                             static_cast<uint8_t>(reloc.type) == 11);

            if (is_rel32) {
                if (reloc.offset + 4 > target_buffer->size()) {
                    if (out_error) {
                        *out_error = "Relocation offset out of bounds for " + reloc.symbol_name;
                    }
                    return false;
                }
                int32_t in_place = 0;
                std::memcpy(&in_place, target_buffer->data() + reloc.offset, 4);
                int64_t full_addend = reloc.addend + in_place;
                int64_t diff = static_cast<int64_t>(sym_vaddr) + full_addend - static_cast<int64_t>(site_vaddr);

                if (diff < INT32_MIN || diff > INT32_MAX) {
                    if (out_error) {
                        *out_error = "Relocation overflow (PC32) for " + reloc.symbol_name;
                    }
                    return false;
                }

                int32_t patched = static_cast<int32_t>(diff);
                std::memcpy(target_buffer->data() + reloc.offset, &patched, 4);
            } else if (is_abs64) {
                if (reloc.offset + 8 > target_buffer->size()) {
                    if (out_error) {
                        *out_error = "Relocation offset out of bounds for " + reloc.symbol_name;
                    }
                    return false;
                }
                int64_t in_place = 0;
                std::memcpy(&in_place, target_buffer->data() + reloc.offset, 8);
                uint64_t patched = sym_vaddr + reloc.addend + in_place;
                std::memcpy(target_buffer->data() + reloc.offset, &patched, 8);
            } else if (is_abs32) {
                if (reloc.offset + 4 > target_buffer->size()) {
                    if (out_error) {
                        *out_error = "Relocation offset out of bounds for " + reloc.symbol_name;
                    }
                    return false;
                }
                int32_t in_place = 0;
                std::memcpy(&in_place, target_buffer->data() + reloc.offset, 4);
                uint64_t val = sym_vaddr + reloc.addend + in_place;
                uint32_t patched = static_cast<uint32_t>(val);
                std::memcpy(target_buffer->data() + reloc.offset, &patched, 4);
            } else {
                if (out_error) {
                    *out_error = "Unknown relocation type for " + reloc.symbol_name;
                }
                return false;
            }
        }
        return true;
    }

    bool GrassElfWriter::write_elf_exe(const CompiledUnit& unit,
                                      std::vector<uint8_t>& out_bytes,
                                      const ElfWriterConfig& config,
                                      ElfLayoutInfo* out_layout,
                                      std::string* out_error) {
        CompiledUnit local_unit = unit;

        if (config.auto_inject_syscalls) {
            inject_missing_syscalls(local_unit);
        }

        ElfLayoutInfo layout;
        if (!compute_layout(local_unit, config, layout, out_error)) {
            return false;
        }

        if (!resolve_relocations(local_unit, layout, out_error)) {
            return false;
        }

        // Build .shstrtab section content
        std::vector<uint8_t> shstrtab;
        shstrtab.push_back(0); // Offset 0 is empty string

        auto add_str = [&](const std::string& str) -> uint32_t {
            if (str.empty()) return 0;
            uint32_t off = static_cast<uint32_t>(shstrtab.size());
            shstrtab.insert(shstrtab.end(), str.begin(), str.end());
            shstrtab.push_back(0);
            return off;
        };

        uint32_t name_text = add_str(".text");
        uint32_t name_rodata = add_str(".rodata");
        uint32_t name_data = add_str(".data");
        uint32_t name_bss = add_str(".bss");
        uint32_t name_shstrtab = add_str(".shstrtab");

        layout.shstrtab_size = shstrtab.size();

        // Align Section Header Table to 8 bytes
        layout.shoff = ((layout.shstrtab_offset + layout.shstrtab_size + 7) / 8) * 8;
        layout.shnum = 6;
        layout.shstrndx = 5;

        uint64_t total_file_size = layout.shoff + layout.shnum * sizeof(Elf64_Shdr);
        out_bytes.assign(total_file_size, 0);

        // 1. ELF Header
        Elf64_Ehdr ehdr = create_elf64_ehdr(
            layout.entry_point,
            sizeof(Elf64_Ehdr),
            layout.shoff,
            2,
            static_cast<uint16_t>(layout.shnum),
            static_cast<uint16_t>(layout.shstrndx)
        );
        std::memcpy(out_bytes.data(), &ehdr, sizeof(Elf64_Ehdr));

        // 2. Program Headers (PT_LOAD)
        Elf64_Phdr phdr_code = create_elf64_phdr(
            ELF_PT_LOAD,
            ELF_PF_R | ELF_PF_X,
            layout.code_seg_offset,
            layout.code_seg_vaddr,
            layout.code_seg_filesz,
            layout.code_seg_memsz,
            layout.page_size
        );
        std::memcpy(out_bytes.data() + sizeof(Elf64_Ehdr), &phdr_code, sizeof(Elf64_Phdr));

        Elf64_Phdr phdr_data = create_elf64_phdr(
            ELF_PT_LOAD,
            ELF_PF_R | ELF_PF_W,
            layout.data_seg_offset,
            layout.data_seg_vaddr,
            layout.data_seg_filesz,
            layout.data_seg_memsz,
            layout.page_size
        );
        std::memcpy(out_bytes.data() + sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr), &phdr_data, sizeof(Elf64_Phdr));

        // 3. Section Data
        if (!local_unit.text.empty()) {
            std::memcpy(out_bytes.data() + layout.text_offset, local_unit.text.data(), local_unit.text.size());
        }

        if (!local_unit.rodata.empty()) {
            std::memcpy(out_bytes.data() + layout.rodata_offset, local_unit.rodata.data(), local_unit.rodata.size());
        }

        if (!local_unit.data.empty()) {
            std::memcpy(out_bytes.data() + layout.data_offset, local_unit.data.data(), local_unit.data.size());
        }

        std::memcpy(out_bytes.data() + layout.shstrtab_offset, shstrtab.data(), shstrtab.size());

        // 4. Section Headers
        std::vector<Elf64_Shdr> shdrs(layout.shnum);

        // Section 0: NULL
        shdrs[0] = create_elf64_shdr(0, ELF_SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0);

        // Section 1: .text
        shdrs[1] = create_elf64_shdr(
            name_text,
            ELF_SHT_PROGBITS,
            ELF_SHF_ALLOC | ELF_SHF_EXECINSTR,
            layout.text_vaddr,
            layout.text_offset,
            local_unit.text.size(),
            0,
            0,
            16,
            0
        );

        // Section 2: .rodata
        shdrs[2] = create_elf64_shdr(
            name_rodata,
            ELF_SHT_PROGBITS,
            ELF_SHF_ALLOC,
            layout.rodata_vaddr,
            layout.rodata_offset,
            local_unit.rodata.size(),
            0,
            0,
            8,
            0
        );

        // Section 3: .data
        shdrs[3] = create_elf64_shdr(
            name_data,
            ELF_SHT_PROGBITS,
            ELF_SHF_ALLOC | ELF_SHF_WRITE,
            layout.data_vaddr,
            layout.data_offset,
            local_unit.data.size(),
            0,
            0,
            8,
            0
        );

        // Section 4: .bss
        shdrs[4] = create_elf64_shdr(
            name_bss,
            ELF_SHT_NOBITS,
            ELF_SHF_ALLOC | ELF_SHF_WRITE,
            layout.bss_vaddr,
            layout.data_offset + local_unit.data.size(),
            local_unit.bss_size,
            0,
            0,
            8,
            0
        );

        // Section 5: .shstrtab
        shdrs[5] = create_elf64_shdr(
            name_shstrtab,
            ELF_SHT_STRTAB,
            0,
            0,
            layout.shstrtab_offset,
            layout.shstrtab_size,
            0,
            0,
            1,
            0
        );

        std::memcpy(out_bytes.data() + layout.shoff, shdrs.data(), layout.shnum * sizeof(Elf64_Shdr));

        if (out_layout != nullptr) {
            *out_layout = layout;
        }

        return true;
    }

    bool GrassElfWriter::write_elf_exe_file(const CompiledUnit& unit,
                                          const std::string& filename,
                                          const ElfWriterConfig& config,
                                          ElfLayoutInfo* out_layout,
                                          std::string* out_error) {
        std::vector<uint8_t> bytes;
        if (!write_elf_exe(unit, bytes, config, out_layout, out_error)) {
            return false;
        }

        std::ofstream out(filename, std::ios::binary);
        if (!out) {
            if (out_error) {
                *out_error = "Failed to open file for writing: " + filename;
            }
            return false;
        }

        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out.good()) {
            if (out_error) {
                *out_error = "Failed to write complete binary to: " + filename;
            }
            return false;
        }

        return true;
    }

} // namespace grass::backend

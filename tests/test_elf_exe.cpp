#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <fstream>
#include <iomanip>

#include "GrassElfWriter.h"

using namespace grass::backend;

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        g_tests_run++; \
        if (cond) { \
            g_tests_passed++; \
            std::cout << "[PASS] " << msg << std::endl; \
        } else { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while(0)

bool test_generate_hello_linux_elf() {
    std::cout << "--- Running test_generate_hello_linux_elf ---" << std::endl;

    CompiledUnit unit;
    unit.arch = TargetArch::X86_64;
    unit.abi = TargetABI::SysV64;

    // 1. .rodata: Hello string
    const char msg_str[] = "Hello from Grass Linux ELF64!\n";
    const uint32_t msg_len = static_cast<uint32_t>(sizeof(msg_str) - 1); // 30 bytes
    unit.rodata.assign(reinterpret_cast<const uint8_t*>(msg_str),
                       reinterpret_cast<const uint8_t*>(msg_str) + msg_len);

    SymbolDef msg_sym;
    msg_sym.name = "msg";
    msg_sym.section = SectionKind::RoData;
    msg_sym.offset = 0;
    msg_sym.size = msg_len;
    msg_sym.is_global = true;
    unit.symbols.push_back(msg_sym);

    // 2. .text: _start entry point
    // Instructions:
    //   mov edi, 1            -> BF 01 00 00 00 (5 bytes)
    //   lea rsi, [rip + msg]  -> 48 8D 35 00 00 00 00 (7 bytes, disp at offset +3)
    //   mov edx, 30           -> BA 1E 00 00 00 (5 bytes)
    //   call sys_write        -> E8 00 00 00 00 (5 bytes, disp at offset +1)
    //   xor edi, edi          -> 31 FF (2 bytes)
    //   call sys_exit         -> E8 00 00 00 00 (5 bytes, disp at offset +1)
    std::vector<uint8_t> code = {
        0xBF, 0x01, 0x00, 0x00, 0x00,             // 0x00: mov edi, 1 (stdout)
        0x48, 0x8D, 0x35, 0x00, 0x00, 0x00, 0x00, // 0x05: lea rsi, [rip + msg] (reloc at 0x08)
        0xBA, static_cast<uint8_t>(msg_len), 0x00, 0x00, 0x00, // 0x0C: mov edx, 30 (count)
        0xE8, 0x00, 0x00, 0x00, 0x00,             // 0x11: call sys_write (reloc at 0x12)
        0x31, 0xFF,                               // 0x16: xor edi, edi (status 0)
        0xE8, 0x00, 0x00, 0x00, 0x00              // 0x18: call sys_exit (reloc at 0x19)
    };
    unit.text = code;

    SymbolDef start_sym;
    start_sym.name = "_start";
    start_sym.section = SectionKind::Text;
    start_sym.offset = 0;
    start_sym.size = code.size();
    start_sym.is_global = true;
    unit.symbols.push_back(start_sym);

    // Relocations (addend = -4 for RIP-relative addressing where RIP is after disp32)
    RelocEntry r_msg;
    r_msg.section = SectionKind::Text;
    r_msg.offset = 0x08;
    r_msg.symbol_name = "msg";
    r_msg.type = RelocType::Rel32;
    r_msg.addend = -4;
    unit.relocations.push_back(r_msg);

    RelocEntry r_write;
    r_write.section = SectionKind::Text;
    r_write.offset = 0x12;
    r_write.symbol_name = "sys_write";
    r_write.type = RelocType::Rel32;
    r_write.addend = -4;
    unit.relocations.push_back(r_write);

    RelocEntry r_exit;
    r_exit.section = SectionKind::Text;
    r_exit.offset = 0x19;
    r_exit.symbol_name = "sys_exit";
    r_exit.type = RelocType::Rel32;
    r_exit.addend = -4;
    unit.relocations.push_back(r_exit);

    // 3. Generate ELF Executable file
    ElfWriterConfig config;
    config.base_address = 0x400000;
    config.page_size = 0x1000;
    config.entry_symbol = "_start";
    config.auto_inject_syscalls = true;

    ElfLayoutInfo layout;
    std::string err;
    const std::string elf_path = "tests/hello_linux.elf";

    bool ok = GrassElfWriter::write_elf_exe_file(unit, elf_path, config, &layout, &err);
    TEST_ASSERT(ok, "write_elf_exe_file returned true: " + err);

    // 4. Read back the generated ELF binary and verify byte-by-byte
    std::ifstream file(elf_path, std::ios::binary | std::ios::ate);
    TEST_ASSERT(file.is_open(), "File tests/hello_linux.elf opened successfully");

    std::streamsize file_size = file.tellg();
    TEST_ASSERT(file_size >= static_cast<std::streamsize>(layout.shoff + layout.shnum * sizeof(Elf64_Shdr)),
                "Binary file size matches or exceeds section header table");

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);
    TEST_ASSERT(file.good(), "Read complete binary bytes successfully");

    // 5. Verify Elf64_Ehdr
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, bytes.data(), sizeof(Elf64_Ehdr));

    TEST_ASSERT(ehdr.e_ident[0] == 0x7F &&
                ehdr.e_ident[1] == 'E' &&
                ehdr.e_ident[2] == 'L' &&
                ehdr.e_ident[3] == 'F',
                "Elf64_Ehdr magic bytes match 0x7F 'ELF'");
    TEST_ASSERT(ehdr.e_ident[4] == ELF_CLASS_64, "Elf64_Ehdr class is ELFCLASS64 (2)");
    TEST_ASSERT(ehdr.e_ident[5] == ELF_DATA_2LSB, "Elf64_Ehdr data is ELFDATA2LSB (1)");
    TEST_ASSERT(ehdr.e_ident[6] == ELF_EV_CURRENT, "Elf64_Ehdr version is EV_CURRENT (1)");
    TEST_ASSERT(ehdr.e_ident[7] == ELF_OSABI_SYSV, "Elf64_Ehdr OSABI is ELFOSABI_SYSV (0)");
    TEST_ASSERT(ehdr.e_type == ELF_ET_EXEC, "Elf64_Ehdr type is ET_EXEC (2)");
    TEST_ASSERT(ehdr.e_machine == ELF_EM_X86_64, "Elf64_Ehdr machine is EM_X86_64 (62 / 0x3E)");
    TEST_ASSERT(ehdr.e_version == ELF_EV_CURRENT, "Elf64_Ehdr e_version is 1");
    TEST_ASSERT(ehdr.e_entry == 0x401000, "Elf64_Ehdr entry point is 0x401000");
    TEST_ASSERT(ehdr.e_phoff == 0x40, "Elf64_Ehdr e_phoff is 64 bytes (0x40)");
    TEST_ASSERT(ehdr.e_shoff == layout.shoff, "Elf64_Ehdr e_shoff matches layout.shoff");
    TEST_ASSERT(ehdr.e_ehsize == 64, "Elf64_Ehdr e_ehsize is 64 bytes");
    TEST_ASSERT(ehdr.e_phentsize == 56, "Elf64_Ehdr e_phentsize is 56 bytes");
    TEST_ASSERT(ehdr.e_phnum == 2, "Elf64_Ehdr e_phnum is 2 (Code and Data)");
    TEST_ASSERT(ehdr.e_shentsize == 64, "Elf64_Ehdr e_shentsize is 64 bytes");
    TEST_ASSERT(ehdr.e_shnum == 6, "Elf64_Ehdr e_shnum is 6 (NULL, .text, .rodata, .data, .bss, .shstrtab)");
    TEST_ASSERT(ehdr.e_shstrndx == 5, "Elf64_Ehdr e_shstrndx is 5 (.shstrtab)");

    // 6. Verify Elf64_Phdr (Program Headers)
    Elf64_Phdr phdr_code;
    std::memcpy(&phdr_code, bytes.data() + 64, sizeof(Elf64_Phdr));
    TEST_ASSERT(phdr_code.p_type == ELF_PT_LOAD, "Program Header 0 is PT_LOAD (1)");
    TEST_ASSERT(phdr_code.p_flags == (ELF_PF_R | ELF_PF_X), "Program Header 0 flags are PF_R | PF_X (5)");
    TEST_ASSERT(phdr_code.p_offset == 0, "Program Header 0 p_offset is 0");
    TEST_ASSERT(phdr_code.p_vaddr == 0x400000, "Program Header 0 p_vaddr is 0x400000");
    TEST_ASSERT(phdr_code.p_paddr == 0x400000, "Program Header 0 p_paddr is 0x400000");
    TEST_ASSERT(phdr_code.p_align == 0x1000, "Program Header 0 p_align is 0x1000 (4KB)");
    TEST_ASSERT(phdr_code.p_filesz >= 0x1000 + code.size(), "Program Header 0 p_filesz covers headers and text");
    TEST_ASSERT(phdr_code.p_memsz == phdr_code.p_filesz, "Program Header 0 p_memsz equals p_filesz");

    Elf64_Phdr phdr_data;
    std::memcpy(&phdr_data, bytes.data() + 64 + sizeof(Elf64_Phdr), sizeof(Elf64_Phdr));
    TEST_ASSERT(phdr_data.p_type == ELF_PT_LOAD, "Program Header 1 is PT_LOAD (1)");
    TEST_ASSERT(phdr_data.p_flags == (ELF_PF_R | ELF_PF_W), "Program Header 1 flags are PF_R | PF_W (6)");
    TEST_ASSERT(phdr_data.p_offset == 0x2000, "Program Header 1 p_offset is 0x2000");
    TEST_ASSERT(phdr_data.p_vaddr == 0x402000, "Program Header 1 p_vaddr is 0x402000");
    TEST_ASSERT(phdr_data.p_paddr == 0x402000, "Program Header 1 p_paddr is 0x402000");
    TEST_ASSERT(phdr_data.p_align == 0x1000, "Program Header 1 p_align is 0x1000 (4KB)");
    TEST_ASSERT(phdr_data.p_filesz == msg_len, "Program Header 1 p_filesz is 30 bytes");
    TEST_ASSERT(phdr_data.p_memsz == msg_len, "Program Header 1 p_memsz is 30 bytes");

    // 7. Verify Section Headers
    const Elf64_Shdr* shdrs = reinterpret_cast<const Elf64_Shdr*>(bytes.data() + layout.shoff);

    // Section 0: NULL
    TEST_ASSERT(shdrs[0].sh_type == ELF_SHT_NULL, "Section 0 is SHT_NULL");

    // Section 1: .text
    TEST_ASSERT(shdrs[1].sh_type == ELF_SHT_PROGBITS, "Section 1 (.text) is SHT_PROGBITS");
    TEST_ASSERT(shdrs[1].sh_flags == (ELF_SHF_ALLOC | ELF_SHF_EXECINSTR), "Section 1 flags are ALLOC | EXECINSTR (6)");
    TEST_ASSERT(shdrs[1].sh_addr == 0x401000, "Section 1 (.text) sh_addr is 0x401000");
    TEST_ASSERT(shdrs[1].sh_offset == 0x1000, "Section 1 (.text) sh_offset is 0x1000");

    // Section 2: .rodata
    TEST_ASSERT(shdrs[2].sh_type == ELF_SHT_PROGBITS, "Section 2 (.rodata) is SHT_PROGBITS");
    TEST_ASSERT(shdrs[2].sh_flags == ELF_SHF_ALLOC, "Section 2 flags are ALLOC (2)");
    TEST_ASSERT(shdrs[2].sh_addr == 0x402000, "Section 2 (.rodata) sh_addr is 0x402000");
    TEST_ASSERT(shdrs[2].sh_offset == 0x2000, "Section 2 (.rodata) sh_offset is 0x2000");
    TEST_ASSERT(shdrs[2].sh_size == msg_len, "Section 2 (.rodata) sh_size is 30 bytes");

    // Section 3: .data
    TEST_ASSERT(shdrs[3].sh_type == ELF_SHT_PROGBITS, "Section 3 (.data) is SHT_PROGBITS");
    TEST_ASSERT(shdrs[3].sh_flags == (ELF_SHF_ALLOC | ELF_SHF_WRITE), "Section 3 flags are ALLOC | WRITE (3)");

    // Section 4: .bss
    TEST_ASSERT(shdrs[4].sh_type == ELF_SHT_NOBITS, "Section 4 (.bss) is SHT_NOBITS");
    TEST_ASSERT(shdrs[4].sh_flags == (ELF_SHF_ALLOC | ELF_SHF_WRITE), "Section 4 flags are ALLOC | WRITE (3)");

    // Section 5: .shstrtab
    TEST_ASSERT(shdrs[5].sh_type == ELF_SHT_STRTAB, "Section 5 (.shstrtab) is SHT_STRTAB");
    TEST_ASSERT(shdrs[5].sh_offset == layout.shstrtab_offset, "Section 5 offset matches layout");

    // 8. Verify .rodata content
    TEST_ASSERT(std::memcmp(bytes.data() + 0x2000, msg_str, msg_len) == 0,
                ".rodata content in file matches original string bytes");

    // 9. Verify Relocations Byte-by-Byte in .text
    // Relocation 1 at 0x1000 + 0x08 (lea rsi, [rip + msg])
    // Target "msg" is at 0x402000.
    // Relocation site is at 0x401008.
    // Next RIP is 0x40100C.
    // Expected displacement = 0x402000 - 0x40100C = 0x0FF4.
    int32_t patched_disp_msg = 0;
    std::memcpy(&patched_disp_msg, bytes.data() + 0x1000 + 0x08, 4);
    TEST_ASSERT(patched_disp_msg == 0x0FF4, "Patched displacement for 'msg' is 0x0FF4 (+4084 bytes)");

    // Relocation 2 at 0x1000 + 0x12 (call sys_write)
    uint64_t sys_write_vaddr = layout.symbol_addresses["sys_write"];
    int64_t expected_write_disp = static_cast<int64_t>(sys_write_vaddr) - 4 - (0x401000 + 0x12);
    int32_t patched_disp_write = 0;
    std::memcpy(&patched_disp_write, bytes.data() + 0x1000 + 0x12, 4);
    TEST_ASSERT(patched_disp_write == static_cast<int32_t>(expected_write_disp),
                "Patched displacement for 'sys_write' call matches expected offset");

    // Relocation 3 at 0x1000 + 0x19 (call sys_exit)
    uint64_t sys_exit_vaddr = layout.symbol_addresses["sys_exit"];
    int64_t expected_exit_disp = static_cast<int64_t>(sys_exit_vaddr) - 4 - (0x401000 + 0x19);
    int32_t patched_disp_exit = 0;
    std::memcpy(&patched_disp_exit, bytes.data() + 0x1000 + 0x19, 4);
    TEST_ASSERT(patched_disp_exit == static_cast<int32_t>(expected_exit_disp),
                "Patched displacement for 'sys_exit' call matches expected offset");

    // 10. Verify auto-injected Syscall Wrappers machine code
    uint64_t sys_write_file_off = 0x1000 + (sys_write_vaddr - 0x401000);
    const uint8_t expected_write_code[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
    TEST_ASSERT(std::memcmp(bytes.data() + sys_write_file_off, expected_write_code, 8) == 0,
                "Injected sys_write code matches 'mov eax, 1; syscall; ret'");

    uint64_t sys_exit_file_off = 0x1000 + (sys_exit_vaddr - 0x401000);
    const uint8_t expected_exit_code[] = { 0xB8, 0x3C, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
    TEST_ASSERT(std::memcmp(bytes.data() + sys_exit_file_off, expected_exit_code, 8) == 0,
                "Injected sys_exit code matches 'mov eax, 60; syscall; ret'");

    return true;
}

bool test_all_syscall_wrappers() {
    std::cout << "--- Running test_all_syscall_wrappers ---" << std::endl;

    // 1. sys_write (1)
    auto w = GrassElfWriter::generate_syscall_wrapper(LinuxSyscall::SysWrite);
    const uint8_t exp_w[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
    TEST_ASSERT(w.size() == 8 && std::memcmp(w.data(), exp_w, 8) == 0, "sys_write wrapper is valid");

    // 2. sys_open (2)
    auto op = GrassElfWriter::generate_syscall_wrapper(LinuxSyscall::SysOpen);
    const uint8_t exp_op[] = { 0xB8, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
    TEST_ASSERT(op.size() == 8 && std::memcmp(op.data(), exp_op, 8) == 0, "sys_open wrapper is valid");

    // 3. sys_close (3)
    auto cl = GrassElfWriter::generate_syscall_wrapper(LinuxSyscall::SysClose);
    const uint8_t exp_cl[] = { 0xB8, 0x03, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
    TEST_ASSERT(cl.size() == 8 && std::memcmp(cl.data(), exp_cl, 8) == 0, "sys_close wrapper is valid");

    // 4. sys_mmap (9) -> mov r10, rcx; mov eax, 9; syscall; ret
    auto mm = GrassElfWriter::generate_syscall_wrapper(LinuxSyscall::SysMmap);
    const uint8_t exp_mm[] = { 0x49, 0x89, 0xCA, 0xB8, 0x09, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
    TEST_ASSERT(mm.size() == 11 && std::memcmp(mm.data(), exp_mm, 11) == 0, "sys_mmap wrapper is valid");

    // 5. sys_munmap (11)
    auto um = GrassElfWriter::generate_syscall_wrapper(LinuxSyscall::SysMunmap);
    const uint8_t exp_um[] = { 0xB8, 0x0B, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
    TEST_ASSERT(um.size() == 8 && std::memcmp(um.data(), exp_um, 8) == 0, "sys_munmap wrapper is valid");

    // 6. sys_exit (60)
    auto ex = GrassElfWriter::generate_syscall_wrapper(LinuxSyscall::SysExit);
    const uint8_t exp_ex[] = { 0xB8, 0x3C, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };
    TEST_ASSERT(ex.size() == 8 && std::memcmp(ex.data(), exp_ex, 8) == 0, "sys_exit wrapper is valid");

    return true;
}

bool test_abs64_and_abs32_relocations() {
    std::cout << "--- Running test_abs64_and_abs32_relocations ---" << std::endl;

    CompiledUnit unit;
    unit.arch = TargetArch::X86_64;
    unit.abi = TargetABI::SysV64;

    // .data: 64-bit pointer and 32-bit pointer
    unit.data.resize(16, 0);

    // .text: empty dummy function
    unit.text = { 0x90, 0xC3 }; // nop; ret

    SymbolDef text_sym;
    text_sym.name = "_start";
    text_sym.section = SectionKind::Text;
    text_sym.offset = 0;
    text_sym.size = 2;
    text_sym.is_global = true;
    unit.symbols.push_back(text_sym);

    // Relocation Abs64 at data offset 0 pointing to _start
    RelocEntry r_abs64;
    r_abs64.section = SectionKind::Data;
    r_abs64.offset = 0;
    r_abs64.symbol_name = "_start";
    r_abs64.type = RelocType::Abs64;
    r_abs64.addend = 0x10;
    unit.relocations.push_back(r_abs64);

    // Relocation Abs32 at data offset 8 pointing to _start
    RelocEntry r_abs32;
    r_abs32.section = SectionKind::Data;
    r_abs32.offset = 8;
    r_abs32.symbol_name = "_start";
    r_abs32.type = RelocType::Abs32;
    r_abs32.addend = 0x20;
    unit.relocations.push_back(r_abs32);

    ElfWriterConfig config;
    config.base_address = 0x400000;
    config.page_size = 0x1000;

    std::vector<uint8_t> out_bytes;
    ElfLayoutInfo layout;
    std::string err;
    bool ok = GrassElfWriter::write_elf_exe(unit, out_bytes, config, &layout, &err);
    TEST_ASSERT(ok, "write_elf_exe with Abs64/Abs32 returned true");

    // Check patched Abs64 value in data section: 0x401000 + 0x10 = 0x401010
    uint64_t patched64 = 0;
    std::memcpy(&patched64, out_bytes.data() + layout.data_offset, 8);
    TEST_ASSERT(patched64 == 0x401010, "Abs64 relocation resolved correctly to 0x401010");

    // Check patched Abs32 value in data section: 0x401000 + 0x20 = 0x401020
    uint32_t patched32 = 0;
    std::memcpy(&patched32, out_bytes.data() + layout.data_offset + 8, 4);
    TEST_ASSERT(patched32 == 0x401020, "Abs32 relocation resolved correctly to 0x401020");

    return true;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " GrassElfWriter Autonomous Test Suite   " << std::endl;
    std::cout << "========================================" << std::endl;

    if (!test_generate_hello_linux_elf()) return 1;
    if (!test_all_syscall_wrappers()) return 1;
    if (!test_abs64_and_abs32_relocations()) return 1;

    std::cout << "========================================" << std::endl;
    std::cout << " ALL TESTS PASSED: " << g_tests_passed << " / " << g_tests_run << " assertions" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}

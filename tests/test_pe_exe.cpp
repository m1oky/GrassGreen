#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sstream>

#include "../src/GrassObjWriter.h"

using namespace grass::backend;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED]: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

static int g_tests_passed = 0;
static int g_tests_total = 0;

static void run_test(const char* name, void (*func)()) {
    ++g_tests_total;
    std::cout << "[RUNNING] " << name << " ... ";
    func();
    std::cout << "PASSED\n";
    ++g_tests_passed;
}

// =============================================================================
// Test 1: PE32+ Header Structures and Offsets
// =============================================================================
static void test_pe_header_structures() {
    TEST_ASSERT(sizeof(PE_DosHeader) == 64, "PE_DosHeader must be 64 bytes");
    TEST_ASSERT(sizeof(COFF_FileHeader) == 20, "COFF_FileHeader must be 20 bytes");
    TEST_ASSERT(sizeof(PE32Plus_OptionalHeader) == 240, "PE32Plus_OptionalHeader must be 240 bytes");
    TEST_ASSERT(sizeof(PE_DataDirectory) == 8, "PE_DataDirectory must be 8 bytes");
    TEST_ASSERT(sizeof(COFF_SectionHeader) == 40, "COFF_SectionHeader must be 40 bytes");
    TEST_ASSERT(sizeof(PE_ImportDirectoryEntry) == 20, "PE_ImportDirectoryEntry must be 20 bytes");

    // Total headers size calculation:
    // DOS header (64) + DOS stub (64) + PE sig (4) + COFF file header (20)
    // + PE32+ optional header (240) + 3 section headers (120) = 512 (0x200)
    size_t total_headers = 64 + 64 + 4 + 20 + 240 + 3 * 40;
    TEST_ASSERT(total_headers == 0x200, "PE headers must fit exactly in 512 bytes (one file alignment sector)");
}

// =============================================================================
// Test 2: Import Table Generation for Default Windows Imports
// =============================================================================
static void test_import_table_generation() {
    auto default_imports = GrassObjWriter::get_default_windows_imports();
    TEST_ASSERT(default_imports.size() == 3, "Default imports must have 3 libraries: KERNEL32, WS2_32, USER32");

    // Verify KERNEL32 required functions exist
    const auto& k32 = default_imports[0];
    TEST_ASSERT(k32.dll_name == "KERNEL32.dll", "First DLL must be KERNEL32.dll");

    std::unordered_set<std::string> k32_syms(k32.symbols.begin(), k32.symbols.end());
    const char* expected_k32[] = {
        "ExitProcess", "GetStdHandle", "WriteFile", "ReadFile",
        "HeapAlloc", "HeapFree", "GetProcessHeap",
        "QueryPerformanceCounter", "QueryPerformanceFrequency",
        "Sleep", "LoadLibraryA", "GetProcAddress"
    };
    for (const char* sym : expected_k32) {
        TEST_ASSERT(k32_syms.count(sym) > 0, std::string("Missing expected KERNEL32 symbol: ") + sym);
    }

    BinaryBuffer idata;
    std::unordered_map<std::string, uint32_t> iat_offsets;
    uint32_t iat_rva = 0;
    uint32_t iat_size = 0;
    std::string err;

    bool ok = GrassObjWriter::generate_import_table(default_imports, 0x2000, idata, iat_offsets, &err, &iat_rva, &iat_size);
    TEST_ASSERT(ok, "generate_import_table should succeed");
    TEST_ASSERT(idata.size() > 0, "idata buffer should not be empty");
    TEST_ASSERT(iat_rva >= 0x2000, "IAT RVA should be within idata section");
    TEST_ASSERT(iat_size > 0, "IAT size should be non-zero");

    // Check that every symbol has an assigned IAT offset
    for (const char* sym : expected_k32) {
        TEST_ASSERT(iat_offsets.find(sym) != iat_offsets.end(), std::string("IAT offset missing for: ") + sym);
        TEST_ASSERT(iat_offsets.find(std::string("__imp_") + sym) != iat_offsets.end(),
                    std::string("__imp_ alias missing for: ") + sym);
    }
}

// =============================================================================
// Test 3: Direct PE32+ Executable Generation (hello_direct.exe)
// Calls GetStdHandle + WriteFile + ExitProcess without GCC or external linkers
// =============================================================================
static void test_hello_direct_pe_executable() {
    CompiledUnit unit;
    unit.arch = TargetArch::X86_64;
    unit.abi = TargetABI::Win64;

    // 1. .rodata section with message string
    const std::string msg = "Hello from direct Grass PE32+ executable without gcc!\n";
    unit.rodata.assign(msg.begin(), msg.end());
    unit.rodata.push_back(0); // null terminator

    SymbolDef sym_rodata;
    sym_rodata.name = "hello_msg";
    sym_rodata.section = SectionKind::RoData;
    sym_rodata.offset = 0;
    sym_rodata.size = unit.rodata.size();
    sym_rodata.is_global = true;
    sym_rodata.is_external = false;
    unit.symbols.push_back(sym_rodata);

    // 2. .data section for bytes_written output DWORD
    unit.data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    SymbolDef sym_data;
    sym_data.name = "bytes_written";
    sym_data.section = SectionKind::Data;
    sym_data.offset = 0;
    sym_data.size = 4;
    sym_data.is_global = true;
    sym_data.is_external = false;
    unit.symbols.push_back(sym_data);

    // 3. External symbols for KERNEL32 WinAPI
    SymbolDef sym_gsh;
    sym_gsh.name = "GetStdHandle";
    sym_gsh.section = SectionKind::Text;
    sym_gsh.is_external = true;
    unit.symbols.push_back(sym_gsh);

    SymbolDef sym_wf;
    sym_wf.name = "WriteFile";
    sym_wf.section = SectionKind::Text;
    sym_wf.is_external = true;
    unit.symbols.push_back(sym_wf);

    SymbolDef sym_ep;
    sym_ep.name = "ExitProcess";
    sym_ep.section = SectionKind::Text;
    sym_ep.is_external = true;
    unit.symbols.push_back(sym_ep);

    // 4. Function symbol 'main'
    SymbolDef sym_main;
    sym_main.name = "main";
    sym_main.section = SectionKind::Text;
    sym_main.offset = 0;
    sym_main.is_global = true;
    sym_main.is_external = false;
    unit.symbols.push_back(sym_main);

    // 5. Build x86_64 machine code for main():
    // Win64 calling convention:
    // Shadow space = 32 bytes, stack alignment to 16 bytes.
    // sub rsp, 56 (0x38) aligns rsp (rsp was 8 mod 16, minus 56 = -48 = 0 mod 16).
    std::vector<uint8_t> code;

    // sub rsp, 56
    // 48 83 EC 38
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x38);

    // mov ecx, -11 (STD_OUTPUT_HANDLE = 0xFFFFFFF5)
    // B9 F5 FF FF FF
    code.push_back(0xB9);
    code.push_back(0xF5); code.push_back(0xFF); code.push_back(0xFF); code.push_back(0xFF);

    // call [rip + disp32] -> GetStdHandle
    // FF 15 [disp32]
    code.push_back(0xFF); code.push_back(0x15);
    uint32_t call_gsh_disp_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);

    RelocEntry rel_gsh;
    rel_gsh.section = SectionKind::Text;
    rel_gsh.offset = call_gsh_disp_off;
    rel_gsh.symbol_name = "GetStdHandle";
    rel_gsh.type = RelocType::Rel32;
    rel_gsh.addend = -4;
    unit.relocations.push_back(rel_gsh);

    // Save hStdOut to local slot: mov [rsp + 48], rax
    // 48 89 44 24 30
    code.push_back(0x48); code.push_back(0x89); code.push_back(0x44); code.push_back(0x24); code.push_back(0x30);

    // Prepare arguments for WriteFile(hStdOut, msg, len, &bytes_written, NULL):
    // arg1 (RCX) = hStdOut: mov rcx, rax
    // 48 89 C1
    code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1);

    // arg2 (RDX) = pointer to msg: lea rdx, [rip + disp32]
    // 48 8D 15 [disp32]
    code.push_back(0x48); code.push_back(0x8D); code.push_back(0x15);
    uint32_t lea_msg_disp_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);

    RelocEntry rel_msg;
    rel_msg.section = SectionKind::Text;
    rel_msg.offset = lea_msg_disp_off;
    rel_msg.symbol_name = "hello_msg";
    rel_msg.type = RelocType::Rel32;
    rel_msg.addend = -4;
    unit.relocations.push_back(rel_msg);

    // arg3 (R8D) = msg length: mov r8d, length
    // 41 B8 [4-byte len]
    uint32_t msg_len = static_cast<uint32_t>(msg.size());
    code.push_back(0x41); code.push_back(0xB8);
    code.push_back(static_cast<uint8_t>(msg_len & 0xFF));
    code.push_back(static_cast<uint8_t>((msg_len >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((msg_len >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((msg_len >> 24) & 0xFF));

    // arg4 (R9) = &bytes_written: lea r9, [rip + disp32]
    // 4C 8D 0D [disp32]
    code.push_back(0x4C); code.push_back(0x8D); code.push_back(0x0D);
    uint32_t lea_bytes_disp_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);

    RelocEntry rel_data;
    rel_data.section = SectionKind::Text;
    rel_data.offset = lea_bytes_disp_off;
    rel_data.symbol_name = "bytes_written";
    rel_data.type = RelocType::Rel32;
    rel_data.addend = -4;
    unit.relocations.push_back(rel_data);

    // arg5 = NULL: mov qword ptr [rsp + 32], 0
    // 48 C7 44 24 20 00 00 00 00
    code.push_back(0x48); code.push_back(0xC7); code.push_back(0x44); code.push_back(0x24); code.push_back(0x20);
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);

    // call [rip + disp32] -> WriteFile
    // FF 15 [disp32]
    code.push_back(0xFF); code.push_back(0x15);
    uint32_t call_wf_disp_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);

    RelocEntry rel_wf;
    rel_wf.section = SectionKind::Text;
    rel_wf.offset = call_wf_disp_off;
    rel_wf.symbol_name = "WriteFile";
    rel_wf.type = RelocType::Rel32;
    rel_wf.addend = -4;
    unit.relocations.push_back(rel_wf);

    // arg1 (ECX) = 0 for ExitProcess: xor ecx, ecx
    // 31 C9
    code.push_back(0x31); code.push_back(0xC9);

    // call [rip + disp32] -> ExitProcess
    // FF 15 [disp32]
    code.push_back(0xFF); code.push_back(0x15);
    uint32_t call_ep_disp_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);

    RelocEntry rel_ep;
    rel_ep.section = SectionKind::Text;
    rel_ep.offset = call_ep_disp_off;
    rel_ep.symbol_name = "ExitProcess";
    rel_ep.type = RelocType::Rel32;
    rel_ep.addend = -4;
    unit.relocations.push_back(rel_ep);

    // Fallback ret in case ExitProcess somehow returns
    // 48 83 C4 38 (add rsp, 56)
    // C3 (ret)
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xC4); code.push_back(0x38);
    code.push_back(0xC3);

    unit.text = code;
    sym_main.size = code.size();

    // 6. Write the direct PE32+ executable
    std::string exe_path = "tests/hello_direct.exe";
    std::string err;
    bool write_ok = GrassObjWriter::write_pe_executable_file(unit, exe_path, &err, "main");
    TEST_ASSERT(write_ok, std::string("write_pe_executable_file failed: ") + err);

    // 7. Verify PE binary structure on disk
    BinaryBuffer read_buf;
    TEST_ASSERT(read_buf.read_from_file(exe_path), "Failed to read generated PE file from disk");
    TEST_ASSERT(read_buf.size() >= 512, "PE executable file size must be at least 512 bytes");

    // DOS Magic: MZ (0x5A4D)
    uint16_t dos_magic = read_buf.read_u16(0);
    TEST_ASSERT(dos_magic == PE_DOS_MAGIC, "DOS header magic must be 'MZ' (0x5A4D)");

    // e_lfanew at offset 0x3C
    uint32_t pe_offset = read_buf.read_u32(0x3C);
    TEST_ASSERT(pe_offset == 0x80, "PE header offset must be 0x80");

    // PE signature: PE\0\0 (0x00004550)
    uint32_t pe_sig = read_buf.read_u32(pe_offset);
    TEST_ASSERT(pe_sig == PE_SIGNATURE, "PE signature must be 'PE\\0\\0' (0x00004550)");

    // Machine AMD64 (0x8664) at pe_offset + 4
    uint16_t machine = read_buf.read_u16(pe_offset + 4);
    TEST_ASSERT(machine == COFF_MACHINE_AMD64, "Machine must be AMD64 (0x8664)");

    // Number of sections at pe_offset + 6
    uint16_t num_sections = read_buf.read_u16(pe_offset + 6);
    TEST_ASSERT(num_sections == 3, "Number of sections must be 3 (.text, .rdata, .data)");

    // Optional header magic at pe_offset + 24
    uint16_t opt_magic = read_buf.read_u16(pe_offset + 24);
    TEST_ASSERT(opt_magic == PE_OPTIONAL_HDR64_MAGIC, "Optional header magic must be PE32+ (0x020B)");

    // ImageBase at pe_offset + 24 + 24
    uint64_t img_base = read_buf.read_u64(pe_offset + 24 + 24);
    TEST_ASSERT(img_base == PE_DEFAULT_IMAGE_BASE, "ImageBase must be 0x140000000");

    // SectionAlignment at pe_offset + 24 + 32
    uint32_t sec_align = read_buf.read_u32(pe_offset + 24 + 32);
    TEST_ASSERT(sec_align == PE_DEFAULT_SECTION_ALIGNMENT, "SectionAlignment must be 0x1000");

    // FileAlignment at pe_offset + 24 + 36
    uint32_t file_align = read_buf.read_u32(pe_offset + 24 + 36);
    TEST_ASSERT(file_align == PE_DEFAULT_FILE_ALIGNMENT, "FileAlignment must be 0x200");

    // Subsystem Console (3) at pe_offset + 24 + 68
    uint16_t subsys = read_buf.read_u16(pe_offset + 24 + 68);
    TEST_ASSERT(subsys == PE_SUBSYSTEM_WINDOWS_CUI, "Subsystem must be Console (3)");

    // 8. Execute tests/hello_direct.exe and capture stdout and return code
    std::cout << "\n[EXEC] Running generated tests/hello_direct.exe ...\n";
    FILE* pipe = _popen("tests\\hello_direct.exe", "r");
    TEST_ASSERT(pipe != nullptr, "Failed to launch tests/hello_direct.exe via _popen");

    char pipe_buf[256];
    std::string captured_output;
    while (fgets(pipe_buf, sizeof(pipe_buf), pipe) != nullptr) {
        captured_output += pipe_buf;
    }
    int exit_code = _pclose(pipe);

    std::cout << "[STDOUT OUTPUT]: " << captured_output;
    std::cout << "[EXIT CODE]: " << exit_code << "\n";

    TEST_ASSERT(exit_code == 0, "tests/hello_direct.exe must return exit code 0");
    TEST_ASSERT(captured_output.find("Hello from direct Grass PE32+ executable without gcc!") != std::string::npos,
                "Console output must match expected text");
}

// =============================================================================
// Test 4: Direct PE32+ Executable with Call via Trampoline (E8 rel32)
// =============================================================================
static void test_thunk_trampoline_pe_executable() {
    CompiledUnit unit;
    unit.arch = TargetArch::X86_64;
    unit.abi = TargetABI::Win64;

    const std::string msg = "Trampoline thunk call works successfully!\n";
    unit.rodata.assign(msg.begin(), msg.end());
    unit.rodata.push_back(0);

    SymbolDef sym_rodata;
    sym_rodata.name = "thunk_msg";
    sym_rodata.section = SectionKind::RoData;
    sym_rodata.offset = 0;
    sym_rodata.size = unit.rodata.size();
    unit.symbols.push_back(sym_rodata);

    SymbolDef sym_data;
    sym_data.name = "thunk_bytes_written";
    sym_data.section = SectionKind::Data;
    sym_data.offset = 0;
    sym_data.size = 4;
    unit.symbols.push_back(sym_data);
    unit.data = {0, 0, 0, 0, 0, 0, 0, 0};

    SymbolDef sym_gsh; sym_gsh.name = "GetStdHandle"; sym_gsh.section = SectionKind::Text; sym_gsh.is_external = true; unit.symbols.push_back(sym_gsh);
    SymbolDef sym_wf;  sym_wf.name = "WriteFile";    sym_wf.section = SectionKind::Text; sym_wf.is_external = true; unit.symbols.push_back(sym_wf);
    SymbolDef sym_ep;  sym_ep.name = "ExitProcess";  sym_ep.section = SectionKind::Text; sym_ep.is_external = true; unit.symbols.push_back(sym_ep);

    SymbolDef sym_main;
    sym_main.name = "main";
    sym_main.section = SectionKind::Text;
    sym_main.offset = 0;
    sym_main.is_global = true;
    unit.symbols.push_back(sym_main);

    // Call external functions using E8 (CALL rel32) to verify trampoline thunks
    std::vector<uint8_t> code;

    // sub rsp, 56
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x38);

    // mov ecx, -11
    code.push_back(0xB9); code.push_back(0xF5); code.push_back(0xFF); code.push_back(0xFF); code.push_back(0xFF);

    // call GetStdHandle (E8 rel32)
    code.push_back(0xE8);
    uint32_t call_gsh_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);
    RelocEntry rel_gsh; rel_gsh.section = SectionKind::Text; rel_gsh.offset = call_gsh_off; rel_gsh.symbol_name = "GetStdHandle"; rel_gsh.type = RelocType::Rel32; rel_gsh.addend = -4;
    unit.relocations.push_back(rel_gsh);

    // mov [rsp + 48], rax
    code.push_back(0x48); code.push_back(0x89); code.push_back(0x44); code.push_back(0x24); code.push_back(0x30);

    // mov rcx, rax
    code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1);

    // lea rdx, [rip + disp32] (thunk_msg)
    code.push_back(0x48); code.push_back(0x8D); code.push_back(0x15);
    uint32_t lea_msg_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);
    RelocEntry rel_msg; rel_msg.section = SectionKind::Text; rel_msg.offset = lea_msg_off; rel_msg.symbol_name = "thunk_msg"; rel_msg.type = RelocType::Rel32; rel_msg.addend = -4;
    unit.relocations.push_back(rel_msg);

    // mov r8d, msg_len
    uint32_t m_len = static_cast<uint32_t>(msg.size());
    code.push_back(0x41); code.push_back(0xB8);
    code.push_back(static_cast<uint8_t>(m_len & 0xFF)); code.push_back(static_cast<uint8_t>((m_len >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((m_len >> 16) & 0xFF)); code.push_back(static_cast<uint8_t>((m_len >> 24) & 0xFF));

    // lea r9, [rip + disp32] (thunk_bytes_written)
    code.push_back(0x4C); code.push_back(0x8D); code.push_back(0x0D);
    uint32_t lea_b_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);
    RelocEntry rel_b; rel_b.section = SectionKind::Text; rel_b.offset = lea_b_off; rel_b.symbol_name = "thunk_bytes_written"; rel_b.type = RelocType::Rel32; rel_b.addend = -4;
    unit.relocations.push_back(rel_b);

    // mov qword ptr [rsp + 32], 0
    code.push_back(0x48); code.push_back(0xC7); code.push_back(0x44); code.push_back(0x24); code.push_back(0x20);
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);

    // call WriteFile (E8 rel32)
    code.push_back(0xE8);
    uint32_t call_wf_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);
    RelocEntry rel_wf; rel_wf.section = SectionKind::Text; rel_wf.offset = call_wf_off; rel_wf.symbol_name = "WriteFile"; rel_wf.type = RelocType::Rel32; rel_wf.addend = -4;
    unit.relocations.push_back(rel_wf);

    // xor ecx, ecx
    code.push_back(0x31); code.push_back(0xC9);

    // call ExitProcess (E8 rel32)
    code.push_back(0xE8);
    uint32_t call_ep_off = static_cast<uint32_t>(code.size());
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);
    RelocEntry rel_ep; rel_ep.section = SectionKind::Text; rel_ep.offset = call_ep_off; rel_ep.symbol_name = "ExitProcess"; rel_ep.type = RelocType::Rel32; rel_ep.addend = -4;
    unit.relocations.push_back(rel_ep);

    code.push_back(0x48); code.push_back(0x83); code.push_back(0xC4); code.push_back(0x38);
    code.push_back(0xC3);

    unit.text = code;

    std::string exe_path = "tests/thunk_direct.exe";
    std::string err;
    bool write_ok = GrassObjWriter::write_pe_executable_file(unit, exe_path, &err, "main");
    TEST_ASSERT(write_ok, std::string("write_pe_executable_file failed: ") + err);

    FILE* pipe = _popen("tests\\thunk_direct.exe", "r");
    TEST_ASSERT(pipe != nullptr, "Failed to launch tests/thunk_direct.exe");

    char pipe_buf[256];
    std::string captured_output;
    while (fgets(pipe_buf, sizeof(pipe_buf), pipe) != nullptr) {
        captured_output += pipe_buf;
    }
    int exit_code = _pclose(pipe);

    TEST_ASSERT(exit_code == 0, "tests/thunk_direct.exe must return exit code 0");
    TEST_ASSERT(captured_output.find("Trampoline thunk call works successfully!") != std::string::npos,
                "Console output must match expected text");
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "Running Grass PE32+ Executable Writer Test Suite\n";
    std::cout << "====================================================\n";

    run_test("PE Header Structures and Size Constraints", test_pe_header_structures);
    run_test("Import Directory and IAT Generation", test_import_table_generation);
    run_test("Direct PE32+ Executable hello_direct.exe Execution", test_hello_direct_pe_executable);
    run_test("Direct PE32+ Executable Trampoline (E8) Call Execution", test_thunk_trampoline_pe_executable);

    std::cout << "====================================================\n";
    std::cout << "All PE32+ tests passed successfully (" << g_tests_passed << "/" << g_tests_total << ")\n";
    std::cout << "====================================================\n";

    return 0;
}

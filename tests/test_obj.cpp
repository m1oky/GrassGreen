#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <sstream>

#include "../src/GrassObjWriter.h"

using namespace grass::backend;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "ASSERTION FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
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
// Test 1: BinaryBuffer Utilities
// =============================================================================
static void test_binary_buffer() {
    BinaryBuffer buf;

    TEST_ASSERT(buf.size() == 0, "Buffer initial size should be 0");
    TEST_ASSERT(buf.empty(), "Buffer should be empty initially");

    buf.emit_u8(0x12);
    buf.emit_u16(0x3456);
    buf.emit_u32(0x789ABCDE);
    buf.emit_u64(0xFEDCBA9876543210ULL);

    TEST_ASSERT(buf.size() == 1 + 2 + 4 + 8, "Buffer size after emits");
    TEST_ASSERT(buf.read_u8(0) == 0x12, "Read u8 at 0");
    TEST_ASSERT(buf.read_u16(1) == 0x3456, "Read u16 at 1 (little-endian)");
    TEST_ASSERT(buf.read_u32(3) == 0x789ABCDE, "Read u32 at 3");
    TEST_ASSERT(buf.read_u64(7) == 0xFEDCBA9876543210ULL, "Read u64 at 7");

    // Signed emits
    buf.emit_i8(-42);
    buf.emit_i16(-1234);
    buf.emit_i32(-999999);
    buf.emit_i64(-8888888888LL);

    size_t signed_off = 15;
    TEST_ASSERT(static_cast<int8_t>(buf.read_u8(signed_off)) == -42, "Read i8");
    TEST_ASSERT(buf.read_i32(signed_off + 3) == -999999, "Read i32");
    TEST_ASSERT(buf.read_i64(signed_off + 7) == -8888888888LL, "Read i64");

    // Patching
    buf.patch_u8(0, 0xAA);
    TEST_ASSERT(buf.read_u8(0) == 0xAA, "Patch u8");

    buf.patch_u32(3, 0x11223344);
    TEST_ASSERT(buf.read_u32(3) == 0x11223344, "Patch u32");

    buf.patch_u64(7, 0xAABBCCDDEEFF0011ULL);
    TEST_ASSERT(buf.read_u64(7) == 0xAABBCCDDEEFF0011ULL, "Patch u64");

    // Alignment
    buf.align_to(16, 0xCC);
    TEST_ASSERT(buf.size() % 16 == 0, "Buffer aligned to 16");

    // Strings and zeros
    size_t sz_before = buf.size();
    buf.emit_string("TEST_STRING");
    TEST_ASSERT(buf.size() == sz_before + 11, "Emit string size");

    buf.emit_cstring("NULL_TERM");
    TEST_ASSERT(buf.read_u8(buf.size() - 1) == 0, "Null-terminated string ends with 0");

    buf.emit_zeros(10);
    for (size_t i = buf.size() - 10; i < buf.size(); ++i) {
        TEST_ASSERT(buf.read_u8(i) == 0, "Zero padding");
    }

    // File I/O
    std::string test_file = "test_buffer_temp.bin";
    TEST_ASSERT(buf.write_to_file(test_file), "Write buffer to file");

    BinaryBuffer read_buf;
    TEST_ASSERT(read_buf.read_from_file(test_file), "Read buffer from file");
    TEST_ASSERT(read_buf.size() == buf.size(), "Read buffer size match");
    TEST_ASSERT(std::memcmp(read_buf.data(), buf.data(), buf.size()) == 0, "Read buffer content match");
    std::remove(test_file.c_str());
}

// =============================================================================
// Test 2: StringTableBuilder
// =============================================================================
static void test_string_table_builder() {
    StringTableBuilder stb;

    // COFF mode: starts at offset 4
    TEST_ASSERT(stb.total_bytes_coff() == 4, "COFF string table initial size is 4");
    uint32_t off1 = stb.add_coff("func_long_name_alpha");
    TEST_ASSERT(off1 == 4, "First COFF string offset is 4");
    uint32_t off2 = stb.add_coff("func_long_name_beta");
    TEST_ASSERT(off2 == 4 + std::string("func_long_name_alpha").size() + 1, "Second COFF string offset");
    TEST_ASSERT(stb.add_coff("func_long_name_alpha") == off1, "COFF string deduplication");

    BinaryBuffer coff_buf;
    stb.build_coff(coff_buf);
    TEST_ASSERT(coff_buf.read_u32(0) == static_cast<uint32_t>(stb.total_bytes_coff()), "COFF string table length header");

    // ELF mode: starts at offset 1
    stb.clear();
    TEST_ASSERT(stb.total_bytes_elf() == 1, "ELF string table initial size is 1");
    TEST_ASSERT(stb.add_elf("") == 0, "ELF empty string has offset 0");
    uint32_t elf_off1 = stb.add_elf("symbol_one");
    TEST_ASSERT(elf_off1 == 1, "First ELF string offset is 1");
    uint32_t elf_off2 = stb.add_elf("symbol_two");
    TEST_ASSERT(elf_off2 == 1 + std::string("symbol_one").size() + 1, "Second ELF string offset");
    TEST_ASSERT(stb.add_elf("symbol_one") == elf_off1, "ELF string deduplication");

    BinaryBuffer elf_buf;
    stb.build_elf(elf_buf);
    TEST_ASSERT(elf_buf.read_u8(0) == 0, "ELF string table starts with null byte");
}

// =============================================================================
// Test 3: ABISpec and Stack Frame Calculations
// =============================================================================
static void test_abi_spec() {
    // Win64 Spec
    ABISpec win64 = ABISpec::for_abi(TargetABI::Win64);
    TEST_ASSERT(win64.get_arch() == TargetArch::X86_64, "Win64 is 64-bit");
    TEST_ASSERT(win64.get_integer_arg_regs().size() == 4, "Win64 has 4 int arg registers");
    TEST_ASSERT(win64.get_integer_arg_regs()[0] == Reg::RCX, "Win64 1st arg is RCX");
    TEST_ASSERT(win64.get_integer_arg_regs()[1] == Reg::RDX, "Win64 2nd arg is RDX");
    TEST_ASSERT(win64.get_integer_arg_regs()[2] == Reg::R8,  "Win64 3rd arg is R8");
    TEST_ASSERT(win64.get_integer_arg_regs()[3] == Reg::R9,  "Win64 4th arg is R9");
    TEST_ASSERT(win64.get_float_arg_regs().size() == 4, "Win64 has 4 float arg registers");
    TEST_ASSERT(win64.get_shadow_space_size() == 32, "Win64 shadow space is 32 bytes");
    TEST_ASSERT(win64.get_red_zone_size() == 0, "Win64 red zone is 0");
    TEST_ASSERT(win64.is_callee_saved(Reg::RBX), "RBX is callee-saved in Win64");
    TEST_ASSERT(!win64.is_callee_saved(Reg::RAX), "RAX is volatile in Win64");
    TEST_ASSERT(win64.is_caller_saved(Reg::RAX), "RAX is caller-saved in Win64");

    // SysV64 Spec
    ABISpec sysv64 = ABISpec::for_abi(TargetABI::SysV64);
    TEST_ASSERT(sysv64.get_integer_arg_regs().size() == 6, "SysV64 has 6 int arg registers");
    TEST_ASSERT(sysv64.get_integer_arg_regs()[0] == Reg::RDI, "SysV64 1st arg is RDI");
    TEST_ASSERT(sysv64.get_integer_arg_regs()[1] == Reg::RSI, "SysV64 2nd arg is RSI");
    TEST_ASSERT(sysv64.get_float_arg_regs().size() == 8, "SysV64 has 8 float arg registers");
    TEST_ASSERT(sysv64.get_shadow_space_size() == 0, "SysV64 shadow space is 0");
    TEST_ASSERT(sysv64.get_red_zone_size() == 128, "SysV64 red zone is 128 bytes");

    // Cdecl32 Spec
    ABISpec cdecl32 = ABISpec::for_abi(TargetABI::Cdecl32);
    TEST_ASSERT(cdecl32.get_arch() == TargetArch::X86_32, "Cdecl32 is 32-bit");
    TEST_ASSERT(cdecl32.get_integer_arg_regs().empty(), "Cdecl32 has 0 int arg registers");

    // Stack frame calculation for Win64 with calls
    StackFrameRequest req_win64;
    req_win64.local_vars_size = 24;
    req_win64.callee_saved_regs = { Reg::RBX, Reg::R12 };
    req_win64.max_outgoing_args_size = 0;
    req_win64.has_calls = true;
    req_win64.use_frame_pointer = true;

    StackFrameInfo frame_win64 = win64.compute_stack_frame(req_win64);
    TEST_ASSERT(frame_win64.shadow_space_size == 32, "Win64 frame allocated shadow space");
    // Pushes: RBP (8) + RBX (8) + R12 (8) = 24 bytes
    // Entry ret_addr = 8 bytes. Total pushed = 32 bytes.
    // Needed: 24 (local) + 32 (shadow) = 56 bytes.
    // Total unaligned = 32 + 56 = 88.
    // Next 16-multiple is 96. Total frame size = 96 - 32 = 64.
    TEST_ASSERT(frame_win64.total_frame_size == 64, "Win64 total_frame_size aligned to 16");
    TEST_ASSERT((8 + frame_win64.pushed_regs_size + frame_win64.total_frame_size) % 16 == 0, "Stack is 16-byte aligned before call");

    // Emit prologue & epilogue bytes
    BinaryBuffer code_buf;
    win64.emit_prologue_bytes(frame_win64, code_buf);
    TEST_ASSERT(code_buf.size() > 0, "Win64 prologue emitted bytes");
    TEST_ASSERT(code_buf.read_u8(0) == 0x55, "Prologue starts with push rbp (0x55)");

    BinaryBuffer epi_buf;
    win64.emit_epilogue_bytes(frame_win64, epi_buf);
    TEST_ASSERT(epi_buf.size() > 0, "Win64 epilogue emitted bytes");
    TEST_ASSERT(epi_buf.read_u8(epi_buf.size() - 1) == 0xC3, "Epilogue ends with ret (0xC3)");
}

// =============================================================================
// Helper: Build a realistic test CompiledUnit
// =============================================================================
static CompiledUnit create_test_unit(TargetArch arch, TargetABI abi) {
    CompiledUnit unit;
    unit.arch = arch;
    unit.abi = abi;

    if (arch == TargetArch::X86_64) {
        // Machine code for:
        // push rbp                  (0x55)
        // mov rbp, rsp              (0x48, 0x89, 0xE5)
        // sub rsp, 32               (0x48, 0x83, 0xEC, 0x20)
        // mov ecx, [rip + disp32]   (0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00) -> reloc Rel32 to "global_var_long_name"
        // call ext_func             (0xE8, 0x00, 0x00, 0x00, 0x00)       -> reloc Rel32 to "ext_long_named_func"
        // add rsp, 32               (0x48, 0x83, 0xC4, 0x20)
        // pop rbp                   (0x5D)
        // ret                       (0xC3)
        unit.text = {
            0x55,
            0x48, 0x89, 0xE5,
            0x48, 0x83, 0xEC, 0x20,
            0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, // offset 8: disp32 at offset 10
            0xE8, 0x00, 0x00, 0x00, 0x00,       // offset 14: disp32 at offset 15
            0x48, 0x83, 0xC4, 0x20,
            0x5D,
            0xC3
        };

        // .rodata: constant string
        std::string rodata_str = "GrassGreen Constant String Data!";
        unit.rodata.assign(rodata_str.begin(), rodata_str.end());
        unit.rodata.push_back(0); // null-terminate

        // .data: 64-bit value = 40
        unit.data = {
            0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 40ULL
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // Pointer to rodata (Abs64)
        };

        unit.bss_size = 64;

        // Symbols
        SymbolDef sym_fn;
        sym_fn.name = "grass_compute_answer_long_name";
        sym_fn.section = SectionKind::Text;
        sym_fn.offset = 0;
        sym_fn.size = unit.text.size();
        sym_fn.is_global = true;
        sym_fn.is_external = false;
        unit.symbols.push_back(sym_fn);

        SymbolDef sym_rodata;
        sym_rodata.name = "grass_rodata_message";
        sym_rodata.section = SectionKind::RoData;
        sym_rodata.offset = 0;
        sym_rodata.size = unit.rodata.size();
        sym_rodata.is_global = true;
        sym_rodata.is_external = false;
        unit.symbols.push_back(sym_rodata);

        SymbolDef sym_data;
        sym_data.name = "global_var_long_name";
        sym_data.section = SectionKind::Data;
        sym_data.offset = 0;
        sym_data.size = 8;
        sym_data.is_global = true;
        sym_data.is_external = false;
        unit.symbols.push_back(sym_data);

        SymbolDef sym_ext;
        sym_ext.name = "ext_long_named_func";
        sym_ext.section = SectionKind::Text;
        sym_ext.offset = 0;
        sym_ext.size = 0;
        sym_ext.is_global = true;
        sym_ext.is_external = true;
        unit.symbols.push_back(sym_ext);

        // Relocations
        RelocEntry rel_data;
        rel_data.section = SectionKind::Text;
        rel_data.offset = 10;
        rel_data.symbol_name = "global_var_long_name";
        rel_data.type = RelocType::Rel32;
        rel_data.addend = -4; // x86_64 rip-relative addend: offset to next instruction
        unit.relocations.push_back(rel_data);

        RelocEntry rel_call;
        rel_call.section = SectionKind::Text;
        rel_call.offset = 15;
        rel_call.symbol_name = "ext_long_named_func";
        rel_call.type = RelocType::Rel32;
        rel_call.addend = -4;
        unit.relocations.push_back(rel_call);

        RelocEntry rel_ptr;
        rel_ptr.section = SectionKind::Data;
        rel_ptr.offset = 8;
        rel_ptr.symbol_name = "grass_rodata_message";
        rel_ptr.type = RelocType::Abs64;
        rel_ptr.addend = 0;
        unit.relocations.push_back(rel_ptr);

    } else {
        // 32-bit machine code:
        // push ebp
        // mov ebp, esp
        // mov eax, [0x00000000] -> reloc Abs32 to "global_var_32"
        // pop ebp
        // ret
        unit.text = {
            0x55,
            0x89, 0xE5,
            0xA1, 0x00, 0x00, 0x00, 0x00, // offset 3: Abs32
            0x5D,
            0xC3
        };

        unit.rodata = { 0x01, 0x02, 0x03, 0x04 };
        unit.data = { 0x2A, 0x00, 0x00, 0x00 };
        unit.bss_size = 32;

        SymbolDef sym_fn;
        sym_fn.name = "grass_func32";
        sym_fn.section = SectionKind::Text;
        sym_fn.offset = 0;
        sym_fn.size = unit.text.size();
        sym_fn.is_global = true;
        sym_fn.is_external = false;
        unit.symbols.push_back(sym_fn);

        SymbolDef sym_data;
        sym_data.name = "global_var_32";
        sym_data.section = SectionKind::Data;
        sym_data.offset = 0;
        sym_data.size = 4;
        sym_data.is_global = true;
        sym_data.is_external = false;
        unit.symbols.push_back(sym_data);

        RelocEntry rel_abs;
        rel_abs.section = SectionKind::Text;
        rel_abs.offset = 4;
        rel_abs.symbol_name = "global_var_32";
        rel_abs.type = RelocType::Abs32;
        rel_abs.addend = 0;
        unit.relocations.push_back(rel_abs);
    }

    return unit;
}

// =============================================================================
// Test 4: COFF Win64 Object Writer & Parser
// =============================================================================
static void test_coff_win64_obj() {
    CompiledUnit unit = create_test_unit(TargetArch::X86_64, TargetABI::Win64);

    BinaryBuffer coff_buf;
    std::string err;
    bool ok = GrassObjWriter::write_coff_obj(unit, coff_buf, &err);
    TEST_ASSERT(ok, "write_coff_obj should succeed: " + err);
    TEST_ASSERT(coff_buf.size() >= sizeof(COFF_FileHeader), "COFF buffer must have file header");

    // Binary verification: parse header
    const COFF_FileHeader* fhdr = reinterpret_cast<const COFF_FileHeader*>(coff_buf.data());
    TEST_ASSERT(fhdr->machine == COFF_MACHINE_AMD64, "COFF machine must be AMD64 (0x8664)");
    TEST_ASSERT(fhdr->number_of_sections == 4, "COFF has 4 sections (.text, .rdata, .data, .bss)");
    TEST_ASSERT(fhdr->size_of_optional_header == 0, "COFF optional header must be 0 for obj");
    TEST_ASSERT(fhdr->pointer_to_symbol_table > 0, "COFF symbol table offset must be > 0");
    TEST_ASSERT(fhdr->number_of_symbols >= 4, "COFF symbol count must include section symbols and symbols");

    // Parse section headers
    const COFF_SectionHeader* shdrs = reinterpret_cast<const COFF_SectionHeader*>(coff_buf.data() + sizeof(COFF_FileHeader));
    bool found_text = false;
    bool found_rdata = false;
    bool found_data = false;
    bool found_bss = false;

    for (uint16_t i = 0; i < fhdr->number_of_sections; ++i) {
        std::string sname(shdrs[i].name, strnlen(shdrs[i].name, 8));
        if (sname == ".text") {
            found_text = true;
            TEST_ASSERT(shdrs[i].characteristics & COFF_SCN_CNT_CODE, ".text has CNT_CODE");
            TEST_ASSERT(shdrs[i].characteristics & COFF_SCN_MEM_EXECUTE, ".text has MEM_EXECUTE");
            TEST_ASSERT(shdrs[i].number_of_relocations == 2, ".text has 2 relocations");
            TEST_ASSERT(shdrs[i].pointer_to_relocations > 0, ".text pointer_to_relocations > 0");

            // Verify relocations for .text
            const COFF_Relocation* rels = reinterpret_cast<const COFF_Relocation*>(coff_buf.data() + shdrs[i].pointer_to_relocations);
            TEST_ASSERT(rels[0].virtual_address == 10, "Reloc 0 offset 10");
            TEST_ASSERT(rels[0].type == COFF_REL_AMD64_REL32, "Reloc 0 type IMAGE_REL_AMD64_REL32");
            TEST_ASSERT(rels[1].virtual_address == 15, "Reloc 1 offset 15");
            TEST_ASSERT(rels[1].type == COFF_REL_AMD64_REL32, "Reloc 1 type IMAGE_REL_AMD64_REL32");
        } else if (sname == ".rdata") {
            found_rdata = true;
            TEST_ASSERT(shdrs[i].characteristics & COFF_SCN_CNT_INITIALIZED_DATA, ".rdata has INITIALIZED_DATA");
        } else if (sname == ".data") {
            found_data = true;
            TEST_ASSERT(shdrs[i].characteristics & COFF_SCN_MEM_WRITE, ".data has MEM_WRITE");
            TEST_ASSERT(shdrs[i].number_of_relocations == 1, ".data has 1 relocation");
            const COFF_Relocation* drel = reinterpret_cast<const COFF_Relocation*>(coff_buf.data() + shdrs[i].pointer_to_relocations);
            TEST_ASSERT(drel[0].virtual_address == 8, ".data reloc offset 8");
            TEST_ASSERT(drel[0].type == COFF_REL_AMD64_ADDR64, ".data reloc type IMAGE_REL_AMD64_ADDR64");
        } else if (sname == ".bss") {
            found_bss = true;
            TEST_ASSERT(shdrs[i].size_of_raw_data == 0, ".bss has raw data size 0");
            TEST_ASSERT(shdrs[i].pointer_to_raw_data == 0, ".bss has pointer to raw data 0");
        }
    }
    TEST_ASSERT(found_text && found_rdata && found_data && found_bss, "All 4 sections present in COFF");

    // Verify Symbol Table & String Table
    const COFF_SymbolRecord* syms = reinterpret_cast<const COFF_SymbolRecord*>(coff_buf.data() + fhdr->pointer_to_symbol_table);
    size_t strtab_offset = fhdr->pointer_to_symbol_table + fhdr->number_of_symbols * sizeof(COFF_SymbolRecord);
    TEST_ASSERT(strtab_offset < coff_buf.size(), "String table offset is within buffer");
    uint32_t strtab_size = coff_buf.read_u32(strtab_offset);
    TEST_ASSERT(strtab_size >= 4, "COFF string table size header valid");

    bool found_long_sym = false;
    for (uint32_t i = 0; i < fhdr->number_of_symbols; ++i) {
        if (syms[i].name.long_name.zeroes == 0 && syms[i].name.long_name.offset >= 4) {
            uint32_t name_off = strtab_offset + syms[i].name.long_name.offset;
            TEST_ASSERT(name_off < coff_buf.size(), "Long name offset in bounds");
            const char* name_str = reinterpret_cast<const char*>(coff_buf.data() + name_off);
            if (std::string(name_str) == "grass_compute_answer_long_name") {
                found_long_sym = true;
                TEST_ASSERT(syms[i].storage_class == COFF_SYM_CLASS_EXTERNAL, "Function symbol is EXTERNAL");
                TEST_ASSERT(syms[i].type == COFF_SYM_TYPE_FUNC, "Function symbol type is 0x20");
            }
        }
    }
    TEST_ASSERT(found_long_sym, "Long symbol 'grass_compute_answer_long_name' verified via string table");

    // Save to file for toolchain verification
    std::string obj_file = "test_win64.obj";
    TEST_ASSERT(GrassObjWriter::write_coff_obj_file(unit, obj_file, &err), "Write test_win64.obj");

    // Real toolchain test: link generated test_win64.obj with GCC and execute!
    // test_win64.obj contains grass_compute_answer_long_name, which reads 40 from
    // .data (global_var_long_name) via Rel32, calls external ext_long_named_func via Rel32,
    // and returns the result (40 + 2 = 42).
    std::string runner_src = "runner_main.c";
    {
        std::ofstream rf(runner_src);
        rf << "int grass_compute_answer_long_name(void);\n"
           << "int ext_long_named_func(int x) {\n"
           << "    return x + 2;\n"
           << "}\n"
           << "int main(void) {\n"
           << "    int val = grass_compute_answer_long_name();\n"
           << "    return (val == 42) ? 42 : 1;\n"
           << "}\n";
    }

    std::string build_cmd = "gcc -O2 " + obj_file + " " + runner_src + " -o runner_test.exe 2> NUL";
    int compile_res = std::system(build_cmd.c_str());
    if (compile_res == 0) {
        int run_res = std::system("runner_test.exe");
        TEST_ASSERT(run_res == 42, "Linked executable returned 42 from test_win64.obj with data and call relocations");
        std::remove("runner_test.exe");
    }
    std::remove(runner_src.c_str());
    std::remove(obj_file.c_str());
}

// =============================================================================
// Test 5: COFF Win32 Object Writer & Parser
// =============================================================================
static void test_coff_win32_obj() {
    CompiledUnit unit = create_test_unit(TargetArch::X86_32, TargetABI::Cdecl32);

    BinaryBuffer buf;
    std::string err;
    TEST_ASSERT(GrassObjWriter::write_coff_obj(unit, buf, &err), "write_coff_obj 32-bit: " + err);

    const COFF_FileHeader* fhdr = reinterpret_cast<const COFF_FileHeader*>(buf.data());
    TEST_ASSERT(fhdr->machine == COFF_MACHINE_I386, "Machine must be i386 (0x014C)");
    TEST_ASSERT(fhdr->number_of_sections == 4, "32-bit COFF has 4 sections");
}

// =============================================================================
// Test 6: ELF64 Linux Object Writer & Parser
// =============================================================================
static void test_elf64_obj() {
    CompiledUnit unit = create_test_unit(TargetArch::X86_64, TargetABI::SysV64);

    BinaryBuffer elf_buf;
    std::string err;
    bool ok = GrassObjWriter::write_elf_obj(unit, elf_buf, &err);
    TEST_ASSERT(ok, "write_elf_obj 64-bit should succeed: " + err);
    TEST_ASSERT(elf_buf.size() >= sizeof(Elf64_Ehdr), "ELF64 buffer size");

    // Verify ELF64 Header
    const Elf64_Ehdr* ehdr = reinterpret_cast<const Elf64_Ehdr*>(elf_buf.data());
    TEST_ASSERT(ehdr->e_ident[0] == 0x7F && ehdr->e_ident[1] == 'E' &&
                ehdr->e_ident[2] == 'L'  && ehdr->e_ident[3] == 'F', "ELF magic number");
    TEST_ASSERT(ehdr->e_ident[4] == ELF_CLASS_64, "ELF class is 64-bit");
    TEST_ASSERT(ehdr->e_ident[5] == ELF_DATA_2LSB, "ELF endianness is little-endian (2LSB)");
    TEST_ASSERT(ehdr->e_type == ELF_ET_REL, "ELF type is ET_REL (1)");
    TEST_ASSERT(ehdr->e_machine == ELF_EM_X86_64, "ELF machine is EM_X86_64 (62)");
    TEST_ASSERT(ehdr->e_ehsize == sizeof(Elf64_Ehdr), "ELF header size is 64 bytes");
    TEST_ASSERT(ehdr->e_shentsize == sizeof(Elf64_Shdr), "ELF section header entry size is 64 bytes");
    TEST_ASSERT(ehdr->e_shoff > 0, "ELF section header table offset > 0");

    // Section headers
    const Elf64_Shdr* shdrs = reinterpret_cast<const Elf64_Shdr*>(elf_buf.data() + ehdr->e_shoff);
    uint16_t shnum = ehdr->e_shnum;
    uint16_t shstrndx = ehdr->e_shstrndx;
    TEST_ASSERT(shstrndx < shnum, "shstrtab index is valid");

    const char* shstrtab = reinterpret_cast<const char*>(elf_buf.data() + shdrs[shstrndx].sh_offset);

    uint32_t symtab_idx = 0;
    uint32_t strtab_idx = 0;
    uint32_t rela_text_idx = 0;

    for (uint16_t i = 0; i < shnum; ++i) {
        std::string sname = shstrtab + shdrs[i].sh_name;
        if (sname == ".symtab") symtab_idx = i;
        else if (sname == ".strtab") strtab_idx = i;
        else if (sname == ".rela.text") rela_text_idx = i;
    }

    TEST_ASSERT(symtab_idx > 0, ".symtab section present");
    TEST_ASSERT(strtab_idx > 0, ".strtab section present");
    TEST_ASSERT(rela_text_idx > 0, ".rela.text section present");

    // Validate .symtab links and symbols
    TEST_ASSERT(shdrs[symtab_idx].sh_link == strtab_idx, ".symtab sh_link points to .strtab");
    uint32_t first_global = shdrs[symtab_idx].sh_info;
    TEST_ASSERT(first_global > 0, ".symtab sh_info points to first global symbol");

    const Elf64_Sym* syms = reinterpret_cast<const Elf64_Sym*>(elf_buf.data() + shdrs[symtab_idx].sh_offset);
    size_t num_symbols = shdrs[symtab_idx].sh_size / sizeof(Elf64_Sym);
    TEST_ASSERT(first_global <= num_symbols, "first_global index in bounds");

    const char* strtab = reinterpret_cast<const char*>(elf_buf.data() + shdrs[strtab_idx].sh_offset);

    // Verify STT_FILE symbol
    TEST_ASSERT(num_symbols >= 2, "At least sym0 and STT_FILE exist in symtab");
    TEST_ASSERT((syms[1].st_info & 0xF) == ELF_STT_FILE, "Symbol 1 must be STT_FILE");
    TEST_ASSERT((syms[1].st_info >> 4) == ELF_STB_LOCAL, "STT_FILE must be STB_LOCAL");
    TEST_ASSERT(syms[1].st_shndx == ELF_SHN_ABS, "STT_FILE st_shndx must be SHN_ABS");
    std::string file_sym_name = strtab + syms[1].st_name;
    TEST_ASSERT(file_sym_name == "grass_source", "STT_FILE symbol name is grass_source");

    // Verify long symbol presence in strtab
    bool found_long_sym_elf = false;
    for (size_t i = 1; i < num_symbols; ++i) {
        if (syms[i].st_name > 0) {
            std::string sname = strtab + syms[i].st_name;
            if (sname == "grass_compute_answer_long_name") {
                found_long_sym_elf = true;
            }
        }
    }
    TEST_ASSERT(found_long_sym_elf, "ELF64 symbol string table contains grass_compute_answer_long_name");

    // ELF standard verification: STB_LOCAL must strictly precede STB_GLOBAL
    for (size_t i = 1; i < num_symbols; ++i) {
        uint8_t bind = syms[i].st_info >> 4;
        if (i < first_global) {
            TEST_ASSERT(bind == ELF_STB_LOCAL, "Symbols before sh_info must be STB_LOCAL");
        } else {
            TEST_ASSERT(bind == ELF_STB_GLOBAL, "Symbols at and after sh_info must be STB_GLOBAL");
        }
    }

    // Verify .rela.text
    TEST_ASSERT(shdrs[rela_text_idx].sh_link == symtab_idx, ".rela.text sh_link points to .symtab");
    const Elf64_Rela* relas = reinterpret_cast<const Elf64_Rela*>(elf_buf.data() + shdrs[rela_text_idx].sh_offset);
    size_t num_relas = shdrs[rela_text_idx].sh_size / sizeof(Elf64_Rela);
    TEST_ASSERT(num_relas == 2, ".rela.text has 2 relocations");
    TEST_ASSERT(relas[0].r_offset == 10, "Reloc 0 offset 10");
    TEST_ASSERT((relas[0].r_info & 0xFFFFFFFFULL) == ELF_R_X86_64_PC32, "Reloc 0 type R_X86_64_PC32");
    TEST_ASSERT(relas[0].r_addend == -4, "Reloc 0 addend -4");

    // Write file and verify with GNU readelf if available
    std::string elf_file = "test_linux64.o";
    TEST_ASSERT(GrassObjWriter::write_elf_obj_file(unit, elf_file, &err), "Write test_linux64.o");

    std::string readelf_cmd = "readelf -h -S -s -r " + elf_file + " > readelf_out.txt 2>&1";
    int readelf_res = std::system(readelf_cmd.c_str());
    if (readelf_res == 0) {
        {
            std::ifstream rf("readelf_out.txt");
            std::string line;
            bool saw_reloc = false;
            while (std::getline(rf, line)) {
                if (line.find("R_X86_64_PC32") != std::string::npos) {
                    saw_reloc = true;
                    break;
                }
            }
            TEST_ASSERT(saw_reloc, "GNU readelf recognized R_X86_64_PC32 relocation in generated ELF");
        }
        std::remove("readelf_out.txt");
    }
    std::remove(elf_file.c_str());

    // Verify unaligned text + BSS section layout with GNU readelf
    CompiledUnit bss_drift_unit;
    bss_drift_unit.arch = TargetArch::X86_64;
    bss_drift_unit.abi = TargetABI::SysV64;
    bss_drift_unit.text.resize(21, 0x90);
    bss_drift_unit.bss_size = 32;
    SymbolDef s_drift;
    s_drift.name = "sym_drift";
    s_drift.section = SectionKind::Text;
    s_drift.offset = 0;
    s_drift.size = 21;
    bss_drift_unit.symbols.push_back(s_drift);
    BinaryBuffer bss_drift_buf;
    TEST_ASSERT(GrassObjWriter::write_elf_obj(bss_drift_unit, bss_drift_buf, &err), "Write unaligned text + BSS ELF");
    std::string drift_file = "test_bss_drift.o";
    TEST_ASSERT(bss_drift_buf.write_to_file(drift_file), "Save test_bss_drift.o");
    int readelf_drift_res = std::system(("readelf -a " + drift_file + " > readelf_drift_out.txt 2>&1").c_str());
    if (readelf_drift_res == 0) {
        {
            std::ifstream rf("readelf_drift_out.txt");
            std::string content((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
            TEST_ASSERT(content.find("Error:") == std::string::npos, "readelf produced 0 errors on unaligned text + BSS");
        }
        std::remove("readelf_drift_out.txt");
    }
    std::remove(drift_file.c_str());
}

// =============================================================================
// Test 7: ELF32 Linux Object Writer & Parser
// =============================================================================
static void test_elf32_obj() {
    CompiledUnit unit = create_test_unit(TargetArch::X86_32, TargetABI::Cdecl32);

    BinaryBuffer buf;
    std::string err;
    TEST_ASSERT(GrassObjWriter::write_elf_obj(unit, buf, &err), "write_elf_obj 32-bit: " + err);

    const Elf32_Ehdr* ehdr = reinterpret_cast<const Elf32_Ehdr*>(buf.data());
    TEST_ASSERT(ehdr->e_ident[0] == 0x7F && ehdr->e_ident[1] == 'E', "ELF32 magic");
    TEST_ASSERT(ehdr->e_ident[4] == ELF_CLASS_32, "ELF32 class is 32-bit");
    TEST_ASSERT(ehdr->e_machine == ELF_EM_386, "ELF32 machine is EM_386 (3)");
    TEST_ASSERT(ehdr->e_ehsize == sizeof(Elf32_Ehdr), "ELF32 header size is 52 bytes");
    TEST_ASSERT(ehdr->e_shentsize == sizeof(Elf32_Shdr), "ELF32 section header size is 40 bytes");

    const Elf32_Shdr* shdrs32 = reinterpret_cast<const Elf32_Shdr*>(buf.data() + ehdr->e_shoff);
    uint32_t symtab32_idx = 0;
    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs32[i].sh_type == ELF_SHT_SYMTAB) symtab32_idx = i;
    }
    TEST_ASSERT(symtab32_idx > 0, "ELF32 has symtab");
    const Elf32_Sym* syms32 = reinterpret_cast<const Elf32_Sym*>(buf.data() + shdrs32[symtab32_idx].sh_offset);
    TEST_ASSERT((syms32[1].st_info & 0xF) == ELF_STT_FILE, "ELF32 symbol 1 is STT_FILE");
    TEST_ASSERT(syms32[1].st_shndx == ELF_SHN_ABS, "ELF32 STT_FILE shndx is SHN_ABS");
}

// =============================================================================
// Test 8: Flat Binary Generator for Bare-Metal RQWos
// =============================================================================
static void test_flat_binary_baremetal() {
    CompiledUnit unit;
    unit.arch = TargetArch::X86_64;
    unit.abi = TargetABI::Baremetal64;

    // Kernel code:
    // func1 at offset 0:
    // call func2 (offset 16) -> Rel32 at offset 1
    // mov rax, [rodata_var]  -> Abs64 at offset 6
    // ret
    // func2 at offset 16:
    // ret
    unit.text = {
        0xE8, 0x00, 0x00, 0x00, 0x00,                                 // 0: call func2 (disp32 at 1)
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 5: mov rax, imm64 (abs64 at 7)
        0xC3,                                                         // 15: ret
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,               // 16..23: func2 padding
        0xC3                                                          // 24: ret
    };

    unit.rodata = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22 };
    unit.data = { 0x00, 0x00, 0x00, 0x00 }; // 4 bytes
    unit.bss_size = 128;

    SymbolDef s_f1;
    s_f1.name = "kernel_entry";
    s_f1.section = SectionKind::Text;
    s_f1.offset = 0;
    s_f1.size = 16;
    unit.symbols.push_back(s_f1);

    SymbolDef s_f2;
    s_f2.name = "kernel_helper";
    s_f2.section = SectionKind::Text;
    s_f2.offset = 16;
    s_f2.size = 9;
    unit.symbols.push_back(s_f2);

    SymbolDef s_ro;
    s_ro.name = "kernel_config";
    s_ro.section = SectionKind::RoData;
    s_ro.offset = 0;
    s_ro.size = 8;
    unit.symbols.push_back(s_ro);

    RelocEntry r_call;
    r_call.section = SectionKind::Text;
    r_call.offset = 1;
    r_call.symbol_name = "kernel_helper";
    r_call.type = RelocType::Rel32;
    r_call.addend = -4; // standard x86 Rel32 addend
    unit.relocations.push_back(r_call);

    RelocEntry r_abs;
    r_abs.section = SectionKind::Text;
    r_abs.offset = 7;
    r_abs.symbol_name = "kernel_config";
    r_abs.type = RelocType::Abs64;
    r_abs.addend = 0;
    unit.relocations.push_back(r_abs);

    uint64_t base_addr = 0x100000; // 1 MB mark
    BinaryBuffer flat_buf;
    LinkMap lmap;
    std::string err;
    bool ok = GrassObjWriter::write_flat_binary(unit, base_addr, flat_buf, &lmap, &err);
    TEST_ASSERT(ok, "write_flat_binary should succeed: " + err);

    // Verify Rel32 resolved in flat binary:
    // Call site address = 0x100000 + 1. Next instruction = 0x100000 + 5.
    // Target address = 0x100000 + 16.
    // Expected disp32 = (0x100010) - (0x100005) = 11 = 0x0B.
    int32_t resolved_disp = flat_buf.read_i32(1);
    TEST_ASSERT(resolved_disp == 11, "Flat binary Rel32 relocation resolved correctly to 11");

    // Verify Abs64 resolved:
    // .text size = 25 bytes. Aligned to 16 bytes -> .rodata starts at 0x100000 + 32 = 0x100020.
    // Expected Abs64 = 0x100020.
    uint64_t resolved_abs = flat_buf.read_u64(7);
    TEST_ASSERT(resolved_abs == 0x100020, "Flat binary Abs64 relocation resolved correctly to 0x100020");

    // Check LinkMap
    std::string report = lmap.generate_link_map();
    TEST_ASSERT(report.find("RQWos Flat Binary Link Map") != std::string::npos, "Link map header present");
    TEST_ASSERT(report.find("kernel_entry") != std::string::npos, "Link map contains kernel_entry");
    TEST_ASSERT(report.find("0x0000000000100020") != std::string::npos, "Link map has rodata address");
    TEST_ASSERT(GrassObjWriter::generate_link_map(lmap) == report, "GrassObjWriter::generate_link_map matches LinkMap::generate_link_map");
}

// =============================================================================
// Test 9: Edge Cases and Error Paths
// =============================================================================
static void test_edge_cases() {
    // Empty compiled unit
    CompiledUnit empty_unit;
    BinaryBuffer buf_coff, buf_elf, buf_flat;
    std::string err;
    TEST_ASSERT(GrassObjWriter::write_coff_obj(empty_unit, buf_coff, &err), "Empty unit COFF write");
    TEST_ASSERT(GrassObjWriter::write_elf_obj(empty_unit, buf_elf, &err), "Empty unit ELF write");
    TEST_ASSERT(GrassObjWriter::write_flat_binary(empty_unit, 0x1000, buf_flat, nullptr, &err), "Empty unit flat binary write");

    // Symbol out of bounds
    CompiledUnit bad_unit;
    bad_unit.text = { 0x90 };
    SymbolDef bad_sym;
    bad_sym.name = "bad_sym";
    bad_sym.section = SectionKind::Text;
    bad_sym.offset = 100; // out of bounds
    bad_sym.size = 10;
    bad_unit.symbols.push_back(bad_sym);
    TEST_ASSERT(!GrassObjWriter::write_coff_obj(bad_unit, buf_coff, &err), "Symbol out of bounds rejected");

    // Relocation out of bounds
    CompiledUnit bad_reloc_unit;
    bad_reloc_unit.text = { 0x90 };
    RelocEntry bad_rel;
    bad_rel.section = SectionKind::Text;
    bad_rel.offset = 50; // out of bounds
    bad_rel.symbol_name = "sym";
    bad_reloc_unit.relocations.push_back(bad_rel);
    TEST_ASSERT(!GrassObjWriter::write_coff_obj(bad_reloc_unit, buf_coff, &err), "Relocation out of bounds rejected");

    // Flat binary unresolved external symbol
    CompiledUnit flat_unresolved;
    flat_unresolved.text = { 0xE8, 0x00, 0x00, 0x00, 0x00 };
    RelocEntry unres_rel;
    unres_rel.section = SectionKind::Text;
    unres_rel.offset = 1;
    unres_rel.symbol_name = "missing_symbol";
    flat_unresolved.relocations.push_back(unres_rel);
    TEST_ASSERT(!GrassObjWriter::write_flat_binary(flat_unresolved, 0x1000, buf_flat, nullptr, &err), "Unresolved symbol rejected in flat binary");

    // Relocation in BSS rejected
    CompiledUnit bss_reloc_unit;
    bss_reloc_unit.bss_size = 32;
    RelocEntry bss_rel;
    bss_rel.section = SectionKind::Bss;
    bss_rel.offset = 0;
    bss_rel.symbol_name = "sym";
    bss_reloc_unit.relocations.push_back(bss_rel);
    TEST_ASSERT(!GrassObjWriter::write_coff_obj(bss_reloc_unit, buf_coff, &err), "BSS relocation rejected");

    // Relocation width overflow rejected
    CompiledUnit overflow_reloc_unit;
    overflow_reloc_unit.text = { 0x90, 0x90 }; // 2 bytes
    RelocEntry of_rel;
    of_rel.section = SectionKind::Text;
    of_rel.offset = 0; // needs 4 bytes, only 2 available
    of_rel.symbol_name = "sym";
    of_rel.type = RelocType::Rel32;
    overflow_reloc_unit.relocations.push_back(of_rel);
    TEST_ASSERT(!GrassObjWriter::write_coff_obj(overflow_reloc_unit, buf_coff, &err), "Relocation width overflow rejected");

    // StackFrame with use_frame_pointer and RBP in callee_saved_regs
    ABISpec sysv = ABISpec::for_abi(TargetABI::SysV64);
    StackFrameRequest req_rbp;
    req_rbp.use_frame_pointer = true;
    req_rbp.callee_saved_regs = { Reg::RBP, Reg::RBX };
    StackFrameInfo sf_rbp = sysv.compute_stack_frame(req_rbp);
    TEST_ASSERT(sf_rbp.callee_saved_regs.size() == 1, "RBP filtered from callee_saved_regs when frame pointer used");
    BinaryBuffer pro_buf;
    sysv.emit_prologue_bytes(sf_rbp, pro_buf);
    int push_count = 0;
    for (size_t i = 0; i < pro_buf.size(); ++i) {
        if (pro_buf.read_u8(i) == 0x55) ++push_count;
    }
    TEST_ASSERT(push_count == 1, "push rbp emitted exactly once in prologue");

    // StringTableBuilder COFF / ELF isolation
    StringTableBuilder stb_iso;
    stb_iso.add_coff("only_coff");
    stb_iso.add_elf("only_elf");
    BinaryBuffer c_buf, e_buf;
    stb_iso.build_coff(c_buf);
    stb_iso.build_elf(e_buf);
    TEST_ASSERT(c_buf.size() == stb_iso.total_bytes_coff(), "COFF string table build matches total_bytes_coff");
    TEST_ASSERT(e_buf.size() == stb_iso.total_bytes_elf(), "ELF string table build matches total_bytes_elf");
}

int main() {
    std::cout << "======================================================================\n";
    std::cout << "  GrassGreen Container & Object Writer Test Suite\n";
    std::cout << "======================================================================\n";

    run_test("test_binary_buffer", test_binary_buffer);
    run_test("test_string_table_builder", test_string_table_builder);
    run_test("test_abi_spec", test_abi_spec);
    run_test("test_coff_win64_obj", test_coff_win64_obj);
    run_test("test_coff_win32_obj", test_coff_win32_obj);
    run_test("test_elf64_obj", test_elf64_obj);
    run_test("test_elf32_obj", test_elf32_obj);
    run_test("test_flat_binary_baremetal", test_flat_binary_baremetal);
    run_test("test_edge_cases", test_edge_cases);

    std::cout << "======================================================================\n";
    std::cout << "  Summary: " << g_tests_passed << "/" << g_tests_total << " test groups passed.\n";
    std::cout << "======================================================================\n";

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}

#include "../src/GrassNativeDebug.h"
#include "../src/GrassX86Encoder.h"
#include "../src/GrassNativeCodegen.h"
#include "../src/GrassIR.h"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>

// Global VM stubs required for standalone test linking
GrassArena g_vm_arena(1024 * 1024);
std::unordered_map<std::string, GrassStructDef> g_struct_registry;

namespace GrassKeyboard {
    void set_layout(int) {}
    void set_layout_by_name(const std::string&) {}
}

namespace GrassHAL {
    bool is_freestanding() { return false; }
    bool vfs_exists(const std::string&) { return false; }
    std::string vfs_read(const std::string&) { return ""; }
}

using namespace grass::debug;
using namespace grass::backend;

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            std::cout << "[PASS] " << msg << "\n"; \
            g_tests_passed++; \
        } else { \
            std::cerr << "[FAIL] " << msg << " (" << #cond << ")\n"; \
            g_tests_failed++; \
        } \
    } while (0)

void test_debug_table_builder() {
    std::cout << "--- Running test_debug_table_builder ---\n";
    GrassDebugTableBuilder builder;
    builder.set_text_bounds(0x1000, 0x500);

    builder.add_entry(0x00, 0x20, "main.grass", "main", 1, 1);
    builder.add_entry(0x20, 0x45, "main.grass", "main", 2, 5);
    builder.add_entry(0x45, 0x80, "math_helper.grass", "compute", 15, 9);
    builder.add_entry(0x80, 0x120, "math_helper.grass", "compute", 16, 12);

    std::vector<uint8_t> data = builder.serialize();
    TEST_ASSERT(!data.empty(), "Serialized table is not empty");
    TEST_ASSERT(data.size() >= sizeof(GrassDebugHeader), "Table size >= header");

    const auto* hdr = reinterpret_cast<const GrassDebugHeader*>(data.data());
    TEST_ASSERT(hdr->magic == GRASS_DEBUG_MAGIC, "Header magic matches GRDB");
    TEST_ASSERT(hdr->version == GRASS_DEBUG_VERSION, "Header version matches 1");
    TEST_ASSERT(hdr->entry_count == 4, "Entry count is 4");
    TEST_ASSERT(hdr->text_base_rva == 0x1000, "Base RVA is 0x1000");
    TEST_ASSERT(hdr->text_size == 0x500, "Text size is 0x500");

    // Lookup within range
    uint64_t fake_module_base = 0x140000000ULL;

    GrassDebugLocation loc1;
    bool found1 = lookup_debug_location(data.data(), data.size(), fake_module_base + 0x1010, fake_module_base, loc1);
    TEST_ASSERT(found1 && loc1.found, "Found RIP in range [0x00, 0x20)");
    TEST_ASSERT(loc1.file == "main.grass", "File is main.grass");
    TEST_ASSERT(loc1.function == "main", "Function is main");
    TEST_ASSERT(loc1.line == 1 && loc1.col == 1, "Line 1, Col 1");

    GrassDebugLocation loc2;
    bool found2 = lookup_debug_location(data.data(), data.size(), fake_module_base + 0x1030, fake_module_base, loc2);
    TEST_ASSERT(found2 && loc2.found, "Found RIP in range [0x20, 0x45)");
    TEST_ASSERT(loc2.line == 2, "Line is 2");

    GrassDebugLocation loc3;
    bool found3 = lookup_debug_location(data.data(), data.size(), fake_module_base + 0x1050, fake_module_base, loc3);
    TEST_ASSERT(found3 && loc3.found, "Found RIP in compute function");
    TEST_ASSERT(loc3.file == "math_helper.grass", "File is math_helper.grass");
    TEST_ASSERT(loc3.function == "compute", "Function is compute");
    TEST_ASSERT(loc3.line == 15, "Line is 15");

    // Out of bounds lookup
    GrassDebugLocation loc_out;
    bool found_out = lookup_debug_location(data.data(), data.size(), fake_module_base + 0x2000, fake_module_base, loc_out);
    TEST_ASSERT(!found_out, "Out of bounds RIP not found");
}

void test_baremetal_intrinsics_encoding() {
    std::cout << "--- Running test_baremetal_intrinsics_encoding ---\n";
    GrassX86Encoder enc(TargetArch::X86_64);

    enc.in_al_dx();
    enc.out_dx_al();
    enc.cli();
    enc.sti();
    enc.hlt();

    const auto& code = enc.code();
    TEST_ASSERT(code.size() == 5, "Encoded 5 single-byte instructions");
    TEST_ASSERT(code[0] == 0xEC, "in al, dx is 0xEC");
    TEST_ASSERT(code[1] == 0xEE, "out dx, al is 0xEE");
    TEST_ASSERT(code[2] == 0xFA, "cli is 0xFA");
    TEST_ASSERT(code[3] == 0xFB, "sti is 0xFB");
    TEST_ASSERT(code[4] == 0xF4, "hlt is 0xF4");
}

void test_xmm_allocation_and_codegen() {
    std::cout << "--- Running test_xmm_allocation_and_codegen ---\n";

    grass::codegen::NativeCodegenOptions opts;
    opts.arch = TargetArch::X86_64;
    opts.abi = TargetABI::Win64;
    opts.opt_level = 0;
    opts.freestanding = true;

    grass::codegen::NativeCompiler compiler(opts);

    // Code with multiple float calculations to pressure XMM registers
    std::string src = R"(
float test_xmm_pressure(float a, float b, float c, float d) {
    float x1 = a + b;
    float x2 = b * c;
    float x3 = c - d;
    float x4 = d / a;
    float x5 = x1 + x2;
    float x6 = x3 * x4;
    float x7 = x5 - x6;
    float x8 = x7 + a;
    float x9 = x8 * b;
    float x10 = x9 - c;
    float x11 = x10 / d;
    float x12 = x11 + x1;
    float x13 = x12 - x2;
    float x14 = x13 * x3;
    float x15 = x14 + x4;
    float x16 = x15 - x5;
    return x16;
}
)";

    auto res = compiler.compile_source(src, "xmm_test.grass");
    if (!res.success) {
        std::cerr << "Compile error in test_xmm_pressure: " << res.error_message << "\n";
    }
    TEST_ASSERT(res.success, "Compilation of heavy float expression succeeded");
    TEST_ASSERT(!res.compiled_unit.text.empty(), "Generated non-empty machine code");

    // Verify debug table was attached to rodata
    bool has_debug_table_symbol = false;
    for (const auto& sym : res.compiled_unit.symbols) {
        if (sym.name == "__grass_debug_table") {
            has_debug_table_symbol = true;
            break;
        }
    }
    TEST_ASSERT(has_debug_table_symbol, "__grass_debug_table registered in unit symbols");
}

void test_baremetal_intrinsics_lowering() {
    std::cout << "--- Running test_baremetal_intrinsics_lowering ---\n";

    grass::codegen::NativeCodegenOptions opts;
    opts.arch = TargetArch::X86_64;
    opts.abi = TargetABI::Win64;
    opts.freestanding = true;

    grass::codegen::NativeCompiler compiler(opts);

    std::string src = R"(
int kernel_main() {
    cpu_cli();
    port_out8(0x3F8, 65);
    int status = port_in8(0x3FD);
    cpu_sti();
    cpu_hlt();
    return status;
}
)";

    auto res = compiler.compile_source(src, "baremetal_test.grass");
    if (!res.success) {
        std::cerr << "Compile error in baremetal_test: " << res.error_message << "\n";
    }
    TEST_ASSERT(res.success, "Baremetal intrinsics compiled successfully");
    if (!res.success) return;
    TEST_ASSERT(!res.compiled_unit.text.empty(), "Machine code emitted for intrinsics");

    // Scan generated machine code for intrinsic opcodes: 0xFA (cli), 0xEE (out dx, al), 0xEC (in al, dx), 0xFB (sti), 0xF4 (hlt)
    const auto& text = res.compiled_unit.text;
    bool found_cli = false;
    bool found_out = false;
    bool found_in = false;
    bool found_sti = false;
    bool found_hlt = false;

    for (uint8_t b : text) {
        if (b == 0xFA) found_cli = true;
        if (b == 0xEE) found_out = true;
        if (b == 0xEC) found_in = true;
        if (b == 0xFB) found_sti = true;
        if (b == 0xF4) found_hlt = true;
    }

    TEST_ASSERT(found_cli, "Found 0xFA (cli) in emitted text");
    TEST_ASSERT(found_out, "Found 0xEE (out dx, al) in emitted text");
    TEST_ASSERT(found_in, "Found 0xEC (in al, dx) in emitted text");
    TEST_ASSERT(found_sti, "Found 0xFB (sti) in emitted text");
    TEST_ASSERT(found_hlt, "Found 0xF4 (hlt) in emitted text");
}

int main() {
    std::cout << "===========================================\n";
    std::cout << "GrassNativeDebug & Stage 1 Verification\n";
    std::cout << "===========================================\n";

    test_debug_table_builder();
    test_baremetal_intrinsics_encoding();
    test_xmm_allocation_and_codegen();
    test_baremetal_intrinsics_lowering();

    std::cout << "===========================================\n";
    std::cout << "Results: " << g_tests_passed << " passed, " << g_tests_failed << " failed.\n";
    std::cout << "===========================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}

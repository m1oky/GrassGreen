#include "native/GrassX86Encoder.h"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void* allocate_exec_mem(size_t size) {
    void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!ptr) {
        throw std::runtime_error("VirtualAlloc failed");
    }
    return ptr;
}

static void free_exec_mem(void* ptr, size_t /*size*/) {
    if (ptr) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
}
#else
#include <sys/mman.h>
#include <unistd.h>

static void* allocate_exec_mem(size_t size) {
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED) {
        throw std::runtime_error("mmap failed");
    }
    return ptr;
}

static void free_exec_mem(void* ptr, size_t size) {
    if (ptr) {
        munmap(ptr, size);
    }
}
#endif

using namespace grass::backend;

static int g_tests_passed = 0;
static int g_tests_failed = 0;

static void check_bytes(const std::string& test_name, const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected) {
    if (actual == expected) {
        std::cout << "[PASS] " << test_name << "\n";
        g_tests_passed++;
    } else {
        std::cerr << "[FAIL] " << test_name << "\n";
        std::cerr << "  Expected: ";
        for (uint8_t b : expected) std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
        std::cerr << "\n  Actual:   ";
        for (uint8_t b : actual) std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
        std::cerr << std::dec << "\n";
        g_tests_failed++;
    }
}

static void check_bool(const std::string& test_name, bool condition) {
    if (condition) {
        std::cout << "[PASS] " << test_name << "\n";
        g_tests_passed++;
    } else {
        std::cerr << "[FAIL] " << test_name << "\n";
        g_tests_failed++;
    }
}

// =============================================================================
// Part 1: ModR/M & SIB Bitwise Reference Validation
// =============================================================================
static void test_bitwise_encoding() {
    std::cout << "\n=== PART 1: ModR/M & SIB Reference Encoding Tests ===\n";

    // 1. mov rax, [rax] -> 48 8B 00
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RAX));
        check_bytes("mov rax, [rax]", enc.code(), { 0x48, 0x8B, 0x00 });
    }

    // 2. mov rax, [rsp] -> 48 8B 04 24 (RSP base requires SIB=0x24)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RSP));
        check_bytes("mov rax, [rsp] (SIB 0x24)", enc.code(), { 0x48, 0x8B, 0x04, 0x24 });
    }

    // 3. mov rax, [r12] -> 49 8B 04 24 (R12 base requires SIB=0x24, REX.B=1)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::R12));
        check_bytes("mov rax, [r12] (SIB 0x24, REX.B=1)", enc.code(), { 0x49, 0x8B, 0x04, 0x24 });
    }

    // 4. mov rax, [rbp] -> 48 8B 45 00 (RBP base disp=0 requires mod=01, disp8=00)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RBP));
        check_bytes("mov rax, [rbp] (mod=01, disp8=0x00)", enc.code(), { 0x48, 0x8B, 0x45, 0x00 });
    }

    // 5. mov rax, [r13] -> 49 8B 45 00 (R13 base disp=0 requires mod=01, disp8=00, REX.B=1)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::R13));
        check_bytes("mov rax, [r13] (mod=01, disp8=0x00, REX.B=1)", enc.code(), { 0x49, 0x8B, 0x45, 0x00 });
    }

    // 6. mov rax, [rsp + 16] -> 48 8B 44 24 10 (disp8 with RSP base)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RSP, 16));
        check_bytes("mov rax, [rsp + 16]", enc.code(), { 0x48, 0x8B, 0x44, 0x24, 0x10 });
    }

    // 7. mov rax, [rsp + 0x123456] -> 48 8B 84 24 56 34 12 00 (disp32 with RSP base)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RSP, 0x123456));
        check_bytes("mov rax, [rsp + 0x123456]", enc.code(), { 0x48, 0x8B, 0x84, 0x24, 0x56, 0x34, 0x12, 0x00 });
    }

    // 8. mov rax, [rbp - 8] -> 48 8B 45 F8
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RBP, -8));
        check_bytes("mov rax, [rbp - 8]", enc.code(), { 0x48, 0x8B, 0x45, 0xF8 });
    }

    // 9. mov rax, [r13 + 12] -> 49 8B 45 0C
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::R13, 12));
        check_bytes("mov rax, [r13 + 12]", enc.code(), { 0x49, 0x8B, 0x45, 0x0C });
    }

    // 10. mov rax, [rcx + rdx*8] -> 48 8B 04 D1
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RCX, Reg::RDX, 8, 0));
        check_bytes("mov rax, [rcx + rdx*8]", enc.code(), { 0x48, 0x8B, 0x04, 0xD1 });
    }

    // 11. mov rax, [rcx + rdx*4 + 32] -> 48 8B 44 91 20
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RCX, Reg::RDX, 4, 32));
        check_bytes("mov rax, [rcx + rdx*4 + 32]", enc.code(), { 0x48, 0x8B, 0x44, 0x91, 0x20 });
    }

    // 12. mov r11, [r12 + r13*2 + 0x1000] -> 4F 8B 9C 6C 00 10 00 00
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::R11, mem(Reg::R12, Reg::R13, 2, 0x1000));
        check_bytes("mov r11, [r12 + r13*2 + 0x1000]", enc.code(), { 0x4F, 0x8B, 0x9C, 0x6C, 0x00, 0x10, 0x00, 0x00 });
    }

    // 13. mov spl, dil -> 40 88 FC (Mandatory empty REX 0x40 for SPL/DIL)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rr(Reg::SPL, Reg::DIL);
        check_bytes("mov spl, dil (REX 0x40)", enc.code(), { 0x40, 0x88, 0xFC });
    }

    // 14. mov al, bl -> 88 D8 (No REX prefix needed)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rr(Reg::AL, Reg::BL);
        check_bytes("mov al, bl (no REX)", enc.code(), { 0x88, 0xD8 });
    }

    // 15. mov r8b, r9b -> 45 88 C8
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rr(Reg::R8B, Reg::R9B);
        check_bytes("mov r8b, r9b (REX.R + REX.B)", enc.code(), { 0x45, 0x88, 0xC8 });
    }

    // 16. mov r10w, r11w -> 66 45 89 DA (16-bit prefix 0x66 + REX)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rr(Reg::R10W, Reg::R11W);
        check_bytes("mov r10w, r11w", enc.code(), { 0x66, 0x45, 0x89, 0xDA });
    }

    // 17. mov r10d, r11d -> 45 89 DA
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rr(Reg::R10D, Reg::R11D);
        check_bytes("mov r10d, r11d", enc.code(), { 0x45, 0x89, 0xDA });
    }

    // 18. mov r10, r11 -> 4D 89 DA
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rr(Reg::R10, Reg::R11);
        check_bytes("mov r10, r11", enc.code(), { 0x4D, 0x89, 0xDA });
    }

    // 19. mov [rsp], rax -> 48 89 04 24
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_mr(mem(Reg::RSP), Reg::RAX);
        check_bytes("mov [rsp], rax", enc.code(), { 0x48, 0x89, 0x04, 0x24 });
    }

    // 20. mov [rbp], rax -> 48 89 45 00
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_mr(mem(Reg::RBP), Reg::RAX);
        check_bytes("mov [rbp], rax", enc.code(), { 0x48, 0x89, 0x45, 0x00 });
    }

    // 21. mov [r12], rax -> 49 89 04 24
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_mr(mem(Reg::R12), Reg::RAX);
        check_bytes("mov [r12], rax", enc.code(), { 0x49, 0x89, 0x04, 0x24 });
    }

    // 22. mov [r13], rax -> 49 89 45 00
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_mr(mem(Reg::R13), Reg::RAX);
        check_bytes("mov [r13], rax", enc.code(), { 0x49, 0x89, 0x45, 0x00 });
    }

    // 23. add rax, rbx -> 48 01 D8
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.add_rr(Reg::RAX, Reg::RBX);
        check_bytes("add rax, rbx", enc.code(), { 0x48, 0x01, 0xD8 });
    }

    // 24. sub rax, 42 -> 48 83 E8 2A
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.sub_ri(Reg::RAX, 42);
        check_bytes("sub rax, 42", enc.code(), { 0x48, 0x83, 0xE8, 0x2A });
    }

    // 25. xor rax, rax -> 48 31 C0
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.xor_rr(Reg::RAX, Reg::RAX);
        check_bytes("xor rax, rax", enc.code(), { 0x48, 0x31, 0xC0 });
    }

    // 26. imul rax, rbx -> 48 0F AF C3
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.imul_rr(Reg::RAX, Reg::RBX);
        check_bytes("imul rax, rbx", enc.code(), { 0x48, 0x0F, 0xAF, 0xC3 });
    }

    // 27. cqo -> 48 99
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.cqo();
        check_bytes("cqo", enc.code(), { 0x48, 0x99 });
    }

    // 28. cdq -> 99
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.cdq();
        check_bytes("cdq", enc.code(), { 0x99 });
    }

    // 29. ret -> C3
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.ret();
        check_bytes("ret", enc.code(), { 0xC3 });
    }

    // 30. nop -> 90
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.nop();
        check_bytes("nop", enc.code(), { 0x90 });
    }

    // 31. ud2 -> 0F 0B
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.ud2();
        check_bytes("ud2", enc.code(), { 0x0F, 0x0B });
    }

    // 32. syscall -> 0F 05
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.syscall();
        check_bytes("syscall", enc.code(), { 0x0F, 0x05 });
    }

    // 33. cpuid -> 0F A2
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.cpuid();
        check_bytes("cpuid", enc.code(), { 0x0F, 0xA2 });
    }

    // 34. rdtsc -> 0F 31
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.rdtsc();
        check_bytes("rdtsc", enc.code(), { 0x0F, 0x31 });
    }

    // 35. mfence -> 0F AE F0
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mfence();
        check_bytes("mfence", enc.code(), { 0x0F, 0xAE, 0xF0 });
    }

    // 36. lfence -> 0F AE E8
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.lfence();
        check_bytes("lfence", enc.code(), { 0x0F, 0xAE, 0xE8 });
    }

    // 37. sfence -> 0F AE F8
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.sfence();
        check_bytes("sfence", enc.code(), { 0x0F, 0xAE, 0xF8 });
    }

    // 38. pause -> F3 90
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.pause();
        check_bytes("pause", enc.code(), { 0xF3, 0x90 });
    }

    // 39. lock xadd [rcx], rax -> F0 48 0F C1 01
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.lock_xadd_mr(mem(Reg::RCX), Reg::RAX);
        check_bytes("lock xadd [rcx], rax", enc.code(), { 0xF0, 0x48, 0x0F, 0xC1, 0x01 });
    }

    // 40. lock cmpxchg [rcx], rbx -> F0 48 0F B1 19
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.lock_cmpxchg_mr(mem(Reg::RCX), Reg::RBX);
        check_bytes("lock cmpxchg [rcx], rbx", enc.code(), { 0xF0, 0x48, 0x0F, 0xB1, 0x19 });
    }

    // 41. movsd xmm0, xmm1 -> F2 0F 10 C1
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movsd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("movsd xmm0, xmm1", enc.code(), { 0xF2, 0x0F, 0x10, 0xC1 });
    }

    // 42. addsd xmm0, xmm1 -> F2 0F 58 C1
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.addsd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("addsd xmm0, xmm1", enc.code(), { 0xF2, 0x0F, 0x58, 0xC1 });
    }

    // 43. subsd xmm0, xmm1 -> F2 0F 5C C1
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.subsd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("subsd xmm0, xmm1", enc.code(), { 0xF2, 0x0F, 0x5C, 0xC1 });
    }

    // 44. mulsd xmm0, xmm1 -> F2 0F 59 C1
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mulsd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("mulsd xmm0, xmm1", enc.code(), { 0xF2, 0x0F, 0x59, 0xC1 });
    }

    // 45. divsd xmm0, xmm1 -> F2 0F 5E C1
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.divsd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("divsd xmm0, xmm1", enc.code(), { 0xF2, 0x0F, 0x5E, 0xC1 });
    }

    // 46. sqrtsd xmm0, xmm1 -> F2 0F 51 C1
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.sqrtsd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("sqrtsd xmm0, xmm1", enc.code(), { 0xF2, 0x0F, 0x51, 0xC1 });
    }

    // 47. cvtsi2sd xmm0, rax -> F2 48 0F 2A C0
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.cvtsi2sd_xr(Reg::XMM0, Reg::RAX);
        check_bytes("cvtsi2sd xmm0, rax", enc.code(), { 0xF2, 0x48, 0x0F, 0x2A, 0xC0 });
    }

    // 48. cvttsd2si rax, xmm0 -> F2 48 0F 2C C0
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.cvttsd2si_rx(Reg::RAX, Reg::XMM0);
        check_bytes("cvttsd2si rax, xmm0", enc.code(), { 0xF2, 0x48, 0x0F, 0x2C, 0xC0 });
    }

    // 49. xorpd xmm0, xmm0 -> 66 0F 57 C0
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.xorpd_xx(Reg::XMM0, Reg::XMM0);
        check_bytes("xorpd xmm0, xmm0", enc.code(), { 0x66, 0x0F, 0x57, 0xC0 });
    }

    // 50. pxor xmm0, xmm0 -> 66 0F EF C0
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.pxor_xx(Reg::XMM0, Reg::XMM0);
        check_bytes("pxor xmm0, xmm0", enc.code(), { 0x66, 0x0F, 0xEF, 0xC0 });
    }

    // 51. mov rax, [rcx + 0] -> 48 8B 01
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RCX, 0));
        check_bytes("mov rax, [rcx + 0] (disp=0, mod=00)", enc.code(), { 0x48, 0x8B, 0x01 });
    }

    // 52. mov rax, [r13 + r12*4 + 0] -> 4B 8B 44 A5 00 (R13 base with index requires disp8=00)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::R13, Reg::R12, 4, 0));
        check_bytes("mov rax, [r13 + r12*4 + 0]", enc.code(), { 0x4B, 0x8B, 0x44, 0xA5, 0x00 });
    }

    // 53. RSP cannot be used as an index register
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        bool caught = false;
        try {
            enc.mov_rm(Reg::RAX, mem(Reg::RCX, Reg::RSP, 1, 0));
        } catch (const std::invalid_argument&) {
            caught = true;
        }
        check_bool("RSP as index register throws std::invalid_argument", caught);
    }

    // 54. CMOVcc and SETcc bitwise check
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.cmov_cc_rr(CondCode::E, Reg::RAX, Reg::RBX);  // 48 0F 44 C3
        enc.cmov_cc_rr(CondCode::NE, Reg::RAX, Reg::RBX); // 48 0F 45 C3
        enc.cmov_cc_rr(CondCode::L, Reg::RAX, Reg::RBX);  // 48 0F 4C C3
        enc.cmov_cc_rr(CondCode::GE, Reg::RAX, Reg::RBX); // 48 0F 4D C3
        enc.setcc_r(CondCode::E, Reg::AL);                // 0F 94 C0
        enc.setcc_r(CondCode::NE, Reg::DIL);              // 40 0F 95 C7 (REX for DIL)
        enc.setcc_r(CondCode::G, Reg::R8B);               // 41 0F 9F C0 (REX.B for R8B)
        check_bytes("cmovcc & setcc opcodes", enc.code(), {
            0x48, 0x0F, 0x44, 0xC3,
            0x48, 0x0F, 0x45, 0xC3,
            0x48, 0x0F, 0x4C, 0xC3,
            0x48, 0x0F, 0x4D, 0xC3,
            0x0F, 0x94, 0xC0,
            0x40, 0x0F, 0x95, 0xC7,
            0x41, 0x0F, 0x9F, 0xC0
        });
    }

    // 55. Shift and rotate operations bitwise check
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.shl_ri(Reg::RAX, 1);  // 48 D1 E0
        enc.shl_ri(Reg::RAX, 4);  // 48 C1 E0 04
        enc.shl_rcl(Reg::RAX);    // 48 D3 E0
        enc.shr_ri(Reg::RAX, 2);  // 48 C1 E8 02
        enc.sar_ri(Reg::RAX, 3);  // 48 C1 F8 03
        enc.rol_ri(Reg::RAX, 1);  // 48 D1 C0
        enc.ror_ri(Reg::RAX, 5);  // 48 C1 C8 05
        check_bytes("Shift and rotate opcodes", enc.code(), {
            0x48, 0xD1, 0xE0,
            0x48, 0xC1, 0xE0, 0x04,
            0x48, 0xD3, 0xE0,
            0x48, 0xC1, 0xE8, 0x02,
            0x48, 0xC1, 0xF8, 0x03,
            0x48, 0xD1, 0xC0,
            0x48, 0xC1, 0xC8, 0x05
        });
    }

    // 56. Bit manipulation: bsf, bsr, popcnt, bswap
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.bsf_rr(Reg::RAX, Reg::RBX);    // 48 0F BC C3
        enc.bsr_rr(Reg::RAX, Reg::RBX);    // 48 0F BD C3
        enc.popcnt_rr(Reg::RAX, Reg::RBX); // F3 48 0F B8 C3
        enc.bswap_r(Reg::RAX);             // 48 0F C8
        enc.bswap_r(Reg::R8D);             // 41 0F C8
        check_bytes("bsf, bsr, popcnt, bswap", enc.code(), {
            0x48, 0x0F, 0xBC, 0xC3,
            0x48, 0x0F, 0xBD, 0xC3,
            0xF3, 0x48, 0x0F, 0xB8, 0xC3,
            0x48, 0x0F, 0xC8,
            0x41, 0x0F, 0xC8
        });
    }

    // 57. Single-precision SSE instructions
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movss_xx(Reg::XMM0, Reg::XMM1); // F3 0F 10 C1
        enc.addss_xx(Reg::XMM0, Reg::XMM1); // F3 0F 58 C1
        enc.subss_xx(Reg::XMM0, Reg::XMM1); // F3 0F 5C C1
        enc.mulss_xx(Reg::XMM0, Reg::XMM1); // F3 0F 59 C1
        enc.divss_xx(Reg::XMM0, Reg::XMM1); // F3 0F 5E C1
        enc.sqrtss_xx(Reg::XMM0, Reg::XMM1);// F3 0F 51 C1
        check_bytes("Single-precision float SSE", enc.code(), {
            0xF3, 0x0F, 0x10, 0xC1,
            0xF3, 0x0F, 0x58, 0xC1,
            0xF3, 0x0F, 0x5C, 0xC1,
            0xF3, 0x0F, 0x59, 0xC1,
            0xF3, 0x0F, 0x5E, 0xC1,
            0xF3, 0x0F, 0x51, 0xC1
        });
    }

    // 58. All 16 Jcc conditions
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        LabelId target = enc.create_label();
        enc.bind_label(target);
        enc.jcc_short_label(CondCode::O, target);   // 70 FE
        enc.jcc_short_label(CondCode::NO, target);  // 71 FC
        enc.jcc_short_label(CondCode::B, target);   // 72 FA
        enc.jcc_short_label(CondCode::AE, target);  // 73 F8
        enc.jcc_short_label(CondCode::E, target);   // 74 F6
        enc.jcc_short_label(CondCode::NE, target);  // 75 F4
        enc.jcc_short_label(CondCode::BE, target);  // 76 F2
        enc.jcc_short_label(CondCode::A, target);   // 77 F0
        enc.jcc_short_label(CondCode::S, target);   // 78 EE
        enc.jcc_short_label(CondCode::NS, target);  // 79 EC
        enc.jcc_short_label(CondCode::P, target);   // 7A EA
        enc.jcc_short_label(CondCode::NP, target);  // 7B E8
        enc.jcc_short_label(CondCode::L, target);   // 7C E6
        enc.jcc_short_label(CondCode::GE, target);  // 7D E4
        enc.jcc_short_label(CondCode::LE, target);  // 7E E2
        enc.jcc_short_label(CondCode::G, target);   // 7F E0
        check_bytes("All 16 Jcc condition codes", enc.code(), {
            0x70, 0xFE, 0x71, 0xFC, 0x72, 0xFA, 0x73, 0xF8,
            0x74, 0xF6, 0x75, 0xF4, 0x76, 0xF2, 0x77, 0xF0,
            0x78, 0xEE, 0x79, 0xEC, 0x7A, 0xEA, 0x7B, 0xE8,
            0x7C, 0xE6, 0x7D, 0xE4, 0x7E, 0xE2, 0x7F, 0xE0
        });
    }

    // 59. Backward RIP-relative label addressing
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        LabelId lbl = enc.create_label();
        enc.bind_label(lbl);
        enc.nop();
        enc.nop();
        enc.nop();
        enc.mov_rm(Reg::RAX, MemOp::rip_rel(lbl));
        // lbl at 0. 3 nops at 0, 1, 2. mov_rm starts at 3: 48 8B 05 [rel32].
        // next_ip = 3 + 7 = 10. target = 0. rel = 0 - 10 = -10 = 0xFFFFFFF6.
        check_bytes("Backward RIP-relative label", enc.code(), {
            0x90, 0x90, 0x90,
            0x48, 0x8B, 0x05, 0xF6, 0xFF, 0xFF, 0xFF
        });
    }

    // 60. Forward RIP-relative label with displacement
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        LabelId fwd = enc.create_label();
        enc.mov_rm(Reg::RAX, MemOp::rip_rel(fwd, 8));
        enc.bind_label(fwd);
        // mov_rm is 7 bytes (48 8B 05 [rel32]).
        // next_ip = 7. fwd at 7. target = 7 + 8 = 15. rel = 15 - 7 = 8.
        check_bytes("Forward RIP-relative label with disp", enc.code(), {
            0x48, 0x8B, 0x05, 0x08, 0x00, 0x00, 0x00
        });
    }

    // 61. Explicit Reg::RIP base addressing & validations
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mov_rm(Reg::RAX, mem(Reg::RIP, 12));
        check_bytes("mov rax, [rip + 12]", enc.code(), { 0x48, 0x8B, 0x05, 0x0C, 0x00, 0x00, 0x00 });

        bool c1 = false;
        try {
            GrassX86Encoder enc32(TargetArch::X86_32);
            enc32.mov_rm(Reg::EAX, mem(Reg::RIP, 12));
        } catch (const std::invalid_argument&) {
            c1 = true;
        }
        check_bool("mem(Reg::RIP) in 32-bit mode throws invalid_argument", c1);

        bool c2 = false;
        try {
            enc.mov_rm(Reg::RAX, mem(Reg::RIP, Reg::RAX, 1, 0));
        } catch (const std::invalid_argument&) {
            c2 = true;
        }
        check_bool("mem(Reg::RIP) with index register is illegal", c2);
    }

    // 62. High-byte registers (AH/CH/DH/BH) cannot be encoded with REX prefix
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        bool c1 = false;
        try {
            enc.movzx_rr8(Reg::RAX, Reg::AH);
        } catch (const std::invalid_argument&) {
            c1 = true;
        }
        check_bool("movzx rax, ah throws invalid_argument (AH cannot be encoded with REX)", c1);

        bool c2 = false;
        try {
            enc.mov_mr(mem(Reg::R8), Reg::AH);
        } catch (const std::invalid_argument&) {
            c2 = true;
        }
        check_bool("mov [r8], ah throws invalid_argument", c2);

        bool c3 = false;
        try {
            enc.mov_rr(Reg::AH, Reg::R8B);
        } catch (const std::invalid_argument&) {
            c3 = true;
        }
        check_bool("mov ah, r8b throws invalid_argument", c3);
    }

    // 63. Destination register size checks
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        bool c1 = false;
        try {
            enc.movzx_rr8(Reg::AL, Reg::BL);
        } catch (const std::invalid_argument&) {
            c1 = true;
        }
        check_bool("movzx al, bl throws invalid_argument", c1);

        bool c2 = false;
        try {
            enc.lea_rm(Reg::AL, mem(Reg::RAX));
        } catch (const std::invalid_argument&) {
            c2 = true;
        }
        check_bool("lea al, [rax] throws invalid_argument", c2);

        bool c3 = false;
        try {
            enc.call_r(Reg::EAX);
        } catch (const std::invalid_argument&) {
            c3 = true;
        }
        check_bool("call eax in 64-bit mode throws invalid_argument", c3);

        bool c4 = false;
        try {
            enc.jmp_r(Reg::AX);
        } catch (const std::invalid_argument&) {
            c4 = true;
        }
        check_bool("jmp ax in 64-bit mode throws invalid_argument", c4);
    }

    // 64. SSE instructions require XMM registers
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        bool c1 = false;
        try {
            enc.addsd_xx(Reg::RAX, Reg::XMM0);
        } catch (const std::invalid_argument&) {
            c1 = true;
        }
        check_bool("addsd rax, xmm0 throws invalid_argument", c1);

        bool c2 = false;
        try {
            enc.movss_xx(Reg::XMM0, Reg::RDX);
        } catch (const std::invalid_argument&) {
            c2 = true;
        }
        check_bool("movss xmm0, rdx throws invalid_argument", c2);
    }
}

// =============================================================================
// Part 2: 32-bit IA-32 TargetArch Validation
// =============================================================================
static void test_x86_32_encoding() {
    std::cout << "\n=== PART 2: 32-bit x86 (IA-32) TargetArch Tests ===\n";

    // 1. mov eax, ebx -> 89 D8 (No REX prefix in 32-bit mode)
    {
        GrassX86Encoder enc(TargetArch::X86_32);
        enc.mov_rr(Reg::EAX, Reg::EBX);
        check_bytes("32-bit mov eax, ebx", enc.code(), { 0x89, 0xD8 });
    }

    // 2. add eax, [ecx + edx*4 + 16] -> 03 44 91 10
    {
        GrassX86Encoder enc(TargetArch::X86_32);
        enc.add_rm(Reg::EAX, mem(Reg::ECX, Reg::EDX, 4, 16));
        check_bytes("32-bit add eax, [ecx + edx*4 + 16]", enc.code(), { 0x03, 0x44, 0x91, 0x10 });
    }

    // 3. push eax, pop ebx, ret -> 50 5B C3
    {
        GrassX86Encoder enc(TargetArch::X86_32);
        enc.push_r(Reg::EAX);
        enc.pop_r(Reg::EBX);
        enc.ret();
        check_bytes("32-bit push/pop/ret", enc.code(), { 0x50, 0x5B, 0xC3 });
    }

    // 4. Forbidden 64-bit / extended registers in 32-bit mode
    {
        GrassX86Encoder enc(TargetArch::X86_32);
        bool caught1 = false;
        try {
            enc.mov_rr(Reg::RAX, Reg::RBX);
        } catch (const std::invalid_argument&) {
            caught1 = true;
        }
        check_bool("32-bit mode forbids 64-bit RAX register", caught1);

        bool caught2 = false;
        try {
            enc.mov_rr(Reg::EAX, Reg::R8D);
        } catch (const std::invalid_argument&) {
            caught2 = true;
        }
        check_bool("32-bit mode forbids extended R8D register", caught2);

        bool caught3 = false;
        try {
            enc.mov_rr(Reg::AL, Reg::SPL);
        } catch (const std::invalid_argument&) {
            caught3 = true;
        }
        check_bool("32-bit mode forbids SPL register", caught3);
    }

    // 5. 32-bit ALU operations
    {
        GrassX86Encoder enc(TargetArch::X86_32);
        enc.sub_ri(Reg::EAX, 42);
        enc.imul_rr(Reg::ECX, Reg::EDX);
        enc.xor_rr(Reg::EDX, Reg::EDX);
        enc.cmp_ri(Reg::EAX, 0);
        check_bytes("32-bit sub/imul/xor/cmp", enc.code(), {
            0x83, 0xE8, 0x2A,
            0x0F, 0xAF, 0xCA,
            0x31, 0xD2,
            0x83, 0xF8, 0x00
        });
    }

    // 6. 32-bit jumps and loop
    {
        GrassX86Encoder enc(TargetArch::X86_32);
        LabelId loop_lbl = enc.create_label();
        LabelId exit_lbl = enc.create_label();
        enc.bind_label(loop_lbl);
        enc.dec_r(Reg::ECX);
        enc.jcc_label(CondCode::LE, exit_lbl);
        enc.jmp_label(loop_lbl);
        enc.bind_label(exit_lbl);
        enc.ret();
        check_bool("32-bit loop emitted without error", enc.code_size() > 0);
    }

    // 7. 32-bit rejection of 64-bit specific operations
    {
        GrassX86Encoder enc(TargetArch::X86_32);
        bool c1 = false;
        try {
            enc.mov_ri64(Reg::EAX, 12345);
        } catch (const std::invalid_argument&) {
            c1 = true;
        }
        check_bool("32-bit mode forbids mov_ri64", c1);

        bool c2 = false;
        try {
            enc.cqo();
        } catch (const std::invalid_argument&) {
            c2 = true;
        }
        check_bool("32-bit mode forbids cqo", c2);

        bool c3 = false;
        try {
            enc.syscall();
        } catch (const std::invalid_argument&) {
            c3 = true;
        }
        check_bool("32-bit mode forbids syscall", c3);

        bool c4 = false;
        try {
            enc.movsxd_rr32(Reg::EAX, Reg::EBX);
        } catch (const std::invalid_argument&) {
            c4 = true;
        }
        check_bool("32-bit mode forbids movsxd_rr32", c4);

        bool c5 = false;
        try {
            enc.mov_mi32(mem(Reg::EAX), 10, 8);
        } catch (const std::invalid_argument&) {
            c5 = true;
        }
        check_bool("32-bit mode forbids 64-bit memory write in mov_mi32", c5);
    }
}

// =============================================================================
// Part 3: NOPs, Relocations, Hex Dump, Labels
// =============================================================================
static void test_utilities_and_relocs() {
    std::cout << "\n=== PART 3: Utilities, NOPs, Relocations & Labels ===\n";

    // Multi-byte NOPs
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.nop_n(1);
        enc.nop_n(3);
        enc.nop_n(5);
        enc.nop_n(9);
        check_bytes("Multi-byte NOPs", enc.code(), {
            0x90,
            0x0F, 0x1F, 0x00,
            0x0F, 0x1F, 0x44, 0x00, 0x00,
            0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00
        });
    }

    // External symbols and relocations
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.call_extern("malloc");
        enc.lea_symbol(Reg::RAX, "g_buffer");
        enc.mov_symbol_addr(Reg::RDX, "g_table");

        check_bool("Relocations count is 3", enc.relocations().size() == 3);
        check_bool("Reloc 0 is Rel32 for malloc", enc.relocations()[0].symbol_name == "malloc" && enc.relocations()[0].type == RelocType::Rel32);
        check_bool("Reloc 1 is Rel32 for g_buffer", enc.relocations()[1].symbol_name == "g_buffer" && enc.relocations()[1].type == RelocType::Rel32);
        check_bool("Reloc 2 is Abs64 for g_table", enc.relocations()[2].symbol_name == "g_table" && enc.relocations()[2].type == RelocType::Abs64);

        CompiledUnit unit = enc.finish("test_proc");
        check_bool("CompiledUnit text size matches", unit.text.size() == enc.code_size());
        check_bool("CompiledUnit symbols contains test_proc", unit.symbols.size() == 1 && unit.symbols[0].name == "test_proc");
    }

    // Hex dump
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.nop();
        enc.ret();
        std::string hex = enc.dump_hex();
        check_bool("Hex dump output formatted properly", hex.find("90 C3") != std::string::npos);
    }
}

// =============================================================================
// Part 4: Real JIT Code Generation & Execution
// =============================================================================
static void test_jit_execution() {
    std::cout << "\n=== PART 4: Real JIT Code Execution Tests ===\n";

    // JIT Test 1: Factorial in a loop
    // int64_t (*factorial)(int64_t n)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg arg_n = Reg::RCX;
#else
        Reg arg_n = Reg::RDI;
#endif
        enc.mov_ri32(Reg::EAX, 1);
        enc.cmp_ri(arg_n, 1);
        LabelId done = enc.create_label();
        enc.jcc_label(CondCode::LE, done);

        LabelId loop_top = enc.create_label();
        enc.bind_label(loop_top);
        enc.imul_rr(Reg::RAX, arg_n);
        enc.dec_r(arg_n);
        enc.cmp_ri(arg_n, 1);
        enc.jcc_label(CondCode::G, loop_top);

        enc.bind_label(done);
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using FactFn = int64_t (*)(int64_t);
        FactFn fn = reinterpret_cast<FactFn>(mem_ptr);

        check_bool("JIT Factorial(0) == 1", fn(0) == 1);
        check_bool("JIT Factorial(1) == 1", fn(1) == 1);
        check_bool("JIT Factorial(5) == 120", fn(5) == 120);
        check_bool("JIT Factorial(10) == 3628800", fn(10) == 3628800);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 2: Recursive Fibonacci
    // int64_t (*fib)(int64_t n)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        LabelId fib_entry = enc.create_label();
        enc.bind_label(fib_entry);

#if defined(_WIN32)
        // Windows x64 ABI: arg in RCX, return in RAX.
        // Callee-saved: RBX, RBP.
        // RSP alignment:
        // Entry: rsp % 16 == 8.
        // push rbx: rsp % 16 == 0
        // push rbp: rsp % 16 == 8
        // sub rsp, 40: rsp % 16 == 0 (32 bytes shadow space + 8 bytes padding)
        enc.push_r(Reg::RBX);
        enc.push_r(Reg::RBP);
        enc.sub_ri(Reg::RSP, 40);

        enc.mov_rr(Reg::RBP, Reg::RCX);
        enc.cmp_ri(Reg::RBP, 1);
        LabelId recurse = enc.create_label();
        enc.jcc_label(CondCode::G, recurse);

        enc.mov_rr(Reg::RAX, Reg::RBP);
        LabelId done = enc.create_label();
        enc.jmp_label(done);

        enc.bind_label(recurse);
        enc.lea_rm(Reg::RCX, mem(Reg::RBP, -1));
        enc.call_label(fib_entry);
        enc.mov_rr(Reg::RBX, Reg::RAX);

        enc.lea_rm(Reg::RCX, mem(Reg::RBP, -2));
        enc.call_label(fib_entry);
        enc.add_rr(Reg::RAX, Reg::RBX);

        enc.bind_label(done);
        enc.add_ri(Reg::RSP, 40);
        enc.pop_r(Reg::RBP);
        enc.pop_r(Reg::RBX);
        enc.ret();
#else
        // SysV x64 ABI: arg in RDI, return in RAX.
        enc.push_r(Reg::RBX);
        enc.push_r(Reg::RBP);
        enc.sub_ri(Reg::RSP, 8);

        enc.mov_rr(Reg::RBP, Reg::RDI);
        enc.cmp_ri(Reg::RBP, 1);
        LabelId recurse = enc.create_label();
        enc.jcc_label(CondCode::G, recurse);

        enc.mov_rr(Reg::RAX, Reg::RBP);
        LabelId done = enc.create_label();
        enc.jmp_label(done);

        enc.bind_label(recurse);
        enc.lea_rm(Reg::RDI, mem(Reg::RBP, -1));
        enc.call_label(fib_entry);
        enc.mov_rr(Reg::RBX, Reg::RAX);

        enc.lea_rm(Reg::RDI, mem(Reg::RBP, -2));
        enc.call_label(fib_entry);
        enc.add_rr(Reg::RAX, Reg::RBX);

        enc.bind_label(done);
        enc.add_ri(Reg::RSP, 8);
        enc.pop_r(Reg::RBP);
        enc.pop_r(Reg::RBX);
        enc.ret();
#endif

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using FibFn = int64_t (*)(int64_t);
        FibFn fn = reinterpret_cast<FibFn>(mem_ptr);

        check_bool("JIT Fibonacci(0) == 0", fn(0) == 0);
        check_bool("JIT Fibonacci(1) == 1", fn(1) == 1);
        check_bool("JIT Fibonacci(2) == 1", fn(2) == 1);
        check_bool("JIT Fibonacci(3) == 2", fn(3) == 2);
        check_bool("JIT Fibonacci(7) == 13", fn(7) == 13);
        check_bool("JIT Fibonacci(10) == 55", fn(10) == 55);
        check_bool("JIT Fibonacci(12) == 144", fn(12) == 144);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 3: Array Sum with SIB [base + index*8]
    // int64_t (*sum_array)(const int64_t* arr, int64_t count)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_arr = Reg::RCX;
        Reg r_count = Reg::RDX;
#else
        Reg r_arr = Reg::RDI;
        Reg r_count = Reg::RSI;
#endif
        enc.xor_rr(Reg::RAX, Reg::RAX); // sum = 0
        enc.xor_rr(Reg::R8, Reg::R8);   // index = 0

        LabelId check_lbl = enc.create_label();
        LabelId body_lbl = enc.create_label();
        enc.jmp_label(check_lbl);

        enc.bind_label(body_lbl);
        // mov r9, [r_arr + r8*8] (SIB scale=8 with R8 index)
        enc.mov_rm(Reg::R9, mem(r_arr, Reg::R8, 8, 0));
        enc.add_rr(Reg::RAX, Reg::R9);
        enc.inc_r(Reg::R8);

        enc.bind_label(check_lbl);
        enc.cmp_rr(Reg::R8, r_count);
        enc.jcc_label(CondCode::L, body_lbl);

        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using SumFn = int64_t (*)(const int64_t*, int64_t);
        SumFn fn = reinterpret_cast<SumFn>(mem_ptr);

        int64_t arr1[] = { 10, 20, 30, 40, 50, 60, 70, 80 };
        check_bool("JIT ArraySum {10..80} == 360", fn(arr1, 8) == 360);

        int64_t arr2[] = { -15, 25, -35, 45, 100 };
        check_bool("JIT ArraySum {-15, 25, -35, 45, 100} == 120", fn(arr2, 5) == 120);
        check_bool("JIT ArraySum empty array == 0", fn(arr2, 0) == 0);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 4: Floating Point double math via XMM0..XMM3
    // double (*math_f64)(double a, double b, double c, double d)
    // Formula: sqrt((a * b + c) / d)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mulsd_xx(Reg::XMM0, Reg::XMM1); // xmm0 = a * b
        enc.addsd_xx(Reg::XMM0, Reg::XMM2); // xmm0 = a * b + c
        enc.divsd_xx(Reg::XMM0, Reg::XMM3); // xmm0 = (a * b + c) / d
        enc.sqrtsd_xx(Reg::XMM0, Reg::XMM0); // xmm0 = sqrt(...)
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using MathFn = double (*)(double, double, double, double);
        MathFn fn = reinterpret_cast<MathFn>(mem_ptr);

        double res1 = fn(3.0, 4.0, 4.0, 4.0); // sqrt((12 + 4) / 4) = sqrt(4) = 2.0
        check_bool("JIT Double Math sqrt((3*4 + 4) / 4) == 2.0", std::fabs(res1 - 2.0) < 1e-9);

        double res2 = fn(6.0, 7.0, 7.0, 2.0); // sqrt((42 + 7) / 2) = sqrt(24.5)
        check_bool("JIT Double Math sqrt((6*7 + 7) / 2) == sqrt(24.5)", std::fabs(res2 - std::sqrt(24.5)) < 1e-9);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 5: Integer to double conversion and back (cvtsi2sd, cvttsd2si)
    // int64_t (*test_conv)(int64_t)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_in = Reg::RCX;
#else
        Reg r_in = Reg::RDI;
#endif
        enc.cvtsi2sd_xr(Reg::XMM0, r_in);
        enc.sqrtsd_xx(Reg::XMM0, Reg::XMM0);
        enc.cvttsd2si_rx(Reg::RAX, Reg::XMM0);
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using ConvFn = int64_t (*)(int64_t);
        ConvFn fn = reinterpret_cast<ConvFn>(mem_ptr);

        check_bool("JIT cvtsi2sd/cvttsd2si sqrt(144) == 12", fn(144) == 12);
        check_bool("JIT cvtsi2sd/cvttsd2si sqrt(81) == 9", fn(81) == 9);
        check_bool("JIT cvtsi2sd/cvttsd2si sqrt(50) == 7", fn(50) == 7);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 6: CMOVcc and SETcc conditional operations
    // int64_t (*test_cond)(int64_t a, int64_t b)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_a = Reg::RCX;
        Reg r_b = Reg::RDX;
#else
        Reg r_a = Reg::RDI;
        Reg r_b = Reg::RSI;
#endif
        // rax = r_a; cmp r_a, r_b; cmovl rax, r_b (rax = max(a, b))
        enc.mov_rr(Reg::RAX, r_a);
        enc.cmp_rr(r_a, r_b);
        enc.cmov_cc_rr(CondCode::L, Reg::RAX, r_b);
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using MaxFn = int64_t (*)(int64_t, int64_t);
        MaxFn fn = reinterpret_cast<MaxFn>(mem_ptr);

        check_bool("JIT cmovL max(10, 20) == 20", fn(10, 20) == 20);
        check_bool("JIT cmovL max(50, 30) == 50", fn(50, 30) == 50);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 7: Atomic operations in JIT
    // int64_t (*test_atomic)(int64_t* ptr, int64_t val)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_ptr = Reg::RCX;
        Reg r_val = Reg::RDX;
#else
        Reg r_ptr = Reg::RDI;
        Reg r_val = Reg::RSI;
#endif
        enc.mov_rr(Reg::RAX, r_val);
        enc.lock_xadd_mr(mem(r_ptr), Reg::RAX); // returns previous value in RAX, ptr updated
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using AtomicFn = int64_t (*)(int64_t*, int64_t);
        AtomicFn fn = reinterpret_cast<AtomicFn>(mem_ptr);

        int64_t target_val = 100;
        int64_t prev = fn(&target_val, 25);
        check_bool("JIT lock_xadd returns old value", prev == 100);
        check_bool("JIT lock_xadd updates memory value", target_val == 125);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 8: Embedded constant load via RIP-relative addressing
    // int64_t (*get_constant)()
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        LabelId data_lbl = enc.create_label();
        LabelId code_lbl = enc.create_label();
        enc.jmp_label(code_lbl);
        enc.bind_label(data_lbl);
        int64_t magic_value = 0x1122334455667788LL;
        enc.emit_raw(&magic_value, sizeof(magic_value));
        enc.bind_label(code_lbl);
        enc.mov_rm(Reg::RAX, MemOp::rip_rel(data_lbl));
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using GetConstFn = int64_t (*)();
        GetConstFn fn = reinterpret_cast<GetConstFn>(mem_ptr);
        check_bool("JIT RIP-relative data load == 0x1122334455667788", fn() == 0x1122334455667788LL);
        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 9: Shifts and Rotates
    // uint64_t (*test_shifts)(uint64_t val)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_val = Reg::RCX;
#else
        Reg r_val = Reg::RDI;
#endif
        enc.mov_rr(Reg::RAX, r_val);
        enc.shl_ri(Reg::RAX, 2);
        enc.rol_ri(Reg::RAX, 1);
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using ShiftFn = uint64_t (*)(uint64_t);
        ShiftFn fn = reinterpret_cast<ShiftFn>(mem_ptr);
        check_bool("JIT shl + rol (5 << 2 = 20, rol 1 = 40)", fn(5) == 40);
        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 10: Signed 64-bit division (CQO + IDIV)
    // int64_t (*test_div)(int64_t dividend, int64_t divisor, int64_t* out_rem)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_num = Reg::RCX;
        Reg r_den = Reg::RDX;
        Reg r_rem = Reg::R8;
#else
        Reg r_num = Reg::RDI;
        Reg r_den = Reg::RSI;
        Reg r_rem = Reg::RDX;
#endif
        enc.mov_rr(Reg::R10, r_den);
        enc.mov_rr(Reg::RAX, r_num);
        enc.cqo();
        enc.idiv_r(Reg::R10);
        enc.mov_mr(mem(r_rem), Reg::RDX);
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using DivFn = int64_t (*)(int64_t, int64_t, int64_t*);
        DivFn fn = reinterpret_cast<DivFn>(mem_ptr);

        int64_t rem = 0;
        int64_t q = fn(100, 7, &rem);
        check_bool("JIT 100 / 7 quotient == 14", q == 14);
        check_bool("JIT 100 / 7 remainder == 2", rem == 2);

        q = fn(-100, 7, &rem);
        check_bool("JIT -100 / 7 quotient == -14", q == -14);
        check_bool("JIT -100 / 7 remainder == -2", rem == -2);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 11: Bit manipulation (popcnt)
    // uint64_t (*test_bits)(uint64_t val)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_in = Reg::RCX;
#else
        Reg r_in = Reg::RDI;
#endif
        enc.popcnt_rr(Reg::RAX, r_in);
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using BitFn = uint64_t (*)(uint64_t);
        BitFn fn = reinterpret_cast<BitFn>(mem_ptr);

        check_bool("JIT popcnt(0x0F) == 4", fn(0x0F) == 4);
        check_bool("JIT popcnt(0xAAAAAAAAAAAAAAAAULL) == 32", fn(0xAAAAAAAAAAAAAAAAULL) == 32);
        check_bool("JIT popcnt(0) == 0", fn(0) == 0);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 12: BSWAP
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_in = Reg::RCX;
#else
        Reg r_in = Reg::RDI;
#endif
        enc.mov_rr(Reg::RAX, r_in);
        enc.bswap_r(Reg::RAX);
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using BswapFn = uint64_t (*)(uint64_t);
        BswapFn fn = reinterpret_cast<BswapFn>(mem_ptr);

        check_bool("JIT bswap(0x1122334455667788ULL) == 0x8877665544332211ULL", fn(0x1122334455667788ULL) == 0x8877665544332211ULL);

        free_exec_mem(mem_ptr, sz);
    }

    // JIT Test 13: Single-precision float math
    // float (*math_f32)(float a, float b)
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mulss_xx(Reg::XMM0, Reg::XMM1);
        enc.sqrtss_xx(Reg::XMM0, Reg::XMM0);
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using MathF32Fn = float (*)(float, float);
        MathF32Fn fn = reinterpret_cast<MathF32Fn>(mem_ptr);

        float res = fn(8.0f, 2.0f);
        check_bool("JIT Float32 sqrt(8.0f * 2.0f) == 4.0f", std::fabs(res - 4.0f) < 1e-6f);

        free_exec_mem(mem_ptr, sz);
    }
}

static void test_simd_encoding() {
    std::cout << "\n=== PART 5: SSE2 & AVX2 SIMD Encoding & JIT Tests ===\n";

    // SSE2 unaligned loads/stores & register moves
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movdqu_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("movdqu xmm0, xmm1", enc.code(), { 0xF3, 0x0F, 0x6F, 0xC1 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movdqu_xm(Reg::XMM8, mem(Reg::R12, 16));
        check_bytes("movdqu xmm8, [r12 + 16]", enc.code(), { 0xF3, 0x45, 0x0F, 0x6F, 0x44, 0x24, 0x10 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movdqu_mx(mem(Reg::RAX), Reg::XMM2);
        check_bytes("movdqu [rax], xmm2", enc.code(), { 0xF3, 0x0F, 0x7F, 0x10 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movupd_xm(Reg::XMM1, mem(Reg::RCX));
        check_bytes("movupd xmm1, [rcx]", enc.code(), { 0x66, 0x0F, 0x10, 0x09 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movupd_mx(mem(Reg::RDX), Reg::XMM3);
        check_bytes("movupd [rdx], xmm3", enc.code(), { 0x66, 0x0F, 0x11, 0x1A });
    }
    // SSE2 arithmetic & horizontal reduction helpers
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.paddq_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("paddq xmm0, xmm1", enc.code(), { 0x66, 0x0F, 0xD4, 0xC1 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.psubq_xx(Reg::XMM2, Reg::XMM3);
        check_bytes("psubq xmm2, xmm3", enc.code(), { 0x66, 0x0F, 0xFB, 0xD3 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.addpd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("addpd xmm0, xmm1", enc.code(), { 0x66, 0x0F, 0x58, 0xC1 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.subpd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("subpd xmm0, xmm1", enc.code(), { 0x66, 0x0F, 0x5C, 0xC1 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.mulpd_xx(Reg::XMM0, Reg::XMM1);
        check_bytes("mulpd xmm0, xmm1", enc.code(), { 0x66, 0x0F, 0x59, 0xC1 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.haddpd_xx(Reg::XMM0, Reg::XMM0);
        check_bytes("haddpd xmm0, xmm0", enc.code(), { 0x66, 0x0F, 0x7C, 0xC0 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.punpckhqdq_xx(Reg::XMM1, Reg::XMM0);
        check_bytes("punpckhqdq xmm1, xmm0", enc.code(), { 0x66, 0x0F, 0x6D, 0xC8 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.psrldq_xi(Reg::XMM1, 8);
        check_bytes("psrldq xmm1, 8", enc.code(), { 0x66, 0x0F, 0x73, 0xD9, 0x08 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movhlps_xx(Reg::XMM1, Reg::XMM0);
        check_bytes("movhlps xmm1, xmm0", enc.code(), { 0x0F, 0x12, 0xC8 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.shufpd_xxi(Reg::XMM0, Reg::XMM1, 1);
        check_bytes("shufpd xmm0, xmm1, 1", enc.code(), { 0x66, 0x0F, 0xC6, 0xC1, 0x01 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.pxor_xx(Reg::XMM0, Reg::XMM0);
        check_bytes("pxor xmm0, xmm0", enc.code(), { 0x66, 0x0F, 0xEF, 0xC0 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movq_rx(Reg::RAX, Reg::XMM0);
        check_bytes("movq rax, xmm0", enc.code(), { 0x66, 0x48, 0x0F, 0x7E, 0xC0 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.movq_xr(Reg::XMM0, Reg::RAX);
        check_bytes("movq xmm0, rax", enc.code(), { 0x66, 0x48, 0x0F, 0x6E, 0xC0 });
    }

    // AVX2 256-bit YMM VEX encoding
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vmovdqu_ym(Reg::YMM0, mem(Reg::RAX));
        check_bytes("vmovdqu ymm0, [rax]", enc.code(), { 0xC5, 0xFE, 0x6F, 0x00 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vmovdqu_my(mem(Reg::R8), Reg::YMM9);
        check_bytes("vmovdqu [r8], ymm9 (3-byte VEX)", enc.code(), { 0xC4, 0x41, 0x7E, 0x7F, 0x08 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vmovupd_ym(Reg::YMM1, mem(Reg::RCX));
        check_bytes("vmovupd ymm1, [rcx]", enc.code(), { 0xC5, 0xFD, 0x10, 0x09 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vmovupd_my(mem(Reg::RDX), Reg::YMM2);
        check_bytes("vmovupd [rdx], ymm2", enc.code(), { 0xC5, 0xFD, 0x11, 0x12 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vpaddq_yyy(Reg::YMM0, Reg::YMM1, Reg::YMM2);
        check_bytes("vpaddq ymm0, ymm1, ymm2", enc.code(), { 0xC5, 0xF5, 0xD4, 0xC2 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vpsubq_yyy(Reg::YMM0, Reg::YMM1, Reg::YMM2);
        check_bytes("vpsubq ymm0, ymm1, ymm2", enc.code(), { 0xC5, 0xF5, 0xFB, 0xC2 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vaddpd_yyy(Reg::YMM0, Reg::YMM1, Reg::YMM2);
        check_bytes("vaddpd ymm0, ymm1, ymm2", enc.code(), { 0xC5, 0xF5, 0x58, 0xC2 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vsubpd_yyy(Reg::YMM0, Reg::YMM1, Reg::YMM2);
        check_bytes("vsubpd ymm0, ymm1, ymm2", enc.code(), { 0xC5, 0xF5, 0x5C, 0xC2 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vmulpd_yyy(Reg::YMM0, Reg::YMM1, Reg::YMM2);
        check_bytes("vmulpd ymm0, ymm1, ymm2", enc.code(), { 0xC5, 0xF5, 0x59, 0xC2 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vpxor_yyy(Reg::YMM0, Reg::YMM0, Reg::YMM0);
        check_bytes("vpxor ymm0, ymm0, ymm0", enc.code(), { 0xC5, 0xFD, 0xEF, 0xC0 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vhaddpd_yyy(Reg::YMM0, Reg::YMM1, Reg::YMM2);
        check_bytes("vhaddpd ymm0, ymm1, ymm2", enc.code(), { 0xC5, 0xF5, 0x7C, 0xC2 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vextracti128_xyi(Reg::XMM1, Reg::YMM0, 1);
        check_bytes("vextracti128 xmm1, ymm0, 1", enc.code(), { 0xC4, 0xE3, 0x7D, 0x39, 0xC1, 0x01 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vextractf128_xyi(Reg::XMM1, Reg::YMM0, 1);
        check_bytes("vextractf128 xmm1, ymm0, 1", enc.code(), { 0xC4, 0xE3, 0x7D, 0x19, 0xC1, 0x01 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vpbroadcastq_yx(Reg::YMM0, Reg::XMM1);
        check_bytes("vpbroadcastq ymm0, xmm1", enc.code(), { 0xC4, 0xE2, 0x7D, 0x59, 0xC1 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vbroadcastsd_yx(Reg::YMM0, Reg::XMM1);
        check_bytes("vbroadcastsd ymm0, xmm1", enc.code(), { 0xC4, 0xE2, 0x7D, 0x19, 0xC1 });
    }
    {
        GrassX86Encoder enc(TargetArch::X86_64);
        enc.vzeroupper();
        check_bytes("vzeroupper", enc.code(), { 0xC5, 0xF8, 0x77 });
    }

    // JIT execution test: AVX2 4-double dot product
    {
        GrassX86Encoder enc(TargetArch::X86_64);
#if defined(_WIN32)
        Reg r_a = Reg::RCX;
        Reg r_b = Reg::RDX;
#else
        Reg r_a = Reg::RDI;
        Reg r_b = Reg::RSI;
#endif
        enc.vmovupd_ym(Reg::YMM0, mem(r_a));
        enc.vmovupd_ym(Reg::YMM1, mem(r_b));
        enc.vmulpd_yyy(Reg::YMM0, Reg::YMM0, Reg::YMM1);
        enc.vextractf128_xyi(Reg::XMM1, Reg::YMM0, 1);
        enc.addpd_xx(Reg::XMM0, Reg::XMM1);
        enc.haddpd_xx(Reg::XMM0, Reg::XMM0);
        enc.vzeroupper();
        enc.ret();

        size_t sz = enc.code_size();
        void* mem_ptr = allocate_exec_mem(sz);
        std::memcpy(mem_ptr, enc.code().data(), sz);

        using Dot4Fn = double (*)(const double*, const double*);
        Dot4Fn fn = reinterpret_cast<Dot4Fn>(mem_ptr);
        double a[4] = { 1.0, 2.0, 3.0, 4.0 };
        double b[4] = { 10.0, 20.0, 30.0, 40.0 };
        double res = fn(a, b);
        check_bool("JIT AVX2 dot4({1,2,3,4}, {10,20,30,40}) == 300.0", std::fabs(res - 300.0) < 1e-9);
        free_exec_mem(mem_ptr, sz);
    }
}

int main() {
    std::cout << "Starting GrassGreen x86/x86_64 Binary Machine Encoder Unit Tests...\n";

    try {
        test_bitwise_encoding();
        test_x86_32_encoding();
        test_utilities_and_relocs();
        test_jit_execution();
        test_simd_encoding();
    } catch (const std::exception& ex) {
        std::cerr << "[UNEXPECTED EXCEPTION] " << ex.what() << "\n";
        g_tests_failed++;
    }

    std::cout << "\n========================================\n";
    std::cout << "Tests passed: " << g_tests_passed << "\n";
    std::cout << "Tests failed: " << g_tests_failed << "\n";
    std::cout << "========================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}

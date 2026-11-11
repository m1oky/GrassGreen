// =============================================================================
//  GrassGreen 4.0 — Minimal x86_64 JIT (Just-In-Time) Compiler Implementation
//  Файл создан Архитектором. Реализация — Кодер Ядра («Занятой-Core»).
// =============================================================================
#include "GrassJIT.h"
#include "GrassHAL.h"
#include <iostream>
#include <cstring>
#include <chrono>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace GrassJIT {

    static bool g_jit_enabled = false;
    static JitStats g_jit_stats;

    // ── Минимальный x86_64 Ассемблер / Генератор Машинного Кода ──
    class X64Assembler {
    public:
        std::vector<uint8_t> buf;

        void emit8(uint8_t b) { buf.push_back(b); }
        void emit16(uint16_t w) { emit8(w & 0xFF); emit8((w >> 8) & 0xFF); }
        void emit32(uint32_t v) {
            buf.push_back((uint8_t)(v & 0xFF));
            buf.push_back((uint8_t)((v >> 8) & 0xFF));
            buf.push_back((uint8_t)((v >> 16) & 0xFF));
            buf.push_back((uint8_t)((v >> 24) & 0xFF));
        }
        void emit64(uint64_t v) {
            for (int i = 0; i < 8; ++i) buf.push_back((uint8_t)((v >> (i * 8)) & 0xFF));
        }
        void patch32(size_t offset, uint32_t v) {
            if (offset + 4 <= buf.size()) {
                buf[offset]     = (uint8_t)(v & 0xFF);
                buf[offset + 1] = (uint8_t)((v >> 8) & 0xFF);
                buf[offset + 2] = (uint8_t)((v >> 16) & 0xFF);
                buf[offset + 3] = (uint8_t)((v >> 24) & 0xFF);
            }
        }
        size_t cur_pos() const { return buf.size(); }

        // ret: C3
        void ret() { emit8(0xC3); }

        // mov rax, imm64: 48 B8 <imm64>
        void mov_rax_imm64(uint64_t imm) {
            emit8(0x48); emit8(0xB8);
            emit64(imm);
        }

        // mov [rcx + disp], rax
        void mov_mrcx_rax(int32_t disp) {
            emit8(0x48); emit8(0x89);
            if (disp >= -128 && disp <= 127) {
                emit8(0x41); emit8((uint8_t)(int8_t)disp);
            } else {
                emit8(0x81); emit32((uint32_t)disp);
            }
        }

        // mov rax, [rcx + disp]
        void mov_rax_mrcx(int32_t disp) {
            emit8(0x48); emit8(0x8B);
            if (disp >= -128 && disp <= 127) {
                emit8(0x41); emit8((uint8_t)(int8_t)disp);
            } else {
                emit8(0x81); emit32((uint32_t)disp);
            }
        }

        // movsd xmm0, [rcx + disp]: F2 0F 10 ...
        void movsd_xmm0_mrcx(int32_t disp) {
            emit8(0xF2); emit8(0x0F); emit8(0x10);
            if (disp >= -128 && disp <= 127) {
                emit8(0x41); emit8((uint8_t)(int8_t)disp);
            } else {
                emit8(0x81); emit32((uint32_t)disp);
            }
        }

        // movsd [rcx + disp], xmm0: F2 0F 11 ...
        void movsd_mrcx_xmm0(int32_t disp) {
            emit8(0xF2); emit8(0x0F); emit8(0x11);
            if (disp >= -128 && disp <= 127) {
                emit8(0x41); emit8((uint8_t)(int8_t)disp);
            } else {
                emit8(0x81); emit32((uint32_t)disp);
            }
        }

        // movsd xmm1, [rcx + disp]
        void movsd_xmm1_mrcx(int32_t disp) {
            emit8(0xF2); emit8(0x0F); emit8(0x10);
            if (disp >= -128 && disp <= 127) {
                emit8(0x49); emit8((uint8_t)(int8_t)disp);
            } else {
                emit8(0x89); emit32((uint32_t)disp);
            }
        }

        // movsd xmm1, xmm0: F2 0F 10 C8
        void movsd_xmm1_xmm0() {
            emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0xC8);
        }

        // movsd xmm0, xmm1: F2 0F 10 C1
        void movsd_xmm0_xmm1() {
            emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0xC1);
        }

        // movq xmm0, rax: 66 48 0F 6E C0
        void movq_xmm0_rax() {
            emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x6E); emit8(0xC0);
        }

        // movq xmm1, rax: 66 48 0F 6E C8
        void movq_xmm1_rax() {
            emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x6E); emit8(0xC8);
        }

        // addsd xmm0, xmm1: F2 0F 58 C1
        void addsd_xmm0_xmm1() {
            emit8(0xF2); emit8(0x0F); emit8(0x58); emit8(0xC1);
        }

        // subsd xmm0, xmm1: F2 0F 5C C1
        void subsd_xmm0_xmm1() {
            emit8(0xF2); emit8(0x0F); emit8(0x5C); emit8(0xC1);
        }

        // subsd xmm1, xmm0: F2 0F 5C C8
        void subsd_xmm1_xmm0() {
            emit8(0xF2); emit8(0x0F); emit8(0x5C); emit8(0xC8);
        }

        // mulsd xmm0, xmm1: F2 0F 59 C1
        void mulsd_xmm0_xmm1() {
            emit8(0xF2); emit8(0x0F); emit8(0x59); emit8(0xC1);
        }

        // divsd xmm0, xmm1: F2 0F 5E C1
        void divsd_xmm0_xmm1() {
            emit8(0xF2); emit8(0x0F); emit8(0x5E); emit8(0xC1);
        }

        // divsd xmm1, xmm0: F2 0F 5E C8
        void divsd_xmm1_xmm0() {
            emit8(0xF2); emit8(0x0F); emit8(0x5E); emit8(0xC8);
        }

        // ucomisd xmm0, xmm1: 66 0F 2E C1
        void ucomisd_xmm0_xmm1() {
            emit8(0x66); emit8(0x0F); emit8(0x2E); emit8(0xC1);
        }

        // ucomisd xmm1, xmm0: 66 0F 2E C8
        void ucomisd_xmm1_xmm0() {
            emit8(0x66); emit8(0x0F); emit8(0x2E); emit8(0xC8);
        }

        // inc qword ptr [rcx + disp]: 48 FF 81 <disp32>
        void inc_mrcx(int32_t disp) {
            emit8(0x48); emit8(0xFF);
            if (disp >= -128 && disp <= 127) {
                emit8(0x41); emit8((uint8_t)(int8_t)disp);
            } else {
                emit8(0x81); emit32((uint32_t)disp);
            }
        }

        // jmp rel32: E9 <disp32>
        size_t jmp_rel32() {
            emit8(0xE9);
            size_t p = buf.size();
            emit32(0);
            return p;
        }

        // jae rel32: 0F 83 <disp32> (Jump if Above or Equal / Not Below)
        size_t jae_rel32() {
            emit8(0x0F); emit8(0x83);
            size_t p = buf.size();
            emit32(0);
            return p;
        }

        // ja rel32: 0F 87 <disp32>
        size_t ja_rel32() {
            emit8(0x0F); emit8(0x87);
            size_t p = buf.size();
            emit32(0);
            return p;
        }

        // jbe rel32: 0F 86 <disp32>
        size_t jbe_rel32() {
            emit8(0x0F); emit8(0x86);
            size_t p = buf.size();
            emit32(0);
            return p;
        }

        // jb rel32: 0F 82 <disp32>
        size_t jb_rel32() {
            emit8(0x0F); emit8(0x82);
            size_t p = buf.size();
            emit32(0);
            return p;
        }

        // je rel32: 0F 84 <disp32>
        size_t je_rel32() {
            emit8(0x0F); emit8(0x84);
            size_t p = buf.size();
            emit32(0);
            return p;
        }

        // jne rel32: 0F 85 <disp32>
        size_t jne_rel32() {
            emit8(0x0F); emit8(0x85);
            size_t p = buf.size();
            emit32(0);
            return p;
        }

        void patch_jmp_to_here(size_t patch_pos) {
            int32_t rel = (int32_t)(buf.size() - (patch_pos + 4));
            patch32(patch_pos, (uint32_t)rel);
        }
    };

    CompiledJitBlock::~CompiledJitBlock() {
        if (code_ptr && code_size > 0) {
            freeExecutableBuffer(code_ptr, code_size);
            code_ptr = nullptr;
            code_size = 0;
        }
    }

    void initialize() {
        g_jit_stats = JitStats();
    }

    void shutdown() {
        g_jit_enabled = false;
    }

    void set_enabled(bool enable) {
        g_jit_enabled = enable;
        g_jit_stats.is_active = enable;
    }

    bool is_enabled() {
        return g_jit_enabled;
    }

    void* allocateExecutableBuffer(size_t size) {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::alloc_executable_memory(size);
        }
        #if defined(_WIN32)
        return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        #else
        return GrassHAL::alloc_executable_memory(size);
        #endif
    }

    void freeExecutableBuffer(void* ptr, size_t size) {
        if (!ptr) return;
        if (GrassHAL::is_freestanding()) {
            GrassHAL::free_executable_memory(ptr, size);
            return;
        }
        #if defined(_WIN32)
        VirtualFree(ptr, 0, MEM_RELEASE);
        #else
        GrassHAL::free_executable_memory(ptr, size);
        #endif
    }

    static inline uint64_t doubleToBits(double d) {
        uint64_t u;
        std::memcpy(&u, &d, sizeof(d));
        return u;
    }

    static inline double readDoubleLE(const uint8_t* p) {
        double d;
        std::memcpy(&d, p, sizeof(d));
        return d;
    }

    static inline uint32_t readU32LE(const uint8_t* p) {
        uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    static std::string readStringAt(const std::vector<uint8_t>& code, size_t& ip) {
        if (ip + 4 > code.size()) return "";
        uint32_t len = readU32LE(&code[ip]);
        ip += 4;
        if (ip + len > code.size()) return "";
        std::string s((const char*)&code[ip], len);
        ip += len;
        return s;
    }

    // BUG-03 / JIT-01 & BUG-04 / JIT-02:
    // Анализатор допустимости цикла для JIT-компиляции
    bool isLoopJITCandidate(
        const std::vector<uint8_t>& code,
        uint32_t loop_start,
        uint32_t loop_end,
        int& max_stack_depth,
        uint32_t& loop_exit_ip,
        std::unordered_map<std::string, int>& var_slot_map
    ) {
        if (!g_jit_enabled) return false;
        if (loop_start >= loop_end || loop_end > code.size()) return false;

        var_slot_map.clear();
        int next_slot = 0;
        size_t ip = loop_start;
        bool has_back_jump = false;
        loop_exit_ip = 0;
        int stack_depth = 0;
        max_stack_depth = 0;

        while (ip < loop_end) {
            uint8_t op = code[ip++];
            switch (op) {
                case OP_NOP:
                    break;
                case OP_CONST_NUM:
                    if (ip + 8 > loop_end) return false;
                    ip += 8;
                    stack_depth++;
                    if (stack_depth > max_stack_depth) max_stack_depth = stack_depth;
                    break;
                case OP_LOAD_VAR: {
                    std::string name = readStringAt(code, ip);
                    if (name.empty()) return false;
                    if (!var_slot_map.count(name)) {
                        if (next_slot >= 120) return false; // лимит слотов
                        var_slot_map[name] = next_slot++;
                    }
                    stack_depth++;
                    if (stack_depth > max_stack_depth) max_stack_depth = stack_depth;
                    break;
                }
                case OP_STORE_VAR:
                case OP_DEF_VAR: {
                    std::string name = readStringAt(code, ip);
                    if (name.empty()) return false;
                    if (!var_slot_map.count(name)) {
                        if (next_slot >= 120) return false;
                        var_slot_map[name] = next_slot++;
                    }
                    if (stack_depth > 0) stack_depth--;
                    break;
                }
                case OP_INC_VAR:
                case OP_DEC_VAR: {
                    std::string name = readStringAt(code, ip);
                    if (name.empty()) return false;
                    if (!var_slot_map.count(name)) {
                        if (next_slot >= 120) return false;
                        var_slot_map[name] = next_slot++;
                    }
                    break;
                }
                case OP_ADD_CONST:
                case OP_SUB_CONST:
                case OP_MUL_CONST:
                case OP_DIV_CONST:
                    if (ip + 8 > loop_end) return false;
                    ip += 8;
                    break;
                case OP_ADD:
                case OP_SUB:
                case OP_MUL:
                case OP_DIV:
                    if (stack_depth > 0) stack_depth--;
                    break;
                case OP_MOD:
                    // BUG-04 / JIT-02: Опкод остатка от деления не поддерживается x86_64 JIT — отклоняем для ВМ
                    return false;
                case OP_POP:
                    if (stack_depth > 0) stack_depth--;
                    break;
                case OP_CMP_LT:
                case OP_CMP_GT:
                case OP_CMP_LTE:
                case OP_CMP_GTE:
                case OP_CMP_EQ:
                case OP_CMP_NEQ:
                    if (stack_depth > 0) stack_depth--;
                    break;
                case OP_JMP_IF_FALSE: {
                    if (ip + 4 > loop_end) return false;
                    uint32_t exit_tgt = readU32LE(&code[ip]);
                    ip += 4;
                    if (loop_exit_ip == 0) loop_exit_ip = exit_tgt;
                    if (stack_depth > 0) stack_depth--;
                    break;
                }
                case OP_JMP: {
                    if (ip + 4 > loop_end) return false;
                    uint32_t tgt = readU32LE(&code[ip]);
                    ip += 4;
                    if (tgt == loop_start) {
                        has_back_jump = true;
                    } else if (tgt < loop_start || tgt >= loop_end) {
                        // Прыжок наружу — не является замкнутым циклом while
                        return false;
                    }
                    break;
                }
                default:
                    // Если цикл содержит неизвестные или не JIT опкоды
                    return false;
            }
        }

        if (!has_back_jump || loop_exit_ip == 0) return false;
        // BUG-03 / JIT-01: Защита от разрушения вычислений при глубине стека выражений > 2 (лимит xmm0/xmm1)
        if (max_stack_depth > 2) return false;

        return true;
    }

    // Анализ и компиляция цикла в x86_64 машинный код
    std::shared_ptr<CompiledJitBlock> tryCompileLoop(
        const std::vector<uint8_t>& code,
        uint32_t loop_start,
        uint32_t loop_end,
        const std::unordered_map<std::string, GrassValue>& current_vars
    ) {
        if (!g_jit_enabled) return nullptr;
        if (loop_start >= loop_end || loop_end > code.size()) return nullptr;

        auto t0 = std::chrono::high_resolution_clock::now();

        // 1. Проверка допустимости инструкций внутри цикла через анализатор isLoopJITCandidate
        std::unordered_map<std::string, int> var_slot_map;
        int max_stack_depth = 0;
        uint32_t loop_exit_ip = 0;

        if (!isLoopJITCandidate(code, loop_start, loop_end, max_stack_depth, loop_exit_ip, var_slot_map)) {
            return nullptr;
        }

        // 2. Генерация машинного кода x86_64
        X64Assembler x64;
        size_t x64_loop_start = x64.cur_pos();
        std::vector<size_t> exit_jumps;

        size_t ip = loop_start;
        uint8_t pending_cmp = 0; // Сохраняем последний оператор сравнения для слияния с jmp_if_false

        while (ip < loop_end) {
            uint8_t op = code[ip++];
            switch (op) {
                case OP_NOP:
                    break;
                case OP_CONST_NUM: {
                    double d = readDoubleLE(&code[ip]);
                    ip += 8;
                    x64.movsd_xmm1_xmm0(); // сдвигаем вершину стека
                    x64.mov_rax_imm64(doubleToBits(d));
                    x64.movq_xmm0_rax();
                    break;
                }
                case OP_LOAD_VAR: {
                    std::string name = readStringAt(code, ip);
                    int slot = var_slot_map[name];
                    x64.movsd_xmm1_xmm0();
                    x64.movsd_xmm0_mrcx(slot * 8);
                    break;
                }
                case OP_STORE_VAR:
                case OP_DEF_VAR: {
                    std::string name = readStringAt(code, ip);
                    int slot = var_slot_map[name];
                    x64.movsd_mrcx_xmm0(slot * 8);
                    break;
                }
                case OP_INC_VAR: {
                    std::string name = readStringAt(code, ip);
                    int slot = var_slot_map[name];
                    x64.movsd_xmm0_mrcx(slot * 8);
                    x64.mov_rax_imm64(doubleToBits(1.0));
                    x64.movq_xmm1_rax();
                    x64.addsd_xmm0_xmm1();
                    x64.movsd_mrcx_xmm0(slot * 8);
                    break;
                }
                case OP_DEC_VAR: {
                    std::string name = readStringAt(code, ip);
                    int slot = var_slot_map[name];
                    x64.movsd_xmm0_mrcx(slot * 8);
                    x64.mov_rax_imm64(doubleToBits(1.0));
                    x64.movq_xmm1_rax();
                    x64.subsd_xmm0_xmm1();
                    x64.movsd_mrcx_xmm0(slot * 8);
                    break;
                }
                case OP_ADD_CONST: {
                    double d = readDoubleLE(&code[ip]);
                    ip += 8;
                    x64.mov_rax_imm64(doubleToBits(d));
                    x64.movq_xmm1_rax();
                    x64.addsd_xmm0_xmm1();
                    break;
                }
                case OP_SUB_CONST: {
                    double d = readDoubleLE(&code[ip]);
                    ip += 8;
                    x64.mov_rax_imm64(doubleToBits(d));
                    x64.movq_xmm1_rax();
                    x64.subsd_xmm0_xmm1();
                    break;
                }
                case OP_MUL_CONST: {
                    double d = readDoubleLE(&code[ip]);
                    ip += 8;
                    x64.mov_rax_imm64(doubleToBits(d));
                    x64.movq_xmm1_rax();
                    x64.mulsd_xmm0_xmm1();
                    break;
                }
                case OP_DIV_CONST: {
                    double d = readDoubleLE(&code[ip]);
                    ip += 8;
                    x64.mov_rax_imm64(doubleToBits(d));
                    x64.movq_xmm1_rax();
                    x64.divsd_xmm0_xmm1();
                    break;
                }
                case OP_ADD:
                    // xmm1 + xmm0 -> xmm0
                    x64.addsd_xmm0_xmm1();
                    break;
                case OP_SUB:
                    // xmm1 - xmm0 -> xmm0
                    x64.subsd_xmm1_xmm0();
                    x64.movsd_xmm0_xmm1();
                    break;
                case OP_MUL:
                    x64.mulsd_xmm0_xmm1();
                    break;
                case OP_DIV:
                    x64.divsd_xmm1_xmm0();
                    x64.movsd_xmm0_xmm1();
                    break;
                case OP_POP:
                    break;
                case OP_CMP_LT:
                case OP_CMP_GT:
                case OP_CMP_LTE:
                case OP_CMP_GTE:
                case OP_CMP_EQ:
                case OP_CMP_NEQ:
                    pending_cmp = op;
                    // ucomisd xmm1, xmm0 (сравнение первого операнда xmm1 со вторым xmm0)
                    x64.ucomisd_xmm1_xmm0();
                    break;
                case OP_JMP_IF_FALSE: {
                    ip += 4;
                    // Условный выход из цикла, если условие ложно
                    if (pending_cmp == OP_CMP_LT) {
                        // условие ложно если >=
                        exit_jumps.push_back(x64.jae_rel32());
                    } else if (pending_cmp == OP_CMP_LTE) {
                        // условие ложно если >
                        exit_jumps.push_back(x64.ja_rel32());
                    } else if (pending_cmp == OP_CMP_GT) {
                        // условие ложно если <=
                        exit_jumps.push_back(x64.jbe_rel32());
                    } else if (pending_cmp == OP_CMP_GTE) {
                        // условие ложно если <
                        exit_jumps.push_back(x64.jb_rel32());
                    } else if (pending_cmp == OP_CMP_EQ) {
                        // условие ложно если !=
                        exit_jumps.push_back(x64.jne_rel32());
                    } else if (pending_cmp == OP_CMP_NEQ) {
                        // условие ложно если ==
                        exit_jumps.push_back(x64.je_rel32());
                    } else {
                        // общее сравнение с нулем
                        exit_jumps.push_back(x64.je_rel32());
                    }
                    pending_cmp = 0;
                    break;
                }
                case OP_JMP: {
                    uint32_t tgt = readU32LE(&code[ip]);
                    ip += 4;
                    if (tgt == loop_start) {
                        // Инкремент счетчика итераций ctx->iterations (offset 1024)
                        x64.inc_mrcx(1024);
                        // Прыжок назад на начало цикла x86_64
                        size_t jmp_pos = x64.jmp_rel32();
                        int32_t rel = (int32_t)(x64_loop_start - (jmp_pos + 4));
                        x64.patch32(jmp_pos, (uint32_t)rel);
                    }
                    break;
                }
            }
        }

        // Точка выхода из цикла: патчим все условные прыжки
        for (size_t patch_pos : exit_jumps) {
            x64.patch_jmp_to_here(patch_pos);
        }
        x64.ret(); // ret: C3

        size_t code_size = x64.buf.size();
        void* exec_mem = allocateExecutableBuffer(code_size);
        if (!exec_mem) return nullptr;

        std::memcpy(exec_mem, x64.buf.data(), code_size);
        #if defined(_WIN32)
        if (!GrassHAL::is_freestanding()) {
            DWORD oldProtect;
            VirtualProtect(exec_mem, code_size, PAGE_EXECUTE_READ, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), exec_mem, code_size);
        }
        #endif

        auto block = std::make_shared<CompiledJitBlock>();
        block->code_ptr = exec_mem;
        block->code_size = code_size;
        block->start_ip = loop_start;
        block->end_ip = loop_exit_ip;
        block->var_slot_map = var_slot_map;
        block->is_valid = true;

        auto t1 = std::chrono::high_resolution_clock::now();
        g_jit_stats.compile_time_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
        g_jit_stats.loops_compiled++;
        g_jit_stats.machine_code_bytes += code_size;

        return block;
    }

    bool executeJitBlock(
        const std::shared_ptr<CompiledJitBlock>& block,
        std::unordered_map<std::string, GrassValue>& vm_vars
    ) {
        if (!block || !block->is_valid || !block->code_ptr) return false;

        JitRuntimeContext ctx;
        std::memset(&ctx, 0, sizeof(ctx));

        // Копируем значения переменных VM в контекст JIT
        for (const auto& kv : block->var_slot_map) {
            auto it = vm_vars.find(kv.first);
            if (it != vm_vars.end()) {
                ctx.vars[kv.second] = it->second.toNumber();
            }
        }

        // Вызов скомпилированного машинного кода x86_64
        JitFn fn = (JitFn)block->code_ptr;
        fn(&ctx);

        // Синхронизируем обновленные переменные обратно в VM
        for (const auto& kv : block->var_slot_map) {
            vm_vars[kv.first] = GrassValue::Num(ctx.vars[kv.second]);
        }

        return true;
    }

    JitStats& getStats() {
        return g_jit_stats;
    }

} // namespace GrassJIT

// =============================================================================
//  GrassGreen 4.0 — Bytecode Optimizer Implementation
//  Файл создан Архитектором. Реализация — Кодер Ядра.
// =============================================================================
#include "GrassOptimizer.h"
#include "GrassSyntax.h"
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <iostream>

namespace GrassOptimizer {

    struct Instruction {
        uint8_t op = 0;
        size_t orig_ip = 0;
        double num_val = 0.0;
        std::string str_val;
        uint32_t u32_val = 0;
        uint8_t u8_val = 0;
        std::vector<uint8_t> raw_payload;
        bool is_jump_target = false;
        bool is_dead = false;
    };

    static inline uint32_t readU32At(const std::vector<uint8_t>& code, size_t& ip) {
        if (ip + 4 > code.size()) { ip = code.size(); return 0; }
        uint32_t v;
        memcpy(&v, &code[ip], 4);
        ip += 4;
        return v;
    }

    static inline double readDoubleAt(const std::vector<uint8_t>& code, size_t& ip) {
        if (ip + 8 > code.size()) { ip = code.size(); return 0.0; }
        double d;
        memcpy(&d, &code[ip], 8);
        ip += 8;
        return d;
    }

    static inline std::string readStringAt(const std::vector<uint8_t>& code, size_t& ip) {
        uint32_t len = readU32At(code, ip);
        if (ip + len > code.size()) { ip = code.size(); return ""; }
        std::string s((const char*)&code[ip], len);
        ip += len;
        return s;
    }

    // Декодирование байт-кода в список IR-инструкций
    static std::vector<Instruction> decodeBytecode(const std::vector<uint8_t>& code, std::unordered_set<size_t>& jump_targets) {
        std::vector<Instruction> instrs;
        size_t ip = 0;

        while (ip < code.size()) {
            size_t start_ip = ip;
            uint8_t op = code[ip++];
            Instruction inst;
            inst.op = op;
            inst.orig_ip = start_ip;

            switch (op) {
                case OP_HALT:
                case OP_POP:
                case OP_NOP:
                case OP_ADD:
                case OP_SUB:
                case OP_MUL:
                case OP_DIV:
                case OP_NEG:
                case OP_MOD:
                case OP_CMP_EQ:
                case OP_CMP_NEQ:
                case OP_CMP_LT:
                case OP_CMP_GT:
                case OP_CMP_LTE:
                case OP_CMP_GTE:
                case OP_AND:
                case OP_OR:
                case OP_NOT:
                case OP_RET:
                case OP_LOG:
                case OP_LOG_EMPTY:
                case OP_WAIT:
                case OP_PRINT_RAW:
                case OP_LOAD_DB:
                case OP_UI_SHOW:
                case OP_GET_INDEX:
                case OP_SET_INDEX:
                case OP_ARRAY_PUSH:
                case OP_ARRAY_POP:
                case OP_PARAM_PROLOGUE:
                case OP_PARAM_EPILOGUE:
                    break;

                case OP_CONST_NUM:
                case OP_ADD_CONST:
                case OP_SUB_CONST:
                case OP_MUL_CONST:
                case OP_DIV_CONST:
                    inst.num_val = readDoubleAt(code, ip);
                    break;

                case OP_JMP:
                case OP_JMP_IF_FALSE:
                case OP_CALL_FUNC:
                case OP_JUMP:
                case OP_CALL_USER:
                    inst.u32_val = readU32At(code, ip);
                    jump_targets.insert(inst.u32_val);
                    break;

                case OP_THREAD_SPAWN:
                case OP_ASYNC_SPAWN:
                    inst.u32_val = readU32At(code, ip);
                    if (ip < code.size()) inst.u8_val = code[ip++];
                    jump_targets.insert(inst.u32_val);
                    break;

                case OP_NEW_ARRAY:
                    inst.u32_val = readU32At(code, ip);
                    break;

                case OP_CONST_STR:
                case OP_LOAD_VAR:
                case OP_STORE_VAR:
                case OP_INC_VAR:
                case OP_DEC_VAR:
                case OP_ASK:
                case OP_START_WEB:
                case OP_UI_CMD:
                case OP_NEW_STRUCT:
                case OP_GET_FIELD:
                case OP_SET_FIELD:
                    inst.str_val = readStringAt(code, ip);
                    break;

                case OP_CALL_BUILTIN:
                    inst.str_val = readStringAt(code, ip);
                    if (ip < code.size()) inst.u8_val = code[ip++];
                    break;

                case OP_UI_INIT:
                    inst.str_val = readStringAt(code, ip); // title
                    inst.u32_val = readU32At(code, ip);    // w
                    // Сохраняем оставшиеся аргументы (h, bg, fon) в raw_payload
                    {
                        size_t payload_start = ip;
                        readU32At(code, ip); // h
                        readStringAt(code, ip); // bg
                        readStringAt(code, ip); // fon
                        inst.raw_payload.assign(code.begin() + payload_start, code.begin() + ip);
                    }
                    break;

                case OP_UI_ADD_ELEM:
                    // Захватываем все поля в raw_payload для гарантированной сохранности
                    {
                        size_t payload_start = start_ip + 1;
                        readStringAt(code, ip); // type
                        readStringAt(code, ip); // id
                        readStringAt(code, ip); // text
                        readU32At(code, ip);    // x
                        readU32At(code, ip);    // y
                        readU32At(code, ip);    // w
                        readU32At(code, ip);    // h
                        readU32At(code, ip);    // align
                        readStringAt(code, ip); // color
                        readU32At(code, ip);    // fsize
                        readStringAt(code, ip); // fstyle
                        if (ip < code.size()) ip++; // glass
                        readStringAt(code, ip); // run
                        inst.raw_payload.assign(code.begin() + payload_start, code.begin() + ip);
                    }
                    break;

                default:
                    // Unknown opcode: abort decoding to prevent byte stream desync and bytecode corruption
                    return {};
            }
            instrs.push_back(inst);
        }

        for (auto& inst : instrs) {
            if (jump_targets.count(inst.orig_ip)) {
                inst.is_jump_target = true;
            }
        }
        return instrs;
    }

    std::vector<uint8_t> optimize(const std::vector<uint8_t>& bytecode, int opt_level, OptimizationStats* out_stats) {
        if (opt_level <= 0 || bytecode.empty()) {
            return bytecode;
        }

        OptimizationStats stats;
        stats.initial_bytes = bytecode.size();

        std::unordered_set<size_t> jump_targets;
        std::vector<Instruction> instrs = decodeBytecode(bytecode, jump_targets);
        if (instrs.empty()) {
            stats.optimized_bytes = bytecode.size();
            if (out_stats) *out_stats = stats;
            return bytecode;
        }

        int max_passes = (opt_level >= 3) ? 4 : (opt_level == 2 ? 2 : 1);

        for (int pass = 0; pass < max_passes; ++pass) {
            bool changed = false;

            // ── Пасс 1: Constant Folding (Сворачивание константных вычислений) ──
            for (size_t i = 0; i + 2 < instrs.size(); ++i) {
                if (instrs[i].is_dead || instrs[i + 1].is_dead || instrs[i + 2].is_dead) continue;
                if (instrs[i].op == OP_CONST_NUM && instrs[i + 1].op == OP_CONST_NUM) {
                    if (instrs[i + 1].is_jump_target || instrs[i + 2].is_jump_target) continue;

                    double a = instrs[i].num_val;
                    double b = instrs[i + 1].num_val;
                    uint8_t op = instrs[i + 2].op;

                    if (op == OP_ADD) {
                        instrs[i].num_val = a + b;
                        instrs[i + 1].is_dead = true;
                        instrs[i + 2].is_dead = true;
                        stats.constants_folded++;
                        changed = true;
                    } else if (op == OP_SUB) {
                        instrs[i].num_val = a - b;
                        instrs[i + 1].is_dead = true;
                        instrs[i + 2].is_dead = true;
                        stats.constants_folded++;
                        changed = true;
                    } else if (op == OP_MUL) {
                        instrs[i].num_val = a * b;
                        instrs[i + 1].is_dead = true;
                        instrs[i + 2].is_dead = true;
                        stats.constants_folded++;
                        changed = true;
                    } else if (op == OP_DIV && b != 0.0) {
                        instrs[i].num_val = a / b;
                        instrs[i + 1].is_dead = true;
                        instrs[i + 2].is_dead = true;
                        stats.constants_folded++;
                        changed = true;
                    }
                }
            }

            // ── Пасс 2: Opcode Fusion (Константная арифметика) ──
            if (opt_level >= 2) {
                for (size_t i = 0; i + 1 < instrs.size(); ++i) {
                    if (instrs[i].is_dead || instrs[i + 1].is_dead) continue;
                    if (instrs[i].op == OP_CONST_NUM && !instrs[i + 1].is_jump_target) {
                        uint8_t next_op = instrs[i + 1].op;
                        if (next_op == OP_ADD) {
                            instrs[i].op = OP_ADD_CONST;
                            instrs[i + 1].is_dead = true;
                            stats.opcodes_fused++;
                            changed = true;
                        } else if (next_op == OP_SUB) {
                            instrs[i].op = OP_SUB_CONST;
                            instrs[i + 1].is_dead = true;
                            stats.opcodes_fused++;
                            changed = true;
                        } else if (next_op == OP_MUL) {
                            instrs[i].op = OP_MUL_CONST;
                            instrs[i + 1].is_dead = true;
                            stats.opcodes_fused++;
                            changed = true;
                        } else if (next_op == OP_DIV && instrs[i].num_val != 0.0) {
                            instrs[i].op = OP_DIV_CONST;
                            instrs[i + 1].is_dead = true;
                            stats.opcodes_fused++;
                            changed = true;
                        }
                    }
                }
            }

            // ── Пасс 3: Opcode Fusion (Инкремент / Декремент переменной: var++ / var--) ──
            if (opt_level >= 2) {
                for (size_t i = 0; i + 2 < instrs.size(); ++i) {
                    if (instrs[i].is_dead || instrs[i + 1].is_dead || instrs[i + 2].is_dead) continue;
                    if (instrs[i].op == OP_LOAD_VAR &&
                        !instrs[i + 1].is_jump_target && !instrs[i + 2].is_jump_target &&
                        instrs[i + 2].op == OP_STORE_VAR &&
                        instrs[i].str_val == instrs[i + 2].str_val) {

                        if (instrs[i + 1].op == OP_ADD_CONST && instrs[i + 1].num_val == 1.0) {
                            instrs[i].op = OP_INC_VAR;
                            instrs[i + 1].is_dead = true;
                            instrs[i + 2].is_dead = true;
                            stats.opcodes_fused++;
                            changed = true;
                        } else if (instrs[i + 1].op == OP_SUB_CONST && instrs[i + 1].num_val == 1.0) {
                            instrs[i].op = OP_DEC_VAR;
                            instrs[i + 1].is_dead = true;
                            instrs[i + 2].is_dead = true;
                            stats.opcodes_fused++;
                            changed = true;
                        }
                    }
                }
            }

            // ── Пасс 4: Dead Code Elimination (Удаление недостижимого кода) ──
            if (opt_level >= 2) {
                for (size_t i = 0; i < instrs.size(); ++i) {
                    if (instrs[i].is_dead) continue;
                    if (instrs[i].op == OP_JMP || instrs[i].op == OP_RET) {
                        size_t j = i + 1;
                        while (j < instrs.size() && !instrs[j].is_jump_target) {
                            if (!instrs[j].is_dead) {
                                instrs[j].is_dead = true;
                                stats.dead_code_removed++;
                                changed = true;
                            }
                            j++;
                        }
                    }
                }
            }

            // Удаляем помеченные мёртвые инструкции из списка
            std::vector<Instruction> live;
            live.reserve(instrs.size());
            for (auto& inst : instrs) {
                if (!inst.is_dead) live.push_back(inst);
            }
            instrs = std::move(live);

            if (!changed) break;
        }

        // ── Пасс 5: Релокация переходов и сборка выходного байт-кода ──
        // 1. Предварительный расчёт новых смещений
        std::unordered_map<size_t, uint32_t> orig_to_new_offset;
        size_t current_offset = 0;

        for (const auto& inst : instrs) {
            orig_to_new_offset[inst.orig_ip] = (uint32_t)current_offset;
            current_offset += 1; // Код операции
            switch (inst.op) {
                case OP_CONST_NUM:
                case OP_ADD_CONST:
                case OP_SUB_CONST:
                case OP_MUL_CONST:
                case OP_DIV_CONST:
                    current_offset += 8;
                    break;
                case OP_JMP:
                case OP_JMP_IF_FALSE:
                case OP_CALL_FUNC:
                case OP_JUMP:
                case OP_CALL_USER:
                case OP_NEW_ARRAY:
                    current_offset += 4;
                    break;
                case OP_THREAD_SPAWN:
                case OP_ASYNC_SPAWN:
                    current_offset += 4 + 1;
                    break;
                case OP_CONST_STR:
                case OP_LOAD_VAR:
                case OP_STORE_VAR:
                case OP_INC_VAR:
                case OP_DEC_VAR:
                case OP_ASK:
                case OP_START_WEB:
                case OP_UI_CMD:
                case OP_NEW_STRUCT:
                case OP_GET_FIELD:
                case OP_SET_FIELD:
                    current_offset += 4 + inst.str_val.size();
                    break;
                case OP_CALL_BUILTIN:
                    current_offset += 4 + inst.str_val.size() + 1;
                    break;
                case OP_UI_INIT:
                    current_offset += 4 + inst.str_val.size() + 4 + inst.raw_payload.size();
                    break;
                case OP_UI_ADD_ELEM:
                    current_offset += inst.raw_payload.size();
                    break;
                default:
                    break;
            }
        }
        // Метка конца файла для переходов за пределы инструкций
        orig_to_new_offset[bytecode.size()] = (uint32_t)current_offset;

        // 2. Генерация финального оптимизированного байт-кода
        std::vector<uint8_t> result;
        result.reserve(current_offset);

        auto emitU32 = [&](uint32_t v) {
            uint8_t buf[4];
            memcpy(buf, &v, 4);
            result.insert(result.end(), buf, buf + 4);
        };

        auto emitDouble = [&](double d) {
            uint8_t buf[8];
            memcpy(buf, &d, 8);
            result.insert(result.end(), buf, buf + 8);
        };

        auto emitString = [&](const std::string& s) {
            emitU32((uint32_t)s.size());
            result.insert(result.end(), s.begin(), s.end());
        };

        for (const auto& inst : instrs) {
            result.push_back(inst.op);
            switch (inst.op) {
                case OP_CONST_NUM:
                case OP_ADD_CONST:
                case OP_SUB_CONST:
                case OP_MUL_CONST:
                case OP_DIV_CONST:
                    emitDouble(inst.num_val);
                    break;

                case OP_JMP:
                case OP_JMP_IF_FALSE:
                case OP_CALL_FUNC:
                case OP_JUMP:
                case OP_CALL_USER: {
                    uint32_t new_target = inst.u32_val;
                    auto it = orig_to_new_offset.find(inst.u32_val);
                    if (it != orig_to_new_offset.end()) {
                        new_target = it->second;
                    }
                    emitU32(new_target);
                    break;
                }

                case OP_THREAD_SPAWN:
                case OP_ASYNC_SPAWN: {
                    uint32_t new_target = inst.u32_val;
                    auto it = orig_to_new_offset.find(inst.u32_val);
                    if (it != orig_to_new_offset.end()) {
                        new_target = it->second;
                    }
                    emitU32(new_target);
                    result.push_back(inst.u8_val);
                    break;
                }

                case OP_NEW_ARRAY:
                    emitU32(inst.u32_val);
                    break;

                case OP_CONST_STR:
                case OP_LOAD_VAR:
                case OP_STORE_VAR:
                case OP_INC_VAR:
                case OP_DEC_VAR:
                case OP_ASK:
                case OP_START_WEB:
                case OP_UI_CMD:
                case OP_NEW_STRUCT:
                case OP_GET_FIELD:
                case OP_SET_FIELD:
                    emitString(inst.str_val);
                    break;

                case OP_CALL_BUILTIN:
                    emitString(inst.str_val);
                    result.push_back(inst.u8_val);
                    break;

                case OP_UI_INIT:
                    emitString(inst.str_val);
                    emitU32(inst.u32_val);
                    result.insert(result.end(), inst.raw_payload.begin(), inst.raw_payload.end());
                    break;

                case OP_UI_ADD_ELEM:
                    result.insert(result.end(), inst.raw_payload.begin(), inst.raw_payload.end());
                    break;

                default:
                    break;
            }
        }

        stats.optimized_bytes = result.size();
        if (out_stats) *out_stats = stats;
        return result;
    }

} // namespace GrassOptimizer

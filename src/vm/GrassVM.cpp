#include "vm/GrassVM.h"
#include "GrassVersion.h"
#include "media/FastMath.h"
#include "baremetal/GrassASM.h"
#include "baremetal/GrassKeyboard.h"
#include "runtime/GrassHardware.h"
#include "baremetal/GrassBoot.h"
#include "baremetal/GrassBaremetal.h"
#include "media/CoreMath.h"
#include "runtime/GrassWeb.h"
#include "vm/GrassMemory.h"
#include "runtime/GrassRandom.h"
#include "baremetal/GSE.h"
#include "media/gui_engine.h"
#include "runtime/GrassTime.h"
#include "runtime/GrassNet.h"
#include "runtime/GrassJson.h"
#include "runtime/GrassSys.h"
#include "runtime/GrassProc.h"
#include "runtime/GrassFsys.h"
#include "runtime/GrassTg.h"
#include "media/Grass2DGraphics.h"
#include "media/GrassMath3D.h"
#include "media/GrassGfx3D.h"
#include "media/GrassPhysics.h"
#include "runtime/GrassIO.h"
#include "baremetal/GrassHAL.h"
#include "vm/GrassJIT.h"
#include "runtime/GrassThread.h"
#include "baremetal/GrassMMIO.h"
#include "media/GrassAudio.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <thread>
#endif

using namespace std;

GrassArena g_vm_arena(4 * 1024 * 1024);
unordered_map<string, GrassStructDef> g_struct_registry;
bool g_not_clear_active = false;
std::vector<std::string> g_protected_console_lines;

static string wstring_to_utf8_helper(const wstring& wstr) {
    if (wstr.empty()) return "";
#if defined(_WIN32)
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
    if (!str.empty() && str.back() == '\0') str.pop_back();
    return str;
#else
    return string(wstr.begin(), wstr.end());
#endif
}

GrassVM::GrassVM(shared_ptr<const vector<uint8_t>> bytecode,
                 shared_ptr<GrassSharedEnv> shared,
                 bool root)
    : bytecode_ptr(bytecode ? std::move(bytecode) : make_shared<const vector<uint8_t>>()),
      code(*bytecode_ptr),
      shared_env(shared),
      is_root_vm(root) {
    if (is_root_vm && !shared_env) {
        shared_env = make_shared<GrassSharedEnv>();
    }
    stack.reserve(512);
    frames.reserve(64);
    frames.push_back({0, {}});
    vars.reserve(128);
    heap_arrays.reserve(64);
}

GrassVM::GrassVM(const vector<uint8_t>& bytecode,
                 shared_ptr<GrassSharedEnv> shared,
                 bool root)
    : GrassVM(make_shared<const vector<uint8_t>>(bytecode), shared, root) {}

GrassVM::GrassVM(vector<uint8_t>&& bytecode,
                 shared_ptr<GrassSharedEnv> shared,
                 bool root)
    : GrassVM(make_shared<const vector<uint8_t>>(std::move(bytecode)), shared, root) {}

GrassVM::~GrassVM() {
    for (GrassArray* a : heap_arrays) delete a;
}

bool GrassVM::tryRunJitLoop(uint32_t loop_start, uint32_t loop_end) {
        if (failed_loops.count(loop_start)) return false;

        // BUG-17: Защита доступа к shared_env->vars мьютексом
        bool is_shared = (frames.size() <= 1) && is_root_vm && (shared_env != nullptr);
        std::unique_lock<std::mutex> lk;
        if (is_shared) {
            lk = std::unique_lock<std::mutex>(shared_env->mtx);
        }

        auto& active_vars = frames.empty() ? ((is_root_vm && shared_env) ? shared_env->vars : vars) : frames.back().locals;
        auto jit_it = jit_cache.find(loop_start);
        if (jit_it != jit_cache.end()) {
            GrassJIT::executeJitBlock(jit_it->second, active_vars);
            ip = jit_it->second->end_ip;
            return true;
        }
        int& trips = loop_trip_counters[loop_start];
        trips++;
        if (trips >= 2) {
            auto block = GrassJIT::tryCompileLoop(code, loop_start, loop_end, active_vars);
            if (block && block->is_valid) {
                jit_cache[loop_start] = block;
                GrassJIT::executeJitBlock(block, active_vars);
                ip = block->end_ip;
                return true;
            } else {
                failed_loops.insert(loop_start);
            }
        }
        return false;
    }

void GrassVM::syncDBToVars() {
        if (is_root_vm && shared_env) {
            std::lock_guard<std::mutex> lk(shared_env->mtx);
            for (const auto& kv : GrassDB::get_all_records()) {
                shared_env->vars[kv.first] = GrassValue::Str(kv.second.value);
                shared_env->vars[kv.first + "()"] = GrassValue::Str(kv.second.value);
            }
            for (const auto& kv : variables_cache) {
                shared_env->vars[kv.first] = GrassValue::Str(wstring_to_utf8_helper(kv.second));
            }
        } else {
            for (const auto& kv : GrassDB::get_all_records()) {
                vars[kv.first] = GrassValue::Str(kv.second.value);
                vars[kv.first + "()"] = GrassValue::Str(kv.second.value);
            }
            for (const auto& kv : variables_cache) {
                vars[kv.first] = GrassValue::Str(wstring_to_utf8_helper(kv.second));
            }
        }
    }

void GrassVM::syncVarsToGUI() {
        if (is_root_vm && shared_env) {
            std::lock_guard<std::mutex> lk(shared_env->mtx);
            for (const auto& kv : shared_env->vars) {
                GrassGUI::set_variable(kv.first, kv.second.toString());
            }
        }
        for (const auto& kv : vars) {
            GrassGUI::set_variable(kv.first, kv.second.toString());
        }
        for (const auto& f : frames) {
            for (const auto& kv : f.locals) {
                GrassGUI::set_variable(kv.first, kv.second.toString());
            }
        }
    }

void GrassVM::run_callback(uint32_t cb_ip) {
        if (cb_ip == 0 || cb_ip >= code.size()) return;
        syncVarsToGUI();
        frames.push_back({ (size_t)0xFFFFFFFF, {} });
        size_t saved_ip = ip;
        execute(cb_ip);
        ip = saved_ip;
        syncVarsToGUI();
        GrassGUI::refresh_elements();
    }

bool GrassVM::isGdbSchemaKey(const string& fn) {
        return fn == "user" || fn == "last_log" || fn == "net_tunnel_protocol" ||
               fn == "net_baypass_state" || fn == "net_bypass_state" || fn == "net_gateway_route" ||
               fn == "remember_me" || fn == "auth_token_hash" || fn == "env_target_runtime" ||
               fn == "env_exec_flags" || fn == "net_bypass_config" || fn == "net_ping_ms" ||
               fn == "env_allocated_ram" || fn == "env_java_path" || fn == "sys_fps_stabilizer" ||
               fn == "sys_gpu_force_integrated" || fn == "user_uuid" || fn == "user_role" ||
               fn == "sys_cpu_threads" || fn == "sys_window_mode" || fn == "net_dns_server" ||
               fn == "net_mtu_size";
    }

int64_t GrassVM::spawnThread(uint32_t entry_ip, uint8_t argc, bool detached) {
        vector<GrassValue> args(argc);
        for (int i = (int)argc - 1; i >= 0; --i) {
            args[i] = pop();
        }

        if (!shared_env) {
            shared_env = make_shared<GrassSharedEnv>();
        }
        auto shared = shared_env;
        auto bc_ptr = this->bytecode_ptr;

        int64_t tid = GrassThread::create_thread([bc_ptr, shared, entry_ip, args = std::move(args)]() -> GrassValue {
            GrassVM thread_vm(bc_ptr, shared, false);
            for (size_t i = 0; i < args.size(); ++i) {
                thread_vm.push(args[i]);
            }
            thread_vm.execute(entry_ip);
            return thread_vm.getLastReturnValue();
        }, detached);

        return tid;
    }

void GrassVM::dispatchBuiltin(const string& fn, uint8_t argc, vector<GrassValue>& args) {
    if (callBuiltinCore(fn, argc, args)) return;
    if (callBuiltinMedia(fn, argc, args)) return;
    if (callBuiltinSys(fn, argc, args)) return;
    if (callBuiltinLowLevel(fn, argc, args)) return;
    if (vars.count(fn + "()")) push(vars[fn + "()"]);
    else if (vars.count(fn)) push(vars[fn]);
    else if (shared_env && shared_env->vars.count(fn)) push(shared_env->vars[fn]);
    else if (GrassDB::has(fn)) push(GrassValue::Str(GrassDB::get(fn)));
    else push(GrassValue::Num(0.0));
}

void GrassVM::execute(size_t start_ip) {
        ip = start_ip;
        size_t op_counter = 0;

        // ── Табличная диспетчеризация (Labels-as-Values / Computed Goto) ──
        static const void* dispatch_table[256];
        static bool table_init = false;
        if (!table_init) {
            for (int i = 0; i < 256; ++i) dispatch_table[i] = &&do_unknown;
            dispatch_table[OP_HALT]          = &&do_halt;
            dispatch_table[OP_CONST_NUM]     = &&do_const_num;
            dispatch_table[OP_CONST_STR]     = &&do_const_str;
            dispatch_table[OP_LOAD_VAR]      = &&do_load_var;
            dispatch_table[OP_STORE_VAR]     = &&do_store_var;
            dispatch_table[OP_DEF_VAR]       = &&do_def_var;
            dispatch_table[OP_POP]           = &&do_pop;
            dispatch_table[OP_ADD]           = &&do_add;
            dispatch_table[OP_SUB]           = &&do_sub;
            dispatch_table[OP_MUL]           = &&do_mul;
            dispatch_table[OP_DIV]           = &&do_div;
            dispatch_table[OP_NEG]           = &&do_neg;
            dispatch_table[OP_MOD]           = &&do_mod;
            dispatch_table[OP_BIT_AND]       = &&do_bit_and;
            dispatch_table[OP_BIT_OR]        = &&do_bit_or;
            dispatch_table[OP_BIT_XOR]       = &&do_bit_xor;
            dispatch_table[OP_BIT_NOT]       = &&do_bit_not;
            dispatch_table[OP_BIT_SHL]       = &&do_bit_shl;
            dispatch_table[OP_BIT_SHR]       = &&do_bit_shr;
            dispatch_table[OP_CMP_EQ]        = &&do_cmp_eq;
            dispatch_table[OP_CMP_NEQ]       = &&do_cmp_neq;
            dispatch_table[OP_CMP_LT]        = &&do_cmp_lt;
            dispatch_table[OP_CMP_GT]        = &&do_cmp_gt;
            dispatch_table[OP_CMP_LTE]       = &&do_cmp_lte;
            dispatch_table[OP_CMP_GTE]       = &&do_cmp_gte;
            dispatch_table[OP_AND]           = &&do_and;
            dispatch_table[OP_OR]            = &&do_or;
            dispatch_table[OP_NOT]           = &&do_not;
            dispatch_table[OP_JMP]           = &&do_jmp;
            dispatch_table[OP_JMP_IF_FALSE]  = &&do_jmp_if_false;
            dispatch_table[OP_CALL_FUNC]     = &&do_call_func;
            dispatch_table[OP_RET]           = &&do_ret;
            dispatch_table[OP_LOG]           = &&do_log;
            dispatch_table[OP_LOG_EMPTY]     = &&do_log_empty;
            dispatch_table[OP_WAIT]          = &&do_wait;
            dispatch_table[OP_ASK]           = &&do_ask;
            dispatch_table[OP_PRINT_RAW]     = &&do_print_raw;
            dispatch_table[OP_CALL_BUILTIN]  = &&do_call_builtin;
            dispatch_table[OP_LOAD_DB]       = &&do_load_db;
            dispatch_table[OP_START_WEB]     = &&do_start_web;
            dispatch_table[OP_UI_INIT]       = &&do_ui_init;
            dispatch_table[OP_UI_ADD_ELEM]   = &&do_ui_add_elem;
            dispatch_table[OP_UI_SHOW]       = &&do_ui_show;
            dispatch_table[OP_UI_CMD]        = &&do_ui_cmd;
            dispatch_table[OP_NEW_ARRAY]     = &&do_new_array;
            dispatch_table[OP_GET_INDEX]     = &&do_get_index;
            dispatch_table[OP_SET_INDEX]     = &&do_set_index;
            dispatch_table[OP_ARRAY_PUSH]    = &&do_array_push;
            dispatch_table[OP_ARRAY_POP]     = &&do_array_pop;
            dispatch_table[OP_NEW_STRUCT]    = &&do_new_struct;
            dispatch_table[OP_GET_FIELD]     = &&do_get_field;
            dispatch_table[OP_SET_FIELD]     = &&do_set_field;
            dispatch_table[OP_JUMP]          = &&do_jmp;
            dispatch_table[OP_CALL_USER]     = &&do_call_func;
            dispatch_table[OP_PARAM_PROLOGUE] = &&do_nop;
            dispatch_table[OP_PARAM_EPILOGUE] = &&do_nop;
            dispatch_table[OP_NOP]           = &&do_nop;
            dispatch_table[OP_ADD_CONST]     = &&do_add_const;
            dispatch_table[OP_SUB_CONST]     = &&do_sub_const;
            dispatch_table[OP_MUL_CONST]     = &&do_mul_const;
            dispatch_table[OP_DIV_CONST]     = &&do_div_const;
            dispatch_table[OP_INC_VAR]       = &&do_inc_var;
            dispatch_table[OP_DEC_VAR]       = &&do_dec_var;
            dispatch_table[OP_THREAD_SPAWN]  = &&do_thread_spawn;
            dispatch_table[OP_ASYNC_SPAWN]   = &&do_async_spawn;
            dispatch_table[OP_ADDR_VAR]      = &&do_addr_var;
            dispatch_table[OP_DEREF]         = &&do_deref;
            dispatch_table[OP_STORE_DEREF]   = &&do_store_deref;
            dispatch_table[OP_ADDR_INDEX]    = &&do_addr_index;
            dispatch_table[OP_ADDR_FIELD]    = &&do_addr_field;
            table_init = true;
        }

        auto find_var_cell = [&](const string& name) -> GrassValue* {
            for (int fi = (int)frames.size() - 1; fi >= 0; --fi) {
                auto it = frames[fi].locals.find(name);
                if (it != frames[fi].locals.end()) return &it->second;
            }
            if (shared_env) {
                std::lock_guard<std::mutex> lk(shared_env->mtx);
                auto sit = shared_env->vars.find(name);
                if (sit != shared_env->vars.end()) return &sit->second;
            }
            auto vit = vars.find(name);
            if (vit != vars.end()) return &vit->second;
            return nullptr;
        };

        #define DISPATCH_STEP() \
            do { \
                if ((++op_counter & 255) == 0 && GrassMemory::is_limit_exceeded()) { \
                    cerr << "\n[GrassGreen Error]: Превышен установленный лимит памяти (memory_limit)!\n"; \
                    return; \
                } \
                if (ip >= code.size()) return; \
                goto *dispatch_table[readByte()]; \
            } while (0)

        DISPATCH_STEP();

    do_halt:
        return;

    do_nop:
        DISPATCH_STEP();

    do_add_const: {
        double c = readDouble();
        GrassValue a = pop();
        if (a.is_ptr()) {
            GrassVMPtr* orig = a.asPtr();
            int64_t off = (int64_t)c;
            if (!orig) push(GrassValue::Num(0.0));
            else {
                GrassVMPtr* np = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
                *np = *orig;
                if (np->vec) np->vec_idx += off;
                else if (np->raw) np->raw_off += off * (np->elem_size > 0 ? np->elem_size : 1);
                else np->cell += off;
                push(GrassValue::Ptr(np));
            }
        }
        else if (a.isNumeric()) push(GrassValue::Num(a.toNumber() + c));
        else if (a.is_str()) push(GrassValue::Str(a.toString() + GrassValue::Num(c).toString()));
        else push(GrassValue::Num(a.toNumber() + c));
        DISPATCH_STEP();
    }

    do_sub_const: {
        double c = readDouble();
        GrassValue a = pop();
        if (a.is_ptr()) {
            GrassVMPtr* orig = a.asPtr();
            int64_t off = (int64_t)c;
            if (!orig) push(GrassValue::Num(0.0));
            else {
                GrassVMPtr* np = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
                *np = *orig;
                if (np->vec) np->vec_idx -= off;
                else if (np->raw) np->raw_off -= off * (np->elem_size > 0 ? np->elem_size : 1);
                else np->cell -= off;
                push(GrassValue::Ptr(np));
            }
        } else {
            push(GrassValue::Num(a.toNumber() - c));
        }
        DISPATCH_STEP();
    }

    do_mul_const: {
        double c = readDouble();
        GrassValue a = pop();
        push(GrassValue::Num(a.toNumber() * c));
        DISPATCH_STEP();
    }

    do_div_const: {
        double c = readDouble();
        GrassValue a = pop();
        push(GrassValue::Num(c != 0.0 ? a.toNumber() / c : 0.0));
        DISPATCH_STEP();
    }

    do_inc_var: {
        {
            string name = readString();
            bool done = false;
            if (!frames.empty() && frames.back().locals.count(name)) {
                double n = frames.back().locals[name].toNumber() + 1.0;
                frames.back().locals[name] = GrassValue::Num(n);
                if (is_root_vm && frames.size() == 2 && shared_env && name.rfind("g_", 0) == 0) {
                    std::lock_guard<std::mutex> lk(shared_env->mtx);
                    if (shared_env->vars.count(name)) shared_env->vars[name] = GrassValue::Num(n);
                }
                done = true;
            }
            if (!done && shared_env) {
                std::lock_guard<std::mutex> lk(shared_env->mtx);
                auto it = shared_env->vars.find(name);
                if (it != shared_env->vars.end()) {
                    it->second = GrassValue::Num(it->second.toNumber() + 1.0);
                    done = true;
                }
            }
            if (!done) {
                if (!frames.empty()) {
                    frames.back().locals[name] = GrassValue::Num(1.0);
                } else {
                    vars[name] = GrassValue::Num(1.0);
                }
            }
        }
        DISPATCH_STEP();
    }

    do_dec_var: {
        {
            string name = readString();
            bool done = false;
            if (!frames.empty() && frames.back().locals.count(name)) {
                double n = frames.back().locals[name].toNumber() - 1.0;
                frames.back().locals[name] = GrassValue::Num(n);
                if (is_root_vm && frames.size() == 2 && shared_env && name.rfind("g_", 0) == 0) {
                    std::lock_guard<std::mutex> lk(shared_env->mtx);
                    if (shared_env->vars.count(name)) shared_env->vars[name] = GrassValue::Num(n);
                }
                done = true;
            }
            if (!done && shared_env) {
                std::lock_guard<std::mutex> lk(shared_env->mtx);
                auto it = shared_env->vars.find(name);
                if (it != shared_env->vars.end()) {
                    it->second = GrassValue::Num(it->second.toNumber() - 1.0);
                    done = true;
                }
            }
            if (!done) {
                if (!frames.empty()) {
                    frames.back().locals[name] = GrassValue::Num(-1.0);
                } else {
                    vars[name] = GrassValue::Num(-1.0);
                }
            }
        }
        DISPATCH_STEP();
    }


    do_const_num:
        push(GrassValue::Num(readDouble()));
        DISPATCH_STEP();

    do_const_str:
        push(GrassValue::Str(readString()));
        DISPATCH_STEP();

    do_jmp: {
        uint32_t target = readU32();
        if (GrassJIT::is_enabled() && target < ip) {
            if (tryRunJitLoop(target, (uint32_t)ip)) {
                DISPATCH_STEP();
            }
        }
        ip = target;
        DISPATCH_STEP();
    }

    do_jmp_if_false: {
        uint32_t target = readU32();
        if (!pop().isTruthy()) ip = target;
        DISPATCH_STEP();
    }

    do_call_func: {
        uint32_t target_ip = readU32();
        auto it = attached_ip_sounds.find(target_ip);
        if (it != attached_ip_sounds.end()) {
            GrassAudio::synth_play(it->second.waveform, it->second.freq_hz, it->second.duration_ms, it->second.volume);
        }
        frames.push_back({ip, {}}); // Создаем новый локальный фрейм вызова
        ip = target_ip;
        DISPATCH_STEP();
    }

    do_ret: {
        if (!stack.empty()) {
            last_return_value = stack.back();
        }
        if (frames.size() > 1) {
            size_t ret_ip = frames.back().return_ip;
            frames.pop_back(); // Уничтожаем локальный фрейм завершенной функции
            if (ret_ip == (size_t)0xFFFFFFFF) {
                return; // Завершение выполнения VM callback
            }
            ip = ret_ip;
        } else {
            return;
        }
        DISPATCH_STEP();
    }

    do_load_var: {
        {
            string name = readString();
            bool found = false;
            GrassValue val;
            // 1. Поиск во всех локальных фреймах от текущего к внешним
            for (int fi = (int)frames.size() - 1; fi >= 0; --fi) {
                auto it = frames[fi].locals.find(name);
                if (it != frames[fi].locals.end()) {
                    val = it->second;
                    found = true;
                    break;
                }
            }
            // 2. Поиск в разделяемых глобальных переменных (shared_env)
            if (!found && shared_env) {
                std::lock_guard<std::mutex> lk(shared_env->mtx);
                auto sit = shared_env->vars.find(name);
                if (sit != shared_env->vars.end()) {
                    val = sit->second;
                    found = true;
                }
            }
            // 3. Поиск в локальных переменных потока
            if (!found && !is_root_vm) {
                auto it = vars.find(name);
                if (it != vars.end()) {
                    val = it->second;
                    found = true;
                }
            }
            if (found) {
                push(val);
            } else if (name == "proc_mem" || name == "ram_used") {
                push(GrassValue::Num((double)GrassHardware::ram_used_mb()));
            } else if (name == "ram_total") {
                push(GrassValue::Num((double)GrassHardware::ram_total_mb()));
            } else if (name == "ram_free") {
                push(GrassValue::Num((double)GrassHardware::ram_free_mb()));
            } else if (GrassDB::has(name)) {
                push(GrassValue::Str(GrassDB::get(name)));
            } else if (GrassGUI::has_element(name)) {
                push(GrassValue::Str(name));
            } else {
                push(GrassValue::Str(""));
            }
        }
        DISPATCH_STEP();
    }

    do_def_var: {
        {
            string name = readString();
            GrassValue val = pop();
            if (frames.size() > 1) {
                frames.back().locals[name] = val;
                if (is_root_vm && frames.size() == 2 && shared_env && name.rfind("g_", 0) == 0) {
                    std::lock_guard<std::mutex> lk(shared_env->mtx);
                    shared_env->vars[name] = val;
                }
            } else {
                if (!frames.empty()) {
                    frames[0].locals[name] = val;
                } else {
                    vars[name] = val;
                }
                if (shared_env) {
                    std::lock_guard<std::mutex> lk(shared_env->mtx);
                    shared_env->vars[name] = std::move(val);
                }
            }
            GrassGUI::set_variable(name, val.toString());
        }
        DISPATCH_STEP();
    }

    do_store_var: {
        {
            string name = readString();
            GrassValue val = pop();
            bool stored = false;
            // 1. Поиск во всех локальных фреймах от текущего к внешним
            for (int fi = (int)frames.size() - 1; fi >= 0; --fi) {
                if (frames[fi].locals.count(name)) {
                    frames[fi].locals[name] = val;
                    stored = true;
                    break;
                }
            }
            if (!stored && shared_env) {
                std::lock_guard<std::mutex> lk(shared_env->mtx);
                auto sit = shared_env->vars.find(name);
                if (sit != shared_env->vars.end()) {
                    sit->second = val;
                    stored = true;
                }
            }
            if (!stored) {
                if (!frames.empty()) {
                    frames.back().locals[name] = val;
                } else {
                    vars[name] = val;
                }
            }
            GrassGUI::set_variable(name, val.toString());
        }
        DISPATCH_STEP();
    }

    do_pop:
        pop();
        DISPATCH_STEP();

    do_add: {
        GrassValue b = pop(), a = pop();
        if (a.is_ptr() || b.is_ptr()) {
            GrassValue pv = a.is_ptr() ? a : b;
            int64_t off = (int64_t)(a.is_ptr() ? b.toNumber() : a.toNumber());
            GrassVMPtr* orig = pv.asPtr();
            if (!orig) {
                push(GrassValue::Num(0.0));
            } else {
                GrassVMPtr* np = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
                *np = *orig;
                if (np->vec) np->vec_idx += off;
                else if (np->raw) np->raw_off += off * (np->elem_size > 0 ? np->elem_size : 1);
                else np->cell += off;
                push(GrassValue::Ptr(np));
            }
        }
        else if (a.is_str() && b.is_str()) push(GrassValue::Str(a.toString() + b.toString()));
        else if (a.isNumeric() && b.isNumeric()) push(GrassValue::Num(a.toNumber() + b.toNumber()));
        else if (a.is_str() || b.is_str()) push(GrassValue::Str(a.toString() + b.toString()));
        else push(GrassValue::Num(a.toNumber() + b.toNumber()));
        DISPATCH_STEP();
    }

    do_sub: {
        GrassValue b = pop(), a = pop();
        if (a.is_ptr() && b.is_ptr()) {
            GrassVMPtr* p1 = a.asPtr();
            GrassVMPtr* p2 = b.asPtr();
            int64_t diff = 0;
            if (p1 && p2) {
                if (p1->vec && p1->vec == p2->vec) {
                    diff = p1->vec_idx - p2->vec_idx;
                } else if (p1->raw && p1->raw == p2->raw) {
                    int64_t es = p1->elem_size > 0 ? p1->elem_size : 1;
                    diff = (p1->raw_off - p2->raw_off) / es;
                } else if (p1->cell && p2->cell) {
                    diff = (int64_t)(p1->cell - p2->cell);
                }
            }
            push(GrassValue::Num((double)diff));
        } else if (a.is_ptr()) {
            GrassVMPtr* orig = a.asPtr();
            int64_t off = (int64_t)b.toNumber();
            if (!orig) {
                push(GrassValue::Num(0.0));
            } else {
                GrassVMPtr* np = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
                *np = *orig;
                if (np->vec) np->vec_idx -= off;
                else if (np->raw) np->raw_off -= off * (np->elem_size > 0 ? np->elem_size : 1);
                else np->cell -= off;
                push(GrassValue::Ptr(np));
            }
        } else {
            push(GrassValue::Num(a.toNumber() - b.toNumber()));
        }
        DISPATCH_STEP();
    }

    do_mul: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num(a.toNumber() * b.toNumber()));
        DISPATCH_STEP();
    }

    do_div: {
        GrassValue b = pop(), a = pop();
        double div = b.toNumber();
        push(GrassValue::Num(div != 0.0 ? a.toNumber() / div : 0.0));
        DISPATCH_STEP();
    }

    do_mod: {
        GrassValue b = pop(), a = pop();
        long long bv = (long long)b.toNumber();
        push(GrassValue::Num(bv != 0 ? (double)((long long)a.toNumber() % bv) : 0.0));
        DISPATCH_STEP();
    }

    do_bit_and: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num((double)((long long)a.toNumber() & (long long)b.toNumber())));
        DISPATCH_STEP();
    }

    do_bit_or: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num((double)((long long)a.toNumber() | (long long)b.toNumber())));
        DISPATCH_STEP();
    }

    do_bit_xor: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num((double)((long long)a.toNumber() ^ (long long)b.toNumber())));
        DISPATCH_STEP();
    }

    do_bit_not: {
        GrassValue a = pop();
        push(GrassValue::Num((double)(~(long long)a.toNumber())));
        DISPATCH_STEP();
    }

    do_bit_shl: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num((double)((long long)a.toNumber() << (long long)b.toNumber())));
        DISPATCH_STEP();
    }

    do_bit_shr: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num((double)((long long)a.toNumber() >> (long long)b.toNumber())));
        DISPATCH_STEP();
    }

    do_neg: {
        GrassValue a = pop();
        push(GrassValue::Num(-a.toNumber()));
        DISPATCH_STEP();
    }

    do_not: {
        GrassValue a = pop();
        push(GrassValue::Num(a.isTruthy() ? 0.0 : 1.0));
        DISPATCH_STEP();
    }

    do_and: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num((a.isTruthy() && b.isTruthy()) ? 1.0 : 0.0));
        DISPATCH_STEP();
    }

    do_or: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num((a.isTruthy() || b.isTruthy()) ? 1.0 : 0.0));
        DISPATCH_STEP();
    }

    do_cmp_eq: {
        GrassValue b = pop(), a = pop();
        if (a.is_ptr() || b.is_ptr()) {
            auto ptr_is_null = [](const GrassValue& v) -> bool {
                if (v.is_nil()) return true;
                if (v.is_num()) return v.toNumber() == 0.0;
                if (v.is_ptr()) {
                    GrassVMPtr* p = v.asPtr();
                    return !p || (!p->cell && !p->vec && !p->raw && !p->struct_inst);
                }
                return false;
            };
            bool eq = false;
            if (a.is_ptr() && b.is_ptr()) {
                GrassVMPtr* p1 = a.asPtr();
                GrassVMPtr* p2 = b.asPtr();
                if (!p1 && !p2) eq = true;
                else if (p1 && p2) {
                    eq = (p1->cell == p2->cell && p1->vec == p2->vec && p1->vec_idx == p2->vec_idx &&
                          p1->raw == p2->raw && p1->raw_off == p2->raw_off && p1->struct_inst == p2->struct_inst);
                }
            } else {
                eq = ptr_is_null(a) && ptr_is_null(b);
            }
            push(GrassValue::Num(eq ? 1.0 : 0.0));
        } else if (a.is_str() && b.is_str()) {
            const char* sa = a.asCStr();
            const char* sb = b.asCStr();
            push(GrassValue::Num((sa && sb && strcmp(sa, sb) == 0) ? 1.0 : 0.0));
        } else {
            push(GrassValue::Num(a.toNumber() == b.toNumber() ? 1.0 : 0.0));
        }
        DISPATCH_STEP();
    }

    do_cmp_neq: {
        GrassValue b = pop(), a = pop();
        if (a.is_ptr() || b.is_ptr()) {
            auto ptr_is_null = [](const GrassValue& v) -> bool {
                if (v.is_nil()) return true;
                if (v.is_num()) return v.toNumber() == 0.0;
                if (v.is_ptr()) {
                    GrassVMPtr* p = v.asPtr();
                    return !p || (!p->cell && !p->vec && !p->raw && !p->struct_inst);
                }
                return false;
            };
            bool eq = false;
            if (a.is_ptr() && b.is_ptr()) {
                GrassVMPtr* p1 = a.asPtr();
                GrassVMPtr* p2 = b.asPtr();
                if (!p1 && !p2) eq = true;
                else if (p1 && p2) {
                    eq = (p1->cell == p2->cell && p1->vec == p2->vec && p1->vec_idx == p2->vec_idx &&
                          p1->raw == p2->raw && p1->raw_off == p2->raw_off && p1->struct_inst == p2->struct_inst);
                }
            } else {
                eq = ptr_is_null(a) && ptr_is_null(b);
            }
            push(GrassValue::Num(eq ? 0.0 : 1.0));
        } else if (a.is_str() && b.is_str()) {
            const char* sa = a.asCStr();
            const char* sb = b.asCStr();
            push(GrassValue::Num((sa && sb && strcmp(sa, sb) != 0) ? 1.0 : 0.0));
        } else {
            push(GrassValue::Num(a.toNumber() != b.toNumber() ? 1.0 : 0.0));
        }
        DISPATCH_STEP();
    }

    do_cmp_lt: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num(a.toNumber() <  b.toNumber() ? 1.0 : 0.0));
        DISPATCH_STEP();
    }

    do_cmp_gt: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num(a.toNumber() >  b.toNumber() ? 1.0 : 0.0));
        DISPATCH_STEP();
    }

    do_cmp_lte: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num(a.toNumber() <= b.toNumber() ? 1.0 : 0.0));
        DISPATCH_STEP();
    }

    do_cmp_gte: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num(a.toNumber() >= b.toNumber() ? 1.0 : 0.0));
        DISPATCH_STEP();
    }

    do_log: {
        {
            string s = pop().toString();
            GrassIO::write_stdout(s.data(), s.size());
            GrassIO::write_stdout("\n", 1);
        }
        DISPATCH_STEP();
    }

    do_print_raw: {
        {
            string s = pop().toString();
            GrassIO::write_stdout(s.data(), s.size());
            if (g_not_clear_active) {
                if (g_protected_console_lines.empty()) g_protected_console_lines.push_back("");
                g_protected_console_lines.back() += s;
            }
        }
        DISPATCH_STEP();
    }

    do_log_empty: {
        GrassIO::write_stdout("\n", 1);
        if (g_not_clear_active) {
            g_protected_console_lines.push_back("");
        }
        DISPATCH_STEP();
    }

    do_wait: {
        int ms = (int)pop().toNumber();
        if (ms > 0) {
#if defined(_WIN32)
            Sleep(ms);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
        }
        DISPATCH_STEP();
    }

    do_ask: {
        {
            string var_name = readString();
            char in_buf[1024];
            size_t len = GrassIO::read_line(in_buf, sizeof(in_buf));
            GrassValue v = GrassValue::Str(string(in_buf, len));
            if (is_root_vm && shared_env) {
                std::lock_guard<std::mutex> lk(shared_env->mtx);
                shared_env->vars[var_name] = v;
            } else {
                vars[var_name] = v;
            }
        }
        DISPATCH_STEP();
    }

    do_load_db: {
        GrassDB::init_default_schema();
        GrassStorageEngine_Load("storage.gdb");
        syncDBToVars();
        DISPATCH_STEP();
    }

    do_start_web: {
        {
            string fname = readString();
            GrassWeb_LoadPage(fname);
        }
        DISPATCH_STEP();
    }

    do_call_builtin: {
        {
            string fn = readString();
            uint8_t argc = readByte();
            vector<GrassValue> args(argc);
            for (int i = (int)argc - 1; i >= 0; --i) args[i] = pop();

            dispatchBuiltin(fn, argc, args);
        }
        DISPATCH_STEP();
    }

    do_ui_init: {
        {
            string title = readString();
            int w = (int)readU32();
            int h = (int)readU32();
            string bg = readString();
            string fon = readString();
            if (vars.count(title)) title = vars[title].toString();

            GrassGUI::reset();
            syncVarsToGUI();
            GrassGUI::exec_command("window");
            GrassGUI::exec_command("title(!!" + title + "!!)");
            GrassGUI::exec_command("size(" + to_string(w) + ", " + to_string(h) + ")");
            if (!bg.empty()) GrassGUI::exec_command("color(" + bg + ")");
            if (!fon.empty()) GrassGUI::exec_command("fon(" + fon + ")");
        }
        DISPATCH_STEP();
    }

    do_ui_add_elem: {
        {
            string type = readString();
            string id = readString();
            string text = readString();
            int x = (int)readU32();
            int y = (int)readU32();
            int w = (int)readU32();
            int h = (int)readU32();
            int align = (int)readU32();
            string color = readString();
            int fsize = (int)readU32();
            string fstyle = readString();
            bool glass = (readByte() != 0);
            string run = readString();

            if (vars.count(text)) text = vars[text].toString();

            GrassGUI::exec_command(type + " !!" + text + "!!");
            if (!id.empty()) GrassGUI::exec_command("id(" + id + ")");
            GrassGUI::exec_command("window-ob(" + to_string(x) + ", " + to_string(y) + ")");
            GrassGUI::exec_command("size(" + to_string(w) + ", " + to_string(h) + ")");
            if (align == 1) GrassGUI::exec_command("middle");
            else if (align == 2) GrassGUI::exec_command("left");
            else if (align == 3) GrassGUI::exec_command("right");
            if (!color.empty()) GrassGUI::exec_command("text-color(" + color + ")");
            GrassGUI::exec_command("font-size(" + to_string(fsize) + ")");
            if (!fstyle.empty()) GrassGUI::exec_command("font-style(" + fstyle + ")");
            if (glass) GrassGUI::exec_command("glass");
            if (!run.empty()) GrassGUI::exec_command("run(" + run + ")");
        }
        DISPATCH_STEP();
    }

    do_ui_cmd: {
        {
            syncVarsToGUI();
            string cmd_line = readString();
            GrassGUI::exec_command(cmd_line);
        }
        DISPATCH_STEP();
    }

    do_ui_show: {
#if defined(_WIN32)
        HWND hConsole = GetConsoleWindow();
        if (hConsole) ShowWindow(hConsole, SW_HIDE);
#endif
        syncVarsToGUI();
        GrassGUI::set_vm_callback_runner([this](uint32_t cb_ip) {
            this->run_callback(cb_ip);
        });
        GrassGUI::show_window();
        DISPATCH_STEP();
    }

    do_new_array: {
        uint32_t count = readU32();
        GrassArray* arr = new GrassArray();
        arr->elements.resize(count);
        for (int i = (int)count - 1; i >= 0; --i) {
            arr->elements[i] = pop();
        }
        heap_arrays.push_back(arr);
        push(GrassValue::Array(arr));
        DISPATCH_STEP();
    }

    do_get_index: {
        GrassValue obj = pop();
        GrassValue idx = pop();
        if (obj.is_ptr()) {
            GrassVMPtr* p = obj.asPtr();
            int64_t i = (int64_t)idx.toNumber();
            if (p && p->vec) {
                int64_t pos = p->vec_idx + i;
                if (pos >= 0 && (size_t)pos < p->vec->size()) push((*p->vec)[pos]);
                else push(GrassValue::Nil());
            } else if (p && p->raw) {
                int64_t es = p->elem_size > 0 ? p->elem_size : 1;
                int64_t off = p->raw_off + i * es;
                if (off >= 0 && (size_t)off < p->raw_size) push(GrassValue::Num((double)*(uint8_t*)(p->raw + off)));
                else push(GrassValue::Num(0.0));
            } else if (p && p->cell && i == 0) {
                push(*p->cell);
            } else {
                push(GrassValue::Nil());
            }
        } else if (obj.is_array()) {
            GrassArray* arr = obj.asArray();
            long long i = (long long)idx.toNumber();
            if (arr && i >= 0 && (size_t)i < arr->elements.size()) {
                push(arr->elements[i]);
            } else {
                push(GrassValue::Nil());
            }
        } else if (obj.is_str()) {
            string s = obj.toString();
            long long i = (long long)idx.toNumber();
            if (i >= 0 && (size_t)i < s.size()) {
                push(GrassValue::Str(string(1, s[i])));
            } else {
                push(GrassValue::Str(""));
            }
        } else {
            push(GrassValue::Nil());
        }
        DISPATCH_STEP();
    }

    do_set_index: {
        GrassValue obj = pop();
        GrassValue val = pop();
        GrassValue idx = pop();
        if (obj.is_ptr()) {
            GrassVMPtr* p = obj.asPtr();
            int64_t i = (int64_t)idx.toNumber();
            if (p && p->vec) {
                int64_t pos = p->vec_idx + i;
                if (pos >= 0 && (size_t)pos < p->vec->size()) (*p->vec)[pos] = val;
            } else if (p && p->raw) {
                int64_t es = p->elem_size > 0 ? p->elem_size : 1;
                int64_t off = p->raw_off + i * es;
                if (off >= 0 && (size_t)off < p->raw_size) *(uint8_t*)(p->raw + off) = (uint8_t)val.toNumber();
            } else if (p && p->cell && i == 0) {
                *p->cell = val;
            }
        } else if (obj.is_array()) {
            GrassArray* arr = obj.asArray();
            long long i = (long long)idx.toNumber();
            if (arr && i >= 0) {
                if ((size_t)i >= arr->elements.size()) {
                    arr->elements.resize(i + 1, GrassValue::Nil());
                }
                arr->elements[i] = val;
            }
        }
        DISPATCH_STEP();
    }

    do_array_push: {
        GrassValue obj = pop();
        GrassValue val = pop();
        if (obj.is_array()) {
            GrassArray* arr = obj.asArray();
            if (arr) {
                arr->elements.push_back(val);
            }
        }
        push(val);
        DISPATCH_STEP();
    }

    do_array_pop: {
        GrassValue obj = pop();
        if (obj.is_array()) {
            GrassArray* arr = obj.asArray();
            if (arr && !arr->elements.empty()) {
                GrassValue val = arr->elements.back();
                arr->elements.pop_back();
                push(val);
            } else {
                push(GrassValue::Nil());
            }
        } else {
            push(GrassValue::Nil());
        }
        DISPATCH_STEP();
    }

    do_new_struct: {
        {
            string s_name = readString();
            auto alloc_struct_inst = [&](auto& self, GrassStructDef& sd) -> GrassStructInstance* {
                GrassStructInstance* inst = (GrassStructInstance*)g_vm_arena.alloc(sizeof(GrassStructInstance));
                uint8_t* buf = (uint8_t*)g_vm_arena.alloc(sd.total_size);
                if (buf) memset(buf, 0, sd.total_size);
                inst->def = &sd;
                inst->data = buf;
                for (const auto& fd : sd.fields) {
                    if (fd.struct_name.rfind("__vec__:", 0) == 0) {
                        GrassArray* arr = new GrassArray();
                        arr->is_vec = true;
                        arr->vec_capacity = 0;
                        heap_arrays.push_back(arr);
                        *(GrassValue*)(buf + fd.offset) = GrassValue::Array(arr);
                    } else if (!fd.struct_name.empty()) {
                        auto fit = g_struct_registry.find(fd.struct_name);
                        if (fit != g_struct_registry.end()) {
                            GrassStructInstance* sub = self(self, fit->second);
                            *(GrassValue*)(buf + fd.offset) = GrassValue::Struct(sub);
                        }
                    }
                }
                return inst;
            };
            auto it = g_struct_registry.find(s_name);
            if (it != g_struct_registry.end()) {
                push(GrassValue::Struct(alloc_struct_inst(alloc_struct_inst, it->second)));
            } else {
                push(GrassValue::Nil());
            }
        }
        DISPATCH_STEP();
    }

    do_get_field: {
        {
            string f_name = readString();
            GrassValue obj = pop();
            GrassStructInstance* inst = nullptr;
            if (obj.is_struct()) {
                inst = obj.asStruct();
            } else if (obj.is_ptr() && obj.asPtr()) {
                GrassVMPtr* p = obj.asPtr();
                if (p->struct_inst) inst = p->struct_inst;
                else if (p->cell && p->cell->is_struct()) inst = p->cell->asStruct();
            }
            if (inst) {
                if (inst && inst->def && inst->data) {
                    auto it = inst->def->field_indices.find(f_name);
                    if (it != inst->def->field_indices.end()) {
                        const GrassFieldDef& fd = inst->def->fields[it->second];
                        uint8_t* ptr = inst->data + fd.offset;
                        switch (fd.type) {
                            case GrassFieldType::U8:     push(GrassValue::Num((double)*(uint8_t*)ptr)); break;
                            case GrassFieldType::U16:    push(GrassValue::Num((double)*(uint16_t*)ptr)); break;
                            case GrassFieldType::U32:    push(GrassValue::Num((double)*(uint32_t*)ptr)); break;
                            case GrassFieldType::INT:    push(GrassValue::Num((double)*(int32_t*)ptr)); break;
                            case GrassFieldType::FLOAT:  push(GrassValue::Num((double)*(float*)ptr)); break;
                            case GrassFieldType::DOUBLE: push(GrassValue::Num(*(double*)ptr)); break;
                            case GrassFieldType::U64:    push(GrassValue::Num((double)*(uint64_t*)ptr)); break;
                            case GrassFieldType::BOOL:   push(GrassValue::Bool(*(uint8_t*)ptr != 0)); break;
                            case GrassFieldType::STRING: {
                                const char* s = *(const char**)ptr;
                                push(GrassValue::Str(s ? s : ""));
                                break;
                            }
                            case GrassFieldType::VALUE:
                                push(*(GrassValue*)ptr);
                                break;
                        }
                    } else {
                        push(GrassValue::Nil());
                    }
                } else {
                    push(GrassValue::Nil());
                }
            } else if (obj.is_array() && obj.asArray()) {
                GrassArray* arr = obj.asArray();
                if (f_name == "x" || f_name == "r" || f_name == "u") {
                    push(arr->elements.size() > 0 ? arr->elements[0] : GrassValue::Num(0.0));
                } else if (f_name == "y" || f_name == "g" || f_name == "v") {
                    push(arr->elements.size() > 1 ? arr->elements[1] : GrassValue::Num(0.0));
                } else if (f_name == "z" || f_name == "b") {
                    push(arr->elements.size() > 2 ? arr->elements[2] : GrassValue::Num(0.0));
                } else if (f_name == "w" || f_name == "a") {
                    push(arr->elements.size() > 3 ? arr->elements[3] : GrassValue::Num(0.0));
                } else {
                    push(GrassValue::Nil());
                }
            } else {
                push(GrassValue::Nil());
            }
        }
        DISPATCH_STEP();
    }

    do_set_field: {
        {
            string f_name = readString();
            GrassValue obj = pop();
            GrassValue val = pop();
            GrassStructInstance* inst = nullptr;
            if (obj.is_struct()) {
                inst = obj.asStruct();
            } else if (obj.is_ptr() && obj.asPtr()) {
                GrassVMPtr* p = obj.asPtr();
                if (p->struct_inst) inst = p->struct_inst;
                else if (p->cell && p->cell->is_struct()) inst = p->cell->asStruct();
            }
            if (inst) {
                if (inst && inst->def && inst->data) {
                    auto it = inst->def->field_indices.find(f_name);
                    if (it != inst->def->field_indices.end()) {
                        const GrassFieldDef& fd = inst->def->fields[it->second];
                        uint8_t* ptr = inst->data + fd.offset;
                        switch (fd.type) {
                            case GrassFieldType::U8:     *(uint8_t*)ptr  = (uint8_t)val.toNumber(); break;
                            case GrassFieldType::U16:    *(uint16_t*)ptr = (uint16_t)val.toNumber(); break;
                            case GrassFieldType::U32:    *(uint32_t*)ptr = (uint32_t)val.toNumber(); break;
                            case GrassFieldType::INT:    *(int32_t*)ptr  = (int32_t)val.toNumber(); break;
                            case GrassFieldType::FLOAT:  *(float*)ptr    = (float)val.toNumber(); break;
                            case GrassFieldType::DOUBLE: *(double*)ptr   = val.toNumber(); break;
                            case GrassFieldType::U64:    *(uint64_t*)ptr = (uint64_t)val.toNumber(); break;
                            case GrassFieldType::BOOL:   *(uint8_t*)ptr  = val.isTruthy() ? 1 : 0; break;
                            case GrassFieldType::STRING: {
                                string s = val.toString();
                                char* arena_s = g_vm_arena.alloc_string(s.c_str(), s.size());
                                *(const char**)ptr = arena_s;
                                break;
                            }
                            case GrassFieldType::VALUE:
                                *(GrassValue*)ptr = val;
                                break;
                        }
                    }
                }
            } else if (obj.is_array() && obj.asArray()) {
                GrassArray* arr = obj.asArray();
                if (f_name == "x" || f_name == "r" || f_name == "u") {
                    if (arr->elements.size() > 0) arr->elements[0] = val;
                } else if (f_name == "y" || f_name == "g" || f_name == "v") {
                    if (arr->elements.size() > 1) arr->elements[1] = val;
                } else if (f_name == "z" || f_name == "b") {
                    if (arr->elements.size() > 2) arr->elements[2] = val;
                } else if (f_name == "w" || f_name == "a") {
                    if (arr->elements.size() > 3) arr->elements[3] = val;
                }
            }
        }
        DISPATCH_STEP();
    }

    do_thread_spawn: {
        uint32_t entry_ip = readU32();
        uint8_t argc = readByte();
        int64_t tid = spawnThread(entry_ip, argc, false);
        push(GrassValue::Num((double)tid));
        DISPATCH_STEP();
    }

    do_async_spawn: {
        uint32_t entry_ip = readU32();
        uint8_t argc = readByte();
        int64_t tid = spawnThread(entry_ip, argc, true);
        push(GrassValue::Num((double)tid));
        DISPATCH_STEP();
    }

    do_addr_var: {
        {
            string name = readString();
            GrassValue* cell = find_var_cell(name);
            GrassVMPtr* p = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
            new (p) GrassVMPtr();
            if (cell) {
                p->cell = cell;
                if (cell->is_struct()) p->struct_inst = cell->asStruct();
            }
            push(GrassValue::Ptr(p));
        }
        DISPATCH_STEP();
    }

    do_addr_index: {
        {
            string name = readString();
            GrassValue idx = pop();
            int64_t i = (int64_t)idx.toNumber();
            GrassValue* cell = find_var_cell(name);
            GrassVMPtr* p = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
            new (p) GrassVMPtr();
            if (cell && cell->is_array() && cell->asArray()) {
                p->vec = &cell->asArray()->elements;
                p->vec_idx = i;
                p->elem_size = 8;
            } else if (cell && cell->is_ptr() && cell->asPtr()) {
                *p = *cell->asPtr();
                if (p->vec) p->vec_idx += i;
                else if (p->raw) p->raw_off += i * (p->elem_size > 0 ? p->elem_size : 1);
                else p->cell += i;
            } else if (cell && cell->is_str() && cell->asCStr()) {
                const char* s = cell->asCStr();
                p->raw = (uint8_t*)s;
                p->raw_size = strlen(s);
                p->raw_off = i;
                p->elem_size = 1;
                p->is_scalar_u8 = true;
                p->scalar_kind = 1;
            }
            push(GrassValue::Ptr(p));
        }
        DISPATCH_STEP();
    }

    do_addr_field: {
        {
            string f_name = readString();
            GrassValue obj = pop();
            GrassStructInstance* inst = nullptr;
            if (obj.is_struct()) {
                inst = obj.asStruct();
            } else if (obj.is_ptr() && obj.asPtr()) {
                GrassVMPtr* op = obj.asPtr();
                if (op->struct_inst) inst = op->struct_inst;
                else if (op->cell && op->cell->is_struct()) inst = op->cell->asStruct();
            }
            GrassVMPtr* p = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
            new (p) GrassVMPtr();
            if (inst && inst->def && inst->data) {
                auto it = inst->def->field_indices.find(f_name);
                if (it != inst->def->field_indices.end()) {
                    const GrassFieldDef& fd = inst->def->fields[it->second];
                    uint8_t* ptr = inst->data + fd.offset;
                    if (fd.type == GrassFieldType::VALUE) {
                        p->cell = (GrassValue*)ptr;
                        if (p->cell->is_struct()) p->struct_inst = p->cell->asStruct();
                    } else {
                        p->raw = ptr;
                        p->raw_size = fd.size;
                        p->elem_size = (int64_t)fd.size;
                        p->is_scalar_u8 = (fd.type == GrassFieldType::U8);
                        switch (fd.type) {
                            case GrassFieldType::U8:     p->scalar_kind = 1; break;
                            case GrassFieldType::U16:    p->scalar_kind = 2; break;
                            case GrassFieldType::U32:    p->scalar_kind = 3; break;
                            case GrassFieldType::INT:    p->scalar_kind = 4; break;
                            case GrassFieldType::FLOAT:  p->scalar_kind = 5; break;
                            case GrassFieldType::DOUBLE: p->scalar_kind = 6; break;
                            case GrassFieldType::U64:    p->scalar_kind = 7; break;
                            case GrassFieldType::BOOL:   p->scalar_kind = 8; break;
                            case GrassFieldType::STRING: p->scalar_kind = 9; break;
                            default: p->scalar_kind = 0; break;
                        }
                    }
                }
            }
            push(GrassValue::Ptr(p));
        }
        DISPATCH_STEP();
    }

    do_deref: {
        GrassValue v = pop();
        if (v.is_ptr()) {
            GrassVMPtr* p = v.asPtr();
            if (!p) {
                push(GrassValue::Num(0.0));
            } else if (p->vec) {
                if (p->vec_idx >= 0 && (size_t)p->vec_idx < p->vec->size()) {
                    push((*p->vec)[p->vec_idx]);
                } else {
                    push(GrassValue::Nil());
                }
            } else if (p->cell) {
                push(*p->cell);
            } else if (p->raw && p->raw_off >= 0 && (size_t)p->raw_off < p->raw_size) {
                uint8_t* rp = p->raw + p->raw_off;
                switch (p->scalar_kind) {
                    case 1: push(GrassValue::Num((double)*(uint8_t*)rp)); break;
                    case 2: push(GrassValue::Num((double)*(uint16_t*)rp)); break;
                    case 3: push(GrassValue::Num((double)*(uint32_t*)rp)); break;
                    case 4: push(GrassValue::Num((double)*(int32_t*)rp)); break;
                    case 5: push(GrassValue::Num((double)*(float*)rp)); break;
                    case 6: push(GrassValue::Num(*(double*)rp)); break;
                    case 7: push(GrassValue::Num((double)*(uint64_t*)rp)); break;
                    case 8: push(GrassValue::Bool(*(uint8_t*)rp != 0)); break;
                    case 9: {
                        const char* s = *(const char**)rp;
                        push(GrassValue::Str(s ? s : ""));
                        break;
                    }
                    default: push(GrassValue::Num((double)*(uint8_t*)rp)); break;
                }
            } else {
                push(GrassValue::Num(0.0));
            }
        } else if (v.is_str() && v.asCStr()) {
            push(GrassValue::Num((double)(unsigned char)v.asCStr()[0]));
        } else {
            push(GrassValue::Num(0.0));
        }
        DISPATCH_STEP();
    }

    do_store_deref: {
        GrassValue val = pop();
        GrassValue addr = pop();
        if (addr.is_ptr() && addr.asPtr()) {
            GrassVMPtr* p = addr.asPtr();
            if (p->vec) {
                if (p->vec_idx >= 0 && (size_t)p->vec_idx < p->vec->size()) {
                    (*p->vec)[p->vec_idx] = val;
                }
            } else if (p->cell) {
                *p->cell = val;
                if (val.is_struct()) p->struct_inst = val.asStruct();
            } else if (p->raw && p->raw_off >= 0 && (size_t)p->raw_off < p->raw_size) {
                uint8_t* rp = p->raw + p->raw_off;
                switch (p->scalar_kind) {
                    case 1: *(uint8_t*)rp  = (uint8_t)val.toNumber(); break;
                    case 2: *(uint16_t*)rp = (uint16_t)val.toNumber(); break;
                    case 3: *(uint32_t*)rp = (uint32_t)val.toNumber(); break;
                    case 4: *(int32_t*)rp  = (int32_t)val.toNumber(); break;
                    case 5: *(float*)rp    = (float)val.toNumber(); break;
                    case 6: *(double*)rp   = val.toNumber(); break;
                    case 7: *(uint64_t*)rp = (uint64_t)val.toNumber(); break;
                    case 8: *(uint8_t*)rp  = val.isTruthy() ? 1 : 0; break;
                    case 9: {
                        string s = val.toString();
                        *(const char**)rp = g_vm_arena.alloc_string(s.c_str(), s.size());
                        break;
                    }
                    default: *(uint8_t*)rp = (uint8_t)val.toNumber(); break;
                }
            }
        }
        DISPATCH_STEP();
    }

    do_unknown:
        DISPATCH_STEP();

#undef DISPATCH_STEP
    }

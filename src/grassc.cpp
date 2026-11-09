#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <thread>
#endif
#include <mutex>

#include "GrassSyntax.h"
#include "FastMath.h"
#include "GrassASM.h"
#include "GrassKeyboard.h"
#include "GrassHardware.h"
#include "GrassBoot.h"
#include "GrassBaremetal.h"
#include "CoreMath.h"
#include "GrassWeb.h"
#include "GrassMemory.h"
#include "GrassRandom.h"
#include "GSE.h"
#include "gui_engine.h"
#include "GrassTime.h"
#include "GrassNet.h"
#include "GrassJson.h"
#include "GrassSys.h"
#include "GrassProc.h"
#include "GrassFsys.h"
#include "GrassTg.h"
#include "Grass2DGraphics.h"
#include "GrassIO.h"
#include "GrassHAL.h"
#include "GrassOptimizer.h"
#include "GrassImport.h"
#include "GrassJIT.h"
#include "GrassThread.h"
#include "GrassMMIO.h"
#include "GrassAudio.h"

using namespace std;
namespace fs = std::filesystem;

// Глобальная арена для строк VM (4 МБ)
GrassArena g_vm_arena(4 * 1024 * 1024);
unordered_map<string, GrassStructDef> g_struct_registry;
static bool g_not_clear_active = false;
static std::vector<std::string> g_protected_console_lines;

static string wstring_to_utf8_helper(const wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
    if (!str.empty() && str.back() == '\0') str.pop_back();
    return str;
}

// Разделяемое окружение для многопоточности GrassGreen
struct GrassSharedEnv {
    std::mutex mtx;
    unordered_map<string, GrassValue> vars;
};

// ============================================================================
// 1. СТЕКОВАЯ ВИРТУАЛЬНАЯ МАШИНА GRASSGREEN (ТУРБО-ВЕРСИЯ)
// ============================================================================
class GrassVM {
    const vector<uint8_t>& code;
    size_t ip = 0;
    vector<GrassValue> stack;
    vector<size_t> call_stack;
    unordered_map<string, GrassValue> vars;
    shared_ptr<GrassSharedEnv> shared_env;
    bool is_root_vm = true;
    GrassValue last_return_value{GrassValue::Num(0.0)};
    vector<GrassArray*> heap_arrays;
    unordered_map<uint32_t, shared_ptr<GrassJIT::CompiledJitBlock>> jit_cache;
    unordered_map<uint32_t, int> loop_trip_counters;
    unordered_set<uint32_t> failed_loops;

    bool tryRunJitLoop(uint32_t loop_start, uint32_t loop_end) {
        if (failed_loops.count(loop_start)) return false;

        auto& active_vars = (is_root_vm && shared_env) ? shared_env->vars : vars;
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

    inline uint8_t readByte() {
        if (ip >= code.size()) return OP_HALT;
        return code[ip++];
    }

    inline uint32_t readU32() {
        if (ip + 4 > code.size()) { ip = code.size(); return 0; }
        uint32_t v;
        memcpy(&v, &code[ip], 4);
        ip += 4;
        return v;
    }

    inline double readDouble() {
        if (ip + 8 > code.size()) { ip = code.size(); return 0.0; }
        double d;
        memcpy(&d, &code[ip], 8);
        ip += 8;
        return d;
    }

    inline string readString() {
        uint32_t len = readU32();
        if (ip + len > code.size()) { ip = code.size(); return ""; }
        string s((const char*)&code[ip], len);
        ip += len;
        return s;
    }

    inline GrassValue pop() {
        if (stack.empty()) return GrassValue::Num(0.0);
        GrassValue v = std::move(stack.back());
        stack.pop_back();
        return v;
    }

    inline void push(const GrassValue& v) { stack.push_back(v); }
    inline void push(GrassValue&& v) { stack.push_back(std::move(v)); }

    void syncDBToVars() {
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

    void syncVarsToGUI() {
        if (is_root_vm && shared_env) {
            std::lock_guard<std::mutex> lk(shared_env->mtx);
            for (const auto& kv : shared_env->vars) {
                GrassGUI::set_variable(kv.first, kv.second.toString());
            }
        } else {
            for (const auto& kv : vars) {
                GrassGUI::set_variable(kv.first, kv.second.toString());
            }
        }
    }

    static bool isGdbSchemaKey(const string& fn) {
        return fn == "user" || fn == "last_log" || fn == "net_tunnel_protocol" ||
               fn == "net_baypass_state" || fn == "net_bypass_state" || fn == "net_gateway_route" ||
               fn == "remember_me" || fn == "auth_token_hash" || fn == "env_target_runtime" ||
               fn == "env_exec_flags" || fn == "net_bypass_config" || fn == "net_ping_ms" ||
               fn == "env_allocated_ram" || fn == "env_java_path" || fn == "sys_fps_stabilizer" ||
               fn == "sys_gpu_force_integrated" || fn == "user_uuid" || fn == "user_role" ||
               fn == "sys_cpu_threads" || fn == "sys_window_mode" || fn == "net_dns_server" ||
               fn == "net_mtu_size";
    }

public:
    explicit GrassVM(const vector<uint8_t>& bytecode,
                     shared_ptr<GrassSharedEnv> shared = nullptr,
                     bool root = true)
        : code(bytecode), shared_env(shared), is_root_vm(root) {
        if (is_root_vm && !shared_env) {
            shared_env = make_shared<GrassSharedEnv>();
        }
        stack.reserve(512);
        call_stack.reserve(128);
        vars.reserve(128);
        heap_arrays.reserve(64);
    }

    ~GrassVM() {
        for (GrassArray* a : heap_arrays) delete a;
    }

    GrassValue getLastReturnValue() const { return last_return_value; }

    int64_t spawnThread(uint32_t entry_ip, uint8_t argc, bool detached) {
        vector<GrassValue> args(argc);
        for (int i = (int)argc - 1; i >= 0; --i) {
            args[i] = pop();
        }

        if (!shared_env) {
            shared_env = make_shared<GrassSharedEnv>();
        }
        auto shared = shared_env;
        const vector<uint8_t>& bc = this->code;

        int64_t tid = GrassThread::create_thread([&bc, shared, entry_ip, args]() -> GrassValue {
            GrassVM thread_vm(bc, shared, false);
            for (size_t i = 0; i < args.size(); ++i) {
                thread_vm.push(args[i]);
            }
            thread_vm.execute(entry_ip);
            return thread_vm.getLastReturnValue();
        }, detached);

        return tid;
    }

    void execute(size_t start_ip = 0) {
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
            dispatch_table[OP_POP]           = &&do_pop;
            dispatch_table[OP_ADD]           = &&do_add;
            dispatch_table[OP_SUB]           = &&do_sub;
            dispatch_table[OP_MUL]           = &&do_mul;
            dispatch_table[OP_DIV]           = &&do_div;
            dispatch_table[OP_NEG]           = &&do_neg;
            dispatch_table[OP_MOD]           = &&do_mod;
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
            table_init = true;
        }

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
        if (a.isNumeric()) push(GrassValue::Num(a.toNumber() + c));
        else if (a.is_str()) push(GrassValue::Str(a.toString() + GrassValue::Num(c).toString()));
        else push(GrassValue::Num(a.toNumber() + c));
        DISPATCH_STEP();
    }

    do_sub_const: {
        double c = readDouble();
        GrassValue a = pop();
        push(GrassValue::Num(a.toNumber() - c));
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
            if (is_root_vm) {
                if (shared_env) {
                    std::lock_guard<std::mutex> lk(shared_env->mtx);
                    auto it = shared_env->vars.find(name);
                    if (it != shared_env->vars.end()) it->second = GrassValue::Num(it->second.toNumber() + 1.0);
                    else shared_env->vars[name] = GrassValue::Num(1.0);
                }
            } else {
                auto it = vars.find(name);
                if (it != vars.end()) {
                    it->second = GrassValue::Num(it->second.toNumber() + 1.0);
                } else {
                    bool in_shared = false;
                    if (shared_env) {
                        std::lock_guard<std::mutex> lk(shared_env->mtx);
                        auto sit = shared_env->vars.find(name);
                        if (sit != shared_env->vars.end()) {
                            sit->second = GrassValue::Num(sit->second.toNumber() + 1.0);
                            in_shared = true;
                        }
                    }
                    if (!in_shared) {
                        vars[name] = GrassValue::Num(1.0);
                    }
                }
            }
        }
        DISPATCH_STEP();
    }

    do_dec_var: {
        {
            string name = readString();
            if (is_root_vm) {
                if (shared_env) {
                    std::lock_guard<std::mutex> lk(shared_env->mtx);
                    auto it = shared_env->vars.find(name);
                    if (it != shared_env->vars.end()) it->second = GrassValue::Num(it->second.toNumber() - 1.0);
                    else shared_env->vars[name] = GrassValue::Num(-1.0);
                }
            } else {
                auto it = vars.find(name);
                if (it != vars.end()) {
                    it->second = GrassValue::Num(it->second.toNumber() - 1.0);
                } else {
                    bool in_shared = false;
                    if (shared_env) {
                        std::lock_guard<std::mutex> lk(shared_env->mtx);
                        auto sit = shared_env->vars.find(name);
                        if (sit != shared_env->vars.end()) {
                            sit->second = GrassValue::Num(sit->second.toNumber() - 1.0);
                            in_shared = true;
                        }
                    }
                    if (!in_shared) {
                        vars[name] = GrassValue::Num(-1.0);
                    }
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
        call_stack.push_back(ip);
        ip = target_ip;
        DISPATCH_STEP();
    }

    do_ret: {
        if (!stack.empty()) {
            last_return_value = stack.back();
        }
        if (!call_stack.empty()) {
            ip = call_stack.back();
            call_stack.pop_back();
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
            if (!is_root_vm) {
                auto it = vars.find(name);
                if (it != vars.end()) {
                    val = it->second;
                    found = true;
                }
            }
            if (!found && shared_env) {
                std::lock_guard<std::mutex> lk(shared_env->mtx);
                auto it = shared_env->vars.find(name);
                if (it != shared_env->vars.end()) {
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
            } else {
                push(GrassValue::Str(""));
            }
        }
        DISPATCH_STEP();
    }

    do_store_var: {
        {
            string name = readString();
            GrassValue val = pop();
            if (is_root_vm) {
                if (shared_env) {
                    std::lock_guard<std::mutex> lk(shared_env->mtx);
                    shared_env->vars[name] = std::move(val);
                }
            } else {
                auto it = vars.find(name);
                if (it != vars.end()) {
                    it->second = std::move(val);
                } else {
                    bool in_shared = false;
                    if (shared_env) {
                        std::lock_guard<std::mutex> lk(shared_env->mtx);
                        auto sit = shared_env->vars.find(name);
                        if (sit != shared_env->vars.end()) {
                            sit->second = std::move(val);
                            in_shared = true;
                        }
                    }
                    if (!in_shared) {
                        vars[name] = std::move(val);
                    }
                }
            }
        }
        DISPATCH_STEP();
    }

    do_pop:
        pop();
        DISPATCH_STEP();

    do_add: {
        GrassValue b = pop(), a = pop();
        if (a.isNumeric() && b.isNumeric()) push(GrassValue::Num(a.toNumber() + b.toNumber()));
        else if (a.is_str() || b.is_str()) push(GrassValue::Str(a.toString() + b.toString()));
        else push(GrassValue::Num(a.toNumber() + b.toNumber()));
        DISPATCH_STEP();
    }

    do_sub: {
        GrassValue b = pop(), a = pop();
        push(GrassValue::Num(a.toNumber() - b.toNumber()));
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
        if (a.is_str() && b.is_str()) {
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
        if (a.is_str() && b.is_str()) {
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

                    if (fn == "gin" || fn == "input") {
                        if (argc >= 1) {
                            string prompt = args[0].toString();
                            GrassIO::write_stdout(prompt.data(), prompt.size());
                        }
                        char buf[1024];
                        size_t len = GrassIO::read_line(buf, sizeof(buf));
                        char* p = buf;
                        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
                        while (*p == ' ' || *p == '\t') p++;
                        char* space = strpbrk(p, " \t\r\n");
                        if (space) *space = '\0';
                        string val_str(p);
                        char* end_ptr = nullptr;
                        double num_val = strtod(val_str.c_str(), &end_ptr);
                        if (!val_str.empty() && end_ptr != val_str.c_str() && *end_ptr == '\0') {
                            push(GrassValue::Num(num_val));
                        } else {
                            push(GrassValue::Str(val_str));
                        }
                    }
                    else if (fn == "gin.getline" || fn == "input.getline" || fn == "getline" || fn == "ask") {
                        if (argc >= 1) {
                            string prompt = args[0].toString();
                            GrassIO::write_stdout(prompt.data(), prompt.size());
                        }
                        char buf[1024];
                        size_t len = GrassIO::read_line(buf, sizeof(buf));
                        char* p = buf;
                        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
                        string val_str(p);
                        while (!val_str.empty() && (val_str.back() == '\r' || val_str.back() == '\n')) val_str.pop_back();
                        char* end_ptr = nullptr;
                        double num_val = strtod(val_str.c_str(), &end_ptr);
                        if (!val_str.empty() && end_ptr != val_str.c_str() && *end_ptr == '\0') {
                            push(GrassValue::Num(num_val));
                        } else {
                            push(GrassValue::Str(val_str));
                        }
                    }
                                        else if (fn == "hw_ram_total" || fn == "ram_total") {
                        push(GrassValue::Num((double)GrassHardware::ram_total_mb()));
                    }
                    else if (fn == "hw_ram_used" || fn == "ram_used" || fn == "proc_mem") {
                        push(GrassValue::Num((double)GrassHardware::ram_used_mb()));
                    }
                    else if (fn == "hw_ram_free" || fn == "ram_free") {
                        push(GrassValue::Num((double)GrassHardware::ram_free_mb()));
                    }
                    else if (fn == "hw_ram_cached") {
                        push(GrassValue::Num((double)GrassHardware::ram_cached_mb()));
                    }
                    else if (fn == "hw_ram_speed") {
                        push(GrassValue::Num((double)GrassHardware::ram_speed_mhz()));
                    }
                    else if (fn == "hw_ram_temp") {
                        push(GrassValue::Num((double)GrassHardware::ram_temp()));
                    }
                    else if (fn == "hw_ram_model") {
                        push(GrassValue::Str(GrassHardware::ram_model()));
                    }
                    else if (fn == "hw_ram_form") {
                        push(GrassValue::Str(GrassHardware::ram_form()));
                    }
                    // ── CPU ──
                    else if (fn == "hw_cpu_vendor" || fn == "cpu_vendor") {
                        push(GrassValue::Str(GrassHardware::cpu_vendor()));
                    }
                    else if (fn == "hw_cpu_brand" || fn == "cpu_brand" || fn == "cpu_model") {
                        push(GrassValue::Str(GrassHardware::cpu_brand()));
                    }
                    else if (fn == "hw_cpu_cores" || fn == "cpu_cores") {
                        push(GrassValue::Num((double)GrassHardware::cpu_cores()));
                    }
                    else if (fn == "hw_cpu_threads" || fn == "hw_cpu_logical") {
                        push(GrassValue::Num((double)GrassHardware::cpu_threads()));
                    }
                    else if (fn == "hw_cpu_sockets") {
                        push(GrassValue::Num((double)GrassHardware::cpu_sockets()));
                    }
                    else if (fn == "hw_cpu_family" || fn == "cpu_family") {
                        push(GrassValue::Num((double)GrassHardware::cpu_family()));
                    }
                    else if (fn == "hw_cpu_freq" || fn == "cpu_freq") {
                        push(GrassValue::Num((double)GrassHardware::cpu_freq_mhz()));
                    }
                    else if (fn == "hw_cpu_base_freq") {
                        push(GrassValue::Num((double)GrassHardware::cpu_base_freq_mhz()));
                    }
                    else if (fn == "hw_cpu_usage") {
                        push(GrassValue::Num((double)GrassHardware::cpu_usage_percent()));
                    }
                    else if (fn == "hw_cpu_temp") {
                        push(GrassValue::Num((double)GrassHardware::cpu_temp()));
                    }
                    else if (fn == "hw_cpu_cache_l1") {
                        push(GrassValue::Num((double)GrassHardware::cpu_cache_l1_kb()));
                    }
                    else if (fn == "hw_cpu_cache_l2") {
                        push(GrassValue::Num((double)GrassHardware::cpu_cache_l2_kb()));
                    }
                    else if (fn == "hw_cpu_cache_l3") {
                        push(GrassValue::Num((double)GrassHardware::cpu_cache_l3_kb()));
                    }
                    else if (fn == "hw_cpu_virt") {
                        push(GrassValue::Str(GrassHardware::cpu_virt()));
                    }
                    else if (fn == "hw_uptime_sec") {
                        push(GrassValue::Num((double)GrassHardware::uptime_sec()));
                    }
                    else if (fn == "hw_uptime_str") {
                        push(GrassValue::Str(GrassHardware::uptime_str()));
                    }
                    // ── DISK ──
                    else if (fn == "hw_disk_size") {
                        push(GrassValue::Num((double)GrassHardware::disk_size_gb()));
                    }
                    else if (fn == "hw_disk_free") {
                        push(GrassValue::Num((double)GrassHardware::disk_free_gb()));
                    }
                    else if (fn == "hw_disk_format") {
                        push(GrassValue::Str(GrassHardware::disk_format()));
                    }
                    else if (fn == "hw_disk_model") {
                        push(GrassValue::Str(GrassHardware::disk_model()));
                    }
                    else if (fn == "hw_disk_type") {
                        push(GrassValue::Str(GrassHardware::disk_type()));
                    }
                    else if (fn == "hw_disk_temp") {
                        push(GrassValue::Num((double)GrassHardware::disk_temp()));
                    }
                    else if (fn == "hw_disk_read_speed") {
                        push(GrassValue::Num((double)GrassHardware::disk_read_speed()));
                    }
                    else if (fn == "hw_disk_write_speed") {
                        push(GrassValue::Num((double)GrassHardware::disk_write_speed()));
                    }
                    else if (fn == "hw_disk_resp_time") {
                        push(GrassValue::Num((double)GrassHardware::disk_resp_time()));
                    }
                    else if (fn == "hw_disk_usage" || fn == "hw_disk_active") {
                        push(GrassValue::Num((double)GrassHardware::disk_usage_percent()));
                    }
                    else if (fn == "hw_disk_is_system") {
                        push(GrassValue::Num((double)GrassHardware::disk_is_system()));
                    }
                    else if (fn == "hw_pagefile_total") {
                        push(GrassValue::Num((double)GrassHardware::pagefile_total_mb()));
                    }
                    else if (fn == "hw_pagefile_used") {
                        push(GrassValue::Num((double)GrassHardware::pagefile_used_mb()));
                    }
                    // ── NETWORK ──
                    else if (fn == "hw_net_adapter") {
                        push(GrassValue::Str(GrassHardware::net_adapter()));
                    }
                    else if (fn == "hw_net_type") {
                        push(GrassValue::Str(GrassHardware::net_type()));
                    }
                    else if (fn == "hw_net_ipv4") {
                        push(GrassValue::Str(GrassHardware::net_ipv4()));
                    }
                    else if (fn == "hw_net_ipv6") {
                        push(GrassValue::Str(GrassHardware::net_ipv6()));
                    }
                    else if (fn == "hw_net_recv_speed") {
                        push(GrassValue::Num((double)GrassHardware::net_recv_speed_kb()));
                    }
                    else if (fn == "hw_net_send_speed") {
                        push(GrassValue::Num((double)GrassHardware::net_send_speed_kb()));
                    }
                    else if (fn == "hw_net_recv_total") {
                        push(GrassValue::Num((double)GrassHardware::net_recv_total_mb()));
                    }
                    else if (fn == "hw_net_send_total") {
                        push(GrassValue::Num((double)GrassHardware::net_send_total_mb()));
                    }
                    // ── GPU ──
                    else if (fn == "hw_gpu_name") {
                        push(GrassValue::Str(GrassHardware::gpu_name()));
                    }
                    else if (fn == "hw_gpu_usage") {
                        push(GrassValue::Num((double)GrassHardware::gpu_usage_percent()));
                    }
                    else if (fn == "hw_gpu_temp") {
                        push(GrassValue::Num((double)GrassHardware::gpu_temp()));
                    }
                    else if (fn == "hw_gpu_vram_total") {
                        push(GrassValue::Num((double)GrassHardware::gpu_vram_total_mb()));
                    }
                    else if (fn == "hw_gpu_vram_used") {
                        push(GrassValue::Num((double)GrassHardware::gpu_vram_used_mb()));
                    }
                    else if (fn == "hw_gpu_vram_shared") {
                        push(GrassValue::Num((double)GrassHardware::gpu_vram_shared_mb()));
                    }
                    // ── MOTHERBOARD & BIOS ──
                    else if (fn == "hw_board_vendor" || fn == "board_vendor") {
                        push(GrassValue::Str(GrassHardware::board_vendor()));
                    }
                    else if (fn == "hw_board_model" || fn == "board_model") {
                        push(GrassValue::Str(GrassHardware::board_model()));
                    }
                    else if (fn == "hw_bios_vendor" || fn == "bios_vendor") {
                        push(GrassValue::Str(GrassHardware::bios_vendor()));
                    }
                    else if (fn == "hw_bios_date" || fn == "bios_date") {
                        push(GrassValue::Str(GrassHardware::bios_date()));
                    }
                    else if (fn == "hw_bios_version") {
                        push(GrassValue::Str(GrassHardware::bios_version()));
                    }
                    else if (fn == "hw_pci_count" || fn == "pci_count") {
                        push(GrassValue::Num((double)GrassHardware::pci_count()));
                    }
                    else if (fn == "hw_rdtsc" || fn == "rdtsc") {
                        push(GrassValue::Num((double)GrassHardware::rdtsc()));
                    }
                    else if (fn == "hw_reboot") {
                        GrassHardware::reboot();
                    }
                    else if (fn == "use_utf" || fn == "use_locale" || fn == "set_layout" || fn == "kbd_set_layout") {
                        if (argc >= 1) {
                            GrassValue arg = args[0];
                            if (arg.is_num()) {
                                int id = (int)arg.toNumber();
                                GrassKeyboard::set_layout(id);
                            } else {
                                string name = arg.toString();
                                GrassKeyboard::set_layout_by_name(name);
                            }
                        } else {
                            GrassKeyboard::set_layout(8);
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "get_layout" || fn == "kbd_get_layout") {
                        push(GrassValue::Str(GrassKeyboard::get_layout_name()));
                    }
                    else if ((fn == "len" || fn == "length" || fn == "size") && argc >= 1) {
                        if (args[0].is_array()) {
                            GrassArray* a = args[0].asArray();
                            push(GrassValue::Num(a ? (double)a->elements.size() : 0.0));
                        } else {
                            push(GrassValue::Num((double)args[0].toString().size()));
                        }
                    }
                    else if (fn == "upper" && argc >= 1) {
                        string s = args[0].toString();
                        transform(s.begin(), s.end(), s.begin(), ::toupper);
                        push(GrassValue::Str(s));
                    }
                    else if (fn == "lower" && argc >= 1) {
                        string s = args[0].toString();
                        transform(s.begin(), s.end(), s.begin(), ::tolower);
                        push(GrassValue::Str(s));
                    }
                    else if (fn == "substr" && argc >= 2) {
                        string s = args[0].toString();
                        size_t start = (size_t)max(0.0, args[1].toNumber());
                        if (start >= s.size()) push(GrassValue::Str(""));
                        else if (argc >= 3) push(GrassValue::Str(s.substr(start, (size_t)args[2].toNumber())));
                        else push(GrassValue::Str(s.substr(start)));
                    }
                    else if (fn == "replace" && argc >= 3) {
                        string s = args[0].toString();
                        string from = args[1].toString();
                        string to = args[2].toString();
                        if (!from.empty()) {
                            size_t start_pos = 0;
                            while ((start_pos = s.find(from, start_pos)) != string::npos) {
                                s.replace(start_pos, from.length(), to);
                                start_pos += to.length();
                            }
                        }
                        push(GrassValue::Str(s));
                    }
                    else if ((fn == "contains" || fn == "find") && argc >= 2) {
                        string s = args[0].toString();
                        string sub = args[1].toString();
                        push(GrassValue::Num(s.find(sub) != string::npos ? 1.0 : 0.0));
                    }
                    else if (fn == "trim" && argc >= 1) {
                        string s = args[0].toString();
                        size_t first = s.find_first_not_of(" \t\r\n");
                        if (first == string::npos) push(GrassValue::Str(""));
                        else push(GrassValue::Str(s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1)));
                    }
                    else if ((fn == "to_str" || fn == "to_string") && argc >= 1) {
                        push(GrassValue::Str(args[0].toString()));
                    }
                    else if ((fn == "to_num" || fn == "stoi" || fn == "stod") && argc >= 1) {
                        push(GrassValue::Num(args[0].toNumber()));
                    }
                    else if (fn == "typeof" && argc >= 1) {
                        push(GrassValue::Str(args[0].is_str() ? "string" : "number"));
                    }
                    // ===== БИБЛИОТЕКА !baremetal! =====
                    else if (fn == "clear" || fn == "cls") {
                        GrassBaremetal::clear();
                        for (const auto& line : g_protected_console_lines) {
                            if (!line.empty()) {
                                GrassIO::write_stdout(line.data(), line.size());
                                GrassIO::write_stdout("\n", 1);
                            }
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "clear_all" || fn == "cls_all") {
                        g_protected_console_lines.clear();
                        GrassBaremetal::clear();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "not_clear_on") {
                        g_not_clear_active = true;
                        GrassBaremetal::not_clear_on();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "not_clear_off") {
                        g_not_clear_active = false;
                        GrassBaremetal::not_clear_off();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "not_clear") {
                        if (argc >= 1) {
                            bool on = (args[0].toNumber() != 0.0);
                            g_not_clear_active = on;
                            if (on) GrassBaremetal::not_clear_on(); else GrassBaremetal::not_clear_off();
                        } else {
                            g_not_clear_active = true;
                            GrassBaremetal::not_clear_on();
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "color") {
                        if (argc >= 2) {
                            uint8_t fg = GrassBaremetal::parse_color(args[0].toString());
                            uint8_t bg = GrassBaremetal::parse_color(args[1].toString());
                            GrassBaremetal::set_color(fg, bg);
                        } else if (argc >= 1) {
                            string c = args[0].toString();
                            if (!c.empty() && c[0] == '#') {
                                GrassBaremetal::set_color_hex(c);
                            } else {
                                GrassBaremetal::set_color(GrassBaremetal::parse_color(c));
                            }
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_key") {
                        push(GrassValue::Str(GrassBaremetal::read_key()));
                    }
                    else if (fn == "draw_box" && argc >= 4) {
                        int bx = (int)args[0].toNumber();
                        int by = (int)args[1].toNumber();
                        int bw = (int)args[2].toNumber();
                        int bh = (int)args[3].toNumber();
                        string btitle = (argc >= 5) ? args[4].toString() : "";
                        int bfg = (argc >= 6) ? (int)GrassBaremetal::parse_color(args[5].toString()) : GrassBaremetal::COLOR_LIGHT_GREEN;
                        int bbg = (argc >= 7) ? (int)GrassBaremetal::parse_color(args[6].toString()) : GrassBaremetal::COLOR_BLACK;
                        GrassBaremetal::draw_box(bx, by, bw, bh, btitle, bfg, bbg);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "menu" && argc >= 2) {
                        string mtitle = args[0].toString();
                        vector<string> mopts;
                        if (args[1].is_array() && args[1].asArray()) {
                            for (const auto& it : args[1].asArray()->elements) mopts.push_back(it.toString());
                        } else {
                            mopts.push_back(args[1].toString());
                        }
                        int sel = GrassBaremetal::menu(mtitle, mopts);
                        push(GrassValue::Num((double)sel));
                    }
                    else if (fn == "progress_bar" && argc >= 4) {
                        int px = (int)args[0].toNumber();
                        int py = (int)args[1].toNumber();
                        int pw = (int)args[2].toNumber();
                        int ppct = (int)args[3].toNumber();
                        int pfg = (argc >= 5) ? (int)GrassBaremetal::parse_color(args[4].toString()) : GrassBaremetal::COLOR_LIGHT_GREEN;
                        int pbg = (argc >= 6) ? (int)GrassBaremetal::parse_color(args[5].toString()) : GrassBaremetal::COLOR_BLACK;
                        GrassBaremetal::progress_bar(px, py, pw, ppct, pfg, pbg);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "reboot") {
                        GrassBaremetal::reboot();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "poweroff") {
                        GrassBaremetal::poweroff();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "virtual_struct") {
                        GrassBaremetal::vfs_set_has_struct(true);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "physical_struct" && argc >= 1) {
                        bool ok = GrassBaremetal::vfs_physical_struct(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "ls") {
                        string flags = (argc >= 1) ? args[0].toString() : "";
                        string listing = GrassBaremetal::vfs_ls(flags);
                        cout << listing << "\n";
                        push(GrassValue::Str(listing));
                    }
                    else if (fn == "pwd") {
                        string p = GrassBaremetal::vfs_pwd();
                        cout << p << "\n";
                        push(GrassValue::Str(p));
                    }
                    else if (fn == "cd") {
                        string p = (argc >= 1) ? args[0].toString() : "/";
                        bool ok = GrassBaremetal::vfs_cd(p);
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "copy" && argc >= 2) {
                        bool ok = GrassBaremetal::vfs_copy(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "move" && argc >= 2) {
                        bool ok = GrassBaremetal::vfs_move(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "delete" && argc >= 1) {
                        bool ok = GrassBaremetal::vfs_delete(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "cr_folder" && argc >= 1) {
                        bool ok = GrassBaremetal::vfs_cr_folder(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "cr_file" && argc >= 1) {
                        string cnt = (argc >= 2) ? args[1].toString() : "";
                        bool ok = GrassBaremetal::vfs_cr_file(args[0].toString(), cnt);
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "not_show" && argc >= 1) {
                        bool ok = GrassBaremetal::vfs_not_show(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "specSign_show" && argc >= 1) {
                        GrassBaremetal::vfs_specSign_show(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "bm_exec" && argc >= 1) {
                        bool ok = GrassBaremetal::bm_exec(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "cat" || fn == "read_file") {
                        string p = (argc >= 1) ? args[0].toString() : "";
                        string res = GrassBaremetal::cat(p);
                        cout << res << "\n";
                        push(GrassValue::Str(res));
                    }
                    else if (fn == "gui" || fn == "vd" || fn == "vd_desktop") {
                        GrassBaremetal::vd_desktop();
                        push(GrassValue::Num(1.0));
                    }
                    // ===== БИБЛИОТЕКА !time! =====
                    else if (fn == "start_time") { GrassTime::start_time(); push(GrassValue::Num(1.0)); }
                    else if (fn == "stop_time") { push(GrassValue::Num(GrassTime::stop_time())); }
                    else if (fn == "conclus_time") { push(GrassValue::Str(GrassTime::conclus_time())); }
                    else if (fn == "clock_ms" || fn == "now_ms") { push(GrassValue::Num(GrassTime::now_ms())); }
                    else if (fn == "time") { push(GrassValue::Num(GrassTime::timestamp())); }
                    else if (fn == "get_date") { push(GrassValue::Str(GrassTime::get_date())); }
                    else if (fn == "get_time_str") { push(GrassValue::Str(GrassTime::get_time_str())); }

                    // ===== БИБЛИОТЕКА !2DGraphics! =====
                    else if (fn == "gfx_init" && argc >= 3) {
                        std::string title = args[0].toString();
                        int w = (int)std::stod(args[1].toString());
                        int h = (int)std::stod(args[2].toString());
                        push(GrassValue::Num(Grass2DGraphics::init(title, w, h) ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_running") {
                        push(GrassValue::Num(Grass2DGraphics::running() ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_clear" && argc >= 1) {
                        Grass2DGraphics::clear(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_rect" && argc >= 5) {
                        int x = (int)std::stod(args[0].toString());
                        int y = (int)std::stod(args[1].toString());
                        int w = (int)std::stod(args[2].toString());
                        int h = (int)std::stod(args[3].toString());
                        Grass2DGraphics::rect(x, y, w, h, args[4].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_circle" && argc >= 4) {
                        int x = (int)std::stod(args[0].toString());
                        int y = (int)std::stod(args[1].toString());
                        int r = (int)std::stod(args[2].toString());
                        Grass2DGraphics::circle(x, y, r, args[3].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_line" && argc >= 6) {
                        int x1 = (int)std::stod(args[0].toString());
                        int y1 = (int)std::stod(args[1].toString());
                        int x2 = (int)std::stod(args[2].toString());
                        int y2 = (int)std::stod(args[3].toString());
                        int w  = (int)std::stod(args[5].toString());
                        Grass2DGraphics::line(x1, y1, x2, y2, args[4].toString(), w);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_text" && argc >= 5) {
                        int x = (int)std::stod(args[0].toString());
                        int y = (int)std::stod(args[1].toString());
                        std::string t = args[2].toString();
                        int sz = (int)std::stod(args[3].toString());
                        Grass2DGraphics::text(x, y, t, sz, args[4].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_key" && argc >= 1) {
                        push(GrassValue::Num(Grass2DGraphics::key(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_mouse_x") {
                        push(GrassValue::Num((double)Grass2DGraphics::mouse_x()));
                    }
                    else if (fn == "gfx_mouse_y") {
                        push(GrassValue::Num((double)Grass2DGraphics::mouse_y()));
                    }
                    else if (fn == "gfx_mouse_down" && argc >= 1) {
                        push(GrassValue::Num(Grass2DGraphics::mouse_down(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_collide" && argc >= 8) {
                        double x1 = std::stod(args[0].toString());
                        double y1 = std::stod(args[1].toString());
                        double w1 = std::stod(args[2].toString());
                        double h1 = std::stod(args[3].toString());
                        double x2 = std::stod(args[4].toString());
                        double y2 = std::stod(args[5].toString());
                        double w2 = std::stod(args[6].toString());
                        double h2 = std::stod(args[7].toString());
                        push(GrassValue::Num(Grass2DGraphics::collide(x1, y1, w1, h1, x2, y2, w2, h2) ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_frame") {
                        int fps = (argc >= 1) ? (int)std::stod(args[0].toString()) : 60;
                        Grass2DGraphics::frame(fps);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_close") {
                        Grass2DGraphics::close();
                        push(GrassValue::Num(1.0));
                    }

                    // ===== РАСШИРЕННАЯ БИБЛИОТЕКА !tg! =====
                    else if (fn == "tg_init" && argc >= 1) {
                        GrassTg::init(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "tg_poll") {
                        int timeout_val = (argc >= 1) ? (int)std::stod(args[0].toString()) : 2;
                        push(GrassValue::Str(GrassTg::poll(timeout_val)));
                    }
                    else if (fn == "tg_get_user") {
                        push(GrassValue::Str(GrassTg::get_user()));
                    }
                    else if (fn == "tg_get_chat_id") {
                        push(GrassValue::Str(GrassTg::get_chat_id()));
                    }
                    else if (fn == "tg_get_msg_id") {
                        push(GrassValue::Str(GrassTg::get_msg_id()));
                    }
                    else if (fn == "tg_get_callback") {
                        push(GrassValue::Str(GrassTg::get_callback()));
                    }
                    else if (fn == "tg_send" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::send(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_reply" && argc >= 1) {
                        push(GrassValue::Num(GrassTg::reply(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_btn" && argc >= 3) {
                        push(GrassValue::Num(GrassTg::send_btn(args[0].toString(), args[1].toString(), args[2].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_inline" && argc >= 3) {
                        push(GrassValue::Num(GrassTg::send_inline(args[0].toString(), args[1].toString(), args[2].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_webapp" && argc >= 4) {
                        push(GrassValue::Num(GrassTg::send_webapp(args[0].toString(), args[1].toString(), args[2].toString(), args[3].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_photo" && argc >= 3) {
                        push(GrassValue::Num(GrassTg::send_photo(args[0].toString(), args[1].toString(), args[2].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_dice" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::send_dice(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_edit" && argc >= 3) {
                        push(GrassValue::Num(GrassTg::edit(args[0].toString(), args[1].toString(), args[2].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_del" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::del(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }

                    // ===== БИБЛИОТЕКА !fsys! =====
                    else if (fn == "fs_mkdir" && argc >= 1) {
                        push(GrassValue::Num(GrassFsys::mkdir(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_is_dir" && argc >= 1) {
                        push(GrassValue::Num(GrassFsys::is_dir(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_size" && argc >= 1) {
                        push(GrassValue::Num((double)GrassFsys::size(args[0].toString())));
                    }
                    else if (fn == "fs_copy" && argc >= 2) {
                        push(GrassValue::Num(GrassFsys::copy(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_list" && argc >= 1) {
                        push(GrassValue::Str(GrassFsys::list(args[0].toString())));
                    }
                    else if (fn == "fs_read" && argc >= 1) {
                        push(GrassValue::Str(GrassFsys::read(args[0].toString())));
                    }
                    else if (fn == "fs_write" && argc >= 2) {
                        push(GrassValue::Num(GrassFsys::write(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_exists" && argc >= 1) {
                        push(GrassValue::Num(GrassFsys::exists(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_remove" && argc >= 1) {
                        push(GrassValue::Num(GrassFsys::remove(args[0].toString()) ? 1.0 : 0.0));
                    }

                    // ===== БИБЛИОТЕКА !proc! =====
                    else if ((fn == "proc_find" || fn == "find") && argc >= 1) {
                        push(GrassValue::Num((double)GrassProc::find(args[0].toString())));
                    }
                    else if ((fn == "proc_kill" || fn == "kill") && argc >= 1) {
                        push(GrassValue::Num(GrassProc::kill((int64_t)args[0].toNumber()) ? 1.0 : 0.0));
                    }
                    else if ((fn == "proc_mem" || fn == "mem") && argc >= 1) {
                        push(GrassValue::Num((double)GrassProc::mem((int64_t)args[0].toNumber())));
                    }
                    else if (fn == "proc_exists" || (fn == "exists" && argc >= 1 && !args[0].is_str())) {
                        push(GrassValue::Num(GrassProc::exists((int64_t)args[0].toNumber()) ? 1.0 : 0.0));
                    }
                    else if (fn == "proc_list" || fn == "list") {
                        push(GrassValue::Str(GrassProc::list()));
                    }

                    // ===== БИБЛИОТЕКА !sys! =====
                    else if (fn == "clip_set" && argc >= 1) {
                        bool ok = GrassSys::clip_set(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "clip_get") {
                        push(GrassValue::Str(GrassSys::clip_get()));
                    }
                    else if (fn == "screen_w") {
                        push(GrassValue::Num((double)GrassSys::screen_w()));
                    }
                    else if (fn == "screen_h") {
                        push(GrassValue::Num((double)GrassSys::screen_h()));
                    }
                    else if (fn == "pc_name") {
                        push(GrassValue::Str(GrassSys::pc_name()));
                    }
                    else if (fn == "user_name") {
                        push(GrassValue::Str(GrassSys::user_name()));
                    }
                    else if (fn == "cpu_cores") {
                        push(GrassValue::Num((double)GrassSys::cpu_cores()));
                    }
                    else if (fn == "ram_total_mb") {
                        push(GrassValue::Num(GrassSys::ram_total_mb()));
                    }
                    else if (fn == "ram_free_mb") {
                        push(GrassValue::Num(GrassSys::ram_free_mb()));
                    }
                    else if (fn == "get_env" && argc >= 1) {
                        push(GrassValue::Str(GrassSys::get_env(args[0].toString())));
                    }
                    else if (fn == "uptime_sec") {
                        push(GrassValue::Num(GrassSys::uptime_sec()));
                    }

                    // ===== БИБЛИОТЕКА !json! =====
                    else if (fn == "json_get" && argc >= 2) {
                        push(GrassValue::Str(GrassJson::get(args[0].toString(), args[1].toString())));
                    }
                    else if (fn == "json_set" && argc >= 3) {
                        push(GrassValue::Str(GrassJson::set(args[0].toString(), args[1].toString(), args[2].toString())));
                    }
                    else if (fn == "json_valid" && argc >= 1) {
                        push(GrassValue::Num(GrassJson::is_valid(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "json_minify" && argc >= 1) {
                        push(GrassValue::Str(GrassJson::minify(args[0].toString())));
                    }
                    else if (fn == "json_clear") {
                        GrassJson::builder_clear();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "json_add" && argc >= 2) {
                        GrassJson::builder_add_str(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "json_add_num" && argc >= 2) {
                        GrassJson::builder_add_num(args[0].toString(), args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "json_build") {
                        push(GrassValue::Str(GrassJson::builder_stringify()));
                    }
                    else if (fn == "msgbox" && argc >= 1) {
                        string title = (argc >= 2) ? args[0].toString() : "GrassGreen";
                        string msg   = (argc >= 2) ? args[1].toString() : args[0].toString();
                        MessageBoxA(NULL, msg.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "system" && argc >= 1) {
                        int r = system(args[0].toString().c_str());
                        push(GrassValue::Num((double)r));
                    }
                    // !random!
                    else if (fn == "random") {
                        double min_v = 0.0, max_v = 100.0;
                        if (argc == 1) max_v = args[0].toNumber();
                        else if (argc >= 2) { min_v = args[0].toNumber(); max_v = args[1].toNumber(); }
                        push(GrassValue::Num(GrassRandom::get_random(min_v, max_v)));
                    }
                    else if (fn == "random_txt") {
                        string lang = (argc >= 1) ? args[0].toString() : "en";
                        int len = (argc >= 2) ? (int)args[1].toNumber() : 1;
                        push(GrassValue::Str(GrassRandom::get_random_txt(lang, len)));
                    }
                    else if (fn == "stop_random") { GrassRandom::stop_random(); push(GrassValue::Num(1.0)); }
                    else if (fn == "play_random") { GrassRandom::play_random(); push(GrassValue::Num(1.0)); }

                    // !memory!
                    else if (fn == "memory_usit") { push(GrassValue::Num(GrassMemory::usit())); }
                    else if (fn == "memory_separ") { GrassMemory::separ(); push(GrassValue::Num(1.0)); }
                    else if (fn == "memory_auto") { GrassMemory::set_auto(true); push(GrassValue::Num(1.0)); }
                    else if (fn == "memory_clean") { GrassMemory::clean(); push(GrassValue::Num(1.0)); }
                    else if (fn == "memory_free") { push(GrassValue::Str(GrassMemory::get_system_free())); }
                    else if (fn == "memory_alloc" && argc >= 1) {
                        int block_id = GrassMemory::alloc_block((size_t)args[0].toNumber());
                        push(GrassValue::Num((double)block_id));
                    }
                    else if (fn == "memory_free_block" && argc >= 1) {
                        bool ok = GrassMemory::free_block((int)args[0].toNumber());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "memory_limit" && argc >= 1) {
                        GrassMemory::set_limit(args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "memory_dump") {
                        vector<string> st_preview;
                        for (int i = (int)stack.size() - 1; i >= max(0, (int)stack.size() - 5); --i) {
                            st_preview.push_back(stack[i].toString());
                        }
                        vector<pair<string, string>> v_preview;
                        for (const auto& kv : vars) v_preview.push_back({kv.first, kv.second.toString()});
                        GrassMemory::dump(stack.size(), st_preview, v_preview);
                        push(GrassValue::Num(1.0));
                    }

                    // !FastMath!
                    else if ((fn == "discor" || fn == "sqrt") && argc >= 1) push(GrassValue::Num(FastMath::discor(args[0].toNumber())));
                    else if (fn == "sin" && argc >= 1) push(GrassValue::Num(FastMath::sin(args[0].toNumber())));
                    else if (fn == "cos" && argc >= 1) push(GrassValue::Num(FastMath::cos(args[0].toNumber())));
                    else if (fn == "abs" && argc >= 1) push(GrassValue::Num(FastMath::abs(args[0].toNumber())));
                    else if (fn == "round" && argc >= 1) push(GrassValue::Num(FastMath::round(args[0].toNumber())));
                    else if (fn == "floor" && argc >= 1) push(GrassValue::Num(std::floor(args[0].toNumber())));
                    else if (fn == "ceil" && argc >= 1) push(GrassValue::Num(std::ceil(args[0].toNumber())));
                    else if (fn == "pow" && argc >= 2) push(GrassValue::Num(std::pow(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "clamp" && argc >= 3) push(GrassValue::Num(std::clamp(args[0].toNumber(), args[1].toNumber(), args[2].toNumber())));
                    else if (fn == "min" && argc >= 2) push(GrassValue::Num(FastMath::min(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "max" && argc >= 2) push(GrassValue::Num(FastMath::max(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "rand" && argc >= 2) push(GrassValue::Num(FastMath::rand((int)args[0].toNumber(), (int)args[1].toNumber())));
                    else if (fn == "lerp" && argc >= 3) push(GrassValue::Num(FastMath::lerp(args[0].toNumber(), args[1].toNumber(), args[2].toNumber())));

                    // !CoreMath!
                    else if (fn == "qb_init") push(GrassValue::Str(CoreMath::qb_init(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "h")       push(GrassValue::Str(CoreMath::h(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "x")       push(GrassValue::Str(CoreMath::x(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "cnot" && argc >= 2) push(GrassValue::Str(CoreMath::cnot(args[0].toString(), args[1].toString())));
                    else if (fn == "q_rand")  push(GrassValue::Num(CoreMath::q_rand(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "pi")      push(GrassValue::Str(CoreMath::pi()));
                    else if (fn == "gcd" && argc >= 2)   push(GrassValue::Num((double)CoreMath::gcd((long long)args[0].toNumber(), (long long)args[1].toNumber())));
                    else if (fn == "pow_e" && argc >= 2) push(GrassValue::Str(CoreMath::pow_e(args[0].toNumber(), args[1].toNumber())));

                    // Файлы и ОС
                    else if (fn == "creat" && argc >= 1) { ofstream f(args[0].toString()); f.close(); push(GrassValue::Num(1.0)); }
                    else if (fn == "redact" && argc >= 2) { ofstream f(args[0].toString()); f << args[1].toString(); f.close(); push(GrassValue::Num(1.0)); }
                    else if (fn == "read" && argc >= 1) {
                        ifstream f(args[0].toString()); stringstream ss;
                        if (f.is_open()) ss << f.rdbuf();
                        push(GrassValue::Str(ss.str()));
                    }
                    else if (fn == "exists" && argc >= 1) {
                        ifstream f(args[0].toString());
                        push(GrassValue::Num(f.is_open() ? 1.0 : 0.0));
                    }
                    else if (fn == "remove" && argc >= 1) {
                        bool ok = fs::remove(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if ((fn == "open" || fn == "run") && argc >= 1) {
                        string target = args[0].toString();
                        string lower = target;
                        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        bool is_dangerous = (lower.find("powershell") != string::npos ||
                                             lower.find("cmd.exe") != string::npos ||
                                             lower.find(".bat") != string::npos ||
                                             lower.find(".cmd") != string::npos ||
                                             lower.find(".vbs") != string::npos ||
                                             lower.find(".ps1") != string::npos ||
                                             lower.find(".scr") != string::npos);
                        if (!is_dangerous) {
                            ShellExecuteA(NULL, "open", target.c_str(), NULL, NULL, SW_SHOWNORMAL);
                            push(GrassValue::Num(1.0));
                        } else {
                            cerr << "[GrassSandbox Guard]: Запуск системных командных процессоров и скриптов заблокирован!\n";
                            push(GrassValue::Num(0.0));
                        }
                    }

                    // ===== БИБЛИОТЕКА !GrassNet! =====
                    else if (fn == "connect" && argc >= 1) {
                        int port = (argc >= 2) ? (int)args[1].toNumber() : 80;
                        bool ok = GrassNet::connect_host(args[0].toString(), port);
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "send" && argc >= 1) {
                        string method = (argc >= 2) ? args[1].toString() : "GET";
                        string body = (argc >= 3) ? args[2].toString() : "";
                        push(GrassValue::Str(GrassNet::send_request(args[0].toString(), method, body)));
                    }
                    else if (fn == "net_set_agent" && argc >= 2) {
                        GrassNet::set_agent(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "API" && argc >= 1) {
                        GrassNet::set_api(args[0].toString());
                        push(GrassValue::Str(args[0].toString()));
                    }
                    else if (fn == "create_serv" && argc >= 4) {
                        bool ok = GrassNet::create_server(
                            args[0].toString(),
                            (int)args[1].toNumber(),
                            args[2].toString(),
                            args[3].toString()
                        );
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "accept") {
                        push(GrassValue::Str(GrassNet::accept_request()));
                    }
                    else if (fn == "ping" && argc >= 1) {
                        int port = (argc >= 2) ? (int)args[1].toNumber() : 80;
                        push(GrassValue::Num(GrassNet::ping(args[0].toString(), port)));
                    }
                    else if (fn == "resolve_ip" && argc >= 1) {
                        push(GrassValue::Str(GrassNet::resolve_ip(args[0].toString())));
                    }
                    else if (fn == "local_ip") {
                        push(GrassValue::Str(GrassNet::get_local_ip()));
                    }
                    else if (fn == "download" && argc >= 2) {
                        bool ok = GrassNet::download_file(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "net_status") {
                        push(GrassValue::Num((double)GrassNet::get_last_status()));
                    }
                    else if (fn == "net_close") {
                        GrassNet::close_all();
                        push(GrassValue::Num(1.0));
                    }

                    // !database! (GSE)
                    else if (fn == "db_open" && argc >= 1) {
                        bool ok = GrassDB::open(args[0].toString());
                        syncDBToVars();
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "db_save") {
                        if (argc >= 2) {
                            string k = args[0].toString(), v = args[1].toString();
                            GrassDB::set(k, v); GrassDB::save();
                            vars[k] = GrassValue::Str(v);
                            push(GrassValue::Num(1.0));
                        } else if (argc == 1) {
                            push(GrassValue::Num(GrassDB::save(args[0].toString()) ? 1.0 : 0.0));
                        } else {
                            push(GrassValue::Num(GrassDB::save() ? 1.0 : 0.0));
                        }
                    }
                    else if (fn == "db_set" && argc >= 2) {
                        string k = args[0].toString(), v = args[1].toString();
                        GrassDB::set(k, v);
                        vars[k] = GrassValue::Str(GrassDB::get(k));
                        push(GrassValue::Str(GrassDB::get(k)));
                    }
                    else if (fn == "db_get" && argc >= 1) {
                        string k = args[0].toString();
                        string def_v = (argc >= 2) ? args[1].toString() : "";
                        if (GrassDB::has(k)) push(GrassValue::Str(GrassDB::get(k, def_v)));
                        else if (vars.count(k)) push(vars[k]);
                        else push(GrassValue::Str(def_v));
                    }
                    else if ((fn == "db_del" || fn == "db_remove") && argc >= 1) {
                        string k = args[0].toString();
                        vars.erase(k);
                        push(GrassValue::Num(GrassDB::remove(k) ? 1.0 : 0.0));
                    }
                    else if ((fn == "db_has" || fn == "db_exists") && argc >= 1) {
                        string k = args[0].toString();
                        push(GrassValue::Num((GrassDB::has(k) || vars.count(k) > 0) ? 1.0 : 0.0));
                    }
                    else if (fn == "db_inc" && argc >= 1) {
                        long long delta = (argc >= 2) ? (long long)args[1].toNumber() : 1;
                        long long res = GrassDB::add_int(args[0].toString(), delta);
                        vars[args[0].toString()] = GrassValue::Num((double)res);
                        push(GrassValue::Num((double)res));
                    }
                    else if (fn == "db_dec" && argc >= 1) {
                        long long delta = (argc >= 2) ? (long long)args[1].toNumber() : 1;
                        long long res = GrassDB::add_int(args[0].toString(), -delta);
                        vars[args[0].toString()] = GrassValue::Num((double)res);
                        push(GrassValue::Num((double)res));
                    }
                    else if (fn == "db_clear") { GrassDB::clear(); push(GrassValue::Num(1.0)); }
                    else if (fn == "db_autosave" && argc >= 1) {
                        GrassDB::exec_command("db_autosave(" + args[0].toString() + ")");
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "db_backup") {
                        string bpath = (argc >= 1) ? args[0].toString() : "storage.gdb.bak";
                        push(GrassValue::Num(GrassDB::backup(bpath) ? 1.0 : 0.0));
                    }
                    else if (fn == "db_export_json") {
                        string jpath = (argc >= 1) ? args[0].toString() : "storage.json";
                        push(GrassValue::Num(GrassDB::export_json(jpath) ? 1.0 : 0.0));
                    }
                    else if (fn == "db_hash" && argc >= 1) {
                        push(GrassValue::Str(GrassDB::hash_string(args[0].toString())));
                    }
                    else if (fn == "db_encrypt" && argc >= 1) {
                        string salt = (argc >= 2) ? args[1].toString() : "GrassGreenKey";
                        GrassDB::encrypt_key(args[0].toString(), salt);
                        vars[args[0].toString()] = GrassValue::Str(GrassDB::get(args[0].toString()));
                        push(GrassValue::Str(GrassDB::get(args[0].toString())));
                    }
                    else if (fn == "db_decrypt" && argc >= 1) {
                        string salt = (argc >= 2) ? args[1].toString() : "GrassGreenKey";
                        push(GrassValue::Str(GrassDB::decrypt_key(args[0].toString(), salt)));
                    }
                    else if (fn == "table") {
                        string tname = (argc >= 1) ? args[0].toString() : "default";
                        GrassDB::use_table(tname);
                        push(GrassValue::Str(tname));
                    }
                    else if (isGdbSchemaKey(fn)) {
                        GrassDB::init_default_schema();
                        if (argc >= 1) {
                            GrassDB::set(fn, args[0].toString());
                            string updated = GrassDB::get(fn);
                            vars[fn] = GrassValue::Str(updated);
                            push(GrassValue::Str(updated));
                        } else {
                            push(GrassValue::Str(GrassDB::get(fn)));
                        }
                    }

                    // !interface! & !GrassWeb!
                    else if (fn == "surface" && argc >= 1) {
                        GrassGUI::set_surface_mode(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "auto_size_display") {
                        GrassGUI::auto_size_display(true);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "fullscreen" || fn == "fullscrean") {
                        GrassGUI::set_fullscreen(true);
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "switch_tab" || fn == "switch") && argc >= 1) {
                        GrassGUI::switch_tab(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gui_cmd" && argc >= 1) {
                        syncVarsToGUI();
                        GrassGUI::exec_command(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gui_get" && argc >= 1) { push(GrassValue::Str(GrassGUI::get_input_value(args[0].toString()))); }
                    else if (fn == "gui_set" && argc >= 2) { GrassGUI::set_variable(args[0].toString(), args[1].toString()); push(GrassValue::Num(1.0)); }
                    else if (fn == "gui_show") {
                        syncVarsToGUI();
                        GrassGUI::show_window();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gui_file" && argc >= 1) {
                        syncVarsToGUI();
                        GrassGUI::run_from_file(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "web_start" && argc >= 1) { GrassWeb_LoadPage(args[0].toString()); push(GrassValue::Num(1.0)); }
                    else if (fn == "web_compile" && argc >= 1) {
                        string html = CompileGrassToHTML(args[0].toString());
                        if (argc >= 2) {
                            ofstream out(args[1].toString(), ios::trunc);
                            out << html;
                            out.close();
                        }
                        push(GrassValue::Str(html));
                    }
                    // ===== БИБЛИОТЕКА !audio! =====
                    else if (fn == "tone" && argc >= 2) {
                        double vol = (argc >= 3) ? args[2].toNumber() : 80.0;
                        GrassAudio::tone(args[0].toNumber(), (int)args[1].toNumber(), vol);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "beep") {
                        double f = (argc >= 1) ? args[0].toNumber() : 750.0;
                        int dur = (argc >= 2) ? (int)args[1].toNumber() : 300;
                        GrassAudio::beep(f, dur);
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "synth_play" || fn == "synth") && argc >= 3) {
                        double vol = (argc >= 4) ? args[3].toNumber() : 80.0;
                        GrassAudio::synth_play(args[0].toString(), args[1].toNumber(), (int)args[2].toNumber(), vol);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_play" && argc >= 1) {
                        bool loop = (argc >= 2) ? (args[1].toNumber() != 0.0) : false;
                        bool ok = GrassAudio::sound_play(args[0].toString(), loop);
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "sound_stop") {
                        GrassAudio::sound_stop();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_pause") {
                        GrassAudio::sound_pause();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_resume") {
                        GrassAudio::sound_resume();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_volume" && argc >= 1) {
                        GrassAudio::sound_volume((int)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_is_playing") {
                        push(GrassValue::Num(GrassAudio::sound_is_playing() ? 1.0 : 0.0));
                    }
                    // ── Многопоточность (!thread!) ──
                    else if (fn == "thread_join" && argc >= 1) {
                        int64_t tid = (int64_t)args[0].toNumber();
                        push(GrassThread::join_thread(tid));
                    }
                    else if (fn == "thread_detach" && argc >= 1) {
                        int64_t tid = (int64_t)args[0].toNumber();
                        push(GrassValue::Num(GrassThread::detach_thread(tid) ? 1.0 : 0.0));
                    }
                    else if (fn == "thread_id") {
                        push(GrassValue::Num((double)GrassThread::get_current_thread_id()));
                    }
                    else if (fn == "thread_sleep" && argc >= 1) {
                        uint32_t ms = (uint32_t)args[0].toNumber();
                        GrassThread::sleep_ms(ms);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mutex_create") {
                        int64_t mid = GrassThread::mutex_create();
                        push(GrassValue::Num((double)mid));
                    }
                    else if (fn == "mutex_lock" && argc >= 1) {
                        int64_t mid = (int64_t)args[0].toNumber();
                        push(GrassValue::Num(GrassThread::mutex_lock(mid) ? 1.0 : 0.0));
                    }
                    else if (fn == "mutex_unlock" && argc >= 1) {
                        int64_t mid = (int64_t)args[0].toNumber();
                        push(GrassValue::Num(GrassThread::mutex_unlock(mid) ? 1.0 : 0.0));
                    }
                    else if (fn == "mutex_destroy" && argc >= 1) {
                        int64_t mid = (int64_t)args[0].toNumber();
                        push(GrassValue::Num(GrassThread::mutex_destroy(mid) ? 1.0 : 0.0));
                    }
                    // ── Аппаратный доступ к памяти: MMIO и Port I/O (!mmio!) ──
                    else if (fn == "mmio_read8" && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::read8((uint64_t)args[0].toNumber())));
                    }
                    else if (fn == "mmio_read16" && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::read16((uint64_t)args[0].toNumber())));
                    }
                    else if (fn == "mmio_read32" && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::read32((uint64_t)args[0].toNumber())));
                    }
                    else if (fn == "mmio_read64" && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::read64((uint64_t)args[0].toNumber())));
                    }
                    else if (fn == "mmio_write8" && argc >= 2) {
                        GrassMMIO::write8((uint64_t)args[0].toNumber(), (uint8_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mmio_write16" && argc >= 2) {
                        GrassMMIO::write16((uint64_t)args[0].toNumber(), (uint16_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mmio_write32" && argc >= 2) {
                        GrassMMIO::write32((uint64_t)args[0].toNumber(), (uint32_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mmio_write64" && argc >= 2) {
                        GrassMMIO::write64((uint64_t)args[0].toNumber(), (uint64_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "io_in8" || fn == "inb") && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::in8((uint16_t)args[0].toNumber())));
                    }
                    else if ((fn == "io_in16" || fn == "inw") && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::in16((uint16_t)args[0].toNumber())));
                    }
                    else if ((fn == "io_in32" || fn == "inl") && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::in32((uint16_t)args[0].toNumber())));
                    }
                    else if ((fn == "io_out8" || fn == "outb") && argc >= 2) {
                        GrassMMIO::out8((uint16_t)args[0].toNumber(), (uint8_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "io_out16" || fn == "outw") && argc >= 2) {
                        GrassMMIO::out16((uint16_t)args[0].toNumber(), (uint16_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "io_out32" || fn == "outl") && argc >= 2) {
                        GrassMMIO::out32((uint16_t)args[0].toNumber(), (uint32_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "memory_barrier") {
                        GrassMMIO::memory_barrier();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_barrier") {
                        GrassMMIO::read_barrier();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "write_barrier") {
                        GrassMMIO::write_barrier();
                        push(GrassValue::Num(1.0));
                    }
                    // ── БИБЛИОТЕКА !asm! ──
                    else if (fn == "cli") {
                        GrassASM::cli();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sti") {
                        GrassASM::sti();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "hlt") {
                        GrassASM::hlt();
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "in" || fn == "in8") && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::in8((uint16_t)args[0].toNumber())));
                    }
                    else if (fn == "in16" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::in16((uint16_t)args[0].toNumber())));
                    }
                    else if (fn == "in32" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::in32((uint16_t)args[0].toNumber())));
                    }
                    else if ((fn == "out" || fn == "out8") && argc >= 2) {
                        GrassASM::out8((uint16_t)args[0].toNumber(), (uint8_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "out16" && argc >= 2) {
                        GrassASM::out16((uint16_t)args[0].toNumber(), (uint16_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "out32" && argc >= 2) {
                        GrassASM::out32((uint16_t)args[0].toNumber(), (uint32_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_cr0") {
                        push(GrassValue::Num((double)GrassASM::read_cr0()));
                    }
                    else if (fn == "write_cr0" && argc >= 1) {
                        GrassASM::write_cr0((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_cr2") {
                        push(GrassValue::Num((double)GrassASM::read_cr2()));
                    }
                    else if (fn == "write_cr2" && argc >= 1) {
                        GrassASM::write_cr2((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_cr3") {
                        push(GrassValue::Num((double)GrassASM::read_cr3()));
                    }
                    else if (fn == "write_cr3" && argc >= 1) {
                        GrassASM::write_cr3((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_cr4") {
                        push(GrassValue::Num((double)GrassASM::read_cr4()));
                    }
                    else if (fn == "write_cr4" && argc >= 1) {
                        GrassASM::write_cr4((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_rflags") {
                        push(GrassValue::Num((double)GrassASM::read_rflags()));
                    }
                    else if (fn == "write_rflags" && argc >= 1) {
                        GrassASM::write_rflags((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "invlpg" && argc >= 1) {
                        GrassASM::invlpg((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "rdtsc") {
                        push(GrassValue::Num((double)GrassASM::rdtsc()));
                    }
                    else if (fn == "pause") {
                        GrassASM::pause();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "nop") {
                        GrassASM::nop();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "cpuid_eax" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::cpuid_eax((uint32_t)args[0].toNumber())));
                    }
                    else if (fn == "cpuid_ebx" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::cpuid_ebx((uint32_t)args[0].toNumber())));
                    }
                    else if (fn == "cpuid_ecx" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::cpuid_ecx((uint32_t)args[0].toNumber())));
                    }
                    else if (fn == "cpuid_edx" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::cpuid_edx((uint32_t)args[0].toNumber())));
                    }
                    else if (fn == "asm_run" && argc >= 1) {
                        push(GrassASM::run(args[0].toString()));
                    }
                    else if (fn == "asm_bytes" && argc >= 1) {
                        push(GrassASM::run_hex(args[0].toString()));
                    }
                    else if (fn == "asm_hex" && argc >= 1) {
                        push(GrassASM::run_hex(args[0].toString()));
                    }
                    else if (fn == "asm_machine_hex") {
                        push(GrassValue::Str(GrassASM::get_last_machine_hex()));
                    }
                    // ── Библиотека контроллера клавиатуры (!keyboard!) ──
                    else if (fn == "kbd_has_key" || fn == "kbd_available") {
                        push(GrassValue::Num(GrassKeyboard::has_key() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_read_scancode") {
                        push(GrassValue::Num((double)GrassKeyboard::read_scancode()));
                    }
                    else if (fn == "kbd_get_scancode") {
                        push(GrassValue::Num((double)GrassKeyboard::get_scancode()));
                    }
                    else if ((fn == "kbd_scancode_to_char" || fn == "kbd_scancode_to_ascii") && argc >= 1) {
                        push(GrassValue::Str(GrassKeyboard::scancode_to_char((uint8_t)args[0].toNumber())));
                    }
                    else if (fn == "kbd_scancode_name" && argc >= 1) {
                        push(GrassValue::Str(GrassKeyboard::scancode_name((uint8_t)args[0].toNumber())));
                    }
                    else if (fn == "kbd_getc") {
                        push(GrassValue::Str(GrassKeyboard::getc()));
                    }
                    else if (fn == "kbd_poll") {
                        push(GrassValue::Str(GrassKeyboard::poll()));
                    }
                    else if (fn == "kbd_read_line") {
                        push(GrassValue::Str(GrassKeyboard::read_line()));
                    }
                    else if (fn == "kbd_is_shift") {
                        push(GrassValue::Num(GrassKeyboard::is_shift() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_is_caps") {
                        push(GrassValue::Num(GrassKeyboard::is_caps() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_is_ctrl") {
                        push(GrassValue::Num(GrassKeyboard::is_ctrl() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_is_alt") {
                        push(GrassValue::Num(GrassKeyboard::is_alt() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_is_key_pressed" && argc >= 1) {
                        push(GrassValue::Num(GrassKeyboard::is_key_pressed((uint8_t)args[0].toNumber()) ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_leds" && argc >= 3) {
                        GrassKeyboard::set_leds(args[0].toNumber() != 0, args[1].toNumber() != 0, args[2].toNumber() != 0);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "kbd_reset") {
                        GrassKeyboard::reset();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "kbd_self_test") {
                        push(GrassValue::Num(GrassKeyboard::self_test() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_wait_ack") {
                        push(GrassValue::Num(GrassKeyboard::wait_ack() ? 1.0 : 0.0));
                    }
                    else if (vars.count(fn + "()")) push(vars[fn + "()"]);
                    else if (vars.count(fn)) push(vars[fn]);
                    else if (shared_env && shared_env->vars.count(fn)) push(shared_env->vars[fn]);
                    else if (GrassDB::has(fn)) push(GrassValue::Str(GrassDB::get(fn)));
                    else push(GrassValue::Num(0.0));
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
        if (obj.is_array()) {
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
        if (obj.is_array()) {
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
            auto it = g_struct_registry.find(s_name);
            if (it != g_struct_registry.end()) {
                GrassStructDef& def = it->second;
                GrassStructInstance* inst = (GrassStructInstance*)g_vm_arena.alloc(sizeof(GrassStructInstance));
                uint8_t* buf = (uint8_t*)g_vm_arena.alloc(def.total_size);
                if (buf) memset(buf, 0, def.total_size);
                inst->def = &def;
                inst->data = buf;
                push(GrassValue::Struct(inst));
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
            if (obj.is_struct()) {
                GrassStructInstance* inst = obj.asStruct();
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
            if (obj.is_struct()) {
                GrassStructInstance* inst = obj.asStruct();
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

    do_unknown:
        DISPATCH_STEP();

#undef DISPATCH_STEP
    }
};

// ============================================================================
// 2. АВТО-УСТАНОВЩИК ЯЗЫКА И ИКОНОК (.GRASS + .GDB) В VS CODE
// ============================================================================
void InstallGrassGreenEcosystem() {
    string self_path_str;
#if defined(_WIN32)
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    self_path_str = self_path;
#else
    char buf[1024];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        self_path_str = buf;
    } else {
        self_path_str = "grassc";
    }
#endif
    fs::path exe_path(self_path_str);
    fs::path root_dir = exe_path.parent_path();
    if (root_dir.filename() == "bin") root_dir = root_dir.parent_path();

    cout << "\n=== [GrassGreen (G&G) v4.1.0 Installer] ===\n";

#if defined(_WIN32)
    {
        ofstream grgcmd(root_dir / "grg.cmd");
        grgcmd << "@echo off\n\"%~dp0bin\\grassc.exe\" %*\n";
    }
    {
        ofstream gcmd(root_dir / "grass.cmd");
        gcmd << "@echo off\n\"%~dp0bin\\grassc.exe\" ./! %*\n";
    }
    {
        ofstream ggcmd(root_dir / "gg.cmd");
        ggcmd << "@echo off\n\"%~dp0bin\\grassc.exe\" ./! %*\n";
    }
    try {
        fs::copy_file(exe_path, root_dir / "bin" / "grg.exe", fs::copy_options::overwrite_existing);
    } catch (...) {}
    cout << "[+] Созданы системные команды: grg <файл.grass>, grass <файл.grass> и gg <файл.grass>\n";
    cout << "[+] Бинарник grg.exe продублирован в bin/grg.exe для интеграции в PATH.\n";
#else
    try {
        fs::copy_file(exe_path, root_dir / "bin" / "grg", fs::copy_options::overwrite_existing);
    } catch (...) {}
    cout << "[+] Бинарник grg продублирован в bin/grg для интеграции в PATH.\n";
#endif

    const char* userprofile = getenv("USERPROFILE");
#if !defined(_WIN32)
    if (!userprofile) userprofile = getenv("HOME");
#endif
    if (userprofile) {
        fs::path ext_dir = fs::path(userprofile) / ".vscode" / "extensions" / "m1oky.grassgreen-lang-4.1.0";
        fs::create_directories(ext_dir / "syntaxes");
        fs::create_directories(ext_dir / "snippets");

        ofstream svg(ext_dir / "logo.svg");
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">\n"
            << "  <defs>\n"
            << "    <linearGradient id=\"ggGrad\" x1=\"0%\" y1=\"100%\" x2=\"100%\" y2=\"0%\">\n"
            << "      <stop offset=\"0%\" stop-color=\"#00b359\"/>\n"
            << "      <stop offset=\"100%\" stop-color=\"#00ffcc\"/>\n"
            << "    </linearGradient>\n"
            << "  </defs>\n"
            << "  <rect x=\"8\" y=\"8\" width=\"112\" height=\"112\" rx=\"28\" fill=\"#0d1117\" stroke=\"url(#ggGrad)\" stroke-width=\"6\"/>\n"
            << "  <path d=\"M40 88 C40 52, 68 36, 88 28 C76 54, 72 72, 74 92\" fill=\"none\" stroke=\"url(#ggGrad)\" stroke-width=\"10\" stroke-linecap=\"round\"/>\n"
            << "  <path d=\"M36 92 C52 72, 72 64, 92 60\" fill=\"none\" stroke=\"#00ff88\" stroke-width=\"8\" stroke-linecap=\"round\"/>\n"
            << "  <circle cx=\"88\" cy=\"28\" r=\"6\" fill=\"#00ffcc\"/>\n"
            << "</svg>\n";
        svg.close();

        ofstream gdb_svg(ext_dir / "gdb_logo.svg");
        gdb_svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">\n"
                << "  <defs>\n"
                << "    <linearGradient id=\"gdbGrad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">\n"
                << "      <stop offset=\"0%\" stop-color=\"#00ffcc\"/>\n"
                << "      <stop offset=\"100%\" stop-color=\"#0088ff\"/>\n"
                << "    </linearGradient>\n"
                << "  </defs>\n"
                << "  <rect x=\"8\" y=\"8\" width=\"112\" height=\"112\" rx=\"28\" fill=\"#0d1117\" stroke=\"url(#gdbGrad)\" stroke-width=\"6\"/>\n"
                << "  <ellipse cx=\"64\" cy=\"36\" rx=\"34\" ry=\"12\" fill=\"#161b22\" stroke=\"url(#gdbGrad)\" stroke-width=\"5\"/>\n"
                << "  <path d=\"M30 36 V64 C30 71, 45 76, 64 76 C83 76, 98 71, 98 64 V36\" fill=\"none\" stroke=\"url(#gdbGrad)\" stroke-width=\"5\"/>\n"
                << "  <path d=\"M30 64 V92 C30 99, 45 104, 64 104 C83 104, 98 99, 98 92 V64\" fill=\"none\" stroke=\"url(#gdbGrad)\" stroke-width=\"5\"/>\n"
                << "  <circle cx=\"80\" cy=\"56\" r=\"4\" fill=\"#00ffcc\"/>\n"
                << "  <circle cx=\"80\" cy=\"84\" r=\"4\" fill=\"#0088ff\"/>\n"
                << "</svg>\n";
        gdb_svg.close();

        try {
            fs::copy_file(ext_dir / "logo.svg", root_dir / "logo.svg", fs::copy_options::overwrite_existing);
            fs::copy_file(ext_dir / "gdb_logo.svg", root_dir / "gdb_logo.svg", fs::copy_options::overwrite_existing);
        } catch (...) {}

        ofstream lconf(ext_dir / "language-configuration.json");
        lconf << "{\n"
              << "  \"comments\": { \"lineComment\": \"//\", \"blockComment\": [\"/*\", \"*/\"] },\n"
              << "  \"brackets\": [ [\"{\", \"}\"], [\"[\", \"]\"], [\"(\", \")\"] ],\n"
              << "  \"autoClosingPairs\": [\n"
              << "    { \"open\": \"{\", \"close\": \"}\" },\n"
              << "    { \"open\": \"[\", \"close\": \"]\" },\n"
              << "    { \"open\": \"(\", \"close\": \")\" },\n"
              << "    { \"open\": \"\\\"\", \"close\": \"\\\"\" },\n"
              << "    { \"open\": \"'\", \"close\": \"'\" },\n"
              << "    { \"open\": \"!\", \"close\": \"!\" }\n"
              << "  ]\n"
              << "}\n";
        lconf.close();

        ofstream tm(ext_dir / "syntaxes" / "grassgreen.tmLanguage.json");
        tm << "{\n"
           << "  \"name\": \"GrassGreen\",\n"
           << "  \"scopeName\": \"source.grass\",\n"
           << "  \"patterns\": [\n"
           << "    { \"name\": \"comment.line.grass\", \"match\": \"(//.*$|^\\\\s*\\\\*.*$)\" },\n"
           << "    { \"name\": \"comment.block.grass\", \"begin\": \"/\\\\*|\\\\*\\\\*\", \"end\": \"\\\\*/|\\\\*\\\\*\" },\n"
           << "    { \"name\": \"entity.name.tag.library.grass\", \"match\": \"![A-Za-z0-9_]+!\" },\n"
           << "    { \"name\": \"keyword.control.directive.grass\", \"match\": \"@[A-Za-z0-9_]+\" },\n"
           << "    { \"name\": \"string.quoted.double.grass\", \"begin\": \"\\\"\", \"end\": \"\\\"\" },\n"
           << "    { \"name\": \"string.quoted.single.grass\", \"begin\": \"'\", \"end\": \"'\" },\n"
           << "    { \"name\": \"constant.other.color.grass\", \"match\": \"#[0-9a-fA-F]{3,8}\\\\b\" },\n"
           << "    { \"name\": \"keyword.control.grass\", \"match\": \"\\\\b(if|else|chek|check|for|while|again|loop|break|stop|stoped|return|exit|wait|sleep|window|gout|gin|cout|cin|endl|text|script-start|script-end|on-click|end-click|on-input|end-input|on-hover|end-hover|on-key|end-key|every|end-every|end-wait|end-chek|table|end-table|box-start|box-end|row-start|row-end|col-start|col-end)\\\\b\" },\n"
           << "    { \"name\": \"storage.type.grass\", \"match\": \"\\\\b(int|take|void|func|alt|fn|string|str|float|double|num|bool|auto|set|var|let|fany|invent)\\\\b\" },\n"
           << "    { \"name\": \"support.function.builtin.grass\", \"match\": \"\\\\b(main|gin\\\\.getline|gin|input\\\\.getline|input|print|println|ask|random|random_txt|stop_random|play_random|memory_usit|memory_separ|memory_auto|memory_clean|memory_free|memory_alloc|memory_free_block|memory_limit|memory_dump|len|length|upper|lower|substr|replace|contains|trim|to_str|to_num|typeof|clear|cls|color|time|clock_ms|beep|msgbox|system|sqrt|discor|sin|cos|abs|round|floor|ceil|pow|clamp|min|max|rand|lerp|creat|redact|read|exists|remove|open|run|db_open|db_save|db_set|db_get|db_del|db_has|db_inc|db_dec|db_clear|db_backup|db_export_json|db_hash|db_encrypt|db_decrypt|gui_cmd|gui_get|gui_set|gui_show|gui_file|web_start|web_compile|surface|auto_size_display|border-radius-app|border_radius_app|topmost|z-index|z_index|layer|clock|clock-format|engine|vd_plus|gradient|glow|border|hover-color|active-color|placeholder|title|size-top|theme|fon|iconP|button|label|enter|icon|video|window-ob|middle|left|right|text-color|button-color|font-size|font-style|radius|pad|shadow|glass|click-ob|switch|set-text|set-val|clear-val|repaint-color|repaint-bg|hide|show|toggle|play-anim)\\\\b\" },\n"
           << "    { \"name\": \"constant.numeric.grass\", \"match\": \"\\\\b\\\\d+(\\\\.\\\\d+)?\\\\b\" },\n"
           << "    { \"name\": \"constant.language.boolean.grass\", \"match\": \"\\\\b(true|false)\\\\b\" }\n"
           << "  ]\n"
           << "}\n";
        tm.close();

        ofstream snip(ext_dir / "snippets" / "grassgreen.json");
        snip << "{\n"
             << "  \"Main Entry Point\": {\n"
             << "    \"prefix\": [\"main\", \"int main\"],\n"
             << "    \"body\": [\"!GrassGreen!\", \"\", \"int main() {\", \"\\t$0\", \"\\treturn 0;\", \"}\"],\n"
             << "    \"description\": \"Главная функция int main() {}\"\n"
             << "  },\n"
             << "  \"GUI Window Block\": {\n"
             << "    \"prefix\": \"window\",\n"
             << "    \"body\": [\"!GrassGreen!\", \"!interface!\", \"\", \"int main() {\", \"\\twindow {\", \"\\t\\ttitle(\\\"${1:GrassGreen App}\\\");\", \"\\t\\tsize(520, 480);\", \"\\t\\tcolor(#12131a);\", \"\\t\\tbutton \\\"${2:Запустить}\\\" {\", \"\\t\\t\\twindow-ob(160, 200);\", \"\\t\\t\\tsize(200, 44);\", \"\\t\\t\\tradius(10);\", \"\\t\\t\\tshadow();\", \"\\t\\t}\", \"\\t\\tshow();\", \"\\t}\", \"\\treturn 0;\", \"}\"],\n"
             << "    \"description\": \"Создание графического окна GrassGUI в едином синтаксисе\"\n"
             << "  }\n"
             << "}\n";
        snip.close();

        ofstream pkg(ext_dir / "package.json");
        pkg << "{\n"
            << "  \"name\": \"grassgreen-lang\",\n"
            << "  \"displayName\": \"GrassGreen Official\",\n"
            << "  \"description\": \"Official language support for GrassGreen (.grass, .gdb)\",\n"
            << "  \"version\": \"3.0.0\",\n"
            << "  \"publisher\": \"m1oky\",\n"
            << "  \"engines\": { \"vscode\": \"^1.60.0\" },\n"
            << "  \"contributes\": {\n"
            << "    \"languages\": [\n"
            << "      {\n"
            << "        \"id\": \"grassgreen\",\n"
            << "        \"aliases\": [\"GrassGreen\", \"grass\"],\n"
            << "        \"extensions\": [\".grass\"],\n"
            << "        \"configuration\": \"./language-configuration.json\",\n"
            << "        \"icon\": { \"light\": \"./logo.svg\", \"dark\": \"./logo.svg\" }\n"
            << "      },\n"
            << "      {\n"
            << "        \"id\": \"grassdb\",\n"
            << "        \"aliases\": [\"GrassDB\", \"gdb\"],\n"
            << "        \"extensions\": [\".gdb\"],\n"
            << "        \"configuration\": \"./language-configuration.json\",\n"
            << "        \"icon\": { \"light\": \"./gdb_logo.svg\", \"dark\": \"./gdb_logo.svg\" }\n"
            << "      }\n"
            << "    ],\n"
            << "    \"grammars\": [\n"
            << "      {\n"
            << "        \"language\": \"grassgreen\",\n"
            << "        \"scopeName\": \"source.grass\",\n"
            << "        \"path\": \"./syntaxes/grassgreen.tmLanguage.json\"\n"
            << "      },\n"
            << "      {\n"
            << "        \"language\": \"grassdb\",\n"
            << "        \"scopeName\": \"source.grass\",\n"
            << "        \"path\": \"./syntaxes/grassgreen.tmLanguage.json\"\n"
            << "      }\n"
            << "    ],\n"
            << "    \"snippets\": [\n"
            << "      {\n"
            << "        \"language\": \"grassgreen\",\n"
            << "        \"path\": \"./snippets/grassgreen.json\"\n"
            << "      }\n"
            << "    ]\n"
            << "  }\n"
            << "}\n";
        pkg.close();
        cout << "[+] Установлены иконки VS Code: logo.svg (.grass) и gdb_logo.svg (.gdb)!\n";
    }

    fs::create_directories(root_dir / ".vscode");
    {
        ofstream vset(root_dir / ".vscode" / "settings.json");
        vset << "{\n  \"files.associations\": { \"*.grass\": \"grassgreen\", \"*.gdb\": \"grassdb\" }\n}\n";
    }
    {
        ofstream tasks(root_dir / ".vscode" / "tasks.json");
        tasks << "{\n"
              << "  \"version\": \"2.0.0\",\n"
              << "  \"tasks\": [\n"
              << "    {\n"
              << "      \"label\": \"Run GrassGreen Script\",\n"
              << "      \"type\": \"shell\",\n"
              << "      \"command\": \"& \\\"${workspaceFolder}\\\\bin\\\\grassc.exe\\\" ./! \\\"${file}\\\"\",\n"
              << "      \"group\": { \"kind\": \"build\", \"isDefault\": true },\n"
              << "      \"presentation\": { \"reveal\": \"always\", \"panel\": \"shared\" }\n"
              << "    }\n"
              << "  ]\n"
              << "}\n";
    }
    cout << "[+] Настроена сборка по Ctrl+Shift+B!\n";
    cout << "=== Установка завершена! Перезапусти окно VS Code (Reload Window) ===\n\n";
}

// ============================================================================
// 3. WIN32 PE-PACKER (ИНКРЕМЕНТАЛЬНАЯ ДОЗАПИСЬ)
// ============================================================================
static const char MAGIC_FOOTER[8] = {'G','R','A','S','S','E','X','E'};

#if defined(_WIN32)
bool CheckAndRunEmbeddedBytecode() {
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    HANDLE hFile = CreateFileA(self_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER total_size;
    if (!GetFileSizeEx(hFile, &total_size) || total_size.QuadPart < 12) {
        CloseHandle(hFile);
        return false;
    }

    LARGE_INTEGER seek_pos;
    seek_pos.QuadPart = total_size.QuadPart - 12;
    SetFilePointerEx(hFile, seek_pos, NULL, FILE_BEGIN);

    uint8_t footer[12];
    DWORD bytes_read = 0;
    if (!ReadFile(hFile, footer, 12, &bytes_read, NULL) || bytes_read != 12) {
        CloseHandle(hFile);
        return false;
    }

    if (memcmp(footer + 4, MAGIC_FOOTER, 8) != 0) {
        CloseHandle(hFile);
        return false;
    }

    uint32_t bc_size = 0;
    memcpy(&bc_size, footer, 4);
    if (bc_size == 0 || (LONGLONG)(bc_size + 12) > total_size.QuadPart) {
        CloseHandle(hFile);
        return false;
    }

    seek_pos.QuadPart = total_size.QuadPart - 12 - bc_size;
    SetFilePointerEx(hFile, seek_pos, NULL, FILE_BEGIN);

    vector<uint8_t> bytecode(bc_size);
    if (!ReadFile(hFile, bytecode.data(), bc_size, &bytes_read, NULL) || bytes_read != bc_size) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    g_vm_arena.reset();
    GrassVM vm(bytecode);
    vm.execute();
    GrassThread::shutdown();
    return true;
}

// Вычисление чистого размера PE-ядра без чтения всего файла
static LONGLONG GetCleanStubSize(const char* path, LONGLONG& out_total_size) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return -1;

    LARGE_INTEGER fsize;
    if (!GetFileSizeEx(hFile, &fsize)) {
        CloseHandle(hFile);
        return -1;
    }
    out_total_size = fsize.QuadPart;
    LONGLONG stub_size = fsize.QuadPart;

    if (stub_size >= 12) {
        LARGE_INTEGER pos;
        pos.QuadPart = stub_size - 12;
        SetFilePointerEx(hFile, pos, NULL, FILE_BEGIN);
        uint8_t footer[12];
        DWORD read_b = 0;
        if (ReadFile(hFile, footer, 12, &read_b, NULL) && read_b == 12) {
            uint32_t old_bc = 0;
            memcpy(&old_bc, footer, 4);
            if (memcmp(footer + 4, MAGIC_FOOTER, 8) == 0 && (LONGLONG)(old_bc + 12) < stub_size) {
                stub_size -= (old_bc + 12);
            }
        }
    }
    CloseHandle(hFile);
    return stub_size;
}

bool BuildStandaloneExe(const string& output_exe, const vector<uint8_t>& bytecode) {
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    LONGLONG self_total = 0;
    LONGLONG stub_size = GetCleanStubSize(self_path, self_total);
    if (stub_size <= 0) return false;

    uint32_t bc_size = (uint32_t)bytecode.size();
    uint8_t footer[12];
    memcpy(footer, &bc_size, 4);
    memcpy(footer + 4, MAGIC_FOOTER, 8);

    HANDLE hSrc = CreateFileA(self_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSrc == INVALID_HANDLE_VALUE) return false;
    vector<char> pe_stub((size_t)stub_size);
    DWORD r = 0;
    ReadFile(hSrc, pe_stub.data(), (DWORD)stub_size, &r, NULL);
    CloseHandle(hSrc);

    HANDLE hDst = CreateFileA(output_exe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDst == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    WriteFile(hDst, pe_stub.data(), (DWORD)pe_stub.size(), &w, NULL);
    WriteFile(hDst, bytecode.data(), bc_size, &w, NULL);
    WriteFile(hDst, footer, 12, &w, NULL);
    CloseHandle(hDst);
    return true;
}
#else
// POSIX ELF-PACKER ДЛЯ LINUX
static int64_t GetCleanStubSize(const char* path, int64_t& out_total_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    int64_t fsize = (int64_t)ftell(f);
    out_total_size = fsize;
    int64_t stub_size = fsize;
    if (stub_size >= 12) {
        fseek(f, stub_size - 12, SEEK_SET);
        uint8_t footer[12];
        if (fread(footer, 1, 12, f) == 12) {
            uint32_t old_bc = 0;
            memcpy(&old_bc, footer, 4);
            if (memcmp(footer + 4, MAGIC_FOOTER, 8) == 0 && (int64_t)(old_bc + 12) < stub_size) {
                stub_size -= (old_bc + 12);
            }
        }
    }
    fclose(f);
    return stub_size;
}

bool CheckAndRunEmbeddedBytecode() {
    char self_path[1024];
    ssize_t len = ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len <= 0) return false;
    self_path[len] = '\0';

    FILE* f = fopen(self_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    int64_t total_size = (int64_t)ftell(f);
    if (total_size < 12) { fclose(f); return false; }

    fseek(f, total_size - 12, SEEK_SET);
    uint8_t footer[12];
    if (fread(footer, 1, 12, f) != 12 || memcmp(footer + 4, MAGIC_FOOTER, 8) != 0) {
        fclose(f);
        return false;
    }

    uint32_t bc_size = 0;
    memcpy(&bc_size, footer, 4);
    if (bc_size == 0 || (int64_t)(bc_size + 12) > total_size) {
        fclose(f);
        return false;
    }

    fseek(f, total_size - 12 - bc_size, SEEK_SET);
    vector<uint8_t> bytecode(bc_size);
    if (fread(bytecode.data(), 1, bc_size, f) != bc_size) {
        fclose(f);
        return false;
    }
    fclose(f);

    g_vm_arena.reset();
    GrassVM vm(bytecode);
    vm.execute();
    GrassThread::shutdown();
    return true;
}

bool BuildStandaloneExe(const string& output_exe, const vector<uint8_t>& bytecode) {
    char self_path[1024];
    ssize_t len = ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len <= 0) return false;
    self_path[len] = '\0';

    int64_t self_total = 0;
    int64_t stub_size = GetCleanStubSize(self_path, self_total);
    if (stub_size <= 0) return false;

    uint32_t bc_size = (uint32_t)bytecode.size();
    uint8_t footer[12];
    memcpy(footer, &bc_size, 4);
    memcpy(footer + 4, MAGIC_FOOTER, 8);

    FILE* fSrc = fopen(self_path, "rb");
    if (!fSrc) return false;
    vector<char> stub((size_t)stub_size);
    if (fread(stub.data(), 1, (size_t)stub_size, fSrc) != (size_t)stub_size) {
        fclose(fSrc);
        return false;
    }
    fclose(fSrc);

    FILE* fDst = fopen(output_exe.c_str(), "wb");
    if (!fDst) return false;
    fwrite(stub.data(), 1, stub.size(), fDst);
    fwrite(bytecode.data(), 1, bc_size, fDst);
    fwrite(footer, 1, 12, fDst);
    fclose(fDst);

    try {
        fs::permissions(output_exe, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, fs::perm_options::replace);
    } catch (...) {}
    return true;
}
#endif

// ============================================================================
// 4. ОПЦИИ КОМПИЛЯТОРА И CLI ФЛАГИ (ROADMAP G&G 4.0)
// ============================================================================
struct CompilerOptions {
    string input_file = "";
    string output_file = "";
    bool emit_bytecode_only = false; // -c (.grassc)
    bool emit_exe = false;           // -emit-exe (.exe)
    bool emit_multiboot = false;     // --multiboot
    bool emit_kernel = false;        // --kernel (.bin)
    bool emit_sys = false;           // --sys (.sys)
    bool emit_iso = false;           // --iso (.iso)
    bool run_after_build = true;     // ./!, --run, -r, или по умолчанию для скриптов
    bool explicit_run = false;
    int opt_level = 2;               // -O0, -O1, -O2, -O3 (default: 2)
    bool dump_ast = false;           // --dump-ast
    bool dump_disasm = false;        // -S
    bool print_timing = false;       // --time
    bool freestanding = false;       // --freestanding
    bool enable_jit = false;         // --jit (x86_64 JIT)
    bool lang_ru = false;            // default: English, true: Russian (--ru)
    bool show_version = false;       // -V, --version
    bool show_help = false;          // -h, --help
    bool install = false;            // --install, install, -i
};

void PrintVersion(bool lang_ru) {
    if (lang_ru) {
        cout << "GrassGreen (G&G) v4.2.0 (x86_64-win32)\n"
             << "Официальный компилятор и ядро выполнения (grg / grassc)\n"
             << "Сайт: grassgreen.info | Репозиторий: github.com/m1oky/GrassGreen\n";
    } else {
        cout << "GrassGreen (G&G) v4.2.0 (x86_64-win32)\n"
             << "Official Compiler & Freestanding Execution Core (grg / grassc)\n"
             << "Domain: grassgreen.info | Repository: github.com/m1oky/GrassGreen\n";
    }
}

void PrintHelp(bool lang_ru) {
    if (lang_ru) {
        cout << "GrassGreen (G&G) v4.2.0 Компилятор и Рантайм (grg / grassc)\n\n"
             << "Использование:\n"
             << "  grg [опции] <файл.grass>\n"
             << "  grassc [опции] <файл.grass>\n\n"
             << "Опции сборки и вывода:\n"
             << "  -o <файл>        Указать путь и имя выходного файла\n"
             << "  -c               Собрать только чистый бинарный байт-код (.grassc)\n"
             << "  -emit-exe        Упаковать байт-код в автономный исполняемый .exe\n"
             << "  -r, --run, ./!   Скомпилировать и немедленно запустить в VM\n"
             << "  -O0              Debug: сборка без оптимизаций\n"
             << "  -O2, -O3         Оптимизация байт-кода (по умолчанию: -O2)\n"
             << "  --jit            Включить нативный JIT-компилятор x86_64 для горячих циклов\n"
             << "  --freestanding   Автономный режим ядра (без libc/C++ рантайма)\n\n"
             << "Низкоуровневая разработка и ОС (G&G v4.2.0 Bare-Metal):\n"
             << "  --multiboot      Генерация заголовка Multiboot 1/2 спецификации (GRUB / QEMU)\n"
             << "  --kernel         Выделенный режим компиляции в чистый бинарный файл ядра (.bin)\n"
             << "  --sys            Режим компиляции драйверов ядра в системный модуль (.sys)\n"
             << "  --iso            Создание готового загрузочного .iso образа (El Torito No-Emulation)\n\n"
             << "Диагностика и анализ:\n"
             << "  --dump-ast       Распечатать дерево токенов (AST) и C-ABI структуры\n"
             << "  -S               Распечатать читаемый дизассемблер байт-кода\n"
             << "  --time           Замерить время каждого этапа компиляции (мс)\n\n"
             << "Локализация и справка:\n"
             << "  --ru             Выводить сообщения компилятора на русском\n"
             << "  --en             Выводить сообщения компилятора на английском (по умолчанию)\n"
             << "  -V, --version    Показать информацию о версии\n"
             << "  -h, --help       Показать эту справку\n"
             << "  --install, -i    Установить инструментарий G&G и расширение VS Code\n";
    } else {
        cout << "GrassGreen (G&G) v4.2.0 Compiler & Runner (grg / grassc)\n\n"
             << "Usage:\n"
             << "  grg [options] <source.grass>\n"
             << "  grassc [options] <source.grass>\n\n"
             << "Build & Output Options:\n"
             << "  -o <filename>    Specify output file name\n"
             << "  -c               Compile to pure bytecode (.grassc) without .exe wrapper\n"
             << "  -emit-exe        Package bytecode into standalone native Win32 .exe\n"
             << "  -r, --run, ./!   Compile and execute immediately in VM\n"
             << "  -O0              Debug: instant build without optimizations\n"
             << "  -O2, -O3         Enable bytecode optimizations (default: -O2)\n"
             << "  --jit            Enable native x86_64 JIT (Just-In-Time) compiler for loops\n"
             << "  --freestanding   Freestanding kernel build (independent of libc/C++)\n\n"
             << "Bare-Metal & OS Development (G&G v4.2.0):\n"
             << "  --multiboot      Generate Multiboot 1/2 header specification for GRUB / QEMU\n"
             << "  --kernel         Dedicated build mode to raw binary OS kernel (.bin)\n"
             << "  --sys            Device driver compilation to system kernel module (.sys)\n"
             << "  --iso            Create standalone bootable ISO CD image (El Torito No-Emulation)\n\n"
             << "Diagnostics & Inspection:\n"
             << "  --dump-ast       Dump syntax tree (AST) and C-ABI struct layouts\n"
             << "  -S               Print disassembled human-readable bytecode\n"
             << "  --time           Display execution time for each compilation stage (ms)\n\n"
             << "Localization & Info:\n"
             << "  --ru             Switch compiler messages to Russian\n"
             << "  --en             Switch compiler messages to English (default)\n"
             << "  -V, --version    Show version information\n"
             << "  -h, --help       Display this help message\n"
             << "  --install, -i    Install G&G toolchain and VS Code extension\n";
    }
}

void DisassembleBytecode(const vector<uint8_t>& code, bool lang_ru) {
    if (lang_ru) {
        cout << "\n=== ДИЗАССЕМБЛЕР БАЙТ-КОДА GRASSGREEN (G&G v4.2.0) ===\n";
        cout << "Смещение  Опкод            Аргументы\n";
        cout << "--------------------------------------------------------\n";
    } else {
        cout << "\n=== GRASSGREEN (G&G v4.2.0) BYTECODE DISASSEMBLY ===\n";
        cout << "Offset    OpCode           Arguments\n";
        cout << "--------------------------------------------------------\n";
    }

    size_t ip = 0;
    while (ip < code.size()) {
        size_t cur_ip = ip;
        uint8_t op = code[ip++];
        cout << setw(6) << setfill('0') << cur_ip << "    " << setfill(' ');

        auto readU32 = [&]() -> uint32_t {
            if (ip + 4 > code.size()) { ip = code.size(); return 0; }
            uint32_t v;
            memcpy(&v, &code[ip], 4);
            ip += 4;
            return v;
        };

        auto readDouble = [&]() -> double {
            if (ip + 8 > code.size()) { ip = code.size(); return 0.0; }
            double d;
            memcpy(&d, &code[ip], 8);
            ip += 8;
            return d;
        };

        auto readString = [&]() -> string {
            uint32_t len = readU32();
            if (ip + len > code.size()) { ip = code.size(); return ""; }
            string s((const char*)&code[ip], len);
            ip += len;
            return s;
        };

        switch (op) {
            case OP_HALT:         cout << "OP_HALT\n"; break;
            case OP_CONST_NUM:    cout << "OP_CONST_NUM    " << readDouble() << "\n"; break;
            case OP_CONST_STR:    cout << "OP_CONST_STR    \"" << readString() << "\"\n"; break;
            case OP_LOAD_VAR:     cout << "OP_LOAD_VAR     \"" << readString() << "\"\n"; break;
            case OP_STORE_VAR:    cout << "OP_STORE_VAR    \"" << readString() << "\"\n"; break;
            case OP_POP:          cout << "OP_POP\n"; break;
            case OP_ADD:          cout << "OP_ADD\n"; break;
            case OP_SUB:          cout << "OP_SUB\n"; break;
            case OP_MUL:          cout << "OP_MUL\n"; break;
            case OP_DIV:          cout << "OP_DIV\n"; break;
            case OP_NEG:          cout << "OP_NEG\n"; break;
            case OP_MOD:          cout << "OP_MOD\n"; break;
            case OP_CMP_EQ:       cout << "OP_CMP_EQ\n"; break;
            case OP_CMP_NEQ:      cout << "OP_CMP_NEQ\n"; break;
            case OP_CMP_LT:       cout << "OP_CMP_LT\n"; break;
            case OP_CMP_GT:       cout << "OP_CMP_GT\n"; break;
            case OP_CMP_LTE:      cout << "OP_CMP_LTE\n"; break;
            case OP_CMP_GTE:      cout << "OP_CMP_GTE\n"; break;
            case OP_AND:          cout << "OP_AND\n"; break;
            case OP_OR:           cout << "OP_OR\n"; break;
            case OP_NOT:          cout << "OP_NOT\n"; break;
            case OP_JMP:          cout << "OP_JMP          -> " << readU32() << "\n"; break;
            case OP_JMP_IF_FALSE: cout << "OP_JMP_IF_FALSE -> " << readU32() << "\n"; break;
            case OP_CALL_FUNC:    cout << "OP_CALL_FUNC    -> " << readU32() << "\n"; break;
            case OP_RET:          cout << "OP_RET\n"; break;
            case OP_LOG:          cout << "OP_LOG\n"; break;
            case OP_LOG_EMPTY:    cout << "OP_LOG_EMPTY\n"; break;
            case OP_WAIT:         cout << "OP_WAIT\n"; break;
            case OP_ASK:          cout << "OP_ASK          \"" << readString() << "\"\n"; break;
            case OP_PRINT_RAW:    cout << "OP_PRINT_RAW\n"; break;
            case OP_CALL_BUILTIN: {
                string fn = readString();
                uint8_t argc = (ip < code.size()) ? code[ip++] : 0;
                cout << "OP_CALL_BUILTIN \"" << fn << "\" (argc=" << (int)argc << ")\n";
                break;
            }
            case OP_LOAD_DB:      cout << "OP_LOAD_DB\n"; break;
            case OP_START_WEB:    cout << "OP_START_WEB    \"" << readString() << "\"\n"; break;
            case OP_UI_INIT:      cout << "OP_UI_INIT\n"; break;
            case OP_UI_ADD_ELEM:  cout << "OP_UI_ADD_ELEM\n"; break;
            case OP_UI_SHOW:      cout << "OP_UI_SHOW\n"; break;
            case OP_UI_CMD:       cout << "OP_UI_CMD       \"" << readString() << "\"\n"; break;
            case OP_NEW_ARRAY:    cout << "OP_NEW_ARRAY    count=" << readU32() << "\n"; break;
            case OP_GET_INDEX:    cout << "OP_GET_INDEX\n"; break;
            case OP_SET_INDEX:    cout << "OP_SET_INDEX\n"; break;
            case OP_ARRAY_PUSH:   cout << "OP_ARRAY_PUSH\n"; break;
            case OP_ARRAY_POP:    cout << "OP_ARRAY_POP\n"; break;
            case OP_NEW_STRUCT:   cout << "OP_NEW_STRUCT   \"" << readString() << "\"\n"; break;
            case OP_GET_FIELD:    cout << "OP_GET_FIELD    \"" << readString() << "\"\n"; break;
            case OP_SET_FIELD:    cout << "OP_SET_FIELD    \"" << readString() << "\"\n"; break;
            case OP_JUMP:         cout << "OP_JUMP         -> " << readU32() << "\n"; break;
            case OP_CALL_USER:    cout << "OP_CALL_USER    -> " << readU32() << "\n"; break;
            case OP_PARAM_PROLOGUE: cout << "OP_PARAM_PROLOGUE\n"; break;
            case OP_PARAM_EPILOGUE: cout << "OP_PARAM_EPILOGUE\n"; break;
            case OP_NOP:          cout << "OP_NOP\n"; break;
            case OP_ADD_CONST:    cout << "OP_ADD_CONST    " << readDouble() << "\n"; break;
            case OP_SUB_CONST:    cout << "OP_SUB_CONST    " << readDouble() << "\n"; break;
            case OP_MUL_CONST:    cout << "OP_MUL_CONST    " << readDouble() << "\n"; break;
            case OP_DIV_CONST:    cout << "OP_DIV_CONST    " << readDouble() << "\n"; break;
            case OP_INC_VAR:      cout << "OP_INC_VAR      \"" << readString() << "\"\n"; break;
            case OP_DEC_VAR:      cout << "OP_DEC_VAR      \"" << readString() << "\"\n"; break;
            case OP_THREAD_SPAWN: {
                uint32_t entry = readU32();
                uint8_t argc = (ip < code.size()) ? code[ip++] : 0;
                cout << "OP_THREAD_SPAWN  -> " << entry << " (argc=" << (int)argc << ")\n";
                break;
            }
            case OP_ASYNC_SPAWN: {
                uint32_t entry = readU32();
                uint8_t argc = (ip < code.size()) ? code[ip++] : 0;
                cout << "OP_ASYNC_SPAWN   -> " << entry << " (argc=" << (int)argc << ")\n";
                break;
            }
            default:              cout << "OP_UNKNOWN_0x" << hex << (int)op << dec << "\n"; break;
        }
    }
    cout << "--------------------------------------------------------\n\n";
}

void DumpAST(const vector<GrassToken>& tokens, const string& filename, bool lang_ru) {
    if (lang_ru) {
        cout << "\n=== ДАМП СИНТАКСИЧЕСКОГО ДЕРЕВА (AST): " << filename << " ===\n";
    } else {
        cout << "\n=== ABSTRACT SYNTAX TREE (AST) DUMP: " << filename << " ===\n";
    }
    int cur_line = -1;
    for (const auto& tok : tokens) {
        if (tok.kind == TOK_EOF) break;
        if (tok.line != cur_line) {
            cur_line = tok.line;
            cout << "\n[Line " << setw(3) << setfill('0') << cur_line << "] " << setfill(' ');
        }
        cout << tok.text << " ";
    }
    cout << "\n\n";
    if (!g_struct_registry.empty()) {
        if (lang_ru) cout << "Зарегистрированные структуры (C-ABI Data Layout):\n";
        else cout << "Registered Structures (C-ABI Data Layout):\n";
        for (const auto& kv : g_struct_registry) {
            const auto& def = kv.second;
            cout << "  struct " << def.name << " (Size: " << def.total_size 
                 << " B, Alignment: " << def.alignment << " B):\n";
            for (const auto& f : def.fields) {
                cout << "    +" << f.offset << " B: " << f.name << " [size=" << f.size 
                     << " B, align=" << f.alignment << " B]\n";
            }
        }
        cout << "\n";
    }
}

string FindDefaultGrassFile() {
    if (fs::exists("demo.grass")) return "demo.grass";
    if (fs::exists("main.grass")) return "main.grass";

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("*.grass", &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        string found = fd.cFileName;
        FindClose(hFind);
        return found;
    }
    return "";
}

bool ProcessCompilation(CompilerOptions& opt) {
    auto t_start = chrono::high_resolution_clock::now();

    string input_path = opt.input_file;
    if (input_path.empty()) {
        input_path = FindDefaultGrassFile();
        if (input_path.empty()) {
            if (opt.lang_ru) cerr << "[GrassError]: Не указан входной файл .grass. Используйте 'grg -h' для справки.\n";
            else cerr << "[GrassError]: No .grass input file specified. Use 'grg -h' for help.\n";
            return false;
        }
    }

    if (input_path.find('.') == string::npos) {
        if (fs::exists(input_path + ".grass")) input_path += ".grass";
        else if (fs::exists(input_path + ".grassc")) input_path += ".grassc";
    }

    // Прямой запуск чистого байт-кода .grassc
    if (input_path.size() >= 7 && input_path.substr(input_path.size() - 7) == ".grassc") {
        ifstream in_bc(input_path, ios::binary | ios::ate);
        if (!in_bc.is_open()) {
            if (opt.lang_ru) cerr << "[GrassError]: Не удалось открыть файл байт-кода -> " << input_path << "\n";
            else cerr << "[GrassError]: Cannot open bytecode file -> " << input_path << "\n";
            return false;
        }
        streamsize sz = in_bc.tellg();
        in_bc.seekg(0, ios::beg);
        vector<uint8_t> bc_data((size_t)sz);
        if (sz > 0) in_bc.read((char*)bc_data.data(), sz);
        in_bc.close();

        if (opt.dump_disasm) {
            DisassembleBytecode(bc_data, opt.lang_ru);
        }
        if (opt.run_after_build && !opt.emit_iso && !opt.emit_kernel && !opt.emit_sys && (!opt.emit_multiboot || opt.explicit_run)) {
            if (opt.lang_ru) cout << "[GrassVM]: Исполнение байт-кода...\n\n";
            else cout << "[GrassVM]: Executing bytecode...\n\n";
            g_vm_arena.reset();
            GrassVM vm(bc_data);
            vm.execute();
            GrassThread::shutdown();
        if (opt.print_timing && GrassJIT::getStats().loops_compiled > 0) {
            const auto& js = GrassJIT::getStats();
            if (opt.lang_ru) {
                cout << "\n[JIT-Компилятор x86_64]: Скомпилировано циклов: " << js.loops_compiled
                     << " (" << js.machine_code_bytes << " B машинного кода x86_64, время JIT: "
                     << js.compile_time_ms << " ms)\n";
            } else {
                cout << "\n[JIT x86_64]: Loops compiled: " << js.loops_compiled
                     << " (" << js.machine_code_bytes << " B native x86_64 code, JIT compile: "
                     << js.compile_time_ms << " ms)\n";
            }
        }
        }
        return true;
    }

    // 1. Этап I/O: Чтение исходного кода
    auto t_read_start = chrono::high_resolution_clock::now();
    ifstream in(input_path, ios::binary | ios::ate);
    if (!in.is_open()) {
        if (opt.lang_ru) cerr << "[GrassError]: Файл не найден -> " << input_path << "\n";
        else cerr << "[GrassError]: File not found -> " << input_path << "\n";
        return false;
    }
    streamsize src_size = in.tellg();
    in.seekg(0, ios::beg);
    string source_code((size_t)max((streamsize)0, src_size), '\0');
    if (src_size > 0) in.read(&source_code[0], src_size);
    in.close();
    auto t_read_end = chrono::high_resolution_clock::now();
    double read_ms = chrono::duration<double, std::milli>(t_read_end - t_read_start).count();

    if (opt.freestanding) {
        GrassHAL::set_freestanding(true);
        if (opt.lang_ru) cout << "[Freestanding]: Автономный режим активирован (ядро независимо от libc)\n";
        else cout << "[Freestanding]: Freestanding mode active (kernel independent from libc)\n";
    } else {
        GrassHAL::set_freestanding(false);
    }

    // 2. Этап Лексера: Токенизация
    auto t_lex_start = chrono::high_resolution_clock::now();
    Lexer lexer(source_code);
    vector<GrassToken> tokens = lexer.tokenize();

    // 2.1. Разрешение директив импорта (Import Pass)
    GrassImport::ImportStats import_stats;
    tokens = GrassImport::resolveImports(tokens, input_path, &import_stats);
    if (opt.print_timing && import_stats.total_modules_imported > 0) {
        if (opt.lang_ru) {
            cout << "[Импорт]: Подключено модулей: " << import_stats.total_modules_imported
                 << " (итого токенов: " << import_stats.total_tokens_after_import << ")\n";
        } else {
            cout << "[Import]: Modules imported: " << import_stats.total_modules_imported
                 << " (total tokens: " << import_stats.total_tokens_after_import << ")\n";
        }
    }

    auto t_lex_end = chrono::high_resolution_clock::now();
    double lex_ms = chrono::duration<double, std::milli>(t_lex_end - t_lex_start).count();

    // 3. Этап Парсера и Компилятора байт-кода
    auto t_comp_start = chrono::high_resolution_clock::now();
    BytecodeCompiler compiler(tokens, input_path);
    compiler.compileProgram();
    auto t_comp_end = chrono::high_resolution_clock::now();
    double comp_ms = chrono::duration<double, std::milli>(t_comp_end - t_comp_start).count();

    if (compiler.has_error) {
        if (opt.lang_ru) cerr << "\n[GrassCompiler]: Сборка остановлена из-за ошибок синтаксиса в " << input_path << "!\n";
        else cerr << "\n[GrassCompiler]: Build stopped due to syntax errors in " << input_path << "!\n";
        return false;
    }

    // ── Этап Оптимизатора Байт-кода (-O1, -O2, -O3) ──
    auto t_opt_start = chrono::high_resolution_clock::now();
    GrassOptimizer::OptimizationStats opt_stats;
    if (opt.opt_level > 0) {
        compiler.code = GrassOptimizer::optimize(compiler.code, opt.opt_level, &opt_stats);
        if (opt.print_timing) {
            if (opt.lang_ru) {
                cout << "[Оптимизатор -O" << opt.opt_level << "]: "
                     << opt_stats.initial_bytes << " B -> " << opt_stats.optimized_bytes
                     << " B (Свернуто констант: " << opt_stats.constants_folded
                     << ", Fused-опкодов: " << opt_stats.opcodes_fused
                     << ", Удалено мертвого кода: " << opt_stats.dead_code_removed << ")\n";
            } else {
                cout << "[Optimizer -O" << opt.opt_level << "]: "
                     << opt_stats.initial_bytes << " B -> " << opt_stats.optimized_bytes
                     << " B (Folded constants: " << opt_stats.constants_folded
                     << ", Fused opcodes: " << opt_stats.opcodes_fused
                     << ", Dead code removed: " << opt_stats.dead_code_removed << ")\n";
            }
        }
    }
    auto t_opt_end = chrono::high_resolution_clock::now();
    double opt_ms = chrono::duration<double, std::milli>(t_opt_end - t_opt_start).count();

    // Опция --dump-ast
    if (opt.dump_ast) {
        DumpAST(tokens, input_path, opt.lang_ru);
    }

    // Опция -S (Дизассемблирование)
    if (opt.dump_disasm) {
        DisassembleBytecode(compiler.code, opt.lang_ru);
    }

    // 4. Этап Генерации выходного файла
    auto t_emit_start = chrono::high_resolution_clock::now();
    string base_name = input_path;
    size_t last_dot = base_name.rfind('.');
    if (last_dot != string::npos) base_name = base_name.substr(0, last_dot);

    // Извлечение машинного кода x86_64 из ассемблерных вставок asm { ... }
    vector<uint8_t> asm_bytes;
    string asm_hex = GrassASM::get_last_machine_hex();
    if (!asm_hex.empty()) {
        stringstream ss(asm_hex);
        string b_str;
        while (ss >> b_str) {
            try { asm_bytes.push_back((uint8_t)stoul(b_str, nullptr, 16)); } catch (...) {}
        }
    }

    if (opt.emit_iso) {
        string out_file = opt.output_file.empty() ? (base_name + ".iso") : opt.output_file;
        GrassBoot::build_bootable_iso(out_file, compiler.code, asm_bytes, "GRASSGREEN_OS", opt.lang_ru);
    } else if (opt.emit_sys) {
        string out_file = opt.output_file.empty() ? (base_name + ".sys") : opt.output_file;
        GrassBoot::build_driver_sys(out_file, compiler.code, base_name, opt.lang_ru);
    } else if (opt.emit_kernel || opt.emit_multiboot) {
        string out_file = opt.output_file.empty() ? (base_name + ".bin") : opt.output_file;
        GrassBoot::build_kernel_bin(out_file, compiler.code, asm_bytes, true, opt.lang_ru);
    } else if (opt.emit_bytecode_only) {
        // -c: Чистый байт-код .grassc
        string out_file = opt.output_file.empty() ? (base_name + ".grassc") : opt.output_file;
        ofstream out_bc(out_file, ios::binary);
        if (!out_bc.is_open()) {
            if (opt.lang_ru) cerr << "[GrassError]: Не удалось записать " << out_file << "\n";
            else cerr << "[GrassError]: Failed to write " << out_file << "\n";
            return false;
        }
        out_bc.write((const char*)compiler.code.data(), compiler.code.size());
        out_bc.close();

        if (opt.lang_ru) {
            cout << "[GrassCompiler]: Скомпилировано " << compiler.code.size()
                 << " байт чистого байт-кода -> " << out_file << "\n";
        } else {
            cout << "[GrassCompiler]: Compiled " << compiler.code.size()
                 << " bytes of pure bytecode -> " << out_file << "\n";
        }
    } else {
        // -emit-exe или упаковка в .exe
        string out_file = opt.output_file.empty() ? (base_name + ".exe") : opt.output_file;
        bool packed_ok = BuildStandaloneExe(out_file, compiler.code);
        if (packed_ok) {
            if (opt.lang_ru) {
                cout << "[GrassCompiler]: Скомпилировано " << compiler.code.size()
                     << " байт двоичного кода -> " << out_file << "\n";
            } else {
                cout << "[GrassCompiler]: Compiled " << compiler.code.size()
                     << " bytes of bytecode -> " << out_file << "\n";
            }
        }
    }
    auto t_emit_end = chrono::high_resolution_clock::now();
    double emit_ms = chrono::duration<double, std::milli>(t_emit_end - t_emit_start).count();

    auto t_end = chrono::high_resolution_clock::now();
    double total_ms = chrono::duration<double, std::milli>(t_end - t_start).count();

    // Опция --time
    if (opt.print_timing) {
        cout << fixed << setprecision(2);
        if (opt.lang_ru) {
            cout << "\n=== ЗАМЕР ВРЕМЕНИ ЭТАПОВ КОМПИЛЯЦИИ ===\n"
                 << "  1. Чтение файла (I/O):        " << setw(6) << read_ms << " ms\n"
                 << "  2. Лексер (токенизация):      " << setw(6) << lex_ms << " ms\n"
                 << "  3. Компилятор байт-кода:      " << setw(6) << comp_ms << " ms\n" \
                 << "  4. Оптимизатор (-O):          " << setw(6) << opt_ms << " ms\n"
                 << "  4. Генерация вывода:          " << setw(6) << emit_ms << " ms\n"
                 << "  ИТОГО сборка:                 " << setw(6) << total_ms << " ms\n"
                 << "========================================\n\n";
        } else {
            cout << "\n=== COMPILATION STAGE TIMINGS ===\n"
                 << "  1. File I/O Read:             " << setw(6) << read_ms << " ms\n"
                 << "  2. Lexer (tokenization):      " << setw(6) << lex_ms << " ms\n"
                 << "  3. Bytecode Compiler:         " << setw(6) << comp_ms << " ms\n" \
                 << "  4. Bytecode Optimizer (-O):   " << setw(6) << opt_ms << " ms\n"
                 << "  4. Output Generation:         " << setw(6) << emit_ms << " ms\n"
                 << "  TOTAL build time:             " << setw(6) << total_ms << " ms\n"
                 << "=================================\n\n";
        }
    }

    // Выполнение в VM
    if (opt.run_after_build) {
        if (opt.lang_ru) cout << "[GrassVM]: Исполнение двоичного кода...\n\n";
        else cout << "[GrassVM]: Executing bytecode...\n\n";
        g_vm_arena.reset();
        GrassJIT::initialize();
        if (opt.enable_jit || opt.opt_level >= 3) {
            GrassJIT::set_enabled(true);
        } else {
            GrassJIT::set_enabled(false);
        }
        GrassVM vm(compiler.code);
        vm.execute();
        GrassThread::shutdown();
        if (opt.print_timing && GrassJIT::getStats().loops_compiled > 0) {
            const auto& js = GrassJIT::getStats();
            if (opt.lang_ru) {
                cout << "\n[JIT-Компилятор x86_64]: Скомпилировано циклов: " << js.loops_compiled
                     << " (" << js.machine_code_bytes << " B машинного кода x86_64, время JIT: "
                     << js.compile_time_ms << " ms)\n";
            } else {
                cout << "\n[JIT x86_64]: Loops compiled: " << js.loops_compiled
                     << " (" << js.machine_code_bytes << " B native x86_64 code, JIT compile: "
                     << js.compile_time_ms << " ms)\n";
            }
        }
    }
    return true;
}

bool CompileAndRunFile(string input_path, bool build_only) {
    CompilerOptions opt;
    opt.input_file = input_path;
    opt.run_after_build = !build_only;
    return ProcessCompilation(opt);
}

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD origInMode = 0;
    if (hIn != INVALID_HANDLE_VALUE) GetConsoleMode(hIn, &origInMode);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    GrassASM::initialize();
    GrassKeyboard::initialize();

    if (CheckAndRunEmbeddedBytecode()) {
#if defined(_WIN32)
        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
#endif
        return 0;
    }

    if (argc >= 2) {
        CompilerOptions opt;

        for (int i = 1; i < argc; ++i) {
            string arg = argv[i];
            if (arg == "-V" || arg == "--version" || arg == "-v") {
                opt.show_version = true;
            } else if (arg == "-h" || arg == "--help" || arg == "/?") {
                opt.show_help = true;
            } else if (arg == "--ru") {
                opt.lang_ru = true;
            } else if (arg == "--en") {
                opt.lang_ru = false;
            } else if (arg == "--install" || arg == "install" || arg == "-i") {
                opt.install = true;
            } else if (arg == "-c" || arg == ".//") {
                opt.emit_bytecode_only = true;
                opt.run_after_build = false;
            } else if (arg == "-emit-exe") {
                opt.emit_exe = true;
                opt.run_after_build = false;
            } else if (arg == "-r" || arg == "--run" || arg == "./!") {
                opt.run_after_build = true;
                opt.explicit_run = true;
            } else if (arg == "-S") {
                opt.dump_disasm = true;
                if (!opt.explicit_run) opt.run_after_build = false;
            } else if (arg == "--dump-ast") {
                opt.dump_ast = true;
            } else if (arg == "--time") {
                opt.print_timing = true;
            } else if (arg == "--multiboot") {
                opt.emit_multiboot = true;
                opt.freestanding = true;
                opt.run_after_build = false;
            } else if (arg == "--kernel") {
                opt.emit_kernel = true;
                opt.freestanding = true;
                opt.run_after_build = false;
            } else if (arg == "--sys") {
                opt.emit_sys = true;
                opt.freestanding = true;
                opt.run_after_build = false;
            } else if (arg == "--iso") {
                opt.emit_iso = true;
                opt.freestanding = true;
                opt.run_after_build = false;
            } else if (arg == "--freestanding") {
                opt.freestanding = true;
            } else if (arg == "--jit") {
                opt.enable_jit = true;
            } else if (arg == "-O0") {
                opt.opt_level = 0;
            } else if (arg == "-O1") {
                opt.opt_level = 1;
            } else if (arg == "-O2") {
                opt.opt_level = 2;
            } else if (arg == "-O3") {
                opt.opt_level = 3;
            } else if (arg == "-o") {
                if (i + 1 < argc) {
                    opt.output_file = argv[++i];
                    if (!opt.explicit_run) opt.run_after_build = false;
                } else {
                    cerr << (opt.lang_ru ? "[GrassError]: Пропущен аргумент для -o\n" : "[GrassError]: Missing argument for -o\n");
                    return 1;
                }
            } else if (!arg.empty() && arg[0] == '-') {
                cerr << (opt.lang_ru ? "[GrassWarning]: Неизвестный флаг: " : "[GrassWarning]: Unknown flag: ") << arg << "\n";
            } else {
                opt.input_file = arg;
            }
        }

        if (opt.show_version) {
            PrintVersion(opt.lang_ru);
            return 0;
        }

        if (opt.show_help) {
            PrintHelp(opt.lang_ru);
            return 0;
        }

        if (opt.install) {
            InstallGrassGreenEcosystem();
            return 0;
        }

        ProcessCompilation(opt);

#if defined(_WIN32)
        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
#endif
        return 0;
    }

    cout << "========================================================\n";
    cout << "  GrassGreen (G&G) Native Compiler & VM v4.2.0 (grg)\n";
    cout << "  Repository: github.com/m1oky/GrassGreen\n";
    cout << "  Type 'help' or '-h' for commands, 'exit' to quit.\n";
    cout << "========================================================\n";
    string line;
    while (true) {
        cout << "G&G> ";
        if (!getline(cin, line)) break;
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);

        if (line == "exit" || line == "quit") break;
        if (line == "install" || line == "--install") { InstallGrassGreenEcosystem(); continue; }
        if (line == "help" || line == "-h" || line == "--help") { PrintHelp(false); continue; }
        if (line == "version" || line == "-V" || line == "--version") { PrintVersion(false); continue; }

        stringstream ss(line);
        string cmd, file_arg;
        ss >> cmd;
        getline(ss, file_arg);
        size_t f_start = file_arg.find_first_not_of(" \t");
        if (f_start != string::npos) file_arg = file_arg.substr(f_start);
        else file_arg = "";

        CompilerOptions opt;
        if (cmd == ".//" || cmd == "compile" || cmd == "build" || cmd == "-c") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.emit_bytecode_only = true;
            opt.run_after_build = false;
        } else if (cmd == "-S") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.dump_disasm = true;
            opt.run_after_build = false;
        } else if (cmd == "--dump-ast") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.dump_ast = true;
        } else if (cmd == "--time") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.print_timing = true;
        } else if (cmd == "./!" || cmd == "run" || cmd == "-r") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.run_after_build = true;
        } else {
            opt.input_file = cmd;
            opt.run_after_build = true;
        }
        ProcessCompilation(opt);
    }

#if defined(_WIN32)
    if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
#endif
    return 0;
}

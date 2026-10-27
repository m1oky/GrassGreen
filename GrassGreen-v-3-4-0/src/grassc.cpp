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
#include <windows.h>

#include "GrassSyntax.h"
#include "FastMath.h"
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

using namespace std;
namespace fs = std::filesystem;

static string wstring_to_utf8_helper(const wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
    if (!str.empty() && str.back() == '\0') str.pop_back();
    return str;
}

// ============================================================================
// 1. СТЕКОВАЯ ВИРТУАЛЬНАЯ МАШИНА GRASSGREEN (ТУРБО-ВЕРСИЯ)
// ============================================================================
class GrassVM {
    const vector<uint8_t>& code;
    size_t ip = 0;
    vector<GrassValue> stack;
    vector<size_t> call_stack;
    unordered_map<string, GrassValue> vars;

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
        for (const auto& kv : GrassDB::get_all_records()) {
            vars[kv.first] = GrassValue::Str(kv.second.value);
            vars[kv.first + "()"] = GrassValue::Str(kv.second.value);
        }
        for (const auto& kv : variables_cache) {
            vars[kv.first] = GrassValue::Str(wstring_to_utf8_helper(kv.second));
        }
    }

    void syncVarsToGUI() {
        for (const auto& kv : vars) {
            GrassGUI::set_variable(kv.first, kv.second.toString());
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
    explicit GrassVM(const vector<uint8_t>& bytecode) : code(bytecode) {
        // Предварительный резерв памяти (ноль реаллокаций при исполнении)
        stack.reserve(512);
        call_stack.reserve(128);
        vars.reserve(128);
    }

    void execute() {
        size_t op_counter = 0;
        while (ip < code.size()) {
            // Проверяем лимит памяти раз в 256 инструкций для максимальной скорости циклов
            if ((++op_counter & 255) == 0 && GrassMemory::is_limit_exceeded()) {
                cerr << "\n[GrassGreen Error]: Превышен установленный лимит памяти (memory_limit)!\n";
                return;
            }

            uint8_t op = readByte();
            switch (op) {
                case OP_HALT: return;

                case OP_CONST_NUM: push(GrassValue::Num(readDouble())); break;
                case OP_CONST_STR: push(GrassValue::Str(readString())); break;

                case OP_JUMP:
                case OP_JMP: {
                    ip = readU32();
                    break;
                }

                case OP_JMP_IF_FALSE: {
                    uint32_t target = readU32();
                    if (!pop().isTruthy()) ip = target;
                    break;
                }

                case OP_CALL_USER:
                case OP_CALL_FUNC: {
                    uint32_t target_ip = readU32();
                    call_stack.push_back(ip);
                    ip = target_ip;
                    break;
                }

                case OP_RET: {
                    if (!call_stack.empty()) {
                        ip = call_stack.back();
                        call_stack.pop_back();
                    } else {
                        return;
                    }
                    break;
                }

                case OP_LOAD_VAR: {
                    string name = readString();
                    auto it = vars.find(name);
                    if (it != vars.end()) push(it->second);
                    else if (GrassDB::has(name)) push(GrassValue::Str(GrassDB::get(name)));
                    else push(GrassValue::Str(""));
                    break;
                }
                case OP_STORE_VAR: {
                    string name = readString();
                    vars[std::move(name)] = pop();
                    break;
                }
                case OP_POP: pop(); break;

                case OP_ADD: {
                    GrassValue b = pop(), a = pop();
                    if (a.is_string || b.is_string) push(GrassValue::Str(a.toString() + b.toString()));
                    else push(GrassValue::Num(a.num + b.num));
                    break;
                }
                case OP_SUB: { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() - b.toNumber())); break; }
                case OP_MUL: { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() * b.toNumber())); break; }
                case OP_DIV: {
                    GrassValue b = pop(), a = pop();
                    double div = b.toNumber();
                    push(GrassValue::Num(div != 0.0 ? a.toNumber() / div : 0.0));
                    break;
                }
                case OP_MOD: {
                    GrassValue b = pop(), a = pop();
                    long long bv = (long long)b.toNumber();
                    push(GrassValue::Num(bv != 0 ? (double)((long long)a.toNumber() % bv) : 0.0));
                    break;
                }
                case OP_NEG: { GrassValue a = pop(); push(GrassValue::Num(-a.toNumber())); break; }
                case OP_NOT: { GrassValue a = pop(); push(GrassValue::Num(a.isTruthy() ? 0.0 : 1.0)); break; }
                case OP_AND: { GrassValue b = pop(), a = pop(); push(GrassValue::Num((a.isTruthy() && b.isTruthy()) ? 1.0 : 0.0)); break; }
                case OP_OR:  { GrassValue b = pop(), a = pop(); push(GrassValue::Num((a.isTruthy() || b.isTruthy()) ? 1.0 : 0.0)); break; }

                case OP_CMP_EQ: {
                    GrassValue b = pop(), a = pop();
                    if (a.is_string && b.is_string) push(GrassValue::Num(a.str == b.str ? 1.0 : 0.0));
                    else push(GrassValue::Num(a.toNumber() == b.toNumber() ? 1.0 : 0.0));
                    break;
                }
                case OP_CMP_NEQ: {
                    GrassValue b = pop(), a = pop();
                    if (a.is_string && b.is_string) push(GrassValue::Num(a.str != b.str ? 1.0 : 0.0));
                    else push(GrassValue::Num(a.toNumber() != b.toNumber() ? 1.0 : 0.0));
                    break;
                }
                case OP_CMP_LT:  { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() <  b.toNumber() ? 1.0 : 0.0)); break; }
                case OP_CMP_GT:  { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() >  b.toNumber() ? 1.0 : 0.0)); break; }
                case OP_CMP_LTE: { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() <= b.toNumber() ? 1.0 : 0.0)); break; }
                case OP_CMP_GTE: { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() >= b.toNumber() ? 1.0 : 0.0)); break; }

                case OP_LOG: {
                    cout << pop().toString() << '\n';
                    break;
                }
                case OP_PRINT_RAW: {
                    cout << pop().toString();
                    break;
                }
                case OP_LOG_EMPTY: cout << '\n'; break;

                case OP_WAIT: {
                    int ms = (int)pop().toNumber();
                    if (ms > 0) Sleep(ms);
                    break;
                }
                case OP_ASK: {
                    string var_name = readString();
                    string input;
                    getline(cin, input);
                    vars[var_name] = GrassValue::Str(input);
                    break;
                }

                case OP_LOAD_DB: {
                    GrassDB::init_default_schema();
                    GrassStorageEngine_Load("storage.gdb");
                    syncDBToVars();
                    break;
                }

                case OP_START_WEB: {
                    string fname = readString();
                    GrassWeb_LoadPage(fname);
                    break;
                }

                case OP_CALL_BUILTIN: {
                    string fn = readString();
                    uint8_t argc = readByte();
                    vector<GrassValue> args(argc);
                    for (int i = argc - 1; i >= 0; --i) args[i] = pop();

                    if (fn == "input") {
                        if (argc >= 1) { cout << args[0].toString(); cout.flush(); }
                        string s;
                        if (cin >> s) {
                            string rest;
                            getline(cin, rest);
                            push(GrassValue::Str(s));
                        } else {
                            cin.clear();
                            push(GrassValue::Str(""));
                        }
                    }
                    else if (fn == "input.getline" || fn == "getline" || fn == "ask") {
                        if (argc >= 1) { cout << args[0].toString(); cout.flush(); }
                        string s;
                        getline(cin, s);
                        push(GrassValue::Str(s));
                    }
                    else if ((fn == "len" || fn == "length" || fn == "size") && argc >= 1) {
                        push(GrassValue::Num((double)args[0].toString().size()));
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
                        push(GrassValue::Str(args[0].is_string ? "string" : "number"));
                    }
                    else if (fn == "clear" || fn == "cls") {
                        system("cls");
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "color" && argc >= 1) {
                        string c = args[0].toString();
                        WORD attr = 7;
                        if (c == "green" || c == "2") attr = 10;
                        else if (c == "cyan" || c == "aqua" || c == "3") attr = 11;
                        else if (c == "red" || c == "4") attr = 12;
                        else if (c == "purple" || c == "magenta" || c == "5") attr = 13;
                        else if (c == "yellow" || c == "6") attr = 14;
                        else if (c == "white" || c == "7") attr = 15;
                        else if (c == "blue" || c == "1") attr = 9;
                        else if (c == "gray" || c == "8") attr = 8;
                        else attr = (WORD)args[0].toNumber();
                        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attr);
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
                        ShellExecuteA(NULL, "open", args[0].toString().c_str(), NULL, NULL, SW_SHOWNORMAL);
                        push(GrassValue::Num(1.0));
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
                    else if (vars.count(fn + "()")) push(vars[fn + "()"]);
                    else if (vars.count(fn)) push(vars[fn]);
                    else if (GrassDB::has(fn)) push(GrassValue::Str(GrassDB::get(fn)));
                    else push(GrassValue::Num(0.0));
                    break;
                }

                case OP_UI_INIT: {
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
                    break;
                }
                case OP_UI_ADD_ELEM: {
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
                    break;
                }
                case OP_UI_CMD: {
                    string cmd_line = readString();
                    GrassGUI::exec_command(cmd_line);
                    break;
                }
                case OP_UI_SHOW: {
                    HWND hConsole = GetConsoleWindow();
                    if (hConsole) ShowWindow(hConsole, SW_HIDE);
                    syncVarsToGUI();
                    GrassGUI::show_window();
                    break;
                }
            }
        }
    }
};

// ============================================================================
// 2. АВТО-УСТАНОВЩИК ЯЗЫКА И ИКОНОК (.GRASS + .GDB) В VS CODE
// ============================================================================
void InstallGrassGreenEcosystem() {
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    fs::path exe_path(self_path);
    fs::path root_dir = exe_path.parent_path();
    if (root_dir.filename() == "bin") root_dir = root_dir.parent_path();

    cout << "\n=== [GrassGreen v3.0 Installer] ===\n";

    {
        ofstream gcmd(root_dir / "grass.cmd");
        gcmd << "@echo off\n\"" << exe_path.string() << "\" ./! %*\n";
    }
    {
        ofstream ggcmd(root_dir / "gg.cmd");
        ggcmd << "@echo off\n\"" << exe_path.string() << "\" ./! %*\n";
    }
    cout << "[+] Созданы быстрые команды: grass <файл.grass> и gg <файл.grass>\n";

    const char* userprofile = getenv("USERPROFILE");
    if (userprofile) {
        fs::path ext_dir = fs::path(userprofile) / ".vscode" / "extensions" / "m1oky.grassgreen-lang-3.0.0";
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
           << "    { \"name\": \"keyword.control.grass\", \"match\": \"\\\\b(if|else|chek|check|for|while|again|loop|break|stop|stoped|return|exit|wait|sleep|window|cout|cin|endl|text|script-start|script-end|on-click|end-click|on-input|end-input|on-hover|end-hover|on-key|end-key|every|end-every|end-wait|end-chek|table|end-table|box-start|box-end|row-start|row-end|col-start|col-end)\\\\b\" },\n"
           << "    { \"name\": \"storage.type.grass\", \"match\": \"\\\\b(int|take|void|func|alt|fn|string|str|float|double|num|bool|auto|set|var|let|fany|invent)\\\\b\" },\n"
           << "    { \"name\": \"support.function.builtin.grass\", \"match\": \"\\\\b(main|input\\\\.getline|input|log|print|println|ask|random|random_txt|stop_random|play_random|memory_usit|memory_separ|memory_auto|memory_clean|memory_free|memory_alloc|memory_free_block|memory_limit|memory_dump|len|length|upper|lower|substr|replace|contains|trim|to_str|to_num|typeof|clear|cls|color|time|clock_ms|beep|msgbox|system|sqrt|discor|sin|cos|abs|round|floor|ceil|pow|clamp|min|max|rand|lerp|creat|redact|read|exists|remove|open|run|db_open|db_save|db_set|db_get|db_del|db_has|db_inc|db_dec|db_clear|db_backup|db_export_json|db_hash|db_encrypt|db_decrypt|gui_cmd|gui_get|gui_set|gui_show|gui_file|web_start|web_compile|title|size-top|theme|fon|iconP|button|label|enter|icon|video|window-ob|middle|left|right|text-color|button-color|font-size|font-style|radius|pad|shadow|glass|click-ob|switch|set-text|set-val|clear-val|repaint-color|repaint-bg|hide|show|toggle|play-anim)\\\\b\" },\n"
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
// 3. СВЕРХБЫСТРЫЙ WIN32 PE-PACKER (ИНКРЕМЕНТАЛЬНАЯ ДОЗАПИСЬ ЗА < 2 МС)
// ============================================================================
static const char MAGIC_FOOTER[8] = {'G','R','A','S','S','E','X','E'};

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

    GrassVM vm(bytecode);
    vm.execute();
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

    // 1. БЫСТРЫЙ ПУТЬ: если выходной .exe уже существует и имеет то же ядро — обновляем только хвост с байт-кодом!
    LONGLONG out_total = 0;
    LONGLONG out_stub = GetCleanStubSize(output_exe.c_str(), out_total);

    if (out_stub == stub_size) {
        HANDLE hOut = CreateFileA(output_exe.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut != INVALID_HANDLE_VALUE) {
            // Если размер старого и нового байт-кода совпадает, проверяем: вдруг код вообще не менялся?
            if (out_total == stub_size + (LONGLONG)bc_size + 12) {
                LARGE_INTEGER pos;
                pos.QuadPart = stub_size;
                SetFilePointerEx(hOut, pos, NULL, FILE_BEGIN);
                vector<uint8_t> existing_bc(bc_size);
                DWORD r = 0;
                if (ReadFile(hOut, existing_bc.data(), bc_size, &r, NULL) && r == bc_size &&
                    memcmp(existing_bc.data(), bytecode.data(), bc_size) == 0) {
                    CloseHandle(hOut);
                    return true; // 0.1 мс — файл уже идентичен!
                }
            }

            // Перезаписываем только крошечный блок байт-кода в конце файла (0.3 мс)
            LARGE_INTEGER pos;
            pos.QuadPart = stub_size;
            SetFilePointerEx(hOut, pos, NULL, FILE_BEGIN);

            DWORD written = 0;
            WriteFile(hOut, bytecode.data(), bc_size, &written, NULL);
            WriteFile(hOut, footer, 12, &written, NULL);
            SetEndOfFile(hOut);
            CloseHandle(hOut);
            return true;
        }
    }

    // 2. Если .exe создаётся впервые: используем системное копирование ядра CopyFileA
    if (self_total == stub_size && CopyFileA(self_path, output_exe.c_str(), FALSE)) {
        HANDLE hOut = CreateFileA(output_exe.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER pos;
            pos.QuadPart = stub_size;
            SetFilePointerEx(hOut, pos, NULL, FILE_BEGIN);
            DWORD written = 0;
            WriteFile(hOut, bytecode.data(), bc_size, &written, NULL);
            WriteFile(hOut, footer, 12, &written, NULL);
            SetEndOfFile(hOut);
            CloseHandle(hOut);
            return true;
        }
    }

    // 3. Резервный путь (если сам компилятор имел встроенный хвост)
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

bool CompileAndRunFile(string input_path, bool build_only) {
    auto t_start = chrono::high_resolution_clock::now();

    if (input_path.find('.') == string::npos) {
        if (fs::exists(input_path + ".grass")) input_path += ".grass";
    }

    // Быстрое чтение исходного кода одним непрерывным блоком
    ifstream in(input_path, ios::binary | ios::ate);
    if (!in.is_open()) {
        cerr << "[GrassError]: Файл не найден -> " << input_path << endl;
        return false;
    }
    streamsize src_size = in.tellg();
    in.seekg(0, ios::beg);
    string source_code((size_t)max((streamsize)0, src_size), '\0');
    if (src_size > 0) in.read(&source_code[0], src_size);
    in.close();

    Lexer lexer(source_code);
    vector<GrassToken> tokens = lexer.tokenize();

    BytecodeCompiler compiler(tokens, input_path);
    compiler.compileProgram();

    if (compiler.has_error) {
        cerr << "\n[GrassCompiler]: Сборка остановлена из-за ошибок синтаксиса в " << input_path << "!\n";
        return false;
    }

    string output_exe = input_path;
    size_t dot = output_exe.rfind('.');
    if (dot != string::npos) output_exe = output_exe.substr(0, dot) + ".exe";
    else output_exe += ".exe";

    bool packed_ok = BuildStandaloneExe(output_exe, compiler.code);

    auto t_end = chrono::high_resolution_clock::now();
    double elapsed_ms = chrono::duration<double, std::milli>(t_end - t_start).count();

    if (packed_ok) {
        cout << "[GrassCompiler]: Скомпилировано " << compiler.code.size()
             << " байт двоичного кода -> " << output_exe
             << " (" << fixed << setprecision(2) << elapsed_ms << " ms)" << endl;
    }

    if (!build_only) {
        cout << "[GrassVM]: Исполнение двоичного кода...\n" << endl;
        GrassVM vm(compiler.code);
        vm.execute();
    }
    return true;
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

int main(int argc, char* argv[]) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD origInMode = 0;
    if (hIn != INVALID_HANDLE_VALUE) GetConsoleMode(hIn, &origInMode);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (CheckAndRunEmbeddedBytecode()) {
        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
        return 0;
    }

    if (argc >= 2) {
        bool build_only = false;
        string target_file = "";

        for (int i = 1; i < argc; ++i) {
            string arg = argv[i];
            if (arg == "--install" || arg == "install" || arg == "-i") {
                InstallGrassGreenEcosystem();
                return 0;
            } else if (arg == "--build" || arg == "-c" || arg == ".//") {
                build_only = true;
            } else if (arg == "--run" || arg == "-r" || arg == "./!") {
                build_only = false;
            } else {
                target_file = arg;
            }
        }

        if (target_file.empty()) target_file = FindDefaultGrassFile();
        if (!target_file.empty()) CompileAndRunFile(target_file, build_only);
        else cerr << "[GrassError]: Не найден ни один .grass файл в папке." << endl;

        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
        return 0;
    }

    cout << "========================================================\n";
    cout << "  GrassGreen Native Bytecode Compiler v3.0 (by m1oky)\n";
    cout << "========================================================\n";
    string line;
    while (true) {
        cout << "GrassGreen> ";
        if (!getline(cin, line)) break;
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);

        if (line == "exit" || line == "quit") break;
        if (line == "install" || line == "--install") { InstallGrassGreenEcosystem(); continue; }

        stringstream ss(line);
        string cmd, file_arg;
        ss >> cmd;
        getline(ss, file_arg);
        size_t f_start = file_arg.find_first_not_of(" \t");
        if (f_start != string::npos) file_arg = file_arg.substr(f_start);
        else file_arg = "";

        if (cmd == ".//" || cmd == "compile" || cmd == "build" || cmd == "-c") {
            CompileAndRunFile(file_arg.empty() ? FindDefaultGrassFile() : file_arg, true);
        } else if (cmd == "./!" || cmd == "run" || cmd == "-r") {
            CompileAndRunFile(file_arg.empty() ? FindDefaultGrassFile() : file_arg, false);
        } else {
            CompileAndRunFile(cmd, false);
        }
    }

    if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
    return 0;
}
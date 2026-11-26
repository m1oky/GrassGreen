#ifndef GRASS_VM_H
#define GRASS_VM_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <cstdint>
#include <cstring>

#include "frontend/GrassSyntax.h"
#include "vm/GrassArena.h"
#include "vm/GrassJIT.h"

extern GrassArena g_vm_arena;
extern std::unordered_map<std::string, GrassStructDef> g_struct_registry;
extern bool g_not_clear_active;
extern std::vector<std::string> g_protected_console_lines;

struct GrassSharedEnv {
    std::mutex mtx;
    std::unordered_map<std::string, GrassValue> vars;
};

class GrassVM {
    std::shared_ptr<const std::vector<uint8_t>> bytecode_ptr;
    const std::vector<uint8_t>& code;
    size_t ip = 0;
    std::vector<GrassValue> stack;

    struct CallFrame {
        size_t return_ip = 0;
        std::unordered_map<std::string, GrassValue> locals;
    };
    std::vector<CallFrame> frames;

    std::unordered_map<std::string, GrassValue> vars;
    std::shared_ptr<GrassSharedEnv> shared_env;
    bool is_root_vm = true;
    GrassValue last_return_value{GrassValue::Num(0.0)};
    std::vector<GrassArray*> heap_arrays;
    std::unordered_map<uint32_t, std::shared_ptr<GrassJIT::CompiledJitBlock>> jit_cache;
    std::unordered_map<uint32_t, int> loop_trip_counters;
    std::unordered_set<uint32_t> failed_loops;

    struct AttachedSound {
        std::string waveform = "sine";
        double freq_hz = 440.0;
        int duration_ms = 100;
        double volume = 80.0;
    };
    std::unordered_map<std::string, uint32_t> func_name_to_ip;
    std::unordered_map<uint32_t, AttachedSound> attached_ip_sounds;

    bool tryRunJitLoop(uint32_t loop_start, uint32_t loop_end);

    inline uint8_t readByte() {
        if (ip >= code.size()) return OP_HALT;
        return code[ip++];
    }
    inline uint32_t readU32() {
        if (ip + 4 > code.size()) { ip = code.size(); return 0; }
        uint32_t v;
        std::memcpy(&v, &code[ip], 4);
        ip += 4;
        return v;
    }
    inline double readDouble() {
        if (ip + 8 > code.size()) { ip = code.size(); return 0.0; }
        double d;
        std::memcpy(&d, &code[ip], 8);
        ip += 8;
        return d;
    }
    inline std::string readString() {
        uint32_t len = readU32();
        if (ip + len > code.size()) { ip = code.size(); return ""; }
        std::string s((const char*)&code[ip], len);
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

    void syncDBToVars();
    void syncVarsToGUI();
    void run_callback(uint32_t cb_ip);
    static bool isGdbSchemaKey(const std::string& fn);

    bool callBuiltinCore(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args);
    bool callBuiltinMedia(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args);
    bool callBuiltinSys(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args);
    bool callBuiltinLowLevel(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args);
    void dispatchBuiltin(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args);

public:
    explicit GrassVM(std::shared_ptr<const std::vector<uint8_t>> bytecode,
                     std::shared_ptr<GrassSharedEnv> shared = nullptr,
                     bool root = true);
    explicit GrassVM(const std::vector<uint8_t>& bytecode,
                     std::shared_ptr<GrassSharedEnv> shared = nullptr,
                     bool root = true);
    explicit GrassVM(std::vector<uint8_t>&& bytecode,
                     std::shared_ptr<GrassSharedEnv> shared = nullptr,
                     bool root = true);
    ~GrassVM();

    GrassValue getLastReturnValue() const { return last_return_value; }
    int64_t spawnThread(uint32_t entry_ip, uint8_t argc, bool detached);
    void execute(size_t start_ip = 0);
};

#endif // GRASS_VM_H

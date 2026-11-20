#define GRASSVM_EXPORTS
#include "grassvm.h"
#include "GrassSyntax.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>

struct GrgVM {
    std::string last_error;
    std::vector<uint8_t> bytecode;
};

extern "C" {

GRG_API GrgVM* grg_vm_create(void) {
    return new GrgVM();
}

GRG_API void grg_vm_destroy(GrgVM* vm) {
    if (vm) delete vm;
}

GRG_API int grg_vm_load_file(GrgVM* vm, const char* path) {
    if (!vm || !path) return 1;
#if defined(_WIN32)
    std::string cmd = "grg.exe -c \"" + std::string(path) + "\"";
#else
    std::string cmd = "grg -c \"" + std::string(path) + "\"";
#endif
    int res = system(cmd.c_str());
    if (res != 0) {
        vm->last_error = "Failed to load and compile script: " + std::string(path);
        return res;
    }
    return 0;
}

GRG_API int grg_vm_eval(GrgVM* vm, const char* code) {
    if (!vm || !code) return 1;
    // Write temporary script and execute
    const char* tmp_file = "_tmp_eval.grass";
    FILE* f = fopen(tmp_file, "w");
    if (!f) {
        vm->last_error = "Failed to create temp script file";
        return 1;
    }
    fputs(code, f);
    fclose(f);

#if defined(_WIN32)
    std::string cmd = "grg.exe -r " + std::string(tmp_file);
#else
    std::string cmd = "grg -r " + std::string(tmp_file);
#endif
    int res = system(cmd.c_str());
    remove(tmp_file);
    if (res != 0) {
        vm->last_error = "Script execution returned non-zero code";
    }
    return res;
}

GRG_API int grg_vm_set_int(GrgVM* vm, const char* name, int64_t val) {
    if (!vm || !name) return 1;
    return 0;
}

GRG_API int grg_vm_set_str(GrgVM* vm, const char* name, const char* val) {
    if (!vm || !name || !val) return 1;
    return 0;
}

GRG_API int grg_vm_call(GrgVM* vm, const char* func_name) {
    if (!vm || !func_name) return 1;
    std::string eval_code = "!GrassGreen!\nint main() { " + std::string(func_name) + "(); return 0; }\n";
    return grg_vm_eval(vm, eval_code.c_str());
}

GRG_API const char* grg_vm_get_error(GrgVM* vm) {
    return vm ? vm->last_error.c_str() : "Invalid VM pointer";
}

}

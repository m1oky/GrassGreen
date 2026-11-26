#pragma once
#ifndef GRASS_NATIVE_STDLIB_H
#define GRASS_NATIVE_STDLIB_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "native/GrassIR.h"
#include "native/GrassBackendTypes.h"

namespace grass::native {

    enum class RuntimeProfile : uint8_t {
        FullOS,       // Standard hosted OS runtime (Win64 / SysV64)
        Freestanding  // Baremetal / RQWos minimal runtime without OS syscalls
    };

    struct NativeFunctionDesc {
        std::string name;
        std::string module;
        ir::IRType* return_type = nullptr;
        std::vector<ir::IRType*> param_types;
        std::vector<std::string> param_names;
        bool is_variadic = false;
        bool is_freestanding = false;
        bool is_extern = false;
        std::string extern_library;
        backend::TargetABI abi = backend::TargetABI::Win64;
    };

    class GrassNativeStdlib {
    public:
        static GrassNativeStdlib& instance();

        GrassNativeStdlib();
        ~GrassNativeStdlib() = default;

        // Initialize standard library function catalog using provided IR context
        void initialize(ir::IRContext& ctx);
        bool is_initialized() const { return initialized_; }

        // Query function descriptors
        const NativeFunctionDesc* find_function(const std::string& name) const;
        bool has_function(const std::string& name) const;
        bool is_available_in_profile(const std::string& name, RuntimeProfile profile) const;

        // Module function queries
        std::vector<std::string> get_supported_modules() const;
        std::vector<NativeFunctionDesc> get_module_functions(const std::string& module_name) const;

        // Manual registration
        void register_function(const NativeFunctionDesc& desc);

        // FFI / extern declaration registration
        void register_extern(const std::string& symbol,
                             const std::string& lib_name,
                             ir::IRType* ret_type,
                             const std::vector<ir::IRType*>& param_types,
                             backend::TargetABI abi = backend::TargetABI::Win64);

        // Populate module with standard definitions according to selected profile
        void register_all_into_module(ir::IRModule& module, RuntimeProfile profile = RuntimeProfile::FullOS);
        void register_module_into_module(ir::IRModule& module, const std::string& module_name, RuntimeProfile profile = RuntimeProfile::FullOS);

        // OS extern registry
        void register_standard_os_externs(ir::IRContext& ctx);
        const NativeFunctionDesc* find_extern(const std::string& symbol) const;
        std::vector<std::string> get_registered_externs() const;

    private:
        std::unordered_map<std::string, NativeFunctionDesc> registry_;
        std::unordered_map<std::string, NativeFunctionDesc> extern_registry_;
        std::unordered_map<std::string, std::vector<std::string>> module_map_;
        bool initialized_ = false;
        ir::IRContext* current_ctx_ = nullptr;

        void populate_io(ir::IRContext& ctx);
        void populate_fsys(ir::IRContext& ctx);
        void populate_math(ir::IRContext& ctx);
        void populate_time(ir::IRContext& ctx);
        void populate_sys(ir::IRContext& ctx);
        void populate_proc(ir::IRContext& ctx);
        void populate_net(ir::IRContext& ctx);
        void populate_json(ir::IRContext& ctx);
        void populate_disp(ir::IRContext& ctx);
        void populate_game2d(ir::IRContext& ctx);
        void populate_physics3d(ir::IRContext& ctx);
        void populate_memory(ir::IRContext& ctx);
        void populate_thread(ir::IRContext& ctx);
        void populate_ffi(ir::IRContext& ctx);
        void populate_extra(ir::IRContext& ctx);
    };

} // namespace grass::native

#endif // GRASS_NATIVE_STDLIB_H

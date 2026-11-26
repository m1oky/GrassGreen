#include "native/GrassNativeCodegen.h"
#include "native/GrassNativeDebug.h"
#include "frontend/GrassImport.h"
#include "frontend/GrassSema.h"
#include "native/GrassNativeOOP.h"
#include "native/GrassNativeMemory.h"
#include "native/GrassNativeStdlib.h"
#include "native/GrassAdvancedOpt.h"
#include "native/GrassElfWriter.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <set>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <chrono>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

#include "native/GrassNativeRuntime.h"
#include "native/GrassASTLowerer.h"

namespace grass::codegen {

    // =========================================================================
    // 2. Default Subsystem Delegates
    // =========================================================================

    class DefaultNativeMemoryDelegate : public INativeMemoryDelegate {
    public:
        explicit DefaultNativeMemoryDelegate(const native::MemoryRuntimeConfig& config = {})
            : mem_(config) {}

        ir::ValueId emit_alloc(ir::IRBuilder& builder, uint64_t size_bytes, const std::string& name = "dyn_alloc") override {
            return mem_.emit_alloc_const(builder, size_bytes, name);
        }
        ir::ValueId emit_alloc(ir::IRBuilder& builder, ir::ValueId size_bytes, const std::string& name = "dyn_alloc") override {
            return mem_.emit_alloc(builder, size_bytes, name);
        }
        void emit_free(ir::IRBuilder& builder, ir::ValueId ptr) override {
            mem_.emit_free(builder, ptr);
        }
        void enter_scope(const std::string& name = "") override {
            mem_.enter_scope(name);
        }
        void exit_scope(ir::IRBuilder& builder) override {
            mem_.exit_scope(builder);
        }
        void register_raii_object(ir::ValueId obj_ptr, const std::string& dtor_fn, const std::string& debug_name = "") override {
            mem_.register_raii_object(obj_ptr, dtor_fn, debug_name);
        }
        void register_dynamic_buffer(ir::ValueId buf_ptr, const std::string& debug_name = "") override {
            mem_.register_dynamic_buffer(buf_ptr, debug_name);
        }
        void emit_all_cleanups(ir::IRBuilder& builder) override {
            mem_.emit_all_cleanups(builder);
        }
        ir::IRInst* emit_return(ir::IRBuilder& builder, std::optional<ir::ValueId> ret_val = std::nullopt) override {
            return mem_.emit_return(builder, ret_val);
        }
        native::GrassNativeMemory& get_memory() { return mem_; }
    private:
        native::GrassNativeMemory mem_;
    };

    class DefaultNativeOOPDelegate : public INativeOOPDelegate {
    public:
        DefaultNativeOOPDelegate(ir::IRModule& module, native::GrassNativeMemory* mem)
            : oop_(module, mem) {}

        bool has_class_or_struct(const std::string& name) const override {
            return oop_.has_class(name);
        }
        bool handle_struct_or_class_decl(const std::vector<GrassToken>& tokens, size_t& pos,
                                         ir::IRModule& module, std::string& error) override {
            (void)tokens; (void)pos; (void)module; (void)error;
            return true;
        }
        bool handle_method_call(ir::IRBuilder& builder, ir::ValueId instance_ptr,
                                const std::string& class_name, const std::string& method_name,
                                const std::vector<ir::ValueId>& args, ir::ValueId& out_res,
                                std::string& error) override {
            (void)error;
            out_res = oop_.emit_call_method(builder, instance_ptr, class_name, method_name, args);
            return out_res != ir::kInvalidValueId;
        }
        bool handle_field_access(ir::IRBuilder& builder, ir::ValueId instance_ptr,
                                 const std::string& class_name, const std::string& field_name,
                                 ir::ValueId& out_res, std::string& error) override {
            (void)error;
            out_res = oop_.emit_load_field(builder, instance_ptr, class_name, field_name);
            return out_res != ir::kInvalidValueId;
        }
        bool handle_field_store(ir::IRBuilder& builder, ir::ValueId instance_ptr,
                                const std::string& class_name, const std::string& field_name,
                                ir::ValueId val_to_store, std::string& error) override {
            (void)error;
            ir::IRInst* inst = oop_.emit_store_field(builder, instance_ptr, class_name, field_name, val_to_store);
            return inst != nullptr;
        }
        bool handle_new_object(ir::IRBuilder& builder, const std::string& class_name,
                               const std::vector<ir::ValueId>& ctor_args, ir::ValueId& out_res,
                               std::string& error) override {
            (void)error;
            out_res = oop_.emit_new_object(builder, class_name, ctor_args);
            return out_res != ir::kInvalidValueId;
        }
        bool handle_delete_object(ir::IRBuilder& builder, ir::ValueId instance_ptr,
                                  const std::string& class_name, std::string& error) override {
            (void)error;
            oop_.emit_delete_object(builder, instance_ptr, class_name);
            return true;
        }
        bool handle_array_create(ir::IRBuilder& builder, ir::IRType* elem_type,
                                 uint64_t initial_cap, ir::ValueId& out_res, std::string& error) override {
            (void)error;
            out_res = oop_.emit_array_create_const(builder, elem_type, initial_cap);
            return out_res != ir::kInvalidValueId;
        }
        bool handle_array_get(ir::IRBuilder& builder, ir::ValueId arr_ptr, ir::ValueId index_val,
                              ir::IRType* elem_type, ir::ValueId& out_res, std::string& error) override {
            (void)error;
            out_res = oop_.emit_array_get(builder, arr_ptr, index_val, elem_type);
            return out_res != ir::kInvalidValueId;
        }
        bool handle_array_set(ir::IRBuilder& builder, ir::ValueId arr_ptr, ir::ValueId index_val,
                              ir::ValueId val_to_store, ir::IRType* elem_type, std::string& error) override {
            (void)error;
            oop_.emit_array_set(builder, arr_ptr, index_val, val_to_store, elem_type);
            return true;
        }
        bool handle_string_literal(ir::IRBuilder& builder, const std::string& text,
                                   ir::ValueId& out_res, std::string& error) override {
            (void)error;
            out_res = oop_.emit_string_literal(builder, text);
            return out_res != ir::kInvalidValueId;
        }
        bool handle_string_concat(ir::IRBuilder& builder, ir::ValueId s1, ir::ValueId s2,
                                  ir::ValueId& out_res, std::string& error) override {
            (void)error;
            out_res = oop_.emit_string_concat(builder, s1, s2);
            return out_res != ir::kInvalidValueId;
        }
        native::GrassNativeOOP& get_oop() { return oop_; }
    private:
        native::GrassNativeOOP oop_;
    };

    class DefaultNativeStdlibDelegate : public INativeStdlibDelegate {
    public:
        DefaultNativeStdlibDelegate() = default;

        bool is_stdlib_module(const std::string& module_name) const override {
            auto mods = native::GrassNativeStdlib::instance().get_supported_modules();
            return std::find(mods.begin(), mods.end(), module_name) != mods.end();
        }

        bool is_stdlib_function(const std::string& fn_name) const override {
            return native::GrassNativeStdlib::instance().has_function(fn_name);
        }

        bool resolve_call(ir::IRBuilder& builder, const std::string& fn_name,
                          const std::vector<ir::ValueId>& args, ir::ValueId& out_res,
                          std::string& error) override {
            const native::NativeFunctionDesc* desc = native::GrassNativeStdlib::instance().find_function(fn_name);
            if (!desc) {
                error = "Unknown standard library function: " + fn_name;
                return false;
            }
            out_res = builder.create_call(desc->name, desc->return_type, args);
            return true;
        }

        bool register_extern(ir::IRModule& module, const std::string& fn_name,
                             ir::IRType* ret_type, const std::vector<ir::IRType*>& param_types,
                             std::string& error) override {
            (void)error;
            native::GrassNativeStdlib::instance().register_extern(fn_name, "", ret_type, param_types, module.abi);
            return true;
        }
    };


    // =========================================================================
    // 4. NativeCompiler Class Implementation
    // =========================================================================

    NativeCompiler::NativeCompiler(const NativeCodegenOptions& options)
        : options_(options) {}

    TargetArch NativeCompiler::parse_arch(const std::string& str) {
        if (str == "x32" || str == "i386" || str == "x86" || str == "x86_32") {
            return TargetArch::X86_32;
        }
        return TargetArch::X86_64;
    }

    TargetABI NativeCompiler::parse_abi(const std::string& str) {
        if (str == "win64" || str == "ms64" || str == "windows") return TargetABI::Win64;
        if (str == "sysv64" || str == "sysv" || str == "linux") return TargetABI::SysV64;
        if (str == "cdecl32" || str == "cdecl" || str == "win32") return TargetABI::Cdecl32;
        if (str == "baremetal64" || str == "bm64") return TargetABI::Baremetal64;
        if (str == "baremetal32" || str == "bm32") return TargetABI::Baremetal32;
        return TargetABI::Win64;
    }

    std::string NativeCompiler::arch_to_string(TargetArch arch) {
        return (arch == TargetArch::X86_64) ? "x86_64" : "x86_32";
    }

    std::string NativeCompiler::abi_to_string(TargetABI abi) {
        switch (abi) {
            case TargetABI::Win64: return "Win64";
            case TargetABI::SysV64: return "SysV64";
            case TargetABI::Cdecl32: return "Cdecl32";
            case TargetABI::Baremetal64: return "Baremetal64";
            case TargetABI::Baremetal32: return "Baremetal32";
        }
        return "Unknown";
    }

    bool NativeCompiler::lower_ast_to_ir(const std::vector<GrassToken>& tokens,
                                         ir::IRModule& module,
                                         std::vector<uint8_t>& rodata,
                                         std::unordered_map<std::string, uint64_t>& rodata_symbols,
                                         std::string& error,
                                         const std::string& source_path) {
        auto profile = options_.freestanding ? grass::native::RuntimeProfile::Freestanding : grass::native::RuntimeProfile::FullOS;
        auto& stdlib = grass::native::GrassNativeStdlib::instance();
        stdlib.initialize(module.context);
        ir::IRType* i64_t = module.context.get_i64_type();
        ir::IRType* ptr_t = module.context.get_ptr_type(module.context.get_i8_type());
        if (!stdlib.has_function("pow_mod")) {
            stdlib.register_function({ "pow_mod", "CoreMath", i64_t, { i64_t, i64_t, i64_t }, { "base", "exp", "mod" }, false, true, false, "", backend::TargetABI::Win64 });
        }
        if (!stdlib.has_function("sha256")) {
            stdlib.register_function({ "sha256", "CoreMath", ptr_t, { ptr_t }, { "input" }, false, true, false, "", backend::TargetABI::Win64 });
        }
        stdlib.register_all_into_module(module, profile);

        native::MemoryRuntimeConfig mem_cfg;
        mem_cfg.baremetal_mode = options_.freestanding;
        native::GrassNativeMemory mem_mgr(mem_cfg);
        native::GrassNativeOOP oop_mgr(module, &mem_mgr);

        ASTLowerer lowerer(tokens, module, rodata, rodata_symbols, source_path, options_.freestanding,
                           oop_delegate_.get(), memory_delegate_.get(), stdlib_delegate_.get(),
                           &oop_mgr, &mem_mgr);
        return lowerer.lower(error);
    }

    bool NativeCompiler::optimize_ir(ir::IRModule& module, int opt_level, std::string& error) {
        // Step 1: Mem2Reg SSA promotion
        ir::Mem2RegPass mem2reg;
        for (auto& fn : module.functions) {
            mem2reg.run_on_function(*fn);
        }

        // Step 2: Advanced optimizations (Inlining, LICM, Loop Unrolling, Strength Reduction)
        if (opt_level >= 2) {
            grass::opt::GrassAdvancedOpt::optimize(module, opt_level);
        }

        // Step 3: Optimization pipeline
        if (opt_level > 0) {
            ir::PassManager pm;
            pm.add_standard_optimizations();
            pm.run(module);
        }

        return module.verify(error);
    }

    bool NativeCompiler::ensure_runtime_c_file(std::string& runtime_path, std::string& error) {
        runtime_path = "bin/grass_native_runtime.c";

        if (!fs::exists("bin")) {
            std::error_code ec;
            fs::create_directories("bin", ec);
        }

        std::ofstream out(runtime_path);
        if (!out.is_open()) {
            error = "Failed to create runtime file: " + runtime_path;
            return false;
        }
        out << kEmbeddedRuntimeC << "\n";
        out << grass::debug::generate_runtime_crash_handler_c() << "\n";
        out << R"(
#if defined(_WIN32)
static void __cdecl grass_auto_init_veh(void) {
    grass_install_runtime_crash_handler();
}
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (__cdecl *p_grass_auto_init_veh)(void) = grass_auto_init_veh;
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void grass_auto_init_sig(void) {
    grass_install_runtime_crash_handler();
}
#endif
)";
        out.close();
        return true;
    }

    bool NativeCompiler::emit_containers(const CompiledUnit& unit,
                                         const std::string& base_name,
                                         NativeCodegenResult& res,
                                         std::string& error) {
        // Flat Binary emission
        if (!options_.emit_bin_path.empty() || options_.freestanding) {
            std::string bin_target = options_.emit_bin_path.empty() ? (base_name + ".bin") : options_.emit_bin_path;
            res.bin_file_created = bin_target;
            std::string map_file = base_name + ".map";
            if (!backend::GrassObjWriter::write_flat_binary_file(unit, options_.base_address, res.bin_file_created, map_file, &error)) {
                return false;
            }
        }

        // Object emission (.obj / .o)
        std::string obj_target = options_.emit_obj_path;
        if (obj_target.empty()) {
            obj_target = (options_.abi == TargetABI::Win64 || options_.abi == TargetABI::Cdecl32)
                ? (base_name + ".obj") : (base_name + ".o");
        }

        if (options_.abi == TargetABI::Win64 || options_.abi == TargetABI::Cdecl32) {
            if (!backend::GrassObjWriter::write_coff_obj_file(unit, obj_target, &error)) {
                return false;
            }
        } else {
            if (!backend::GrassObjWriter::write_elf_obj_file(unit, obj_target, &error)) {
                return false;
            }
        }
        res.obj_file_created = obj_target;
        return true;
    }

    bool NativeCompiler::link_executable(const std::string& obj_path,
                                         const std::string& exe_path,
                                         const CompiledUnit& unit,
                                         std::string& error) {
        if (options_.freestanding) {
            return true;
        }

        // If user explicitly requested gcc linker via --use-gcc
        if (options_.use_gcc) {
            std::string runtime_c;
            if (!ensure_runtime_c_file(runtime_c, error)) return false;

            std::ostringstream cmd;
            cmd << "gcc -O2 \"" << obj_path << "\" \"" << runtime_c << "\" -o \"" << exe_path << "\"";
            if (options_.abi == TargetABI::Win64 || options_.abi == TargetABI::Cdecl32) {
                cmd << " -lkernel32 -lws2_32 -luser32 -lm";
            } else {
                cmd << " -lm";
            }

            int code = std::system(cmd.str().c_str());
            if (code != 0) {
                error = "System linker invocation failed with code " + std::to_string(code) + ": " + cmd.str();
                return false;
            }
            return true;
        }

        // Linux ELF64 binary emission (--target=linux or sysv64 ABI)
        if (options_.target == "linux" || options_.abi == TargetABI::SysV64) {
            backend::ElfWriterConfig elf_cfg;
            elf_cfg.auto_inject_syscalls = true;
            elf_cfg.entry_symbol = "main";
            if (!backend::GrassElfWriter::write_elf_exe_file(unit, exe_path, elf_cfg, nullptr, &error)) {
                return false;
            }
            return true;
        }

        // Windows PE32+ executable (default for --native without gcc)
        std::string entry_point = "main";
        for (const auto& s : unit.symbols) {
            if (s.name == "_start" && !s.is_external) {
                entry_point = "_start";
                break;
            }
        }

        if (!backend::GrassObjWriter::write_pe_executable_file(unit, exe_path, &error, entry_point)) {
            return false;
        }
        return true;
    }

    bool NativeCompiler::link_executable(const std::string& obj_path,
                                         const std::string& exe_path,
                                         std::string& error) {
        CompiledUnit empty_unit;
        return link_executable(obj_path, exe_path, empty_unit, error);
    }

    NativeCodegenResult NativeCompiler::compile(const std::vector<GrassToken>& tokens, const std::string& source_path) {
        NativeCodegenResult res;
        std::string error;

        std::string base_name = source_path;
        size_t dot = base_name.rfind('.');
        if (dot != std::string::npos) base_name = base_name.substr(0, dot);
        if (base_name.empty()) base_name = "output";

        // Stage 1: Lower AST / Tokens to GrassIR
        ir::IRModule module(base_name);
        module.abi = options_.abi;
        module.arch = options_.arch;

        std::vector<uint8_t> rodata;
        std::unordered_map<std::string, uint64_t> rodata_symbols;

        if (!lower_ast_to_ir(tokens, module, rodata, rodata_symbols, error, source_path)) {
            res.success = false;
            res.error_message = "Lowering Error: " + error;
            return res;
        }

        res.ir_dump_initial = module.dump();

        // Stage 2: Optimize IR
        if (!optimize_ir(module, options_.opt_level, error)) {
            res.success = false;
            res.error_message = "Optimization Error: " + error + "\n=== IR DUMP ===\n" + module.dump();
            return res;
        }

        res.ir_dump_optimized = module.dump();

        // Stage 3: Instruction Selection & Machine Code Generation
        if (!codegen_module(module, rodata, rodata_symbols, res.compiled_unit, error)) {
            res.success = false;
            res.error_message = "Codegen Error: " + error;
            return res;
        }

        // Stage 4: Container Emission (.obj / .o / .bin)
        if (!emit_containers(res.compiled_unit, base_name, res, error)) {
            res.success = false;
            res.error_message = "Container Emission Error: " + error;
            return res;
        }

        // Stage 5: Linking Executable (if output_exe_path is set or default native target)
        if (!options_.output_exe_path.empty() && !options_.freestanding) {
            if (!link_executable(res.obj_file_created, options_.output_exe_path, res.compiled_unit, error)) {
                res.success = false;
                res.error_message = "Link Error: " + error;
                return res;
            }
            res.exe_file_created = options_.output_exe_path;

            // Clean up temporary .obj if explicit --emit-obj was not specified
            if (options_.emit_obj_path.empty() && !res.obj_file_created.empty()) {
                std::error_code ec;
                std::filesystem::remove(res.obj_file_created, ec);
                res.obj_file_created.clear();
            }
        }

        res.success = true;
        return res;
    }

    NativeCodegenResult NativeCompiler::compile_source(const std::string& source_code, const std::string& source_path) {
        Lexer lexer(source_code, source_path);
        std::vector<GrassToken> tokens = lexer.tokenize();
        GrassImport::ImportStats import_stats;
        tokens = GrassImport::resolveImports(tokens, source_path, &import_stats);
        if (import_stats.has_error) {
            NativeCodegenResult res;
            res.success = false;
            res.error_message = "Import resolution failed!";
            return res;
        }
        return compile(tokens, source_path);
    }

    NativeCodegenResult NativeCompiler::compile_file(const std::string& file_path) {
        std::ifstream in(file_path);
        if (!in.is_open()) {
            NativeCodegenResult res;
            res.success = false;
            res.error_message = "Cannot open source file: " + file_path;
            return res;
        }
        std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        return compile_source(source, file_path);
    }

} // namespace grass::codegen

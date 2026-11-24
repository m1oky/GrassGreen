#pragma once
#ifndef GRASS_NATIVE_CODEGEN_H
#define GRASS_NATIVE_CODEGEN_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include "GrassBackendTypes.h"
#include "GrassIR.h"
#include "GrassX86Encoder.h"
#include "GrassObjWriter.h"
#include "GrassSyntax.h"

namespace grass::codegen {

    using TargetArch = grass::backend::TargetArch;
    using TargetABI = grass::backend::TargetABI;
    using CompiledUnit = grass::backend::CompiledUnit;

    // =========================================================================
    // Modular Subsystem Interfaces (Delegates / Hooks)
    // =========================================================================

    class INativeOOPDelegate {
    public:
        virtual ~INativeOOPDelegate() = default;
        virtual bool has_class_or_struct(const std::string& name) const = 0;
        virtual bool handle_struct_or_class_decl(const std::vector<GrassToken>& tokens, size_t& pos,
                                                 ir::IRModule& module, std::string& error) = 0;
        virtual bool handle_method_call(ir::IRBuilder& builder, ir::ValueId instance_ptr,
                                        const std::string& class_name, const std::string& method_name,
                                        const std::vector<ir::ValueId>& args, ir::ValueId& out_res,
                                        std::string& error) = 0;
        virtual bool handle_field_access(ir::IRBuilder& builder, ir::ValueId instance_ptr,
                                         const std::string& class_name, const std::string& field_name,
                                         ir::ValueId& out_res, std::string& error) = 0;
        virtual bool handle_field_store(ir::IRBuilder& builder, ir::ValueId instance_ptr,
                                        const std::string& class_name, const std::string& field_name,
                                        ir::ValueId val_to_store, std::string& error) = 0;
        virtual bool handle_new_object(ir::IRBuilder& builder, const std::string& class_name,
                                       const std::vector<ir::ValueId>& ctor_args, ir::ValueId& out_res,
                                       std::string& error) = 0;
        virtual bool handle_delete_object(ir::IRBuilder& builder, ir::ValueId instance_ptr,
                                          const std::string& class_name, std::string& error) = 0;
        virtual bool handle_array_create(ir::IRBuilder& builder, ir::IRType* elem_type,
                                         uint64_t initial_cap, ir::ValueId& out_res, std::string& error) = 0;
        virtual bool handle_array_get(ir::IRBuilder& builder, ir::ValueId arr_ptr, ir::ValueId index_val,
                                      ir::IRType* elem_type, ir::ValueId& out_res, std::string& error) = 0;
        virtual bool handle_array_set(ir::IRBuilder& builder, ir::ValueId arr_ptr, ir::ValueId index_val,
                                      ir::ValueId val_to_store, ir::IRType* elem_type, std::string& error) = 0;
        virtual bool handle_string_literal(ir::IRBuilder& builder, const std::string& text,
                                           ir::ValueId& out_res, std::string& error) = 0;
        virtual bool handle_string_concat(ir::IRBuilder& builder, ir::ValueId s1, ir::ValueId s2,
                                          ir::ValueId& out_res, std::string& error) = 0;
    };

    class INativeMemoryDelegate {
    public:
        virtual ~INativeMemoryDelegate() = default;
        virtual ir::ValueId emit_alloc(ir::IRBuilder& builder, uint64_t size_bytes, const std::string& name = "dyn_alloc") = 0;
        virtual ir::ValueId emit_alloc(ir::IRBuilder& builder, ir::ValueId size_bytes, const std::string& name = "dyn_alloc") = 0;
        virtual void emit_free(ir::IRBuilder& builder, ir::ValueId ptr) = 0;
        virtual void enter_scope(const std::string& name = "") = 0;
        virtual void exit_scope(ir::IRBuilder& builder) = 0;
        virtual void register_raii_object(ir::ValueId obj_ptr, const std::string& dtor_fn, const std::string& debug_name = "") = 0;
        virtual void register_dynamic_buffer(ir::ValueId buf_ptr, const std::string& debug_name = "") = 0;
        virtual void emit_all_cleanups(ir::IRBuilder& builder) = 0;
        virtual ir::IRInst* emit_return(ir::IRBuilder& builder, std::optional<ir::ValueId> ret_val = std::nullopt) = 0;
    };

    class INativeStdlibDelegate {
    public:
        virtual ~INativeStdlibDelegate() = default;
        virtual bool is_stdlib_module(const std::string& module_name) const = 0;
        virtual bool is_stdlib_function(const std::string& fn_name) const = 0;
        virtual bool resolve_call(ir::IRBuilder& builder, const std::string& fn_name,
                                  const std::vector<ir::ValueId>& args, ir::ValueId& out_res,
                                  std::string& error) = 0;
        virtual bool register_extern(ir::IRModule& module, const std::string& fn_name,
                                     ir::IRType* ret_type, const std::vector<ir::IRType*>& param_types,
                                     std::string& error) = 0;
    };

    struct NativeCodegenOptions {
        TargetArch arch = TargetArch::X86_64;
        TargetABI abi = TargetABI::Win64;
        int opt_level = 2; // 0, 1, 2, 3
        uint64_t base_address = 0x100000; // RQWos baremetal default
        bool emit_ir = false;
        std::string emit_obj_path = "";
        std::string emit_bin_path = "";
        std::string output_exe_path = "";
        bool run_after_build = false;
        bool freestanding = false;
        bool verbose = false;
        bool lang_ru = false;
        bool use_gcc = false;
        std::string target = "";
    };

    struct NativeCodegenResult {
        bool success = false;
        std::string error_message;
        std::string ir_dump_initial;
        std::string ir_dump_optimized;
        CompiledUnit compiled_unit;
        std::string obj_file_created;
        std::string bin_file_created;
        std::string exe_file_created;
    };

    class NativeCompiler {
    public:
        explicit NativeCompiler(const NativeCodegenOptions& options = {});

        // Subsystem delegation hooks
        void set_oop_delegate(std::shared_ptr<INativeOOPDelegate> delegate) { oop_delegate_ = std::move(delegate); }
        void set_memory_delegate(std::shared_ptr<INativeMemoryDelegate> delegate) { memory_delegate_ = std::move(delegate); }
        void set_stdlib_delegate(std::shared_ptr<INativeStdlibDelegate> delegate) { stdlib_delegate_ = std::move(delegate); }

        std::shared_ptr<INativeOOPDelegate> get_oop_delegate() const { return oop_delegate_; }
        std::shared_ptr<INativeMemoryDelegate> get_memory_delegate() const { return memory_delegate_; }
        std::shared_ptr<INativeStdlibDelegate> get_stdlib_delegate() const { return stdlib_delegate_; }

        // Full compilation pipeline entry point
        NativeCodegenResult compile(const std::vector<GrassToken>& tokens, const std::string& source_path = "");
        NativeCodegenResult compile_source(const std::string& source_code, const std::string& source_path = "");
        NativeCodegenResult compile_file(const std::string& file_path);

        // Individual pipeline stages
        bool lower_ast_to_ir(const std::vector<GrassToken>& tokens,
                             ir::IRModule& module,
                             std::vector<uint8_t>& rodata,
                             std::unordered_map<std::string, uint64_t>& rodata_symbols,
                             std::string& error,
                             const std::string& source_path = "");

        bool optimize_ir(ir::IRModule& module, int opt_level, std::string& error);

        bool codegen_module(ir::IRModule& module,
                            const std::vector<uint8_t>& rodata,
                            const std::unordered_map<std::string, uint64_t>& rodata_symbols,
                            CompiledUnit& unit,
                            std::string& error);

        bool emit_containers(const CompiledUnit& unit,
                             const std::string& base_name,
                             NativeCodegenResult& res,
                             std::string& error);

        bool link_executable(const std::string& obj_path,
                             const std::string& exe_path,
                             std::string& error);
        bool link_executable(const std::string& obj_path,
                             const std::string& exe_path,
                             const CompiledUnit& unit,
                             std::string& error);

        static TargetArch parse_arch(const std::string& str);
        static TargetABI parse_abi(const std::string& str);
        static std::string arch_to_string(TargetArch arch);
        static std::string abi_to_string(TargetABI abi);

    private:
        NativeCodegenOptions options_;
        std::shared_ptr<INativeOOPDelegate> oop_delegate_;
        std::shared_ptr<INativeMemoryDelegate> memory_delegate_;
        std::shared_ptr<INativeStdlibDelegate> stdlib_delegate_;

        bool ensure_runtime_c_file(std::string& runtime_path, std::string& error);
    };

} // namespace grass::codegen

#endif // GRASS_NATIVE_CODEGEN_H

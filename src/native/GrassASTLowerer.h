#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include "native/GrassNativeCodegen.h"
#include "native/GrassNativeOOP.h"
#include "native/GrassNativeMemory.h"
#include "native/GrassNativeStdlib.h"

namespace grass::codegen {

    class ASTLowerer {
    private:
        const std::vector<GrassToken>& tokens_;
        size_t pos_ = 0;
        ir::IRModule& module_;
        ir::IRContext& ctx_;
        ir::IRBuilder builder_;

        std::vector<uint8_t>& rodata_;
        std::unordered_map<std::string, uint64_t>& rodata_symbols_;
        uint32_t string_counter_ = 0;
        std::string source_path_;
        bool freestanding_ = false;

        INativeOOPDelegate* oop_delegate_ = nullptr;
        INativeMemoryDelegate* memory_delegate_ = nullptr;
        INativeStdlibDelegate* stdlib_delegate_ = nullptr;
        native::GrassNativeOOP* oop_mgr_ = nullptr;
        native::GrassNativeMemory* mem_mgr_ = nullptr;

        struct VarBinding {
            ir::ValueId alloca_id = 0;
            ir::IRType* type = nullptr;
            std::string struct_type_name;
            bool is_raii = false;
            bool is_vec = false;
            ir::IRType* vec_elem_type = nullptr;
            std::string vec_elem_struct;
            bool is_array = false;
            ir::IRType* array_elem_type = nullptr;
            std::string array_elem_struct;
            bool is_global = false;
            std::string global_sym;
        };
        struct ScopeFrame {
            std::unordered_map<std::string, VarBinding> vars;
            std::vector<std::string> raii_vars;
        };
        std::vector<ScopeFrame> scopes_;
        std::unordered_map<ir::ValueId, ir::IRType*> expr_array_elem_types_;
        std::unordered_map<ir::ValueId, std::string> expr_array_elem_structs_;

        struct GlobalDeclSpec {
            std::string global_sym;
            std::string var_name;
            std::string type_name;
            bool is_explicit_ptr = false;
            ir::IRType* decl_type = nullptr;
            std::string struct_name;
            bool has_init = false;
            size_t init_start_pos = 0;
            size_t init_end_pos = 0;
        };
        std::vector<GlobalDeclSpec> global_decls_;
        std::unordered_map<std::string, VarBinding> global_vars_;
        bool globals_initialized_ = false;

        struct LoopContext {
            ir::BasicBlock* b_step = nullptr;
            ir::BasicBlock* b_exit = nullptr;
            size_t break_scope_depth = 0;
            size_t continue_scope_depth = 0;
        };
        std::vector<LoopContext> loop_stack_;
        bool in_stream_io_ = false;
        std::string last_struct_name_;
        std::string current_dtor_struct_;
        bool last_expr_is_borrowed_ptr_ = false;
        struct VecMeta {
            ir::IRType* vec_struct_type = nullptr;
            ir::IRType* elem_type = nullptr;
            std::string elem_struct;
        };
        std::unordered_map<std::string, VecMeta> vec_struct_meta_;

        std::vector<std::string> namespace_stack_;
        std::vector<std::string> using_namespaces_;
        std::unordered_map<std::string, std::string> using_symbols_;

        std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> enum_defs_;
        std::unordered_map<std::string, int64_t> unscoped_enum_vals_;
        std::string current_class_context_;
        std::unordered_map<std::string, std::string> class_base_names_;
        struct FieldDefaultInit {
            std::string field_name;
            size_t expr_start = 0;
            size_t expr_end = 0;
        };
        std::unordered_map<std::string, std::vector<FieldDefaultInit>> class_field_defaults_;

        std::string current_namespace() const;
        std::string qualify_name(const std::string& name) const;
        std::string resolve_callee(const std::string& callee) const;
        const GrassToken& peek(int offset = 0) const;
        GrassToken advance();
        bool match(GrassTokenKind kind);
        bool match_ident(const std::string& name);
        bool consume(GrassTokenKind kind, const std::string& err_msg, std::string& error);
        void push_scope();
        void emit_var_dtor(const VarBinding& b);
        void emit_struct_field_dtors(ir::ValueId this_ptr, const std::string& struct_name);
        void emit_scope_cleanups(size_t frame_idx);
        void emit_scopes_cleanup_to(size_t target_depth);
        void pop_scope(bool emit_cleanups = true);
        bool declare_var(const std::string& name, ir::ValueId alloca_id, ir::IRType* type,
                         const std::string& struct_name = "", bool is_raii = false,
                         bool is_vec = false, ir::IRType* vec_elem_t = nullptr, const std::string& vec_elem_struct = "",
                         bool is_array = false, ir::IRType* arr_elem_t = nullptr, const std::string& arr_elem_struct = "");
        const VarBinding* lookup_var(const std::string& name) const;
        VarBinding* lookup_var_mut(const std::string& name);
        ir::ValueId var_addr(const VarBinding* var);
        ir::IRType* predict_array_elem_from_tokens(const std::vector<GrassToken>& toks, size_t start, size_t end);
        ir::ValueId encode_array_slot(ir::ValueId val, ir::IRType* target_elem_t);
        ir::ValueId decode_array_slot(ir::ValueId raw_i64, ir::IRType* elem_t, const std::string& elem_struct = "");
        void emit_zero_struct(ir::ValueId struct_alloca, ir::IRType* st_type);
        bool is_vec_type_start(int offset = 0) const;
        static uint64_t get_ir_type_size_bytes(ir::IRType* t);
        static std::string mangle_ir_type_key(ir::IRType* t, const std::string& elem_struct);
        ir::IRType* get_or_create_vec_ir_type(ir::IRType* elem_t, const std::string& elem_struct, std::string& out_sname);
        ir::IRType* parse_vec_ir_type(ir::IRType*& out_elem_t, std::string& out_elem_struct, std::string& out_vec_sname);
        void emit_vec_elem_dtor(ir::ValueId elem_ptr, const std::string& elem_struct);
        ir::ValueId emit_vec_size(ir::ValueId vec_ptr);
        ir::ValueId emit_vec_capacity(ir::ValueId vec_ptr);
        ir::ValueId emit_vec_empty(ir::ValueId vec_ptr);
        ir::ValueId emit_vec_data(ir::ValueId vec_ptr, ir::IRType* elem_t);
        void emit_vec_reserve(ir::ValueId vec_ptr, ir::IRType* elem_t, ir::ValueId new_cap);
        void emit_vec_push(ir::ValueId vec_ptr, ir::IRType* elem_t, ir::ValueId val);
        void emit_vec_pop(ir::ValueId vec_ptr, ir::IRType* elem_t, const std::string& elem_struct);
        void emit_vec_clear(ir::ValueId vec_ptr, ir::IRType* elem_t, const std::string& elem_struct);
        void emit_vec_dtor(ir::ValueId vec_ptr, ir::IRType* vec_st, ir::IRType* elem_t, const std::string& elem_struct);
        void emit_copy_struct(ir::ValueId dst_ptr, ir::ValueId src_ptr, ir::IRType* st_type);
        std::string add_rodata_string(const std::string& str);
        ir::ValueId coerce_type(ir::ValueId val, ir::IRType* target_type);
        void match_types(ir::ValueId& lhs, ir::ValueId& rhs);
    public:
        ASTLowerer(const std::vector<GrassToken>& tokens,
                   ir::IRModule& module,
                   std::vector<uint8_t>& rodata,
                   std::unordered_map<std::string, uint64_t>& rodata_symbols,
                   const std::string& source_path = "",
                   bool freestanding = false,
                   INativeOOPDelegate* oop_del = nullptr,
                   INativeMemoryDelegate* mem_del = nullptr,
                   INativeStdlibDelegate* stdlib_del = nullptr,
                   native::GrassNativeOOP* oop_mgr = nullptr,
                   native::GrassNativeMemory* mem_mgr = nullptr);
        bool lower_declaration(std::string& error);
        bool lower(std::string& error);
    private:
        static bool is_type_or_func_keyword(const std::string& s);
        static bool is_var_keyword(const std::string& s);
        bool is_known_user_type(const std::string& name) const;
        ir::IRType* parse_type_name(const std::string& name);
        bool parse_enum_definition(std::string& error);
        void emit_ctor_call(ir::ValueId obj_ptr, const std::string& class_name, const std::vector<ir::ValueId>& args);
        bool parse_struct_definition(std::string& error);
        bool parse_function(std::string& error);
        bool parse_statement(std::string& error);
        ir::ValueId parse_expression(std::string& error);
        ir::ValueId parse_assignment(std::string& error);
        ir::ValueId parse_logical_or(std::string& error);
        ir::ValueId parse_logical_and(std::string& error);
        ir::ValueId parse_bitwise_or(std::string& error);
        ir::ValueId parse_bitwise_xor(std::string& error);
        ir::ValueId parse_bitwise_and(std::string& error);
        ir::ValueId parse_equality(std::string& error);
        ir::ValueId parse_relational(std::string& error);
        ir::ValueId parse_shift(std::string& error);
        ir::ValueId parse_additive(std::string& error);
        ir::ValueId parse_multiplicative(std::string& error);
        ir::ValueId parse_unary(std::string& error);
        ir::ValueId parse_postfix(std::string& error);
        ir::ValueId parse_primary(std::string& error);
    };

} // namespace grass::codegen

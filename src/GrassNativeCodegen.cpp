#include "GrassNativeCodegen.h"
#include "GrassImport.h"
#include "GrassSema.h"
#include "GrassNativeOOP.h"
#include "GrassNativeMemory.h"
#include "GrassNativeStdlib.h"

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

namespace grass::codegen {

    // =========================================================================
    // 1. Embedded C Runtime Source for Native Binaries
    // =========================================================================

    static const char* kEmbeddedRuntimeC = R"(/* GrassGreen Standalone Native Runtime (Zero Dependencies) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
static LARGE_INTEGER g_perf_freq;
static LARGE_INTEGER g_perf_start;
static LARGE_INTEGER g_perf_stop;
static double g_elapsed_ms = 0.0;
static char g_conclus_buf[64];
static char g_date_buf[32];
static char g_time_buf[32];

void start_time(void) {
    QueryPerformanceFrequency(&g_perf_freq);
    QueryPerformanceCounter(&g_perf_start);
}

double stop_time(void) {
    QueryPerformanceCounter(&g_perf_stop);
    if (g_perf_freq.QuadPart != 0) {
        g_elapsed_ms = (double)(g_perf_stop.QuadPart - g_perf_start.QuadPart) * 1000.0 / (double)g_perf_freq.QuadPart;
    } else {
        g_elapsed_ms = 0.0;
    }
    return g_elapsed_ms;
}

const char* conclus_time(void) {
    snprintf(g_conclus_buf, sizeof(g_conclus_buf), "%.2f ms", g_elapsed_ms);
    return g_conclus_buf;
}

const char* get_date(void) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(g_date_buf, sizeof(g_date_buf), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return g_date_buf;
}

const char* get_time_str(void) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(g_time_buf, sizeof(g_time_buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return g_time_buf;
}
#else
#include <sys/time.h>
static struct timespec g_perf_start;
static struct timespec g_perf_stop;
static double g_elapsed_ms = 0.0;
static char g_conclus_buf[64];
static char g_date_buf[32];
static char g_time_buf[32];

void start_time(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_perf_start);
}

double stop_time(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_perf_stop);
    g_elapsed_ms = (g_perf_stop.tv_sec - g_perf_start.tv_sec) * 1000.0 +
                   (g_perf_stop.tv_nsec - g_perf_start.tv_nsec) / 1000000.0;
    return g_elapsed_ms;
}

const char* conclus_time(void) {
    snprintf(g_conclus_buf, sizeof(g_conclus_buf), "%.2f ms", g_elapsed_ms);
    return g_conclus_buf;
}

const char* get_date(void) {
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    strftime(g_date_buf, sizeof(g_date_buf), "%Y-%m-%d", tm_info);
    return g_date_buf;
}

const char* get_time_str(void) {
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    strftime(g_time_buf, sizeof(g_time_buf), "%H:%M:%S", tm_info);
    return g_time_buf;
}
#endif

void grass_print_int(int64_t v) {
    printf("%lld", (long long)v);
}

void grass_print_double(double v) {
    if (floor(v) == v && fabs(v) < 1e15) {
        printf("%lld", (long long)v);
    } else {
        printf("%g", v);
    }
}

void grass_print_str(const char* s) {
    if (s) fputs(s, stdout);
}

void grass_print_newline(void) {
    putchar('\n');
}

void grass_print_bool(int64_t b) {
    fputs(b ? "true" : "false", stdout);
}

/* FastMath implementations */
double discor(double x) {
    return (x >= 0.0) ? sqrt(x) : 0.0;
}

double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

/* Dynamic Array Runtime */
typedef struct {
    int64_t* data;
    size_t size;
    size_t capacity;
} GrassDynArray;

void* grass_array_create(void) {
    GrassDynArray* arr = (GrassDynArray*)malloc(sizeof(GrassDynArray));
    if (!arr) return NULL;
    arr->size = 0;
    arr->capacity = 8;
    arr->data = (int64_t*)malloc(arr->capacity * sizeof(int64_t));
    return arr;
}

void grass_array_push(void* ptr, int64_t val) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    if (!arr) return;
    if (arr->size >= arr->capacity) {
        arr->capacity *= 2;
        arr->data = (int64_t*)realloc(arr->data, arr->capacity * sizeof(int64_t));
    }
    arr->data[arr->size++] = val;
}

int64_t grass_array_get(void* ptr, int64_t idx) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    if (!arr || idx < 0 || (size_t)idx >= arr->size) return 0;
    return arr->data[idx];
}

void grass_array_set(void* ptr, int64_t idx, int64_t val) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    if (!arr || idx < 0 || (size_t)idx >= arr->size) return;
    arr->data[idx] = val;
}

int64_t grass_array_len(void* ptr) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    return arr ? (int64_t)arr->size : 0;
}
)";

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
    // 3. AST Lowering: Tokens -> GrassIR::IRModule
    // =========================================================================

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
        };
        std::vector<std::unordered_map<std::string, VarBinding>> scopes_;

        struct LoopContext {
            ir::BasicBlock* b_step = nullptr;
            ir::BasicBlock* b_exit = nullptr;
        };
        std::vector<LoopContext> loop_stack_;
        bool in_stream_io_ = false;
        std::string last_struct_name_;

        const GrassToken& peek(int offset = 0) const {
            size_t idx = pos_ + offset;
            if (idx >= tokens_.size()) return tokens_.back();
            return tokens_[idx];
        }

        GrassToken advance() {
            if (pos_ < tokens_.size()) {
                const auto& tok = tokens_[pos_++];
                builder_.set_current_loc(ir::SourceLocation(source_path_, tok.line, tok.col));
                return tok;
            }
            return tokens_.back();
        }

        bool match(GrassTokenKind kind) {
            if (peek().kind == kind) {
                advance();
                return true;
            }
            return false;
        }

        bool match_ident(const std::string& name) {
            if (peek().kind == TOK_IDENT && peek().text == name) {
                advance();
                return true;
            }
            return false;
        }

        bool consume(GrassTokenKind kind, const std::string& err_msg, std::string& error) {
            if (match(kind)) return true;
            error = "Line " + std::to_string(peek().line) + ": " + err_msg;
            return false;
        }

        void push_scope() {
            scopes_.emplace_back();
        }

        void pop_scope() {
            if (!scopes_.empty()) scopes_.pop_back();
        }

        bool declare_var(const std::string& name, ir::ValueId alloca_id, ir::IRType* type, const std::string& struct_name = "") {
            if (scopes_.empty()) push_scope();
            scopes_.back()[name] = { alloca_id, type, struct_name };
            return true;
        }

        const VarBinding* lookup_var(const std::string& name) const {
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                auto f = it->find(name);
                if (f != it->end()) return &f->second;
            }
            return nullptr;
        }

        std::string add_rodata_string(const std::string& str) {
            std::string sym_name = ".Lstr_" + std::to_string(string_counter_++);
            uint64_t offset = rodata_.size();
            rodata_.insert(rodata_.end(), str.begin(), str.end());
            rodata_.push_back('\0'); // null terminator
            rodata_symbols_[sym_name] = offset;
            return sym_name;
        }

        ir::ValueId coerce_type(ir::ValueId val, ir::IRType* target_type) {
            if (!val || !target_type) return val;
            if (!builder_.get_function()->has_value(val)) return val;
            ir::IRType* src_type = builder_.get_function()->get_value(val).type;
            if (!src_type || src_type->equals(target_type)) return val;

            if (src_type->is_integer() && target_type->is_integer()) {
                uint32_t src_bits = src_type->bit_width();
                uint32_t tgt_bits = target_type->bit_width();
                if (src_bits > tgt_bits) {
                    return builder_.create_trunc(val, target_type);
                } else if (src_bits < tgt_bits) {
                    return builder_.create_sext(val, target_type);
                }
                return val;
            }

            if (src_type->is_integer() && target_type->is_floating_point()) {
                return builder_.create_sitofp(val, target_type);
            }

            if (src_type->is_floating_point() && target_type->is_integer()) {
                return builder_.create_fptosi(val, target_type);
            }

            return val;
        }

        void match_types(ir::ValueId& lhs, ir::ValueId& rhs) {
            if (!lhs || !rhs) return;
            if (!builder_.get_function()->has_value(lhs) || !builder_.get_function()->has_value(rhs)) return;
            ir::IRType* t1 = builder_.get_function()->get_value(lhs).type;
            ir::IRType* t2 = builder_.get_function()->get_value(rhs).type;
            if (!t1 || !t2 || t1->equals(t2)) return;

            if (t1->is_floating_point() || t2->is_floating_point()) {
                ir::IRType* target = (t1->is_f64() || t2->is_f64()) ? ctx_.get_f64_type() : ctx_.get_f32_type();
                lhs = coerce_type(lhs, target);
                rhs = coerce_type(rhs, target);
            } else if (t1->is_integer() && t2->is_integer()) {
                uint32_t w1 = t1->bit_width();
                uint32_t w2 = t2->bit_width();
                if (w1 > w2) {
                    rhs = coerce_type(rhs, t1);
                } else {
                    lhs = coerce_type(lhs, t2);
                }
            }
        }

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
                   native::GrassNativeMemory* mem_mgr = nullptr)
            : tokens_(tokens), module_(module), ctx_(module.context),
              rodata_(rodata), rodata_symbols_(rodata_symbols),
              source_path_(source_path), freestanding_(freestanding),
              oop_delegate_(oop_del), memory_delegate_(mem_del), stdlib_delegate_(stdlib_del),
              oop_mgr_(oop_mgr), mem_mgr_(mem_mgr) {}

        bool lower(std::string& error) {
            while (peek().kind != TOK_EOF) {
                if (peek().kind == TOK_LIB) {
                    advance(); // skip library directive e.g. !GrassGreen!, !time!
                    continue;
                }
                if (peek().kind == TOK_SEMI) {
                    advance();
                    continue;
                }
                if (peek().kind == TOK_IDENT && peek().text == "struct") {
                    if (!parse_struct_definition(error)) return false;
                    continue;
                }
                if (is_type_or_func_keyword(peek().text) &&
                    peek(1).kind == TOK_IDENT && peek(2).kind == TOK_LPAREN) {
                    if (!parse_function(error)) return false;
                    continue;
                }
                // Skip unhandled top-level tokens gracefully
                advance();
            }
            return true;
        }

    private:
        static bool is_type_or_func_keyword(const std::string& s) {
            return s == "int" || s == "take" || s == "void" || s == "fn" ||
                   s == "double" || s == "float" || s == "string" || s == "bool" ||
                   s == "i8" || s == "i16" || s == "i32" || s == "i64";
        }

        static bool is_var_keyword(const std::string& s) {
            return s == "take" || s == "int" || s == "double" || s == "float" ||
                   s == "string" || s == "bool" || s == "set" || s == "array" ||
                   s == "i8" || s == "i16" || s == "i32" || s == "i64";
        }

        ir::IRType* parse_type_name(const std::string& name) {
            if (name == "void") return ctx_.get_void_type();
            if (name == "bool") return ctx_.get_i1_type();
            if (name == "i8") return ctx_.get_i8_type();
            if (name == "i16") return ctx_.get_i16_type();
            if (name == "i32" || name == "int") return ctx_.get_i32_type();
            if (name == "i64" || name == "take") return ctx_.get_i64_type();
            if (name == "float") return ctx_.get_f32_type();
            if (name == "double") return ctx_.get_f64_type();
            if (name == "string") return ctx_.get_ptr_type(ctx_.get_i8_type());
            if (name == "array") return ctx_.get_ptr_type(ctx_.get_void_type());
            return ctx_.get_i64_type();
        }

        struct PendingMethod {
            std::string name;
            ir::IRType* ret_type = nullptr;
            std::vector<ir::IRType*> param_types;
            std::vector<std::string> param_names;
            size_t body_start_pos = 0;
            size_t body_end_pos = 0;
        };

        bool parse_struct_definition(std::string& error) {
            advance(); // consume 'struct'
            if (peek().kind != TOK_IDENT) {
                error = "Line " + std::to_string(peek().line) + ": Expected struct name!";
                return false;
            }
            std::string struct_name = advance().text;
            if (!consume(TOK_LBRACE, "Expected '{' after struct name!", error)) return false;

            if (oop_mgr_) {
                oop_mgr_->begin_struct(struct_name);
            }

            std::vector<ir::IRType*> field_types;
            std::vector<std::string> field_names;
            std::vector<PendingMethod> pending_methods;

            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (peek().kind == TOK_IDENT && is_var_keyword(peek().text)) {
                    std::string ftype_str = advance().text;
                    ir::IRType* ftype = parse_type_name(ftype_str);
                    if (peek().kind != TOK_IDENT) {
                        error = "Line " + std::to_string(peek().line) + ": Expected field or method name!";
                        return false;
                    }
                    std::string mname = advance().text;

                    if (peek().kind == TOK_LPAREN) {
                        advance(); // '('
                        std::vector<ir::IRType*> param_types;
                        std::vector<std::string> param_names;
                        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                            while (true) {
                                ir::IRType* ptype = ctx_.get_i64_type();
                                if (is_type_or_func_keyword(peek().text)) {
                                    ptype = parse_type_name(advance().text);
                                }
                                if (peek().kind != TOK_IDENT) {
                                    error = "Line " + std::to_string(peek().line) + ": Expected parameter name!";
                                    return false;
                                }
                                std::string pname = advance().text;
                                param_types.push_back(ptype);
                                param_names.push_back(pname);
                                if (!match(TOK_COMMA)) break;
                            }
                        }
                        if (!consume(TOK_RPAREN, "Expected ')' closing method parameters!", error)) return false;
                        if (!consume(TOK_LBRACE, "Expected '{' at start of method body!", error)) return false;

                        size_t body_start = pos_;
                        int brace_depth = 1;
                        while (brace_depth > 0 && peek().kind != TOK_EOF) {
                            if (peek().kind == TOK_LBRACE) brace_depth++;
                            else if (peek().kind == TOK_RBRACE) {
                                brace_depth--;
                                if (brace_depth == 0) break;
                            }
                            advance();
                        }
                        size_t body_end = pos_;
                        consume(TOK_RBRACE, "Expected '}' closing method body!", error);

                        pending_methods.push_back({ mname, ftype, param_types, param_names, body_start, body_end });
                        if (oop_mgr_) {
                            oop_mgr_->add_method(mname, ftype, param_types, param_names);
                        }
                    } else {
                        match(TOK_SEMI);
                        if (mname == "name" || mname == "title" || mname == "msg" || mname == "text" || mname == "path" || mname == "str") {
                            ftype = ctx_.get_ptr_type(ctx_.get_i8_type());
                        }
                        field_types.push_back(ftype);
                        field_names.push_back(mname);
                        if (oop_mgr_) {
                            oop_mgr_->add_field(mname, ftype);
                        }
                    }
                } else {
                    advance();
                }
            }
            consume(TOK_RBRACE, "Expected '}' closing struct definition!", error);
            match(TOK_SEMI);

            ctx_.get_struct_type(struct_name, field_types, field_names);
            if (oop_mgr_) {
                oop_mgr_->finalize_class();
            }

            for (const auto& pm : pending_methods) {
                ir::IRFunction* fn = nullptr;
                if (oop_mgr_) {
                    fn = oop_mgr_->create_method_function(struct_name, pm.name);
                }
                if (!fn) {
                    fn = module_.create_function(struct_name + "_" + pm.name, pm.ret_type);
                    ir::IRType* this_t = ctx_.get_ptr_type(ctx_.get_struct_type(struct_name, field_types, field_names));
                    fn->add_argument("this", this_t);
                    for (size_t i = 0; i < pm.param_types.size(); ++i) {
                        fn->add_argument(pm.param_names[i], pm.param_types[i]);
                    }
                }

                ir::IRFunction* prev_fn = builder_.get_function();
                builder_.set_function(fn);
                scopes_.clear();
                push_scope();

                ir::BasicBlock* entry_bb = builder_.create_block("entry");
                builder_.set_insert_point(entry_bb);

                ir::ValueId this_alloca = builder_.create_alloca(fn->arguments[0].type, 1, 0, "this");
                builder_.create_store(fn->arguments[0].id, this_alloca);
                declare_var("this", this_alloca, fn->arguments[0].type, struct_name);

                for (size_t i = 1; i < fn->arguments.size(); ++i) {
                    ir::ValueId p_alloca = builder_.create_alloca(fn->arguments[i].type, 1, 0, fn->arguments[i].name);
                    builder_.create_store(fn->arguments[i].id, p_alloca);
                    declare_var(fn->arguments[i].name, p_alloca, fn->arguments[i].type);
                }

                size_t saved_pos = pos_;
                pos_ = pm.body_start_pos;
                while (pos_ < pm.body_end_pos && peek().kind != TOK_EOF) {
                    if (!parse_statement(error)) {
                        pos_ = saved_pos;
                        return false;
                    }
                }
                pos_ = saved_pos;

                ir::BasicBlock* cur_bb = builder_.get_insert_block();
                if (cur_bb && !cur_bb->has_terminator()) {
                    if (pm.ret_type->is_void()) {
                        builder_.create_ret_void();
                    } else {
                        ir::ValueId def_ret = builder_.create_const_int(pm.ret_type, 0);
                        builder_.create_ret(def_ret);
                    }
                }

                pop_scope();
                fn->rebuild_cfg();
                builder_.set_function(prev_fn);
            }

            return true;
        }

        bool parse_function(std::string& error) {
            std::string ret_type_name = advance().text;
            ir::IRType* ret_type = parse_type_name(ret_type_name);

            std::string fn_name = advance().text;
            ir::IRFunction* fn = module_.create_function(fn_name, ret_type);

            builder_.set_function(fn);
            scopes_.clear();
            push_scope();

            if (!consume(TOK_LPAREN, "Expected '(' in function signature!", error)) return false;

            struct ParamInfo {
                std::string name;
                ir::IRType* type;
            };
            std::vector<ParamInfo> params;

            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                while (true) {
                    ir::IRType* ptype = ctx_.get_i64_type();
                    if (is_type_or_func_keyword(peek().text)) {
                        ptype = parse_type_name(advance().text);
                    }
                    if (peek().kind != TOK_IDENT) {
                        error = "Line " + std::to_string(peek().line) + ": Expected parameter name!";
                        return false;
                    }
                    std::string pname = advance().text;
                    params.push_back({ pname, ptype });
                    fn->add_argument(pname, ptype);
                    if (!match(TOK_COMMA)) break;
                }
            }
            if (!consume(TOK_RPAREN, "Expected ')' closing parameter list!", error)) return false;
            if (!consume(TOK_LBRACE, "Expected '{' at start of function body!", error)) return false;

            ir::BasicBlock* entry_bb = builder_.create_block("entry");
            builder_.set_insert_point(entry_bb);

            // Store arguments in stack allocas for SSA promotion
            for (size_t i = 0; i < params.size(); ++i) {
                ir::ValueId alloca_id = builder_.create_alloca(params[i].type, 1, 0, params[i].name);
                builder_.create_store(fn->arguments[i].id, alloca_id);
                declare_var(params[i].name, alloca_id, params[i].type);
            }

            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!parse_statement(error)) return false;
            }
            if (!consume(TOK_RBRACE, "Expected '}' closing function body!", error)) return false;

            // Ensure block ends with terminator
            ir::BasicBlock* current_bb = builder_.get_insert_block();
            if (current_bb && !current_bb->has_terminator()) {
                if (ret_type->is_void()) {
                    builder_.create_ret_void();
                } else {
                    ir::ValueId default_ret = builder_.create_const_int(ret_type, 0);
                    builder_.create_ret(default_ret);
                }
            }

            pop_scope();
            fn->rebuild_cfg();
            return true;
        }

        bool parse_statement(std::string& error) {
            if (match(TOK_SEMI)) return true;

            // Block { ... }
            if (match(TOK_LBRACE)) {
                push_scope();
                while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                    if (!parse_statement(error)) return false;
                }
                consume(TOK_RBRACE, "Expected '}' closing block!", error);
                pop_scope();
                return true;
            }

            // Stream Output: gout << expr << endl;
            if (match_ident("gout") || match_ident("cout")) {
                bool prev_io = in_stream_io_;
                in_stream_io_ = true;
                while (match(TOK_SHL)) {
                    if (match_ident("endl")) {
                        builder_.create_call("grass_print_newline", ctx_.get_void_type(), {});
                    } else {
                        ir::ValueId val = parse_expression(error);
                        if (!error.empty()) {
                            in_stream_io_ = prev_io;
                            return false;
                        }
                        if (val != 0) {
                            ir::IRType* vtype = builder_.get_function()->get_value(val).type;
                            if (vtype->is_pointer()) {
                                builder_.create_call("grass_print_str", ctx_.get_void_type(), { val });
                            } else if (vtype->is_floating_point()) {
                                builder_.create_call("grass_print_double", ctx_.get_void_type(), { val });
                            } else if (vtype->is_bool()) {
                                builder_.create_call("grass_print_bool", ctx_.get_void_type(), { val });
                            } else {
                                builder_.create_call("grass_print_int", ctx_.get_void_type(), { val });
                            }
                        }
                    }
                }
                in_stream_io_ = prev_io;
                match(TOK_SEMI);
                return true;
            }

            // Stream Input: gin >> var;
            if (match_ident("gin") || match_ident("cin")) {
                bool prev_io = in_stream_io_;
                in_stream_io_ = true;
                while (match(TOK_SHR)) {
                    if (peek().kind == TOK_IDENT) {
                        std::string var_name = advance().text;
                        const VarBinding* b = lookup_var(var_name);
                        if (!b) {
                            error = "Line " + std::to_string(peek().line) + ": Undeclared variable '" + var_name + "' in stream input!";
                            in_stream_io_ = prev_io;
                            return false;
                        }
                        ir::ValueId read_val = builder_.create_call("grass_read_int", ctx_.get_i64_type(), {});
                        if (b->type->is_i32()) {
                            read_val = builder_.create_trunc(read_val, ctx_.get_i32_type());
                        }
                        builder_.create_store(read_val, b->alloca_id);
                    }
                }
                in_stream_io_ = prev_io;
                match(TOK_SEMI);
                return true;
            }

            // Variable Declaration
            bool is_struct_decl = (oop_mgr_ && oop_mgr_->has_class(peek().text));
            if ((is_var_keyword(peek().text) || is_struct_decl) && peek(1).kind == TOK_IDENT) {
                std::string type_name = advance().text;
                ir::IRType* vtype = nullptr;
                std::string struct_name = "";
                if (is_struct_decl) {
                    struct_name = type_name;
                    const auto* cd = oop_mgr_->get_class(type_name);
                    vtype = cd ? cd->ir_struct_type : ctx_.get_i64_type();
                } else {
                    vtype = parse_type_name(type_name);
                }

                while (peek().kind == TOK_IDENT) {
                    std::string var_name = advance().text;

                    if (match(TOK_ASSIGN)) {
                        ir::ValueId init_val = parse_expression(error);
                        if (!init_val) return false;
                        if ((type_name == "take" || type_name == "var" || type_name == "set") && builder_.get_function()->has_value(init_val)) {
                            ir::IRType* actual_t = builder_.get_function()->get_value(init_val).type;
                            if (actual_t && !actual_t->is_void()) {
                                vtype = actual_t;
                            }
                        }
                        ir::ValueId alloca_id = builder_.create_alloca(vtype, 1, 0, var_name);
                        declare_var(var_name, alloca_id, vtype, struct_name);
                        init_val = coerce_type(init_val, vtype);
                        builder_.create_store(init_val, alloca_id);
                    } else {
                        ir::ValueId alloca_id = builder_.create_alloca(vtype, 1, 0, var_name);
                        declare_var(var_name, alloca_id, vtype, struct_name);
                        if (!is_struct_decl) {
                            ir::ValueId default_val = builder_.create_const_int(vtype, 0);
                            builder_.create_store(default_val, alloca_id);
                        }
                    }
                    if (!match(TOK_COMMA)) break;
                }
                match(TOK_SEMI);
                return true;
            }

            // If Statement
            if (match_ident("if") || match_ident("check")) {
                match(TOK_LPAREN);
                ir::ValueId cond = parse_expression(error);
                if (!cond) return false;
                match(TOK_RPAREN);

                ir::IRType* c_type = builder_.get_function()->get_value(cond).type;
                if (c_type && !c_type->is_bool()) {
                    ir::ValueId zero = builder_.create_const_int(c_type, 0);
                    cond = builder_.create_icmp_ne(cond, zero);
                }

                ir::BasicBlock* b_then = builder_.create_block("if_then");
                ir::BasicBlock* b_else = builder_.create_block("if_else");
                ir::BasicBlock* b_merge = builder_.create_block("if_merge");

                bool has_else = false;
                // Pre-check if else is coming
                // Emit branch
                builder_.create_branch(cond, b_then, b_else);

                // Then Block
                builder_.set_insert_point(b_then);
                if (!parse_statement(error)) return false;
                if (!builder_.get_insert_block()->has_terminator()) {
                    builder_.create_jump(b_merge);
                }

                // Else Block
                builder_.set_insert_point(b_else);
                if (match_ident("else")) {
                    has_else = true;
                    if (!parse_statement(error)) return false;
                }
                if (!builder_.get_insert_block()->has_terminator()) {
                    builder_.create_jump(b_merge);
                }

                if (!has_else) {
                    // Else block was just a jump to merge
                    b_else->name = "if_skip";
                }

                builder_.set_insert_point(b_merge);
                return true;
            }

            // While Loop
            if (match_ident("while")) {
                match(TOK_LPAREN);
                ir::BasicBlock* b_cond = builder_.create_block("while_cond");
                ir::BasicBlock* b_body = builder_.create_block("while_body");
                ir::BasicBlock* b_exit = builder_.create_block("while_exit");

                builder_.create_jump(b_cond);

                // Condition block
                builder_.set_insert_point(b_cond);
                ir::ValueId cond = parse_expression(error);
                if (!cond) return false;
                match(TOK_RPAREN);

                ir::IRType* c_type = builder_.get_function()->get_value(cond).type;
                if (c_type && !c_type->is_bool()) {
                    ir::ValueId zero = builder_.create_const_int(c_type, 0);
                    cond = builder_.create_icmp_ne(cond, zero);
                }
                builder_.create_branch(cond, b_body, b_exit);

                // Body block
                loop_stack_.push_back({ b_cond, b_exit });
                builder_.set_insert_point(b_body);
                if (!parse_statement(error)) return false;
                if (!builder_.get_insert_block()->has_terminator()) {
                    builder_.create_jump(b_cond);
                }
                loop_stack_.pop_back();

                builder_.set_insert_point(b_exit);
                return true;
            }

            // For Loop
            if (match_ident("for")) {
                if (!consume(TOK_LPAREN, "Expected '(' in for loop!", error)) return false;

                push_scope(); // for loop init scope
                if (peek().kind != TOK_SEMI) {
                    if (!parse_statement(error)) return false;
                } else {
                    advance(); // skip ';'
                }

                ir::BasicBlock* b_cond = builder_.create_block("for_cond");
                ir::BasicBlock* b_body = builder_.create_block("for_body");
                ir::BasicBlock* b_step = builder_.create_block("for_step");
                ir::BasicBlock* b_exit = builder_.create_block("for_exit");

                builder_.create_jump(b_cond);

                // Cond Block
                builder_.set_insert_point(b_cond);
                ir::ValueId cond = 0;
                if (peek().kind != TOK_SEMI) {
                    cond = parse_expression(error);
                    if (!error.empty()) return false;
                    ir::IRType* c_type = builder_.get_function()->get_value(cond).type;
                    if (c_type && !c_type->is_bool()) {
                        ir::ValueId zero = builder_.create_const_int(c_type, 0);
                        cond = builder_.create_icmp_ne(cond, zero);
                    }
                } else {
                    cond = builder_.create_const_i1(true);
                }
                match(TOK_SEMI);
                builder_.create_branch(cond, b_body, b_exit);

                // Step Tokens Range
                size_t step_start = pos_;
                while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                    advance();
                }
                size_t step_end = pos_;
                if (!consume(TOK_RPAREN, "Expected ')' closing for loop header!", error)) return false;

                // Body Block
                loop_stack_.push_back({ b_step, b_exit });
                builder_.set_insert_point(b_body);
                if (!parse_statement(error)) return false;
                if (!builder_.get_insert_block()->has_terminator()) {
                    builder_.create_jump(b_step);
                }
                loop_stack_.pop_back();

                // Step Block
                builder_.set_insert_point(b_step);
                if (step_start < step_end) {
                    size_t saved_pos = pos_;
                    pos_ = step_start;
                    std::string step_err;
                    parse_expression(step_err);
                    if (!step_err.empty()) {
                        error = step_err;
                        return false;
                    }
                    pos_ = saved_pos;
                }
                builder_.create_jump(b_cond);

                pop_scope();
                builder_.set_insert_point(b_exit);
                return true;
            }

            // Break / Continue
            if (match_ident("break") || match_ident("stop")) {
                match(TOK_SEMI);
                if (loop_stack_.empty()) {
                    error = "Break statement outside loop!";
                    return false;
                }
                builder_.create_jump(loop_stack_.back().b_exit);
                return true;
            }

            if (match_ident("continue") || match_ident("next")) {
                match(TOK_SEMI);
                if (loop_stack_.empty()) {
                    error = "Continue statement outside loop!";
                    return false;
                }
                builder_.create_jump(loop_stack_.back().b_step);
                return true;
            }

            // Return Statement
            if (match_ident("return")) {
                if (peek().kind != TOK_SEMI) {
                    ir::ValueId ret_val = parse_expression(error);
                    if (!error.empty()) return false;
                    ir::IRType* exp_t = builder_.get_function()->return_type;
                    if (exp_t && !exp_t->is_void()) {
                        ret_val = coerce_type(ret_val, exp_t);
                    }
                    builder_.create_ret(ret_val);
                } else {
                    builder_.create_ret_void();
                }
                match(TOK_SEMI);
                return true;
            }

            // Expression Statement / Assignment / Function Call
            std::string expr_err;
            parse_expression(expr_err);
            if (!expr_err.empty()) {
                error = expr_err;
                return false;
            }
            match(TOK_SEMI);
            return true;
        }

        ir::ValueId parse_expression(std::string& error) {
            return parse_assignment(error);
        }

        ir::ValueId parse_assignment(std::string& error) {
            // Check for member assignment: obj.field = expr or obj.field += expr
            if (peek().kind == TOK_IDENT && peek(1).kind == TOK_DOT && peek(2).kind == TOK_IDENT &&
                (peek(3).kind == TOK_ASSIGN || peek(3).kind == TOK_PLUS_ASSIGN ||
                 peek(3).kind == TOK_MINUS_ASSIGN || peek(3).kind == TOK_MUL_ASSIGN ||
                 peek(3).kind == TOK_DIV_ASSIGN)) {
                std::string obj_name = advance().text;
                advance(); // consume '.'
                std::string field_name = advance().text;
                GrassTokenKind op = advance().kind;

                const VarBinding* obj_var = lookup_var(obj_name);
                if (!obj_var) {
                    error = "Undeclared object: " + obj_name;
                    return 0;
                }

                ir::ValueId inst_ptr = obj_var->alloca_id;
                if (obj_var->type && obj_var->type->is_pointer()) {
                    inst_ptr = builder_.create_load(obj_var->alloca_id, obj_name + "_ptr");
                }

                std::string sname = obj_var->struct_type_name;
                ir::ValueId rhs = parse_assignment(error);
                if (!rhs) return 0;

                if (op == TOK_ASSIGN) {
                    if (oop_mgr_) {
                        oop_mgr_->emit_store_field(builder_, inst_ptr, sname, field_name, rhs);
                    }
                    return rhs;
                } else {
                    ir::ValueId cur = 0;
                    if (oop_mgr_) {
                        cur = oop_mgr_->emit_load_field(builder_, inst_ptr, sname, field_name, field_name + "_val");
                    }
                    match_types(cur, rhs);
                    ir::ValueId computed = 0;
                    if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                    else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                    else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                    else if (op == TOK_DIV_ASSIGN) computed = builder_.create_sdiv(cur, rhs);

                    if (oop_mgr_) {
                        oop_mgr_->emit_store_field(builder_, inst_ptr, sname, field_name, computed);
                    }
                    return computed;
                }
            }

            // Check for assignment: identifier = expr
            if (peek().kind == TOK_IDENT &&
                (peek(1).kind == TOK_ASSIGN || peek(1).kind == TOK_PLUS_ASSIGN ||
                 peek(1).kind == TOK_MINUS_ASSIGN || peek(1).kind == TOK_MUL_ASSIGN ||
                 peek(1).kind == TOK_DIV_ASSIGN)) {
                std::string var_name = advance().text;
                GrassTokenKind op = advance().kind;

                const VarBinding* var = lookup_var(var_name);
                if (!var) {
                    error = "Undeclared variable: " + var_name;
                    return 0;
                }

                ir::ValueId rhs = parse_assignment(error);
                if (!rhs) return 0;

                if (op == TOK_ASSIGN) {
                    rhs = coerce_type(rhs, var->type);
                    builder_.create_store(rhs, var->alloca_id);
                    return rhs;
                } else {
                    ir::ValueId cur = builder_.create_load(var->alloca_id, var_name);
                    match_types(cur, rhs);
                    ir::ValueId computed = 0;
                    if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                    else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                    else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                    else if (op == TOK_DIV_ASSIGN) computed = builder_.create_sdiv(cur, rhs);

                    computed = coerce_type(computed, var->type);
                    builder_.create_store(computed, var->alloca_id);
                    return computed;
                }
            }

            return parse_logical_or(error);
        }

        ir::ValueId parse_logical_or(std::string& error) {
            ir::ValueId lhs = parse_logical_and(error);
            if (!lhs) return 0;

            while (match(TOK_OR)) {
                ir::ValueId rhs = parse_logical_and(error);
                if (!rhs) return 0;
                lhs = builder_.create_or(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_logical_and(std::string& error) {
            ir::ValueId lhs = parse_bitwise_or(error);
            if (!lhs) return 0;

            while (match(TOK_AND)) {
                ir::ValueId rhs = parse_bitwise_or(error);
                if (!rhs) return 0;
                lhs = builder_.create_and(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_bitwise_or(std::string& error) {
            ir::ValueId lhs = parse_bitwise_xor(error);
            if (!lhs) return 0;

            while (match(TOK_PIPE)) {
                ir::ValueId rhs = parse_bitwise_xor(error);
                if (!rhs) return 0;
                lhs = builder_.create_or(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_bitwise_xor(std::string& error) {
            ir::ValueId lhs = parse_bitwise_and(error);
            if (!lhs) return 0;

            while (match(TOK_CARET)) {
                ir::ValueId rhs = parse_bitwise_and(error);
                if (!rhs) return 0;
                lhs = builder_.create_xor(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_bitwise_and(std::string& error) {
            ir::ValueId lhs = parse_equality(error);
            if (!lhs) return 0;

            while (match(TOK_AMP)) {
                ir::ValueId rhs = parse_equality(error);
                if (!rhs) return 0;
                lhs = builder_.create_and(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_equality(std::string& error) {
            ir::ValueId lhs = parse_relational(error);
            if (!lhs) return 0;

            while (peek().kind == TOK_EQ || peek().kind == TOK_NEQ) {
                GrassTokenKind op = advance().kind;
                ir::ValueId rhs = parse_relational(error);
                if (!rhs) return 0;
                match_types(lhs, rhs);
                if (op == TOK_EQ) lhs = builder_.create_icmp_eq(lhs, rhs);
                else lhs = builder_.create_icmp_ne(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_relational(std::string& error) {
            ir::ValueId lhs = parse_shift(error);
            if (!lhs) return 0;

            while (peek().kind == TOK_LT || peek().kind == TOK_LTE ||
                   peek().kind == TOK_GT || peek().kind == TOK_GTE) {
                GrassTokenKind op = advance().kind;
                ir::ValueId rhs = parse_shift(error);
                if (!rhs) return 0;
                match_types(lhs, rhs);
                if (op == TOK_LT) lhs = builder_.create_icmp_slt(lhs, rhs);
                else if (op == TOK_LTE) lhs = builder_.create_icmp_sle(lhs, rhs);
                else if (op == TOK_GT) lhs = builder_.create_icmp_sgt(lhs, rhs);
                else if (op == TOK_GTE) lhs = builder_.create_icmp_sge(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_shift(std::string& error) {
            ir::ValueId lhs = parse_additive(error);
            if (!error.empty()) return 0;

            if (!in_stream_io_) {
                while (peek().kind == TOK_SHL || peek().kind == TOK_SHR) {
                    GrassTokenKind op = advance().kind;
                    ir::ValueId rhs = parse_additive(error);
                    if (!error.empty()) return 0;
                    match_types(lhs, rhs);
                    if (op == TOK_SHL) lhs = builder_.create_shl(lhs, rhs);
                    else lhs = builder_.create_ashr(lhs, rhs);
                }
            }
            return lhs;
        }

        ir::ValueId parse_additive(std::string& error) {
            ir::ValueId lhs = parse_multiplicative(error);
            if (!lhs) return 0;

            while (peek().kind == TOK_PLUS || peek().kind == TOK_MINUS) {
                GrassTokenKind op = advance().kind;
                ir::ValueId rhs = parse_multiplicative(error);
                if (!rhs) return 0;
                match_types(lhs, rhs);
                if (op == TOK_PLUS) lhs = builder_.create_add(lhs, rhs);
                else lhs = builder_.create_sub(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_multiplicative(std::string& error) {
            ir::ValueId lhs = parse_unary(error);
            if (!lhs) return 0;

            while (peek().kind == TOK_STAR || peek().kind == TOK_SLASH || peek().kind == TOK_MOD) {
                GrassTokenKind op = advance().kind;
                ir::ValueId rhs = parse_unary(error);
                if (!rhs) return 0;
                match_types(lhs, rhs);
                if (op == TOK_STAR) lhs = builder_.create_mul(lhs, rhs);
                else if (op == TOK_SLASH) lhs = builder_.create_sdiv(lhs, rhs);
                else if (op == TOK_MOD) lhs = builder_.create_srem(lhs, rhs);
            }
            return lhs;
        }

        ir::ValueId parse_unary(std::string& error) {
            if (match(TOK_MINUS)) {
                ir::ValueId operand = parse_unary(error);
                return builder_.create_neg(operand);
            }
            if (match(TOK_NOT)) {
                ir::ValueId operand = parse_unary(error);
                ir::ValueId zero = builder_.create_const_int(ctx_.get_i64_type(), 0);
                return builder_.create_icmp_eq(operand, zero);
            }
            if (match(TOK_TILDE)) {
                ir::ValueId operand = parse_unary(error);
                return builder_.create_not(operand);
            }
            if (match(TOK_INC)) {
                // Prefix ++x
                if (peek().kind == TOK_IDENT) {
                    std::string var_name = advance().text;
                    const VarBinding* var = lookup_var(var_name);
                    if (!var) { error = "Undeclared variable: " + var_name; return 0; }
                    ir::ValueId cur = builder_.create_load(var->alloca_id, var_name);
                    ir::ValueId one = builder_.create_const_int(var->type, 1);
                    ir::ValueId next = builder_.create_add(cur, one);
                    builder_.create_store(next, var->alloca_id);
                    return next;
                }
            }
            if (match(TOK_DEC)) {
                // Prefix --x
                if (peek().kind == TOK_IDENT) {
                    std::string var_name = advance().text;
                    const VarBinding* var = lookup_var(var_name);
                    if (!var) { error = "Undeclared variable: " + var_name; return 0; }
                    ir::ValueId cur = builder_.create_load(var->alloca_id, var_name);
                    ir::ValueId one = builder_.create_const_int(var->type, 1);
                    ir::ValueId next = builder_.create_sub(cur, one);
                    builder_.create_store(next, var->alloca_id);
                    return next;
                }
            }

            return parse_postfix(error);
        }

        ir::ValueId parse_postfix(std::string& error) {
            ir::ValueId base = parse_primary(error);
            if (!base) return 0;

            while (true) {
                // Member access or method call obj.field / obj.method(args)
                if (match(TOK_DOT)) {
                    if (peek().kind != TOK_IDENT) {
                        error = "Expected identifier after '.'!";
                        return 0;
                    }
                    std::string member_name = advance().text;
                    if (match(TOK_LPAREN)) {
                        // Method call e.g. arr.push(10) or hero.status() or hero.take_damage(35)
                        std::vector<ir::ValueId> args;
                        if (peek().kind != TOK_RPAREN) {
                            while (true) {
                                ir::ValueId arg = parse_expression(error);
                                if (!arg) return 0;
                                args.push_back(arg);
                                if (!match(TOK_COMMA)) break;
                            }
                        }
                        consume(TOK_RPAREN, "Expected ')' closing method call!", error);
                        if (member_name == "push") {
                            std::vector<ir::ValueId> push_args = { base };
                            push_args.insert(push_args.end(), args.begin(), args.end());
                            builder_.create_call("grass_array_push", ctx_.get_void_type(), push_args);
                            return base;
                        } else if (oop_mgr_ && !last_struct_name_.empty()) {
                            base = oop_mgr_->emit_call_method(builder_, base, last_struct_name_, member_name, args);
                            return base;
                        }
                    } else {
                        // Field access e.g. this.name, hero.hp
                        if (oop_mgr_ && !last_struct_name_.empty()) {
                            base = oop_mgr_->emit_load_field(builder_, base, last_struct_name_, member_name);
                            return base;
                        }
                    }
                    continue;
                }
                // Array subscript: base[idx]
                if (match(TOK_LBRACKET)) {
                    ir::ValueId idx = parse_expression(error);
                    if (!idx) return 0;
                    consume(TOK_RBRACKET, "Expected ']' closing array index!", error);
                    base = builder_.create_call("grass_array_get", ctx_.get_i64_type(), { base, idx });
                    continue;
                }
                break;
            }
            return base;
        }

        ir::ValueId parse_primary(std::string& error) {
            // Parenthesized expression: ( expr )
            if (match(TOK_LPAREN)) {
                bool prev_io = in_stream_io_;
                in_stream_io_ = false;
                ir::ValueId inner = parse_expression(error);
                in_stream_io_ = prev_io;
                consume(TOK_RPAREN, "Expected ')' closing expression!", error);
                return inner;
            }

            // Array literal: []
            if (match(TOK_LBRACKET)) {
                consume(TOK_RBRACKET, "Expected ']' closing empty array!", error);
                return builder_.create_call("grass_array_create", ctx_.get_ptr_type(), {});
            }

            // Number literal
            if (peek().kind == TOK_NUMBER) {
                double val = advance().num_val;
                if (std::floor(val) == val && std::fabs(val) < 1e15) {
                    return builder_.create_const_i64(static_cast<int64_t>(val));
                }
                return builder_.create_const_f64(val);
            }

            // String literal
            if (peek().kind == TOK_STRING) {
                std::string s = advance().text;
                std::string sym = add_rodata_string(s);
                return builder_.create_call(sym, ctx_.get_ptr_type(ctx_.get_i8_type()), {});
            }

            // Boolean literals: true / false
            if (peek().kind == TOK_IDENT && peek().text == "true") {
                advance();
                return builder_.create_const_i1(true);
            }
            if (peek().kind == TOK_IDENT && peek().text == "false") {
                advance();
                return builder_.create_const_i1(false);
            }

            // Identifier or Function Call
            if (peek().kind == TOK_IDENT) {
                std::string ident = advance().text;

                // Member access ahead? (e.g. hero.name or this.hp)
                if (peek().kind == TOK_DOT) {
                    const VarBinding* var = lookup_var(ident);
                    if (!var) {
                        error = "Undeclared identifier: " + ident;
                        return 0;
                    }
                    last_struct_name_ = var->struct_type_name;
                    ir::ValueId inst_ptr = var->alloca_id;
                    if (var->type && var->type->is_pointer()) {
                        inst_ptr = builder_.create_load(var->alloca_id, ident + "_ptr");
                    }
                    return inst_ptr;
                }

                // Function call: ident(args)
                if (match(TOK_LPAREN)) {
                    std::vector<ir::ValueId> args;
                    if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                        while (true) {
                            ir::ValueId arg = parse_expression(error);
                            if (!arg) return 0;
                            args.push_back(arg);
                            if (!match(TOK_COMMA)) break;
                        }
                    }
                    consume(TOK_RPAREN, "Expected ')' closing function arguments!", error);

                    ir::IRType* call_ret_type = ctx_.get_i64_type();
                    const native::NativeFunctionDesc* std_fn = native::GrassNativeStdlib::instance().find_function(ident);
                    if (std_fn && std_fn->return_type) {
                        call_ret_type = std_fn->return_type;
                    } else if (ident == "start_time") call_ret_type = ctx_.get_void_type();
                    else if (ident == "stop_time" || ident == "discor" || ident == "lerp" ||
                             ident == "sin" || ident == "cos" || ident == "memory_usit") {
                        call_ret_type = ctx_.get_f64_type();
                    } else if (ident == "conclus_time" || ident == "get_date" || ident == "get_time_str" ||
                               ident == "memory_free" || ident == "sha256" || ident == "fs_list" ||
                               ident == "fs_read") {
                        call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                    } else if (ident == "len") {
                        ident = "grass_array_len";
                        call_ret_type = ctx_.get_i64_type();
                    }

                    ir::IRFunction* target_fn = module_.get_function(ident);
                    if (target_fn) {
                        for (size_t i = 0; i < args.size() && i < target_fn->arguments.size(); ++i) {
                            args[i] = coerce_type(args[i], target_fn->arguments[i].type);
                        }
                    } else if (std_fn) {
                        for (size_t i = 0; i < args.size() && i < std_fn->param_types.size(); ++i) {
                            args[i] = coerce_type(args[i], std_fn->param_types[i]);
                        }
                    }

                    return builder_.create_call(ident, call_ret_type, args);
                }

                // Variable access
                const VarBinding* var = lookup_var(ident);
                if (!var) {
                    error = "Undeclared identifier: " + ident;
                    return 0;
                }
                if (match(TOK_INC)) {
                    ir::ValueId cur = builder_.create_load(var->alloca_id, ident);
                    ir::ValueId one = builder_.create_const_int(var->type, 1);
                    ir::ValueId next = builder_.create_add(cur, one);
                    builder_.create_store(next, var->alloca_id);
                    return cur; // Postfix returns old value
                }
                if (match(TOK_DEC)) {
                    ir::ValueId cur = builder_.create_load(var->alloca_id, ident);
                    ir::ValueId one = builder_.create_const_int(var->type, 1);
                    ir::ValueId next = builder_.create_sub(cur, one);
                    builder_.create_store(next, var->alloca_id);
                    return cur; // Postfix returns old value
                }
                return builder_.create_load(var->alloca_id, ident);
            }

            error = "Line " + std::to_string(peek().line) + ": Unexpected token in expression: " + peek().text;
            return 0;
        }
    };

    // =========================================================================
    // 3. Native Machine Code Generator: GrassIR -> GrassX86Encoder
    // =========================================================================

    static backend::Reg phys_reg_to_backend(ir::PhysReg pr, TargetArch arch) {
        if (pr == ir::PhysReg::None) return backend::Reg::None;
        int idx = static_cast<int>(pr);
        if (idx >= static_cast<int>(ir::PhysReg::RAX) && idx <= static_cast<int>(ir::PhysReg::R15)) {
            int gpr = idx - static_cast<int>(ir::PhysReg::RAX);
            return (arch == TargetArch::X86_64) ? static_cast<backend::Reg>(0x0030 + gpr)
                                                : static_cast<backend::Reg>(0x0020 + gpr);
        }
        if (idx >= static_cast<int>(ir::PhysReg::XMM0) && idx <= static_cast<int>(ir::PhysReg::XMM15)) {
            int xmm = idx - static_cast<int>(ir::PhysReg::XMM0);
            return static_cast<backend::Reg>(0x0050 + xmm);
        }
        return backend::Reg::None;
    }

    static backend::CondCode icmp_to_cond_code(ir::ICmpCond cond) {
        switch (cond) {
            case ir::ICmpCond::EQ:  return backend::CondCode::E;
            case ir::ICmpCond::NE:  return backend::CondCode::NE;
            case ir::ICmpCond::SLT: return backend::CondCode::L;
            case ir::ICmpCond::SLE: return backend::CondCode::LE;
            case ir::ICmpCond::SGT: return backend::CondCode::G;
            case ir::ICmpCond::SGE: return backend::CondCode::GE;
            case ir::ICmpCond::ULT: return backend::CondCode::B;
            case ir::ICmpCond::ULE: return backend::CondCode::BE;
            case ir::ICmpCond::UGT: return backend::CondCode::A;
            case ir::ICmpCond::UGE: return backend::CondCode::AE;
        }
        return backend::CondCode::E;
    }

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
        if (!stdlib.is_initialized()) {
            stdlib.initialize(module.context);
        }
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

        // Step 2: Optimization pipeline
        if (opt_level > 0) {
            ir::PassManager pm;
            pm.add_standard_optimizations();
            pm.run(module);
        }

        return module.verify(error);
    }

    bool NativeCompiler::codegen_module(ir::IRModule& module,
                                        const std::vector<uint8_t>& rodata,
                                        const std::unordered_map<std::string, uint64_t>& rodata_symbols,
                                        CompiledUnit& unit,
                                        std::string& error) {
        (void)error;
        unit.arch = options_.arch;
        unit.abi = options_.abi;
        unit.text.clear();
        unit.rodata = rodata;
        unit.data.clear();
        unit.bss_size = 0;
        unit.symbols.clear();
        unit.relocations.clear();

        // Register rodata symbols
        for (const auto& [name, off] : rodata_symbols) {
            backend::SymbolDef sym;
            sym.name = name;
            sym.section = backend::SectionKind::RoData;
            sym.offset = off;
            sym.size = 0;
            sym.is_global = false;
            sym.is_external = false;
            unit.symbols.push_back(sym);
        }

        backend::ABISpec abi_spec = backend::ABISpec::for_abi(options_.abi);
        ir::LinearScanAllocator allocator(options_.abi);

        backend::GrassX86Encoder encoder(options_.arch);

        for (auto& fn : module.functions) {
            if (fn->is_declaration || fn->blocks.empty()) {
                backend::SymbolDef ext_sym;
                ext_sym.name = fn->name;
                ext_sym.section = backend::SectionKind::Text;
                ext_sym.offset = 0;
                ext_sym.size = 0;
                ext_sym.is_global = true;
                ext_sym.is_external = true;
                unit.symbols.push_back(ext_sym);
                continue;
            }

            ir::AllocationResult alloc_res = allocator.allocate(*fn);

            // Function symbol in text section
            backend::SymbolDef fn_sym;
            fn_sym.name = fn->name;
            fn_sym.section = backend::SectionKind::Text;
            fn_sym.offset = encoder.code_size();
            fn_sym.is_global = true;
            fn_sym.is_external = false;

            int32_t callee_saved_size = static_cast<int32_t>(alloc_res.used_callee_saved.size() * 8);
            int32_t spill_base_offset = callee_saved_size + 16;
            int32_t alloca_stack_cursor = spill_base_offset + alloc_res.stack_frame_size + 32;
            std::unordered_map<ir::ValueId, int32_t> alloca_offsets;
            for (const auto& bb : fn->blocks) {
                for (const auto& inst : bb->instructions) {
                    if (inst->op == ir::IROp::Alloca) {
                        uint32_t bytes = 8;
                        if (inst->type && inst->type->element_type) {
                            if (inst->type->element_type->is_struct()) {
                                bytes = static_cast<uint32_t>(inst->type->element_type->struct_fields.size() * 8);
                                if (bytes == 0) bytes = 8;
                            }
                        }
                        alloca_stack_cursor = (alloca_stack_cursor + 15) & ~15;
                        alloca_offsets[inst->result_id] = alloca_stack_cursor;
                        alloca_stack_cursor += bytes;
                    }
                }
            }

            // Compute stack frame
            backend::StackFrameRequest req;
            req.local_vars_size = alloca_stack_cursor + 256;
            req.has_calls = true;
            req.max_outgoing_args_size = (options_.abi == TargetABI::Win64) ? 32 : 0;
            req.use_frame_pointer = true;
            for (ir::PhysReg pr : alloc_res.used_callee_saved) {
                backend::Reg r = phys_reg_to_backend(pr, options_.arch);
                if (r != backend::Reg::None && backend::reg_is_gpr(r)) {
                    req.callee_saved_regs.push_back(r);
                }
            }
            backend::StackFrameInfo frame_info = abi_spec.compute_stack_frame(req);

            // Block labels
            std::unordered_map<ir::BasicBlock*, backend::LabelId> bb_labels;
            for (const auto& bb : fn->blocks) {
                bb_labels[bb.get()] = encoder.create_label();
            }

            backend::LabelId epilogue_label = encoder.create_label();

            // Emit function prologue
            if (options_.arch == TargetArch::X86_64) {
                encoder.push_r(backend::Reg::RBP);
                encoder.mov_rr(backend::Reg::RBP, backend::Reg::RSP);
                for (backend::Reg r : frame_info.callee_saved_regs) encoder.push_r(r);
                if (frame_info.total_frame_size > 0) encoder.sub_ri(backend::Reg::RSP, frame_info.total_frame_size);
            } else {
                encoder.push_r(backend::Reg::EBP);
                encoder.mov_rr(backend::Reg::EBP, backend::Reg::ESP);
                for (backend::Reg r : frame_info.callee_saved_regs) encoder.push_r(r);
                if (frame_info.total_frame_size > 0) encoder.sub_ri(backend::Reg::ESP, frame_info.total_frame_size);
            }

            // In-function value location helpers
            auto get_loc_reg = [&](ir::ValueId val) -> backend::Reg {
                auto it = alloc_res.intervals.find(val);
                if (it != alloc_res.intervals.end() && !it->second.is_spilled) {
                    return phys_reg_to_backend(it->second.reg, options_.arch);
                }
                return backend::Reg::None;
            };

            auto get_spill_offset = [&](ir::ValueId val) -> int32_t {
                auto it = alloc_res.intervals.find(val);
                if (it != alloc_res.intervals.end() && it->second.is_spilled) {
                    return spill_base_offset + it->second.stack_slot_offset;
                }
                return spill_base_offset + static_cast<int32_t>(val * 8);
            };

            auto load_val = [&](ir::ValueId val, backend::Reg temp_reg) -> backend::Reg {
                backend::Reg r = get_loc_reg(val);
                if (r != backend::Reg::None) return r;
                int32_t off = get_spill_offset(val);
                ir::IRType* vtype = fn->get_value(val).type;
                if (vtype && vtype->is_floating_point()) {
                    if (backend::reg_is_xmm(temp_reg)) {
                        encoder.movsd_xm(temp_reg, backend::mem(backend::Reg::RBP, -off, 8));
                    } else {
                        encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, -off, 8));
                        return backend::Reg::XMM0;
                    }
                } else {
                    encoder.mov_rm(temp_reg, backend::mem(backend::Reg::RBP, -off, 8));
                }
                return temp_reg;
            };

            auto store_val = [&](ir::ValueId val, backend::Reg src_reg) {
                backend::Reg r = get_loc_reg(val);
                ir::IRType* vtype = fn->get_value(val).type;

                if (r != backend::Reg::None) {
                    if (r != src_reg) {
                        if (backend::reg_is_xmm(r) && backend::reg_is_xmm(src_reg)) {
                            encoder.movsd_xx(r, src_reg);
                        } else if (backend::reg_is_gpr(r) && backend::reg_is_gpr(src_reg)) {
                            encoder.mov_rr(r, src_reg);
                        } else if (backend::reg_is_gpr(r) && backend::reg_is_xmm(src_reg)) {
                            encoder.cvttsd2si_rx(r, src_reg);
                        } else if (backend::reg_is_xmm(r) && backend::reg_is_gpr(src_reg)) {
                            encoder.cvtsi2sd_xr(r, src_reg);
                        }
                    }
                } else {
                    int32_t off = get_spill_offset(val);
                    if (backend::reg_is_xmm(src_reg)) {
                        encoder.movsd_mx(backend::mem(backend::Reg::RBP, -off, 8), src_reg);
                    } else {
                        encoder.mov_mr(backend::mem(backend::Reg::RBP, -off, 8), src_reg);
                    }
                }
            };

            // Incoming function arguments placement
            const auto& arg_regs = abi_spec.get_integer_arg_regs();
            for (size_t i = 0; i < fn->arguments.size(); ++i) {
                if (i < arg_regs.size()) {
                    store_val(fn->arguments[i].id, arg_regs[i]);
                } else {
                    int32_t stack_off = 16 + (i - arg_regs.size()) * 8;
                    backend::Reg tmp = backend::Reg::R10;
                    encoder.mov_rm(tmp, backend::mem(backend::Reg::RBP, stack_off, 8));
                    store_val(fn->arguments[i].id, tmp);
                }
            }

            auto emit_phi_copies = [&](ir::BasicBlock* from_bb, ir::BasicBlock* to_bb) {
                if (!to_bb) return;
                struct PhiMove {
                    ir::ValueId src_val;
                    ir::ValueId dst_val;
                    bool is_fp;
                };
                std::vector<PhiMove> moves;
                for (const auto& phi_inst : to_bb->instructions) {
                    if (phi_inst->op != ir::IROp::Phi) break;
                    ir::ValueId in_val = phi_inst->get_phi_incoming_value(from_bb);
                    if (in_val != ir::kInvalidValueId) {
                        ir::IRType* vt = phi_inst->type;
                        bool fp = vt && vt->is_floating_point();
                        moves.push_back({ in_val, phi_inst->result_id, fp });
                    }
                }
                if (moves.empty()) return;

                // Pass 1: save all source values into stack scratch slots
                int32_t scratch_base = frame_info.total_frame_size + 64;
                for (size_t i = 0; i < moves.size(); ++i) {
                    int32_t off = scratch_base + static_cast<int32_t>(i * 8);
                    if (moves[i].is_fp) {
                        backend::Reg r = load_val(moves[i].src_val, backend::Reg::XMM0);
                        encoder.movsd_mx(backend::mem(backend::Reg::RBP, -off, 8), r);
                    } else {
                        backend::Reg r = load_val(moves[i].src_val, backend::Reg::R10);
                        encoder.mov_mr(backend::mem(backend::Reg::RBP, -off, 8), r);
                    }
                }

                // Pass 2: load from scratch slots into destination values
                for (size_t i = 0; i < moves.size(); ++i) {
                    int32_t off = scratch_base + static_cast<int32_t>(i * 8);
                    if (moves[i].is_fp) {
                        encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, -off, 8));
                        store_val(moves[i].dst_val, backend::Reg::XMM0);
                    } else {
                        encoder.mov_rm(backend::Reg::R10, backend::mem(backend::Reg::RBP, -off, 8));
                        store_val(moves[i].dst_val, backend::Reg::R10);
                    }
                }
            };

            // Translate blocks
            for (const auto& bb : fn->blocks) {
                encoder.bind_label(bb_labels[bb.get()]);

                for (const auto& inst : bb->instructions) {
                    try {
                    switch (inst->op) {
                        case ir::IROp::ConstInt: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            if (dst != backend::Reg::None && backend::reg_is_gpr(dst)) {
                                encoder.mov_ri64(dst, inst->int_imm);
                            } else {
                                encoder.mov_ri64(backend::Reg::R11, inst->int_imm);
                                store_val(inst->result_id, backend::Reg::R11);
                            }
                            break;
                        }
                        case ir::IROp::ConstFloat: {
                            uint64_t raw_bits;
                            std::memcpy(&raw_bits, &inst->float_imm, 8);
                            int32_t off = get_spill_offset(inst->result_id);
                            encoder.mov_ri64(backend::Reg::R11, (int64_t)raw_bits);
                            encoder.mov_mr(backend::mem(backend::Reg::RBP, -off, 8), backend::Reg::R11);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            if (dst != backend::Reg::None && backend::reg_is_xmm(dst)) {
                                encoder.movsd_xm(dst, backend::mem(backend::Reg::RBP, -off, 8));
                            }
                            break;
                        }
                        case ir::IROp::Copy: {
                            ir::IRType* ct = fn->get_value(inst->operands[0]).type;
                            if (ct && ct->is_floating_point()) {
                                backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM0);
                                store_val(inst->result_id, src);
                            } else {
                                backend::Reg src = load_val(inst->operands[0], backend::Reg::R10);
                                store_val(inst->result_id, src);
                            }
                            break;
                        }
                        case ir::IROp::Add: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg lhs = load_val(inst->operands[0], out);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (lhs != out) encoder.mov_rr(out, lhs);
                            encoder.add_rr(out, rhs);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Sub: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg lhs = load_val(inst->operands[0], out);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (lhs != out) encoder.mov_rr(out, lhs);
                            encoder.sub_rr(out, rhs);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Mul: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg lhs = load_val(inst->operands[0], out);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (lhs != out) encoder.mov_rr(out, lhs);
                            encoder.imul_rr(out, rhs);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::SDiv: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                            encoder.cqo();
                            encoder.idiv_r(rhs);
                            store_val(inst->result_id, backend::Reg::RAX);
                            break;
                        }
                        case ir::IROp::SRem: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                            encoder.cqo();
                            encoder.idiv_r(rhs);
                            store_val(inst->result_id, backend::Reg::RDX);
                            break;
                        }
                        case ir::IROp::And: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg lhs = load_val(inst->operands[0], out);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (lhs != out) encoder.mov_rr(out, lhs);
                            encoder.and_rr(out, rhs);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Or: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg lhs = load_val(inst->operands[0], out);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (lhs != out) encoder.mov_rr(out, lhs);
                            encoder.or_rr(out, rhs);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Xor: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg lhs = load_val(inst->operands[0], out);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (lhs != out) encoder.mov_rr(out, lhs);
                            encoder.xor_rr(out, rhs);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Neg: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg src = load_val(inst->operands[0], out);
                            if (src != out) encoder.mov_rr(out, src);
                            encoder.neg_r(out);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Not: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg src = load_val(inst->operands[0], out);
                            if (src != out) encoder.mov_rr(out, src);
                            encoder.not_r(out);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Shl: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::RCX);
                            if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                            if (rhs != backend::Reg::RCX) encoder.mov_rr(backend::Reg::RCX, rhs);
                            encoder.shl_rcl(backend::Reg::RAX);
                            store_val(inst->result_id, backend::Reg::RAX);
                            break;
                        }
                        case ir::IROp::AShr: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::RCX);
                            if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                            if (rhs != backend::Reg::RCX) encoder.mov_rr(backend::Reg::RCX, rhs);
                            encoder.sar_rcl(backend::Reg::RAX);
                            store_val(inst->result_id, backend::Reg::RAX);
                            break;
                        }
                        case ir::IROp::ICmp: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg lhs = load_val(inst->operands[0], out);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            encoder.cmp_rr(lhs, rhs);
                            backend::CondCode cc = icmp_to_cond_code(inst->icmp_cond);
                            encoder.setcc_r(cc, backend::Reg::AL);
                            encoder.movzx_rr8(out, backend::Reg::AL);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Alloca: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            int32_t off = alloca_offsets.count(inst->result_id) ? alloca_offsets[inst->result_id] : get_spill_offset(inst->result_id);
                            encoder.lea_rm(out, backend::mem(backend::Reg::RBP, -off, 8));
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Load: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg ptr = load_val(inst->operands[0], backend::Reg::R10);
                            encoder.mov_rm(out, backend::mem(ptr, 0, 8));
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Store: {
                            backend::Reg ptr = load_val(inst->operands[1], backend::Reg::R10);
                            backend::Reg val = load_val(inst->operands[0], backend::Reg::R11);
                            encoder.mov_mr(backend::mem(ptr, 0, 8), val);
                            break;
                        }
                        case ir::IROp::GetElementPtr: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg base = load_val(inst->operands[0], backend::Reg::RAX);
                            encoder.mov_rr(out, base);
                            if (inst->gep_field_index >= 0) {
                                int64_t byte_off = static_cast<int64_t>(inst->gep_field_index) * 8;
                                if (byte_off != 0) {
                                    encoder.add_ri(out, static_cast<int32_t>(byte_off));
                                }
                            } else if (inst->operands.size() > 1) {
                                backend::Reg idx = load_val(inst->operands[1], backend::Reg::R10);
                                encoder.imul_rri(backend::Reg::R10, idx, 8);
                                encoder.add_rr(out, backend::Reg::R10);
                            }
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Call: {
                            // Check if callee is a string symbol in rodata
                            if (rodata_symbols.count(inst->callee_name)) {
                                encoder.lea_symbol(backend::Reg::RAX, inst->callee_name);
                                store_val(inst->result_id, backend::Reg::RAX);
                                break;
                            }

                            // Pass arguments according to ABI
                            // Phase 1: spill all operands to argument scratch slots at [RSP + i*8] to avoid register clobbering
                            for (size_t i = 0; i < inst->operands.size(); ++i) {
                                ir::IRType* arg_t = fn->get_value(inst->operands[i]).type;
                                bool is_fp = arg_t && arg_t->is_floating_point();
                                int32_t temp_off = static_cast<int32_t>(i * 8);
                                if (is_fp) {
                                    backend::Reg src = load_val(inst->operands[i], backend::Reg::XMM0);
                                    encoder.movsd_mx(backend::mem(backend::Reg::RSP, temp_off, 8), src);
                                } else {
                                    backend::Reg src = load_val(inst->operands[i], backend::Reg::R10);
                                    encoder.mov_mr(backend::mem(backend::Reg::RSP, temp_off, 8), src);
                                }
                            }

                            // Phase 2: load from scratch slots into ABI target registers and stack parameter slots
                            for (size_t i = 0; i < inst->operands.size(); ++i) {
                                ir::IRType* arg_t = fn->get_value(inst->operands[i]).type;
                                bool is_fp = arg_t && arg_t->is_floating_point();
                                int32_t temp_off = static_cast<int32_t>(i * 8);
                                if (i < arg_regs.size()) {
                                    if (is_fp) {
                                        backend::Reg xmm_reg = static_cast<backend::Reg>(0x0050 + i);
                                        encoder.movsd_xm(xmm_reg, backend::mem(backend::Reg::RSP, temp_off, 8));
                                    } else {
                                        backend::Reg gpr_reg = arg_regs[i];
                                        encoder.mov_rm(gpr_reg, backend::mem(backend::Reg::RSP, temp_off, 8));
                                    }
                                } else {
                                    int32_t stack_off = (options_.abi == TargetABI::Win64)
                                        ? (32 + (i - 4) * 8)
                                        : ((i - arg_regs.size()) * 8);
                                    if (stack_off != temp_off) {
                                        if (is_fp) {
                                            encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RSP, temp_off, 8));
                                            encoder.movsd_mx(backend::mem(backend::Reg::RSP, stack_off, 8), backend::Reg::XMM0);
                                        } else {
                                            encoder.mov_rm(backend::Reg::R10, backend::mem(backend::Reg::RSP, temp_off, 8));
                                            encoder.mov_mr(backend::mem(backend::Reg::RSP, stack_off, 8), backend::Reg::R10);
                                        }
                                    }
                                }
                            }

                            encoder.call_extern(inst->callee_name);

                            if (inst->result_id != ir::kInvalidValueId) {
                                ir::IRType* res_t = inst->type;
                                if (res_t && res_t->is_floating_point()) {
                                    store_val(inst->result_id, backend::Reg::XMM0);
                                } else {
                                    store_val(inst->result_id, backend::Reg::RAX);
                                }
                            }
                            break;
                        }
                        case ir::IROp::Jump: {
                            emit_phi_copies(bb.get(), inst->true_target);
                            encoder.jmp_label(bb_labels[inst->true_target]);
                            break;
                        }
                        case ir::IROp::Branch: {
                            backend::Reg cond = load_val(inst->operands[0], backend::Reg::R10);
                            encoder.test_rr(cond, cond);
                            backend::LabelId skip_true = encoder.create_label();
                            encoder.jcc_label(backend::CondCode::Z, skip_true);
                            emit_phi_copies(bb.get(), inst->true_target);
                            encoder.jmp_label(bb_labels[inst->true_target]);
                            encoder.bind_label(skip_true);
                            emit_phi_copies(bb.get(), inst->false_target);
                            encoder.jmp_label(bb_labels[inst->false_target]);
                            break;
                        }
                        case ir::IROp::Ret: {
                            if (!inst->operands.empty()) {
                                ir::IRType* ret_t = fn->get_value(inst->operands[0]).type;
                                if (ret_t && ret_t->is_floating_point()) {
                                    backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM0);
                                    if (src != backend::Reg::XMM0) {
                                        if (backend::reg_is_xmm(src)) encoder.movsd_xx(backend::Reg::XMM0, src);
                                        else encoder.cvtsi2sd_xr(backend::Reg::XMM0, src);
                                    }
                                } else {
                                    backend::Reg src = load_val(inst->operands[0], backend::Reg::RAX);
                                    if (src != backend::Reg::RAX) {
                                        if (backend::reg_is_gpr(src)) encoder.mov_rr(backend::Reg::RAX, src);
                                        else encoder.cvttsd2si_rx(backend::Reg::RAX, src);
                                    }
                                }
                            }
                            encoder.jmp_label(epilogue_label);
                            break;
                        }
                        case ir::IROp::FAdd: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::XMM0);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::XMM1);
                            if (lhs != backend::Reg::XMM0) encoder.movsd_xx(backend::Reg::XMM0, lhs);
                            encoder.addsd_xx(backend::Reg::XMM0, rhs);
                            store_val(inst->result_id, backend::Reg::XMM0);
                            break;
                        }
                        case ir::IROp::FSub: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::XMM0);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::XMM1);
                            if (lhs != backend::Reg::XMM0) encoder.movsd_xx(backend::Reg::XMM0, lhs);
                            encoder.subsd_xx(backend::Reg::XMM0, rhs);
                            store_val(inst->result_id, backend::Reg::XMM0);
                            break;
                        }
                        case ir::IROp::FMul: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::XMM0);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::XMM1);
                            if (lhs != backend::Reg::XMM0) encoder.movsd_xx(backend::Reg::XMM0, lhs);
                            encoder.mulsd_xx(backend::Reg::XMM0, rhs);
                            store_val(inst->result_id, backend::Reg::XMM0);
                            break;
                        }
                        case ir::IROp::FDiv: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::XMM0);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::XMM1);
                            if (lhs != backend::Reg::XMM0) encoder.movsd_xx(backend::Reg::XMM0, lhs);
                            encoder.divsd_xx(backend::Reg::XMM0, rhs);
                            store_val(inst->result_id, backend::Reg::XMM0);
                            break;
                        }
                        case ir::IROp::SIToFP: {
                            backend::Reg src = load_val(inst->operands[0], backend::Reg::RAX);
                            encoder.cvtsi2sd_xr(backend::Reg::XMM0, src);
                            store_val(inst->result_id, backend::Reg::XMM0);
                            break;
                        }
                        case ir::IROp::FPToSI: {
                            backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM0);
                            encoder.cvttsd2si_rx(backend::Reg::RAX, src);
                            store_val(inst->result_id, backend::Reg::RAX);
                            break;
                        }
                        case ir::IROp::Trunc:
                        case ir::IROp::SExt:
                        case ir::IROp::ZExt: {
                            backend::Reg src = load_val(inst->operands[0], backend::Reg::RAX);
                            store_val(inst->result_id, src);
                            break;
                        }
                        case ir::IROp::Phi: {
                            // Handled at predecessor jump edges
                            break;
                        }
                        default:
                            break;
                    }
                    } catch (const std::exception& ex) {
                        error = std::string("In block '") + bb->name + "' inst op=" + std::to_string(static_cast<int>(inst->op)) +
                                ": " + ex.what();
                        return false;
                    }
                }
            }

            // Function epilogue
            encoder.bind_label(epilogue_label);
            if (frame_info.total_frame_size > 0) {
                encoder.add_ri((options_.arch == TargetArch::X86_64) ? backend::Reg::RSP : backend::Reg::ESP,
                               frame_info.total_frame_size);
            }
            for (auto it = frame_info.callee_saved_regs.rbegin(); it != frame_info.callee_saved_regs.rend(); ++it) {
                if (backend::reg_is_gpr(*it)) encoder.pop_r(*it);
            }
            encoder.pop_r((options_.arch == TargetArch::X86_64) ? backend::Reg::RBP : backend::Reg::EBP);
            encoder.ret();

            fn_sym.size = encoder.code_size() - fn_sym.offset;
            unit.symbols.push_back(fn_sym);
        }

        unit.text = encoder.code();
        unit.relocations = encoder.relocations();
        return true;
    }

    bool NativeCompiler::ensure_runtime_c_file(std::string& runtime_path, std::string& error) {
        runtime_path = "bin/grass_native_runtime.c";
        if (fs::exists(runtime_path)) return true;

        if (!fs::exists("bin")) {
            std::error_code ec;
            fs::create_directories("bin", ec);
        }

        std::ofstream out(runtime_path);
        if (!out.is_open()) {
            error = "Failed to create runtime file: " + runtime_path;
            return false;
        }
        out << kEmbeddedRuntimeC;
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
                                         std::string& error) {
        if (options_.freestanding) {
            return true;
        }

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
            res.error_message = "Optimization Error: " + error;
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
            if (!link_executable(res.obj_file_created, options_.output_exe_path, error)) {
                res.success = false;
                res.error_message = "Link Error: " + error;
                return res;
            }
            res.exe_file_created = options_.output_exe_path;
        }

        res.success = true;
        return res;
    }

    NativeCodegenResult NativeCompiler::compile_source(const std::string& source_code, const std::string& source_path) {
        Lexer lexer(source_code);
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

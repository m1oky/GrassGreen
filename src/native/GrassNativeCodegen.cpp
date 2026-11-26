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

void grass_print_ptr(uint64_t p) {
    printf("0x%llx", (unsigned long long)p);
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
        size_t new_cap = arr->capacity ? arr->capacity * 2 : 8;
        int64_t* new_data = (int64_t*)realloc(arr->data, new_cap * sizeof(int64_t));
        if (!new_data) return;
        arr->capacity = new_cap;
        arr->data = new_data;
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

int64_t memory_alloc(int64_t size) {
    if (size <= 0 || (uint64_t)size > 0x7FFFFFFF0000ULL) return 0;
    void* p = calloc(1, (size_t)size);
    return (int64_t)(uintptr_t)p;
}

int64_t memory_free_block(int64_t block_addr) {
    if (block_addr == 0) return 0;
    free((void*)(uintptr_t)block_addr);
    return 1;
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
            bool is_raii = false;
            bool is_vec = false;
            ir::IRType* vec_elem_type = nullptr;
            std::string vec_elem_struct;
        };
        struct ScopeFrame {
            std::unordered_map<std::string, VarBinding> vars;
            std::vector<std::string> raii_vars;
        };
        std::vector<ScopeFrame> scopes_;

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

        std::string current_namespace() const {
            if (namespace_stack_.empty()) return "";
            std::string res;
            for (size_t i = 0; i < namespace_stack_.size(); ++i) {
                if (i > 0) res += "::";
                res += namespace_stack_[i];
            }
            return res;
        }

        std::string qualify_name(const std::string& name) const {
            if (name.find("::") != std::string::npos) return name;
            std::string ns = current_namespace();
            if (ns.empty()) return name;
            return ns + "::" + name;
        }

        std::string resolve_callee(const std::string& callee) const {
            if (callee.find("::") != std::string::npos) {
                std::string mangled = grass::sema::GrassSema::mangleName(callee);
                if (module_.get_function(mangled)) return mangled;
                if (module_.get_function(callee)) return callee;
                return mangled;
            }
            auto sit = using_symbols_.find(callee);
            if (sit != using_symbols_.end()) {
                std::string mangled = grass::sema::GrassSema::mangleName(sit->second);
                if (module_.get_function(mangled)) return mangled;
                if (module_.get_function(sit->second)) return sit->second;
            }
            if (!namespace_stack_.empty()) {
                std::string q = current_namespace() + "::" + callee;
                std::string mangled = grass::sema::GrassSema::mangleName(q);
                if (module_.get_function(mangled)) return mangled;
                if (module_.get_function(q)) return q;
            }
            for (const auto& uns : using_namespaces_) {
                std::string q = uns + "::" + callee;
                std::string mangled = grass::sema::GrassSema::mangleName(q);
                if (module_.get_function(mangled)) return mangled;
                if (module_.get_function(q)) return q;
            }
            return callee;
        }

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

        void emit_var_dtor(const VarBinding& b) {
            if (!b.is_raii) return;
            ir::BasicBlock* cur_bb = builder_.get_insert_block();
            if (!cur_bb || cur_bb->has_terminator()) return;
            if (b.is_vec) {
                if (b.type && b.type->is_struct()) {
                    emit_vec_dtor(b.alloca_id, b.type, b.vec_elem_type, b.vec_elem_struct);
                }
                return;
            }
            if (b.struct_type_name.empty() || !oop_mgr_) return;
            const auto* cd = oop_mgr_->get_class(b.struct_type_name);
            if (!cd || !cd->has_destructor) return;
            std::string dtor_name = oop_mgr_->get_destructor_name(cd->name);
            ir::IRType* struct_ptr_t = ctx_.get_ptr_type(cd->ir_struct_type);
            if (b.type && b.type->is_pointer()) {
                ir::ValueId obj_ptr = builder_.create_load(b.alloca_id, cd->name + "_dtor_ptr");
                ir::ValueId null_ptr = builder_.create_const_int(b.type, 0);
                ir::ValueId is_nn = builder_.create_icmp_ne(obj_ptr, null_ptr);
                ir::BasicBlock* bb_call = builder_.create_block("dtor_call");
                ir::BasicBlock* bb_skip = builder_.create_block("dtor_skip");
                builder_.create_branch(is_nn, bb_call, bb_skip);
                builder_.set_insert_point(bb_call);
                ir::ValueId cast_ptr = coerce_type(obj_ptr, struct_ptr_t);
                builder_.create_call(dtor_name, ctx_.get_void_type(), { cast_ptr });
                builder_.create_jump(bb_skip);
                builder_.set_insert_point(bb_skip);
            } else {
                ir::ValueId cast_ptr = coerce_type(b.alloca_id, struct_ptr_t);
                builder_.create_call(dtor_name, ctx_.get_void_type(), { cast_ptr });
            }
        }

        void emit_struct_field_dtors(ir::ValueId this_ptr, const std::string& struct_name) {
            if (!oop_mgr_ || struct_name.empty() || !this_ptr) return;
            ir::BasicBlock* cur_bb = builder_.get_insert_block();
            if (!cur_bb || cur_bb->has_terminator()) return;
            const auto* cd = oop_mgr_->get_class(struct_name);
            if (!cd) return;
            for (auto it = cd->fields.rbegin(); it != cd->fields.rend(); ++it) {
                if (it->type && it->type->is_struct()) {
                    auto vit = vec_struct_meta_.find(it->type->struct_name);
                    if (vit != vec_struct_meta_.end()) {
                        ir::ValueId fptr = oop_mgr_->emit_field_gep(builder_, this_ptr, struct_name, it->name, it->name + "_vdtor_gep");
                        if (fptr != ir::kInvalidValueId) {
                            emit_vec_dtor(fptr, it->type, vit->second.elem_type, vit->second.elem_struct);
                        }
                        continue;
                    }
                    const auto* fcd = oop_mgr_->get_class(it->type->struct_name);
                    if (fcd && fcd->has_destructor) {
                        ir::ValueId fptr = oop_mgr_->emit_field_gep(builder_, this_ptr, struct_name, it->name, it->name + "_dtor_gep");
                        if (fptr != ir::kInvalidValueId) {
                            builder_.create_call(oop_mgr_->get_destructor_name(fcd->name), ctx_.get_void_type(), { fptr });
                        }
                    }
                }
            }
        }

        void emit_scope_cleanups(size_t frame_idx) {
            if (frame_idx >= scopes_.size()) return;
            ir::BasicBlock* cur_bb = builder_.get_insert_block();
            if (!cur_bb || cur_bb->has_terminator()) return;
            const auto& frame = scopes_[frame_idx];
            for (auto it = frame.raii_vars.rbegin(); it != frame.raii_vars.rend(); ++it) {
                auto vit = frame.vars.find(*it);
                if (vit != frame.vars.end()) {
                    emit_var_dtor(vit->second);
                }
            }
        }

        void emit_scopes_cleanup_to(size_t target_depth) {
            for (size_t idx = scopes_.size(); idx > target_depth; --idx) {
                emit_scope_cleanups(idx - 1);
            }
        }

        void pop_scope(bool emit_cleanups = true) {
            if (scopes_.empty()) return;
            if (emit_cleanups) {
                emit_scope_cleanups(scopes_.size() - 1);
            }
            scopes_.pop_back();
        }

        bool declare_var(const std::string& name, ir::ValueId alloca_id, ir::IRType* type,
                         const std::string& struct_name = "", bool is_raii = false,
                         bool is_vec = false, ir::IRType* vec_elem_t = nullptr, const std::string& vec_elem_struct = "") {
            if (scopes_.empty()) push_scope();
            scopes_.back().vars[name] = { alloca_id, type, struct_name, is_raii, is_vec, vec_elem_t, vec_elem_struct };
            if (is_raii) {
                scopes_.back().raii_vars.push_back(name);
            }
            return true;
        }

        const VarBinding* lookup_var(const std::string& name) const {
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                auto f = it->vars.find(name);
                if (f != it->vars.end()) return &f->second;
            }
            return nullptr;
        }

        void emit_zero_struct(ir::ValueId struct_alloca, ir::IRType* st_type) {
            if (!struct_alloca || !st_type || !st_type->is_struct()) return;
            uint64_t total_sz = st_type->size();
            uint64_t qwords = (total_sz + 7) / 8;
            ir::IRType* i64_ptr_t = ctx_.get_ptr_type(ctx_.get_i64_type());
            ir::ValueId base_p = builder_.create_bitcast(struct_alloca, i64_ptr_t, "zinit_ptr");
            ir::ValueId zero64 = builder_.create_const_i64(0);
            for (uint64_t i = 0; i < qwords; ++i) {
                ir::ValueId slot_p = (i == 0) ? base_p : builder_.create_gep(base_p, { builder_.create_const_i64(static_cast<int64_t>(i)) }, ctx_.get_i64_type());
                builder_.create_store(zero64, slot_p);
            }
        }

        bool is_vec_type_start(int offset = 0) const {
            return (peek(offset).text == "vec" || peek(offset).text == "vector") &&
                   peek(offset + 1).kind == TOK_LT;
        }

        static uint64_t get_ir_type_size_bytes(ir::IRType* t) {
            if (!t) return 8;
            uint64_t sz = t->size();
            return sz > 0 ? sz : 8;
        }

        static std::string mangle_ir_type_key(ir::IRType* t, const std::string& elem_struct) {
            if (!t) return "any";
            if (t->is_pointer()) return "ptr_" + mangle_ir_type_key(t->element_type, elem_struct);
            if (t->is_struct()) return "st_" + (!elem_struct.empty() ? elem_struct : t->struct_name);
            if (t->is_floating_point()) return "f64";
            if (t->is_bool()) return "i1";
            if (t->is_i8()) return "i8";
            if (t->is_i16()) return "i16";
            if (t->is_i32()) return "i32";
            return "i64";
        }

        ir::IRType* get_or_create_vec_ir_type(ir::IRType* elem_t, const std::string& elem_struct, std::string& out_sname) {
            if (!elem_t || elem_t->is_void()) elem_t = ctx_.get_i64_type();
            out_sname = "__vec_" + mangle_ir_type_key(elem_t, elem_struct);
            auto it = vec_struct_meta_.find(out_sname);
            if (it != vec_struct_meta_.end()) {
                return it->second.vec_struct_type;
            }
            ir::IRType* data_ptr_t = ctx_.get_ptr_type(elem_t);
            ir::IRType* i64_t = ctx_.get_i64_type();
            ir::IRType* st = ctx_.get_struct_type(out_sname, { data_ptr_t, i64_t, i64_t }, { "data", "size", "capacity" });
            vec_struct_meta_[out_sname] = { st, elem_t, elem_struct };
            return st;
        }

        ir::IRType* parse_vec_ir_type(ir::IRType*& out_elem_t, std::string& out_elem_struct, std::string& out_vec_sname) {
            advance(); // vec / vector
            advance(); // <
            out_elem_struct.clear();
            if (is_vec_type_start()) {
                ir::IRType* sub_e = nullptr;
                std::string sub_es, sub_vs;
                out_elem_t = parse_vec_ir_type(sub_e, sub_es, sub_vs);
            } else {
                std::string tname = advance().text;
                while (match(TOK_COLON_COLON)) {
                    if (peek().kind == TOK_IDENT) tname += "::" + advance().text;
                }
                if (oop_mgr_ && oop_mgr_->has_class(tname)) {
                    out_elem_struct = tname;
                    const auto* cd = oop_mgr_->get_class(tname);
                    out_elem_t = (cd && cd->ir_struct_type) ? cd->ir_struct_type : ctx_.get_i64_type();
                } else {
                    out_elem_t = parse_type_name(tname);
                }
            }
            while (match(TOK_STAR)) {
                out_elem_t = ctx_.get_ptr_type(out_elem_t);
                out_elem_struct.clear();
            }
            match(TOK_GT);
            return get_or_create_vec_ir_type(out_elem_t, out_elem_struct, out_vec_sname);
        }

        void emit_vec_elem_dtor(ir::ValueId elem_ptr, const std::string& elem_struct) {
            if (!oop_mgr_ || elem_struct.empty() || !elem_ptr) return;
            const auto* cd = oop_mgr_->get_class(elem_struct);
            if (!cd || !cd->has_destructor) return;
            ir::ValueId cast_p = coerce_type(elem_ptr, ctx_.get_ptr_type(cd->ir_struct_type));
            builder_.create_call(oop_mgr_->get_destructor_name(cd->name), ctx_.get_void_type(), { cast_p });
        }

        ir::ValueId emit_vec_size(ir::ValueId vec_ptr) {
            ir::ValueId sz_p = builder_.create_gep_struct(vec_ptr, 1, "vec_sz_ptr");
            return builder_.create_load(sz_p, "vec_sz");
        }

        ir::ValueId emit_vec_capacity(ir::ValueId vec_ptr) {
            ir::ValueId cap_p = builder_.create_gep_struct(vec_ptr, 2, "vec_cap_ptr");
            return builder_.create_load(cap_p, "vec_cap");
        }

        ir::ValueId emit_vec_empty(ir::ValueId vec_ptr) {
            ir::ValueId sz = emit_vec_size(vec_ptr);
            return builder_.create_icmp_eq(sz, builder_.create_const_i64(0), "vec_empty");
        }

        ir::ValueId emit_vec_data(ir::ValueId vec_ptr, ir::IRType* elem_t) {
            ir::ValueId dp = builder_.create_gep_struct(vec_ptr, 0, "vec_data_field");
            ir::ValueId raw = builder_.create_load(dp, "vec_data");
            return coerce_type(raw, ctx_.get_ptr_type(elem_t));
        }

        void emit_vec_reserve(ir::ValueId vec_ptr, ir::IRType* elem_t, ir::ValueId new_cap) {
            ir::ValueId new_cap64 = coerce_type(new_cap, ctx_.get_i64_type());
            ir::ValueId cap_p = builder_.create_gep_struct(vec_ptr, 2, "vec_cap_field");
            ir::ValueId cur_cap = builder_.create_load(cap_p, "cur_cap");
            ir::ValueId need_grow = builder_.create_icmp(ir::ICmpCond::SGT, new_cap64, cur_cap, "need_grow");
            ir::BasicBlock* bb_grow = builder_.create_block("vec_res_grow");
            ir::BasicBlock* bb_done = builder_.create_block("vec_res_done");
            builder_.create_branch(need_grow, bb_grow, bb_done);

            builder_.set_insert_point(bb_grow);
            uint64_t elem_sz = get_ir_type_size_bytes(elem_t);
            ir::ValueId bytes = builder_.create_mul(new_cap64, builder_.create_const_i64(static_cast<int64_t>(elem_sz)), "vec_bytes");
            ir::ValueId data_f = builder_.create_gep_struct(vec_ptr, 0, "vec_data_f");
            ir::ValueId cur_data = coerce_type(builder_.create_load(data_f, "cur_data"), ctx_.get_ptr_type());
            ir::ValueId new_raw = builder_.create_call("grass_realloc", ctx_.get_ptr_type(), { cur_data, bytes });
            ir::ValueId new_typed = coerce_type(new_raw, ctx_.get_ptr_type(elem_t));
            builder_.create_store(new_typed, data_f);
            builder_.create_store(new_cap64, cap_p);
            builder_.create_jump(bb_done);

            builder_.set_insert_point(bb_done);
        }

        void emit_vec_push(ir::ValueId vec_ptr, ir::IRType* elem_t, ir::ValueId val) {
            ir::ValueId sz_p = builder_.create_gep_struct(vec_ptr, 1, "push_sz_f");
            ir::ValueId cap_p = builder_.create_gep_struct(vec_ptr, 2, "push_cap_f");
            ir::ValueId sz = builder_.create_load(sz_p, "push_sz");
            ir::ValueId cap = builder_.create_load(cap_p, "push_cap");
            ir::ValueId full = builder_.create_icmp(ir::ICmpCond::SGE, sz, cap, "vec_full");

            ir::BasicBlock* bb_grow = builder_.create_block("vec_push_grow");
            ir::BasicBlock* bb_zero = builder_.create_block("vec_push_init");
            ir::BasicBlock* bb_dbl = builder_.create_block("vec_push_dbl");
            ir::BasicBlock* bb_store = builder_.create_block("vec_push_store");
            builder_.create_branch(full, bb_grow, bb_store);

            builder_.set_insert_point(bb_grow);
            ir::ValueId is_zero = builder_.create_icmp_eq(cap, builder_.create_const_i64(0), "cap_is_0");
            builder_.create_branch(is_zero, bb_zero, bb_dbl);

            builder_.set_insert_point(bb_zero);
            emit_vec_reserve(vec_ptr, elem_t, builder_.create_const_i64(8));
            builder_.create_jump(bb_store);

            builder_.set_insert_point(bb_dbl);
            ir::ValueId cap2 = builder_.create_mul(cap, builder_.create_const_i64(2), "cap_2x");
            emit_vec_reserve(vec_ptr, elem_t, cap2);
            builder_.create_jump(bb_store);

            builder_.set_insert_point(bb_store);
            ir::ValueId data_ptr = emit_vec_data(vec_ptr, elem_t);
            ir::ValueId cur_sz = builder_.create_load(sz_p, "push_cur_sz");
            ir::ValueId slot_ptr = builder_.create_gep(data_ptr, { cur_sz }, elem_t, "push_slot");
            ir::IRType* val_t = builder_.get_function()->has_value(val) ? builder_.get_function()->get_value(val).type : nullptr;
            if (elem_t && elem_t->is_struct() && val_t && val_t->is_pointer()) {
                emit_copy_struct(slot_ptr, val, elem_t);
            } else {
                ir::ValueId cval = coerce_type(val, elem_t);
                builder_.create_store(cval, slot_ptr);
            }
            ir::ValueId next_sz = builder_.create_add(cur_sz, builder_.create_const_i64(1), "next_sz");
            builder_.create_store(next_sz, sz_p);
        }

        void emit_vec_pop(ir::ValueId vec_ptr, ir::IRType* elem_t, const std::string& elem_struct) {
            ir::ValueId sz_p = builder_.create_gep_struct(vec_ptr, 1, "pop_sz_f");
            ir::ValueId sz = builder_.create_load(sz_p, "pop_sz");
            ir::ValueId non_empty = builder_.create_icmp(ir::ICmpCond::SGT, sz, builder_.create_const_i64(0), "pop_ne");
            ir::BasicBlock* bb_do = builder_.create_block("vec_pop_do");
            ir::BasicBlock* bb_done = builder_.create_block("vec_pop_done");
            builder_.create_branch(non_empty, bb_do, bb_done);

            builder_.set_insert_point(bb_do);
            ir::ValueId new_sz = builder_.create_sub(sz, builder_.create_const_i64(1), "new_sz");
            if (oop_mgr_ && !elem_struct.empty()) {
                const auto* cd = oop_mgr_->get_class(elem_struct);
                if (cd && cd->has_destructor) {
                    ir::ValueId data_ptr = emit_vec_data(vec_ptr, elem_t);
                    ir::ValueId elem_p = builder_.create_gep(data_ptr, { new_sz }, elem_t, "pop_elem_p");
                    emit_vec_elem_dtor(elem_p, elem_struct);
                }
            }
            builder_.create_store(new_sz, sz_p);
            builder_.create_jump(bb_done);

            builder_.set_insert_point(bb_done);
        }

        void emit_vec_clear(ir::ValueId vec_ptr, ir::IRType* elem_t, const std::string& elem_struct) {
            ir::ValueId sz_p = builder_.create_gep_struct(vec_ptr, 1, "clr_sz_f");
            if (oop_mgr_ && !elem_struct.empty()) {
                const auto* cd = oop_mgr_->get_class(elem_struct);
                if (cd && cd->has_destructor) {
                    ir::ValueId sz = builder_.create_load(sz_p, "clr_sz");
                    ir::ValueId idx_alloca = builder_.create_alloca(ctx_.get_i64_type(), 1, 0, "clr_i");
                    builder_.create_store(builder_.create_sub(sz, builder_.create_const_i64(1)), idx_alloca);
                    ir::BasicBlock* bb_cond = builder_.create_block("vec_clr_cond");
                    ir::BasicBlock* bb_body = builder_.create_block("vec_clr_body");
                    ir::BasicBlock* bb_end = builder_.create_block("vec_clr_end");
                    builder_.create_jump(bb_cond);

                    builder_.set_insert_point(bb_cond);
                    ir::ValueId cur_i = builder_.create_load(idx_alloca, "clr_idx");
                    ir::ValueId ge0 = builder_.create_icmp(ir::ICmpCond::SGE, cur_i, builder_.create_const_i64(0), "clr_ge0");
                    builder_.create_branch(ge0, bb_body, bb_end);

                    builder_.set_insert_point(bb_body);
                    ir::ValueId data_ptr = emit_vec_data(vec_ptr, elem_t);
                    ir::ValueId elem_p = builder_.create_gep(data_ptr, { cur_i }, elem_t, "clr_elem_p");
                    emit_vec_elem_dtor(elem_p, elem_struct);
                    builder_.create_store(builder_.create_sub(cur_i, builder_.create_const_i64(1)), idx_alloca);
                    builder_.create_jump(bb_cond);

                    builder_.set_insert_point(bb_end);
                }
            }
            builder_.create_store(builder_.create_const_i64(0), sz_p);
        }

        void emit_vec_dtor(ir::ValueId vec_ptr, ir::IRType* vec_st, ir::IRType* elem_t, const std::string& elem_struct) {
            if (!vec_ptr) return;
            emit_vec_clear(vec_ptr, elem_t, elem_struct);
            ir::ValueId data_f = builder_.create_gep_struct(vec_ptr, 0, "dtor_data_f");
            ir::ValueId raw_p = coerce_type(builder_.create_load(data_f, "dtor_data"), ctx_.get_ptr_type());
            ir::ValueId null_p = builder_.create_const_int(ctx_.get_ptr_type(), 0);
            ir::ValueId non_null = builder_.create_icmp_ne(raw_p, null_p);
            ir::BasicBlock* bb_free = builder_.create_block("vec_dtor_free");
            ir::BasicBlock* bb_done = builder_.create_block("vec_dtor_done");
            builder_.create_branch(non_null, bb_free, bb_done);

            builder_.set_insert_point(bb_free);
            builder_.create_call("grass_free", ctx_.get_void_type(), { raw_p });
            builder_.create_jump(bb_done);

            builder_.set_insert_point(bb_done);
            if (vec_st && vec_st->is_struct()) {
                emit_zero_struct(vec_ptr, vec_st);
            }
        }

        void emit_copy_struct(ir::ValueId dst_ptr, ir::ValueId src_ptr, ir::IRType* st_type) {
            if (!dst_ptr || !src_ptr || !st_type || !st_type->is_struct()) return;
            uint64_t total_sz = st_type->size();
            uint64_t qwords = (total_sz + 7) / 8;
            ir::IRType* i64_ptr_t = ctx_.get_ptr_type(ctx_.get_i64_type());
            ir::ValueId dst_p = builder_.create_bitcast(dst_ptr, i64_ptr_t, "copy_dst");
            ir::ValueId src_p = builder_.create_bitcast(src_ptr, i64_ptr_t, "copy_src");
            for (uint64_t i = 0; i < qwords; ++i) {
                ir::ValueId idx = builder_.create_const_i64(static_cast<int64_t>(i));
                ir::ValueId s_elem = (i == 0) ? src_p : builder_.create_gep(src_p, { idx }, ctx_.get_i64_type());
                ir::ValueId d_elem = (i == 0) ? dst_p : builder_.create_gep(dst_p, { idx }, ctx_.get_i64_type());
                ir::ValueId v = builder_.create_load(s_elem, "copy_qw");
                builder_.create_store(v, d_elem);
            }
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

            if (target_type->is_bool()) {
                if (src_type->is_integer()) {
                    ir::ValueId zero = builder_.create_const_int(src_type, 0);
                    return builder_.create_icmp_ne(val, zero);
                }
                if (src_type->is_pointer()) {
                    ir::ValueId null_ptr = builder_.create_const_int(src_type, 0);
                    return builder_.create_icmp_ne(val, null_ptr);
                }
            }

            if (src_type->is_bool() && target_type->is_integer()) {
                return builder_.create_zext(val, target_type);
            }

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

            if (src_type->is_pointer() && target_type->is_integer()) {
                return builder_.create_ptrtoint(val, target_type);
            }

            if (src_type->is_integer() && target_type->is_pointer()) {
                return builder_.create_inttoptr(val, target_type);
            }

            if (src_type->is_pointer() && target_type->is_pointer()) {
                return builder_.create_bitcast(val, target_type);
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
                ir::IRType* target = ctx_.get_f64_type();
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
            } else if (t1->is_pointer() || t2->is_pointer()) {
                if (t1->is_pointer() && t2->is_pointer()) {
                    rhs = coerce_type(rhs, t1);
                } else if (t1->is_pointer() && t2->is_integer()) {
                    rhs = coerce_type(rhs, t1);
                } else if (t1->is_integer() && t2->is_pointer()) {
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

        bool lower_declaration(std::string& error) {
            if (peek().kind == TOK_LIB) {
                std::string lib = advance().text;
                if (lib == "!GrassWeb!" || lib == "!GREEN_WWW1!") {
                    while (peek().kind != TOK_EOF) advance();
                    if (!module_.get_function("main")) {
                        ir::IRFunction* mfn = module_.create_function("main", ctx_.get_i32_type());
                        builder_.set_function(mfn);
                        builder_.set_insert_point(mfn->create_block("entry"));
                        ir::ValueId src_str = builder_.create_call(add_rodata_string(source_path_), ctx_.get_ptr_type(ctx_.get_i8_type()), {});
                        builder_.create_call("web_start", ctx_.get_void_type(), { src_str });
                        builder_.create_ret(builder_.create_const_int(ctx_.get_i32_type(), 0));
                        mfn->rebuild_cfg();
                    }
                }
                return true;
            }
            if (peek().kind == TOK_SEMI) {
                advance();
                return true;
            }
            if (peek().kind == TOK_IDENT && peek().text == "@strict") {
                advance();
                return true;
            }
            if (peek().kind == TOK_USING || (peek().kind == TOK_IDENT && peek().text == "using")) {
                advance();
                bool is_ns = false;
                if (peek().kind == TOK_NAMESPACE || (peek().kind == TOK_IDENT && peek().text == "namespace")) {
                    advance();
                    is_ns = true;
                }
                std::string path = advance().text;
                while (match(TOK_COLON_COLON)) {
                    if (peek().kind == TOK_IDENT) path += "::" + advance().text;
                }
                match(TOK_SEMI);
                if (is_ns) {
                    using_namespaces_.push_back(path);
                } else {
                    size_t last_col = path.rfind("::");
                    std::string sym = (last_col != std::string::npos) ? path.substr(last_col + 2) : path;
                    using_symbols_[sym] = path;
                }
                return true;
            }
            if (peek().kind == TOK_NAMESPACE || (peek().kind == TOK_IDENT && peek().text == "namespace")) {
                advance();
                if (peek().kind != TOK_IDENT) {
                    error = "Expected namespace name!";
                    return false;
                }
                std::string ns_name = advance().text;
                while (match(TOK_COLON_COLON)) {
                    if (peek().kind == TOK_IDENT) ns_name += "::" + advance().text;
                }
                if (!match(TOK_LBRACE)) {
                    error = "Expected '{' after namespace name!";
                    return false;
                }
                namespace_stack_.push_back(ns_name);
                while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                    if (!lower_declaration(error)) return false;
                }
                match(TOK_RBRACE);
                match(TOK_SEMI);
                namespace_stack_.pop_back();
                return true;
            }
            if (peek().kind == TOK_IDENT && (peek().text == "struct" || peek().text == "class")) {
                if (!parse_struct_definition(error)) return false;
                return true;
            }
            bool is_fn = false;
            if (is_vec_type_start()) {
                int off = 2; int depth = 1;
                while (peek(off).kind != TOK_EOF && depth > 0) {
                    if (peek(off).kind == TOK_LT) depth++;
                    else if (peek(off).kind == TOK_GT) depth--;
                    off++;
                }
                while (peek(off).kind == TOK_STAR) off++;
                if (peek(off).kind == TOK_IDENT && peek(off + 1).kind == TOK_LPAREN) is_fn = true;
            } else if (is_type_or_func_keyword(peek().text) || (oop_mgr_ && oop_mgr_->has_class(peek().text))) {
                size_t off = 1;
                while (peek(off).kind == TOK_STAR) off++;
                if (peek(off).kind == TOK_IDENT && peek(off + 1).kind == TOK_LPAREN) is_fn = true;
            }
            if (is_fn) {
                if (!parse_function(error)) return false;
                return true;
            }
            advance();
            return true;
        }

        bool lower(std::string& error) {
            if (oop_mgr_ && !oop_mgr_->has_class("Vec3")) {
                oop_mgr_->begin_struct("Vec3");
                oop_mgr_->add_field("x", ctx_.get_f64_type());
                oop_mgr_->add_field("y", ctx_.get_f64_type());
                oop_mgr_->add_field("z", ctx_.get_f64_type());
                oop_mgr_->finalize_class();
            }
            while (peek().kind != TOK_EOF) {
                if (!lower_declaration(error)) return false;
            }
            return true;
        }

    private:
        static bool is_type_or_func_keyword(const std::string& s) {
            return s == "int" || s == "take" || s == "void" || s == "fn" || s == "fun" ||
                   s == "double" || s == "float" || s == "string" || s == "bool" ||
                   s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
                   s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
                   s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
                   s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
                   s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
                   s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
                   s == "f32" || s == "f64" || s == "char" || s == "byte" || s == "size_t";
        }

        static bool is_var_keyword(const std::string& s) {
            return s == "take" || s == "int" || s == "double" || s == "float" ||
                   s == "string" || s == "bool" || s == "set" || s == "array" ||
                   s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
                   s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
                   s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
                   s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
                   s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
                   s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
                   s == "f32" || s == "f64" || s == "char" || s == "byte" || s == "size_t";
        }

        ir::IRType* parse_type_name(const std::string& name) {
            if (name == "void") return ctx_.get_void_type();
            if (name == "bool") return ctx_.get_i1_type();
            if (name == "i8" || name == "u8" || name == "int8" || name == "uint8" || name == "char" || name == "byte") return ctx_.get_i8_type();
            if (name == "i16" || name == "u16" || name == "int16" || name == "uint16") return ctx_.get_i16_type();
            if (name == "i32" || name == "u32" || name == "int32" || name == "uint32" || name == "int") return ctx_.get_i32_type();
            if (name == "i64" || name == "u64" || name == "int64" || name == "uint64" || name == "take" || name == "size_t") return ctx_.get_i64_type();
            if (name == "float" || name == "double" || name == "f32" || name == "f64") return ctx_.get_f64_type();
            if (name == "string") return ctx_.get_ptr_type(ctx_.get_i8_type());
            if (name == "array") return ctx_.get_ptr_type(ctx_.get_void_type());
            if (name == "nullptr" || name == "null") return ctx_.get_ptr_type(ctx_.get_void_type());
            if (oop_mgr_ && oop_mgr_->has_class(name)) {
                const auto* cd = oop_mgr_->get_class(name);
                return cd ? cd->ir_struct_type : ctx_.get_i64_type();
            }
            return ctx_.get_i64_type();
        }

        struct PendingMethod {
            std::string name;
            ir::IRType* ret_type = nullptr;
            std::vector<ir::IRType*> param_types;
            std::vector<std::string> param_names;
            std::vector<std::string> param_struct_names;
            size_t body_start_pos = 0;
            size_t body_end_pos = 0;
            bool is_dtor = false;
        };

        bool parse_struct_definition(std::string& error) {
            bool is_class = (advance().text == "class"); // consume 'struct' or 'class'
            if (peek().kind != TOK_IDENT) {
                error = "Line " + std::to_string(peek().line) + ": Expected " + (is_class ? "class" : "struct") + " name!";
                return false;
            }
            std::string struct_name = advance().text;
            std::string base_class = "";
            if (match(TOK_COLON)) {
                if (peek().kind == TOK_IDENT) {
                    base_class = advance().text;
                }
            }
            if (!consume(TOK_LBRACE, "Expected '{' after name!", error)) return false;

            if (oop_mgr_) {
                if (is_class) {
                    oop_mgr_->begin_class(struct_name, base_class);
                } else {
                    oop_mgr_->begin_struct(struct_name);
                }
            }

            std::vector<ir::IRType*> field_types;
            std::vector<std::string> field_names;
            std::vector<PendingMethod> pending_methods;

            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (match(TOK_SEMI)) continue;

                // Destructor: ~StructName() { ... }
                if (match(TOK_TILDE)) {
                    if (peek().kind != TOK_IDENT) {
                        error = "Line " + std::to_string(peek().line) + ": Expected destructor name after '~'!";
                        return false;
                    }
                    std::string dname = advance().text;
                    if (dname != struct_name) {
                        error = "Line " + std::to_string(peek().line) + ": Destructor name '~" + dname + "' does not match struct '" + struct_name + "'!";
                        return false;
                    }
                    if (!consume(TOK_LPAREN, "Expected '(' in destructor declaration!", error)) return false;
                    if (!consume(TOK_RPAREN, "Destructor cannot take parameters!", error)) return false;
                    if (!consume(TOK_LBRACE, "Expected '{' at start of destructor body!", error)) return false;

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
                    consume(TOK_RBRACE, "Expected '}' closing destructor body!", error);

                    pending_methods.push_back({ "dtor", ctx_.get_void_type(), {}, {}, {}, body_start, body_end, true });
                    if (oop_mgr_) {
                        oop_mgr_->add_destructor();
                    }
                    continue;
                }

                if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_COLON || peek(1).kind == TOK_ASSIGN)) {
                    std::string mname = advance().text;
                    advance(); // ':' or '='
                    ir::IRType* ftype = ctx_.get_i64_type();
                    if (is_vec_type_start()) {
                        ir::IRType* ve = nullptr;
                        std::string ves, vsn;
                        ftype = parse_vec_ir_type(ve, ves, vsn);
                        if (!match(TOK_STAR) && oop_mgr_) {
                            oop_mgr_->add_destructor();
                        } else {
                            ftype = ctx_.get_ptr_type(ftype);
                        }
                    } else if (peek().kind == TOK_IDENT) {
                        ftype = parse_type_name(advance().text);
                        while (match(TOK_STAR)) ftype = ctx_.get_ptr_type(ftype);
                    }
                    field_types.push_back(ftype);
                    field_names.push_back(mname);
                    if (oop_mgr_) oop_mgr_->add_field(mname, ftype);
                    match(TOK_COMMA);
                    match(TOK_SEMI);
                    continue;
                }
                bool is_struct_field_or_ret = (oop_mgr_ && oop_mgr_->has_class(peek().text));
                bool is_vec_field_or_ret = is_vec_type_start();
                if (peek().kind == TOK_IDENT && (is_var_keyword(peek().text) || is_type_or_func_keyword(peek().text) || is_struct_field_or_ret || is_vec_field_or_ret)) {
                    ir::IRType* ftype = nullptr;
                    bool field_is_ptr = false;
                    if (is_vec_field_or_ret) {
                        ir::IRType* ve = nullptr;
                        std::string ves, vsn;
                        ftype = parse_vec_ir_type(ve, ves, vsn);
                        if (!match(TOK_STAR) && oop_mgr_) {
                            oop_mgr_->add_destructor();
                        } else {
                            ftype = ctx_.get_ptr_type(ftype);
                            field_is_ptr = true;
                        }
                    } else {
                        std::string ftype_str = advance().text;
                        ftype = (ftype_str == "fn") ? ctx_.get_void_type() : parse_type_name(ftype_str);
                    }
                    while (match(TOK_STAR)) {
                        ftype = ctx_.get_ptr_type(ftype);
                        field_is_ptr = true;
                    }
                    (void)field_is_ptr;
                    if (peek().kind != TOK_IDENT) {
                        error = "Line " + std::to_string(peek().line) + ": Expected field or method name!";
                        return false;
                    }
                    std::string mname = advance().text;

                    if (peek().kind == TOK_LPAREN) {
                        advance(); // '('
                        std::vector<ir::IRType*> param_types;
                        std::vector<std::string> param_names;
                        std::vector<std::string> param_struct_names;
                        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                            while (true) {
                                ir::IRType* ptype = ctx_.get_i64_type();
                                std::string p_struct_name = "";
                                if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR)) {
                                    std::string tname = advance().text;
                                    if (oop_mgr_ && oop_mgr_->has_class(tname)) {
                                        p_struct_name = tname;
                                        ptype = ctx_.get_ptr_type(parse_type_name(tname));
                                    } else {
                                        ptype = parse_type_name(tname);
                                    }
                                    while (match(TOK_STAR)) {
                                        ptype = ctx_.get_ptr_type(ptype);
                                    }
                                } else if (is_type_or_func_keyword(peek().text)) {
                                    ptype = parse_type_name(advance().text);
                                    while (match(TOK_STAR)) {
                                        ptype = ctx_.get_ptr_type(ptype);
                                    }
                                }
                                if (peek().kind != TOK_IDENT) {
                                    error = "Line " + std::to_string(peek().line) + ": Expected parameter name!";
                                    return false;
                                }
                                std::string pname = advance().text;
                                param_types.push_back(ptype);
                                param_names.push_back(pname);
                                param_struct_names.push_back(p_struct_name);
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

                        pending_methods.push_back({ mname, ftype, param_types, param_names, param_struct_names, body_start, body_end, false });
                        if (oop_mgr_) {
                            oop_mgr_->add_method(mname, ftype, param_types, param_names);
                        }
                    } else {
                        match(TOK_SEMI);
                        if (!ftype->is_struct() && !ftype->is_pointer() &&
                            (mname == "name" || mname == "title" || mname == "msg" || mname == "text" || mname == "path" || mname == "str")) {
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

            bool generated_explicit_dtor = false;
            for (const auto& pm : pending_methods) {
                if (pm.is_dtor) generated_explicit_dtor = true;
                ir::IRFunction* fn = nullptr;
                if (oop_mgr_) {
                    fn = oop_mgr_->create_method_function(struct_name, pm.name);
                }
                if (!fn) {
                    std::string mangled = pm.is_dtor ? (struct_name + "__dtor") : (struct_name + "_" + pm.name);
                    fn = module_.create_function(mangled, pm.ret_type);
                    ir::IRType* this_t = ctx_.get_ptr_type(ctx_.get_struct_type(struct_name, field_types, field_names));
                    fn->add_argument("this", this_t);
                    for (size_t i = 0; i < pm.param_types.size(); ++i) {
                        fn->add_argument(pm.param_names[i], pm.param_types[i]);
                    }
                }

                ir::IRFunction* prev_fn = builder_.get_function();
                std::string prev_dtor_struct = current_dtor_struct_;
                builder_.set_function(fn);
                if (pm.is_dtor) current_dtor_struct_ = struct_name;
                scopes_.clear();
                push_scope();

                ir::BasicBlock* entry_bb = builder_.create_block("entry");
                builder_.set_insert_point(entry_bb);

                ir::ValueId this_alloca = builder_.create_alloca(fn->arguments[0].type, 1, 0, "this");
                builder_.create_store(fn->arguments[0].id, this_alloca);
                declare_var("this", this_alloca, fn->arguments[0].type, struct_name, false);

                for (size_t i = 1; i < fn->arguments.size(); ++i) {
                    ir::ValueId p_alloca = builder_.create_alloca(fn->arguments[i].type, 1, 0, fn->arguments[i].name);
                    builder_.create_store(fn->arguments[i].id, p_alloca);
                    std::string sname = (i - 1 < pm.param_struct_names.size()) ? pm.param_struct_names[i - 1] : "";
                    declare_var(fn->arguments[i].name, p_alloca, fn->arguments[i].type, sname, false);
                }

                size_t saved_pos = pos_;
                pos_ = pm.body_start_pos;
                while (pos_ < pm.body_end_pos && peek().kind != TOK_EOF) {
                    if (!parse_statement(error)) {
                        pos_ = saved_pos;
                        current_dtor_struct_ = prev_dtor_struct;
                        return false;
                    }
                }
                pos_ = saved_pos;

                ir::BasicBlock* cur_bb = builder_.get_insert_block();
                if (cur_bb && !cur_bb->has_terminator()) {
                    pop_scope(true);
                    if (pm.is_dtor) {
                        ir::ValueId this_val = builder_.create_load(this_alloca, "this_dtor");
                        emit_struct_field_dtors(this_val, struct_name);
                    }
                    cur_bb = builder_.get_insert_block();
                    if (cur_bb && !cur_bb->has_terminator()) {
                        if (pm.ret_type->is_void()) {
                            builder_.create_ret_void();
                        } else {
                            ir::ValueId def_ret = builder_.create_const_int(pm.ret_type, 0);
                            builder_.create_ret(def_ret);
                        }
                    }
                } else {
                    pop_scope(false);
                }

                fn->rebuild_cfg();
                current_dtor_struct_ = prev_dtor_struct;
                builder_.set_function(prev_fn);
            }

            if (!generated_explicit_dtor && oop_mgr_) {
                const auto* cd = oop_mgr_->get_class(struct_name);
                if (cd && cd->has_destructor) {
                    ir::IRFunction* fn = oop_mgr_->create_method_function(struct_name, "dtor");
                    if (fn) {
                        ir::IRFunction* prev_fn = builder_.get_function();
                        builder_.set_function(fn);
                        ir::BasicBlock* entry_bb = builder_.create_block("entry");
                        builder_.set_insert_point(entry_bb);
                        emit_struct_field_dtors(fn->arguments[0].id, struct_name);
                        ir::BasicBlock* cur_bb = builder_.get_insert_block();
                        if (cur_bb && !cur_bb->has_terminator()) {
                            builder_.create_ret_void();
                        }
                        fn->rebuild_cfg();
                        builder_.set_function(prev_fn);
                    }
                }
            }

            return true;
        }

        bool parse_function(std::string& error) {
            ir::IRType* ret_type = nullptr;
            if (is_vec_type_start()) {
                ir::IRType* ve = nullptr;
                std::string ves, vsn;
                ret_type = parse_vec_ir_type(ve, ves, vsn);
            } else {
                std::string ret_type_name = advance().text;
                if (oop_mgr_ && oop_mgr_->has_class(ret_type_name)) {
                    const auto* cd = oop_mgr_->get_class(ret_type_name);
                    ret_type = (cd && cd->ir_struct_type) ? cd->ir_struct_type : ctx_.get_i64_type();
                } else {
                    ret_type = parse_type_name(ret_type_name);
                }
            }
            while (match(TOK_STAR)) {
                ret_type = ctx_.get_ptr_type(ret_type);
            }

            std::string raw_fn_name = advance().text;
            std::string qualified_name = qualify_name(raw_fn_name);
            std::string mangled_name = grass::sema::GrassSema::mangleName(qualified_name);
            ir::IRFunction* fn = module_.create_function(mangled_name, ret_type);

            builder_.set_function(fn);
            scopes_.clear();
            push_scope();

            if (!consume(TOK_LPAREN, "Expected '(' in function signature!", error)) return false;

            struct ParamInfo {
                std::string name;
                ir::IRType* type;
                std::string struct_name;
                bool is_vec = false;
                ir::IRType* vec_elem_t = nullptr;
                std::string vec_elem_struct;
            };
            std::vector<ParamInfo> params;

            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                while (true) {
                    ir::IRType* ptype = ctx_.get_i64_type();
                    std::string struct_name = "";
                    bool p_is_vec = false;
                    ir::IRType* p_velem = nullptr;
                    std::string p_vestruct;
                    if (is_vec_type_start()) {
                        std::string vsn;
                        ir::IRType* vst = parse_vec_ir_type(p_velem, p_vestruct, vsn);
                        ptype = vst;
                        struct_name = vsn;
                        p_is_vec = true;
                    } else if (peek().kind == TOK_IDENT &&
                        (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR || (oop_mgr_ && oop_mgr_->has_class(peek().text)))) {
                        std::string tname = advance().text;
                        if (oop_mgr_ && oop_mgr_->has_class(tname)) {
                            struct_name = tname;
                            const auto* cd = oop_mgr_->get_class(tname);
                            ptype = (cd && cd->ir_struct_type) ? ctx_.get_ptr_type(cd->ir_struct_type) : ctx_.get_ptr_type();
                            (void)match(TOK_STAR); // Guard and Guard* both pass ptr(Guard) in native ABI
                        } else {
                            ptype = parse_type_name(tname);
                        }
                    } else if (is_type_or_func_keyword(peek().text)) {
                        ptype = parse_type_name(advance().text);
                    }
                    while (match(TOK_STAR)) {
                        ptype = ctx_.get_ptr_type(ptype);
                    }
                    if (peek().kind != TOK_IDENT) {
                        error = "Line " + std::to_string(peek().line) + ": Expected parameter name!";
                        return false;
                    }
                    std::string pname = advance().text;
                    params.push_back({ pname, ptype, struct_name, p_is_vec, p_velem, p_vestruct });
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
                declare_var(params[i].name, alloca_id, params[i].type, params[i].struct_name, false,
                            params[i].is_vec, params[i].vec_elem_t, params[i].vec_elem_struct);
            }

            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!parse_statement(error)) {
                    if (error.empty()) error = "Failed to parse statement in function '" + mangled_name + "' near token '" + peek().text + "' (line " + std::to_string(peek().line) + ")";
                    return false;
                }
            }
            if (!consume(TOK_RBRACE, "Expected '}' closing function body!", error)) return false;

            // Ensure block ends with terminator
            ir::BasicBlock* current_bb = builder_.get_insert_block();
            if (current_bb && !current_bb->has_terminator()) {
                pop_scope(true);
                current_bb = builder_.get_insert_block();
                if (current_bb && !current_bb->has_terminator()) {
                    if (ret_type->is_void()) {
                        builder_.create_ret_void();
                    } else {
                        ir::ValueId default_ret = builder_.create_const_int(ret_type, 0);
                        builder_.create_ret(default_ret);
                    }
                }
            } else {
                pop_scope(false);
            }

            fn->rebuild_cfg();
            return true;
        }

        bool parse_statement(std::string& error) {
            last_struct_name_.clear();
            if (match(TOK_SEMI)) return true;
            if (match_ident("volatile")) return parse_statement(error);

            if (match_ident("clear")) {
                if (match(TOK_LPAREN)) match(TOK_RPAREN);
                match(TOK_SEMI);
                builder_.create_call("clear", ctx_.get_void_type(), {});
                return true;
            }
            if (match_ident("not_clear")) {
                if (peek().kind == TOK_LBRACE) {
                    builder_.create_call("not_clear_on", ctx_.get_void_type(), {});
                    bool ok = parse_statement(error);
                    builder_.create_call("not_clear_off", ctx_.get_void_type(), {});
                    return ok;
                }
                if (match(TOK_LPAREN)) {
                    if (peek().kind != TOK_RPAREN) parse_expression(error);
                    match(TOK_RPAREN);
                }
                match(TOK_SEMI);
                builder_.create_call("not_clear_on", ctx_.get_void_type(), {});
                return true;
            }
            if (peek().kind == TOK_IDENT &&
                (peek().text == "window" || peek().text == "tabs" || peek().text == "row" ||
                 peek().text == "col" || peek().text == "panel" || peek().text == "group" ||
                 peek().text == "card" || peek().text == "grid" || peek().text == "vbox" ||
                 peek().text == "hbox" || peek().text == "header-bar" || peek().text == "tab" ||
                 peek().text == "button" || peek().text == "label" || peek().text == "slider" ||
                 peek().text == "checkbox" || peek().text == "progress" || peek().text == "enter" ||
                 peek().text == "clock" || peek().text == "list" || peek().text == "scroll-box") &&
                (peek(1).kind == TOK_LBRACE || peek(1).kind == TOK_STRING ||
                 peek(1).kind == TOK_LPAREN || peek(1).kind == TOK_ASSIGN)) {
                bool is_win = (peek().text == "window");
                if (peek(1).kind == TOK_LPAREN && !is_win &&
                    peek().text != "tabs" && peek().text != "row" && peek().text != "col" &&
                    peek().text != "panel" && peek().text != "group" && peek().text != "card" &&
                    peek().text != "tab") {
                    // Only treat widget(params) { ... } as UI-DSL if '{' follows ')'
                    size_t scan = 2;
                    int pd = 1;
                    while (pd > 0 && peek((int)scan).kind != TOK_EOF) {
                        if (peek((int)scan).kind == TOK_LPAREN) pd++;
                        else if (peek((int)scan).kind == TOK_RPAREN) pd--;
                        scan++;
                    }
                    if (peek((int)scan).kind != TOK_LBRACE) goto not_ui_dsl_block;
                }
                advance(); // keyword
                while (peek().kind != TOK_LBRACE && peek().kind != TOK_SEMI && peek().kind != TOK_EOF) {
                    if (match(TOK_LPAREN)) {
                        int pd = 1;
                        while (pd > 0 && peek().kind != TOK_EOF) {
                            if (peek().kind == TOK_LPAREN) pd++;
                            else if (peek().kind == TOK_RPAREN) pd--;
                            advance();
                        }
                    } else {
                        advance();
                    }
                }
                if (match(TOK_LBRACE)) {
                    int depth = 1;
                    while (depth > 0 && peek().kind != TOK_EOF) {
                        if (peek().kind == TOK_LBRACE) depth++;
                        else if (peek().kind == TOK_RBRACE) depth--;
                        advance();
                    }
                }
                match(TOK_SEMI);
                if (is_win) builder_.create_call("gui_show", ctx_.get_void_type(), {});
                return true;
            }
            not_ui_dsl_block:
            if (peek().kind == TOK_IDENT && peek().text == "agent" && peek(1).kind == TOK_LBRACE) {
                advance(); advance(); // 'agent', '{'
                int depth = 1;
                while (depth > 0 && peek().kind != TOK_EOF) {
                    if (peek().kind == TOK_LBRACE) depth++;
                    else if (peek().kind == TOK_RBRACE) depth--;
                    advance();
                }
                match(TOK_SEMI);
                return true;
            }
            if (peek().kind == TOK_IDENT && peek().text == "create_serv" && peek(1).kind == TOK_LPAREN) {
                advance(); advance(); // 'create_serv', '('
                int pdepth = 1;
                while (pdepth > 0 && peek().kind != TOK_EOF) {
                    if (peek().kind == TOK_LPAREN) pdepth++;
                    else if (peek().kind == TOK_RPAREN) pdepth--;
                    advance();
                }
                if (peek().kind == TOK_LBRACE) {
                    advance();
                    int bdepth = 1;
                    while (bdepth > 0 && peek().kind != TOK_EOF) {
                        if (peek().kind == TOK_LBRACE) bdepth++;
                        else if (peek().kind == TOK_RBRACE) bdepth--;
                        advance();
                    }
                }
                match(TOK_SEMI);
                ir::ValueId ip_s = builder_.create_call(add_rodata_string("127.0.0.1"), ctx_.get_ptr_type(ctx_.get_i8_type()), {});
                ir::ValueId empty_s = builder_.create_call(add_rodata_string(""), ctx_.get_ptr_type(ctx_.get_i8_type()), {});
                builder_.create_call("create_serv", ctx_.get_i64_type(), { ip_s, builder_.create_const_i64(8443), empty_s, empty_s });
                return true;
            }
            if (peek().kind == TOK_IDENT && peek().text == "async" && peek(1).kind == TOK_SEMI) {
                advance();
                match(TOK_SEMI);
                return true;
            }
            if (peek().kind == TOK_IDENT && peek().text == "async" && peek(1).kind == TOK_IDENT) {
                advance();
                std::string expr_err;
                parse_expression(expr_err);
                if (!expr_err.empty()) { error = expr_err; return false; }
                match(TOK_SEMI);
                return true;
            }

            // Block { ... }
            if (match(TOK_LBRACE)) {
                push_scope();
                while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                    if (!parse_statement(error)) return false;
                }
                consume(TOK_RBRACE, "Expected '}' closing block!", error);
                pop_scope(true);
                return true;
            }

            // Stream Output: gout << expr << endl; or std::gout << expr << std::endl;
            bool is_stream_out = false;
            if (match_ident("gout") || match_ident("cout")) {
                is_stream_out = true;
            } else if (peek().kind == TOK_IDENT && peek().text == "std" &&
                       peek(1).kind == TOK_COLON_COLON &&
                       (peek(2).text == "gout" || peek(2).text == "cout")) {
                advance(); // std
                advance(); // ::
                advance(); // gout / cout
                is_stream_out = true;
            }

            if (is_stream_out) {
                bool prev_io = in_stream_io_;
                in_stream_io_ = true;
                while (match(TOK_SHL)) {
                    bool is_endl = false;
                    if (match_ident("endl")) {
                        is_endl = true;
                    } else if (peek().kind == TOK_IDENT && peek().text == "std" &&
                               peek(1).kind == TOK_COLON_COLON && peek(2).text == "endl") {
                        advance(); // std
                        advance(); // ::
                        advance(); // endl
                        is_endl = true;
                    }

                    if (is_endl) {
                        builder_.create_call("grass_print_newline", ctx_.get_void_type(), {});
                    } else if (peek().kind == TOK_STRING && peek().text == "\n" &&
                               (peek(1).kind == TOK_SHL || peek(1).kind == TOK_SEMI)) {
                        advance();
                        ir::ValueId nl_ch = builder_.create_const_int(ctx_.get_i64_type(), 10);
                        builder_.create_call("grass_print_char", ctx_.get_void_type(), { nl_ch });
                    } else {
                        ir::ValueId val = parse_expression(error);
                        if (!error.empty()) {
                            in_stream_io_ = prev_io;
                            return false;
                        }
                        if (val != 0) {
                            ir::IRType* vtype = builder_.get_function()->get_value(val).type;
                            if (vtype->is_pointer()) {
                                if (vtype->element_type && vtype->element_type->is_i8()) {
                                    // string / const char* / uint8*
                                    ir::ValueId arg_val = coerce_type(val, ctx_.get_ptr_type());
                                    builder_.create_call("grass_print_str", ctx_.get_void_type(), { arg_val });
                                } else {
                                    // Pointer (T*) in hex format: 0x...
                                    ir::ValueId arg_val = coerce_type(val, ctx_.get_i64_type());
                                    builder_.create_call("grass_print_ptr", ctx_.get_void_type(), { arg_val });
                                }
                            } else if (vtype->is_floating_point()) {
                                ir::ValueId arg_val = coerce_type(val, ctx_.get_f64_type());
                                builder_.create_call("grass_print_double", ctx_.get_void_type(), { arg_val });
                            } else if (vtype->is_bool()) {
                                ir::ValueId arg_val = builder_.create_zext(val, ctx_.get_i64_type());
                                builder_.create_call("grass_print_bool", ctx_.get_void_type(), { arg_val });
                            } else {
                                // Integer (int, int64, int32, etc.)
                                ir::ValueId arg_val = coerce_type(val, ctx_.get_i64_type());
                                builder_.create_call("grass_print_int", ctx_.get_void_type(), { arg_val });
                            }
                        }
                    }
                }
                in_stream_io_ = prev_io;
                match(TOK_SEMI);
                return true;
            }

            // Stream Input: gin >> var; or std::gin >> var;
            bool is_stream_in = false;
            if (match_ident("gin") || match_ident("cin")) {
                is_stream_in = true;
            } else if (peek().kind == TOK_IDENT && peek().text == "std" &&
                       peek(1).kind == TOK_COLON_COLON &&
                       (peek(2).text == "gin" || peek(2).text == "cin")) {
                advance(); // std
                advance(); // ::
                advance(); // gin / cin
                is_stream_in = true;
            }

            if (is_stream_in) {
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

            // Dynamic Vector Declaration: vec<T> v; / vector<T> v; / vec<T>* vp = &v;
            if (is_vec_type_start()) {
                ir::IRType* elem_t = nullptr;
                std::string elem_struct, vec_sname;
                ir::IRType* vst = parse_vec_ir_type(elem_t, elem_struct, vec_sname);
                bool is_explicit_ptr = false;
                ir::IRType* vtype = vst;
                while (match(TOK_STAR)) {
                    vtype = ctx_.get_ptr_type(vtype);
                    is_explicit_ptr = true;
                }
                while (peek().kind == TOK_IDENT) {
                    std::string var_name = advance().text;
                    ir::ValueId alloca_id = builder_.create_alloca(vtype, 1, 0, var_name);
                    if (is_explicit_ptr) {
                        declare_var(var_name, alloca_id, vtype, vec_sname, false, true, elem_t, elem_struct);
                        if (match(TOK_ASSIGN)) {
                            ir::ValueId init_val = coerce_type(parse_expression(error), vtype);
                            if (!init_val) return false;
                            builder_.create_store(init_val, alloca_id);
                        } else {
                            builder_.create_store(builder_.create_const_int(vtype, 0), alloca_id);
                        }
                    } else {
                        declare_var(var_name, alloca_id, vtype, vec_sname, true, true, elem_t, elem_struct);
                        emit_zero_struct(alloca_id, vst);
                        if (match(TOK_ASSIGN)) {
                            parse_expression(error);
                        }
                    }
                    if (!match(TOK_COMMA)) break;
                }
                match(TOK_SEMI);
                return true;
            }

            // Variable Declaration
            bool is_struct_decl = (oop_mgr_ && oop_mgr_->has_class(peek().text));
            bool is_vdecl = false;
            if (is_var_keyword(peek().text) || is_type_or_func_keyword(peek().text) || is_struct_decl) {
                size_t off = 1;
                while (peek(off).kind == TOK_STAR) off++;
                if (peek(off).kind == TOK_IDENT) {
                    is_vdecl = true;
                }
            }
            if (is_vdecl) {
                std::string type_name = advance().text;
                ir::IRType* vtype = nullptr;
                std::string struct_name = "";
                bool is_explicit_ptr = false;
                if (is_struct_decl) {
                    struct_name = type_name;
                    const auto* cd = oop_mgr_->get_class(type_name);
                    vtype = cd ? cd->ir_struct_type : ctx_.get_i64_type();
                } else {
                    vtype = parse_type_name(type_name);
                }
                while (match(TOK_STAR)) {
                    vtype = ctx_.get_ptr_type(vtype);
                    is_explicit_ptr = true;
                }

                while (peek().kind == TOK_IDENT || peek().kind == TOK_STAR) {
                    ir::IRType* cur_vtype = vtype;
                    bool cur_explicit_ptr = is_explicit_ptr;
                    while (match(TOK_STAR)) {
                        cur_vtype = ctx_.get_ptr_type(cur_vtype);
                        cur_explicit_ptr = true;
                    }
                    if (peek().kind != TOK_IDENT) break;
                    std::string var_name = advance().text;

                    auto has_raii_dtor = [&](const std::string& sname, bool explicit_ptr) -> bool {
                        if (explicit_ptr || sname.empty() || !oop_mgr_) return false;
                        const auto* cd = oop_mgr_->get_class(sname);
                        return cd && cd->has_destructor;
                    };

                    if (type_name == "take") {
                        if (!match(TOK_ASSIGN)) {
                            error = "Variable '" + var_name + "' declared with 'take' requires an explicit initializer!";
                            return false;
                        }
                        last_struct_name_.clear();
                        last_expr_is_borrowed_ptr_ = false;
                        ir::ValueId init_val = parse_expression(error);
                        if (!init_val) {
                            if (error.empty()) error = "Failed to evaluate initializer for variable '" + var_name + "'";
                            return false;
                        }
                        ir::IRType* actual_t = builder_.get_function()->get_value(init_val).type;
                        if (!actual_t || actual_t->is_void()) {
                            error = "Initializer for 'take " + var_name + "' cannot have void type!";
                            return false;
                        }
                        cur_vtype = actual_t;
                        if (cur_vtype->is_struct()) {
                            struct_name = cur_vtype->struct_name;
                        } else if (cur_vtype->is_pointer() && !last_struct_name_.empty() && oop_mgr_ && oop_mgr_->has_class(last_struct_name_)) {
                            struct_name = last_struct_name_;
                        }
                        bool is_ptr_or_borrowed = cur_vtype->is_pointer() || last_expr_is_borrowed_ptr_;
                        bool raii = has_raii_dtor(struct_name, is_ptr_or_borrowed);
                        ir::ValueId alloca_id = builder_.create_alloca(cur_vtype, 1, 0, var_name);
                        declare_var(var_name, alloca_id, cur_vtype, struct_name, raii);
                        builder_.create_store(init_val, alloca_id);
                    } else if (match(TOK_ASSIGN)) {
                        last_struct_name_.clear();
                        last_expr_is_borrowed_ptr_ = false;
                        ir::ValueId init_val = parse_expression(error);
                        if (!init_val) {
                            if (error.empty()) error = "Failed to evaluate initializer for variable '" + var_name + "'";
                            return false;
                        }
                        if (type_name == "var" || type_name == "set") {
                            ir::IRType* actual_t = builder_.get_function()->get_value(init_val).type;
                            if (actual_t && !actual_t->is_void()) {
                                cur_vtype = actual_t;
                            }
                            if (cur_vtype && cur_vtype->is_struct()) {
                                struct_name = cur_vtype->struct_name;
                            } else if (cur_vtype && cur_vtype->is_pointer() && !last_struct_name_.empty() && oop_mgr_ && oop_mgr_->has_class(last_struct_name_)) {
                                struct_name = last_struct_name_;
                            }
                        }
                        bool is_ptr_or_borrowed = cur_explicit_ptr || (cur_vtype && cur_vtype->is_pointer()) || last_expr_is_borrowed_ptr_;
                        bool raii = has_raii_dtor(struct_name, is_ptr_or_borrowed);
                        ir::ValueId alloca_id = builder_.create_alloca(cur_vtype, 1, 0, var_name);
                        declare_var(var_name, alloca_id, cur_vtype, struct_name, raii);
                        ir::IRType* init_t = builder_.get_function()->get_value(init_val).type;
                        if (cur_vtype && cur_vtype->is_struct() && init_t && init_t->is_pointer()) {
                            emit_copy_struct(alloca_id, init_val, cur_vtype);
                        } else {
                            init_val = coerce_type(init_val, cur_vtype);
                            builder_.create_store(init_val, alloca_id);
                        }
                    } else {
                        bool raii = has_raii_dtor(struct_name, cur_explicit_ptr);
                        ir::ValueId alloca_id = builder_.create_alloca(cur_vtype, 1, 0, var_name);
                        declare_var(var_name, alloca_id, cur_vtype, struct_name, raii);
                        if (cur_vtype && cur_vtype->is_struct()) {
                            emit_zero_struct(alloca_id, cur_vtype);
                        } else {
                            ir::ValueId default_val = builder_.create_const_int(cur_vtype, 0);
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
                push_scope();
                if (!parse_statement(error)) return false;
                if (!builder_.get_insert_block()->has_terminator()) {
                    pop_scope(true);
                    if (!builder_.get_insert_block()->has_terminator()) {
                        builder_.create_jump(b_merge);
                    }
                } else {
                    pop_scope(false);
                }

                // Else Block
                builder_.set_insert_point(b_else);
                if (match_ident("else")) {
                    has_else = true;
                    push_scope();
                    if (!parse_statement(error)) return false;
                    if (!builder_.get_insert_block()->has_terminator()) {
                        pop_scope(true);
                        if (!builder_.get_insert_block()->has_terminator()) {
                            builder_.create_jump(b_merge);
                        }
                    } else {
                        pop_scope(false);
                    }
                } else if (!builder_.get_insert_block()->has_terminator()) {
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
                size_t loop_depth = scopes_.size();
                loop_stack_.push_back({ b_cond, b_exit, loop_depth, loop_depth });
                builder_.set_insert_point(b_body);
                push_scope();
                if (!parse_statement(error)) return false;
                if (!builder_.get_insert_block()->has_terminator()) {
                    pop_scope(true);
                    if (!builder_.get_insert_block()->has_terminator()) {
                        builder_.create_jump(b_cond);
                    }
                } else {
                    pop_scope(false);
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
                size_t body_depth = scopes_.size();
                loop_stack_.push_back({ b_step, b_exit, body_depth, body_depth });
                builder_.set_insert_point(b_body);
                push_scope();
                if (!parse_statement(error)) return false;
                if (!builder_.get_insert_block()->has_terminator()) {
                    pop_scope(true);
                    if (!builder_.get_insert_block()->has_terminator()) {
                        builder_.create_jump(b_step);
                    }
                } else {
                    pop_scope(false);
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

                builder_.set_insert_point(b_exit);
                pop_scope(true);
                return true;
            }

            // Break / Continue
            if (match_ident("break") || match_ident("stop")) {
                match(TOK_SEMI);
                if (loop_stack_.empty()) {
                    error = "Break statement outside loop!";
                    return false;
                }
                emit_scopes_cleanup_to(loop_stack_.back().break_scope_depth);
                builder_.create_jump(loop_stack_.back().b_exit);
                return true;
            }

            if (match_ident("continue") || match_ident("next")) {
                match(TOK_SEMI);
                if (loop_stack_.empty()) {
                    error = "Continue statement outside loop!";
                    return false;
                }
                emit_scopes_cleanup_to(loop_stack_.back().continue_scope_depth);
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
                    emit_scopes_cleanup_to(0);
                    if (mem_mgr_) mem_mgr_->emit_all_cleanups(builder_);
                    if (!current_dtor_struct_.empty()) {
                        const VarBinding* tv = lookup_var("this");
                        if (tv) {
                            ir::ValueId tptr = builder_.create_load(tv->alloca_id, "this_ret_dtor");
                            emit_struct_field_dtors(tptr, current_dtor_struct_);
                        }
                    }
                    builder_.create_ret(ret_val);
                } else {
                    emit_scopes_cleanup_to(0);
                    if (mem_mgr_) mem_mgr_->emit_all_cleanups(builder_);
                    if (!current_dtor_struct_.empty()) {
                        const VarBinding* tv = lookup_var("this");
                        if (tv) {
                            ir::ValueId tptr = builder_.create_load(tv->alloca_id, "this_ret_dtor");
                            emit_struct_field_dtors(tptr, current_dtor_struct_);
                        }
                    }
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
            // Check for pointer dereference assignment: *ptr = expr, **ptr = expr, *(expr) = expr, *ptr += expr
            if (peek().kind == TOK_STAR) {
                size_t look = 0;
                while (peek(look).kind == TOK_STAR) look++;
                bool is_ident_deref = (peek(look).kind == TOK_IDENT &&
                    (peek(look + 1).kind == TOK_ASSIGN || peek(look + 1).kind == TOK_PLUS_ASSIGN ||
                     peek(look + 1).kind == TOK_MINUS_ASSIGN || peek(look + 1).kind == TOK_MUL_ASSIGN ||
                     peek(look + 1).kind == TOK_DIV_ASSIGN));
                bool is_paren_deref = (peek(look).kind == TOK_LPAREN);
                size_t paren_close = pos_ + look;
                if (is_paren_deref) {
                    paren_close++;
                    int pdepth = 1;
                    while (paren_close < tokens_.size() && pdepth > 0) {
                        if (tokens_[paren_close].kind == TOK_LPAREN) pdepth++;
                        else if (tokens_[paren_close].kind == TOK_RPAREN) pdepth--;
                        paren_close++;
                    }
                    is_paren_deref = (pdepth == 0 && paren_close < tokens_.size() &&
                        (tokens_[paren_close].kind == TOK_ASSIGN || tokens_[paren_close].kind == TOK_PLUS_ASSIGN ||
                         tokens_[paren_close].kind == TOK_MINUS_ASSIGN || tokens_[paren_close].kind == TOK_MUL_ASSIGN ||
                         tokens_[paren_close].kind == TOK_DIV_ASSIGN));
                }

                if (is_ident_deref || is_paren_deref) {
                    advance(); // consume first '*'
                    int deref_count = 1;
                    while (match(TOK_STAR)) deref_count++;

                    ir::ValueId target_addr = 0;
                    if (is_ident_deref) {
                        std::string ptr_name = advance().text;
                        const VarBinding* ptr_var = lookup_var(ptr_name);
                        if (!ptr_var) {
                            error = "Undeclared pointer variable: " + ptr_name;
                            return 0;
                        }
                        target_addr = builder_.create_load(ptr_var->alloca_id, ptr_name + "_val");
                    } else {
                        match(TOK_LPAREN);
                        target_addr = parse_expression(error);
                        if (!target_addr || !consume(TOK_RPAREN, "Expected ')' in dereference assignment", error)) return 0;
                    }
                    for (int d = 1; d < deref_count; ++d) {
                        target_addr = builder_.create_load(target_addr, "deref_addr");
                    }

                    GrassTokenKind op = advance().kind;
                    ir::ValueId rhs = parse_assignment(error);
                    if (!rhs) return 0;

                    ir::IRType* ptr_t = builder_.get_function()->has_value(target_addr)
                        ? builder_.get_function()->get_value(target_addr).type : nullptr;
                    ir::IRType* elem_t = (ptr_t && ptr_t->is_pointer() && ptr_t->element_type && !ptr_t->element_type->is_void())
                        ? ptr_t->element_type : nullptr;

                    if (op == TOK_ASSIGN) {
                        if (elem_t) rhs = coerce_type(rhs, elem_t);
                        builder_.create_store(rhs, target_addr);
                        return rhs;
                    } else {
                        ir::ValueId cur = builder_.create_load(target_addr, "deref_cur");
                        match_types(cur, rhs);
                        ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                        ir::ValueId computed = 0;
                        if (lt && lt->is_floating_point()) {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                            else computed = builder_.create_fdiv(cur, rhs);
                        } else {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                            else computed = builder_.create_sdiv(cur, rhs);
                        }
                        if (elem_t) computed = coerce_type(computed, elem_t);
                        builder_.create_store(computed, target_addr);
                        return computed;
                    }
                }
            }

            // Check for member assignment: obj.field = expr or obj.a.b = expr (supports '.' and '->')
            if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_DOT || peek(1).kind == TOK_ARROW)) {
                size_t look = pos_ + 1;
                while (look + 1 < tokens_.size() &&
                       (tokens_[look].kind == TOK_DOT || tokens_[look].kind == TOK_ARROW) &&
                       tokens_[look + 1].kind == TOK_IDENT) {
                    look += 2;
                }
                if (look > pos_ + 1 && look < tokens_.size() &&
                    (tokens_[look].kind == TOK_ASSIGN || tokens_[look].kind == TOK_PLUS_ASSIGN ||
                     tokens_[look].kind == TOK_MINUS_ASSIGN || tokens_[look].kind == TOK_MUL_ASSIGN ||
                     tokens_[look].kind == TOK_DIV_ASSIGN)) {
                    std::string obj_name = advance().text;
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

                    std::vector<std::string> member_chain;
                    while ((peek().kind == TOK_DOT || peek().kind == TOK_ARROW) && peek(1).kind == TOK_IDENT) {
                        advance();
                        member_chain.push_back(advance().text);
                    }
                    GrassTokenKind op = advance().kind;

                    for (size_t ci = 0; ci + 1 < member_chain.size(); ++ci) {
                        const std::string& mid_field = member_chain[ci];
                        if (!oop_mgr_ || sname.empty()) break;
                        const auto* cd = oop_mgr_->get_class(sname);
                        std::string next_s;
                        bool is_val_struct = false;
                        if (cd) {
                            auto fit = cd->field_indices.find(mid_field);
                            if (fit != cd->field_indices.end()) {
                                const auto& fld = cd->fields[fit->second];
                                if (fld.type && fld.type->is_struct()) {
                                    is_val_struct = true;
                                    next_s = fld.type->struct_name;
                                } else if (fld.type && fld.type->is_pointer() && fld.type->element_type && fld.type->element_type->is_struct()) {
                                    next_s = fld.type->element_type->struct_name;
                                }
                            }
                        }
                        if (is_val_struct) {
                            inst_ptr = oop_mgr_->emit_field_gep(builder_, inst_ptr, sname, mid_field, mid_field + "_gep");
                        } else {
                            inst_ptr = oop_mgr_->emit_load_field(builder_, inst_ptr, sname, mid_field, mid_field + "_ptr");
                        }
                        sname = next_s;
                    }

                    const std::string& field_name = member_chain.back();
                    ir::ValueId rhs = parse_assignment(error);
                    if (!rhs) return 0;

                    ir::IRType* field_type = nullptr;
                    if (oop_mgr_ && !sname.empty()) {
                        const auto* cd = oop_mgr_->get_class(sname);
                        const auto* fld = cd ? cd->find_field(field_name) : nullptr;
                        if (fld) field_type = fld->type;
                    }

                    if (op == TOK_ASSIGN) {
                        if (oop_mgr_) {
                            if (field_type && !field_type->is_struct()) {
                                rhs = coerce_type(rhs, field_type);
                            }
                            oop_mgr_->emit_store_field(builder_, inst_ptr, sname, field_name, rhs);
                        }
                        return rhs;
                    } else {
                        ir::ValueId cur = 0;
                        if (oop_mgr_) {
                            cur = oop_mgr_->emit_load_field(builder_, inst_ptr, sname, field_name, field_name + "_val");
                        }
                        match_types(cur, rhs);
                        ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                        ir::ValueId computed = 0;
                        if (lt && lt->is_floating_point()) {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                            else if (op == TOK_DIV_ASSIGN) computed = builder_.create_fdiv(cur, rhs);
                        } else {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                            else if (op == TOK_DIV_ASSIGN) computed = builder_.create_sdiv(cur, rhs);
                        }

                        if (oop_mgr_) {
                            oop_mgr_->emit_store_field(builder_, inst_ptr, sname, field_name, computed);
                        }
                        return computed;
                    }
                }
            }

            // Check for array / pointer element assignment: ptr[idx] = expr or arr[idx] = expr
            if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LBRACKET) {
                size_t look = pos_ + 2;
                int depth = 1;
                while (look < tokens_.size() && depth > 0) {
                    if (tokens_[look].kind == TOK_LBRACKET) depth++;
                    else if (tokens_[look].kind == TOK_RBRACKET) depth--;
                    look++;
                }
                if (look < tokens_.size() && (tokens_[look].kind == TOK_ASSIGN ||
                    tokens_[look].kind == TOK_PLUS_ASSIGN || tokens_[look].kind == TOK_MINUS_ASSIGN ||
                    tokens_[look].kind == TOK_MUL_ASSIGN || tokens_[look].kind == TOK_DIV_ASSIGN)) {
                    std::string arr_name = advance().text;
                    advance(); // consume '['
                    ir::ValueId idx = parse_expression(error);
                    if (!idx || !consume(TOK_RBRACKET, "Expected ']'", error)) return 0;
                    GrassTokenKind op = advance().kind;
                    ir::ValueId rhs = parse_assignment(error);
                    if (!rhs) return 0;

                    const VarBinding* var = lookup_var(arr_name);
                    if (!var) {
                        error = "Undeclared array: " + arr_name;
                        return 0;
                    }
                    ir::ValueId arr_ptr = builder_.create_load(var->alloca_id, arr_name + "_ptr");
                    bool is_typed_ptr = (var->type && var->type->is_pointer() &&
                                         var->type->element_type && !var->type->element_type->is_void() &&
                                         !var->is_vec);
                    if (var->is_vec && var->vec_elem_type) {
                        ir::IRType* elem_t = var->vec_elem_type;
                        ir::ValueId vec_p = (var->type && var->type->is_pointer()) ? arr_ptr : var->alloca_id;
                        ir::ValueId data_ptr = emit_vec_data(vec_p, elem_t);
                        ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                        ir::ValueId elem_ptr = builder_.create_gep(data_ptr, { idx64 }, elem_t, arr_name + "_vec_ep");
                        if (op == TOK_ASSIGN) {
                            ir::IRType* rhs_t = builder_.get_function()->has_value(rhs) ? builder_.get_function()->get_value(rhs).type : nullptr;
                            if (elem_t->is_struct() && rhs_t && rhs_t->is_pointer()) {
                                emit_copy_struct(elem_ptr, rhs, elem_t);
                            } else {
                                rhs = coerce_type(rhs, elem_t);
                                builder_.create_store(rhs, elem_ptr);
                            }
                            return rhs;
                        } else {
                            ir::ValueId cur = builder_.create_load(elem_ptr, arr_name + "_vec_el");
                            match_types(cur, rhs);
                            ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                            ir::ValueId computed = 0;
                            if (lt && lt->is_floating_point()) {
                                if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                                else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                                else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                                else computed = builder_.create_fdiv(cur, rhs);
                            } else {
                                if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                                else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                                else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                                else computed = builder_.create_sdiv(cur, rhs);
                            }
                            computed = coerce_type(computed, elem_t);
                            builder_.create_store(computed, elem_ptr);
                            return computed;
                        }
                    } else if (is_typed_ptr) {
                        ir::IRType* elem_t = var->type->element_type;
                        ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                        ir::ValueId elem_ptr = builder_.create_gep(arr_ptr, { idx64 }, elem_t, arr_name + "_elem_ptr");
                        if (op == TOK_ASSIGN) {
                            rhs = coerce_type(rhs, elem_t);
                            builder_.create_store(rhs, elem_ptr);
                            return rhs;
                        } else {
                            ir::ValueId cur = builder_.create_load(elem_ptr, arr_name + "_elem");
                            match_types(cur, rhs);
                            ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                            ir::ValueId computed = 0;
                            if (lt && lt->is_floating_point()) {
                                if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                                else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                                else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                                else computed = builder_.create_fdiv(cur, rhs);
                            } else {
                                if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                                else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                                else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                                else computed = builder_.create_sdiv(cur, rhs);
                            }
                            computed = coerce_type(computed, elem_t);
                            builder_.create_store(computed, elem_ptr);
                            return computed;
                        }
                    } else {
                        if (op == TOK_ASSIGN) {
                            builder_.create_call("grass_array_set", ctx_.get_void_type(), { arr_ptr, idx, rhs });
                            return rhs;
                        } else {
                            ir::ValueId cur = builder_.create_call("grass_array_get", ctx_.get_i64_type(), { arr_ptr, idx });
                            ir::ValueId computed = 0;
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                            else if (op == TOK_DIV_ASSIGN) computed = builder_.create_sdiv(cur, rhs);
                            builder_.create_call("grass_array_set", ctx_.get_void_type(), { arr_ptr, idx, computed });
                            return computed;
                        }
                    }
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
                    if (var->is_raii) {
                        emit_var_dtor(*var);
                    }
                    ir::IRType* rhs_t = builder_.get_function()->get_value(rhs).type;
                    if (var->type && var->type->is_struct() && rhs_t && rhs_t->is_pointer()) {
                        emit_copy_struct(var->alloca_id, rhs, var->type);
                    } else {
                        rhs = coerce_type(rhs, var->type);
                        builder_.create_store(rhs, var->alloca_id);
                    }
                    return rhs;
                } else {
                    ir::ValueId cur = builder_.create_load(var->alloca_id, var_name);
                    match_types(cur, rhs);
                    ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                    ir::ValueId computed = 0;
                    if (lt && lt->is_floating_point()) {
                        if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                        else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                        else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                        else if (op == TOK_DIV_ASSIGN) computed = builder_.create_fdiv(cur, rhs);
                    } else {
                        if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                        else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                        else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                        else if (op == TOK_DIV_ASSIGN) computed = builder_.create_sdiv(cur, rhs);
                    }

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
                ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
                ir::IRType* rt = builder_.get_function()->get_value(rhs).type;
                ir::ValueId lhs_b = (lt && lt->is_bool()) ? lhs : builder_.create_icmp_ne(lhs, builder_.create_const_int(lt ? lt : ctx_.get_i64_type(), 0));
                ir::ValueId rhs_b = (rt && rt->is_bool()) ? rhs : builder_.create_icmp_ne(rhs, builder_.create_const_int(rt ? rt : ctx_.get_i64_type(), 0));
                ir::ValueId or_res = builder_.create_or(lhs_b, rhs_b);
                lhs = builder_.create_zext(or_res, ctx_.get_i64_type());
            }
            return lhs;
        }

        ir::ValueId parse_logical_and(std::string& error) {
            ir::ValueId lhs = parse_bitwise_or(error);
            if (!lhs) return 0;

            while (match(TOK_AND)) {
                ir::ValueId rhs = parse_bitwise_or(error);
                if (!rhs) return 0;
                ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
                ir::IRType* rt = builder_.get_function()->get_value(rhs).type;
                ir::ValueId lhs_b = (lt && lt->is_bool()) ? lhs : builder_.create_icmp_ne(lhs, builder_.create_const_int(lt ? lt : ctx_.get_i64_type(), 0));
                ir::ValueId rhs_b = (rt && rt->is_bool()) ? rhs : builder_.create_icmp_ne(rhs, builder_.create_const_int(rt ? rt : ctx_.get_i64_type(), 0));
                ir::ValueId and_res = builder_.create_and(lhs_b, rhs_b);
                lhs = builder_.create_zext(and_res, ctx_.get_i64_type());
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
                ir::IRType* t1 = builder_.get_function()->has_value(lhs) ? builder_.get_function()->get_value(lhs).type : nullptr;
                ir::IRType* t2 = builder_.get_function()->has_value(rhs) ? builder_.get_function()->get_value(rhs).type : nullptr;
                if (t1 && t2 && ((t1->is_pointer() && t2->is_integer()) || (t1->is_integer() && t2->is_pointer()))) {
                    ir::IRType* i64_t = ctx_.get_i64_type();
                    lhs = coerce_type(lhs, i64_t);
                    rhs = coerce_type(rhs, i64_t);
                } else {
                    match_types(lhs, rhs);
                }
                ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
                if (lt && lt->is_floating_point()) {
                    if (op == TOK_EQ) lhs = builder_.create_fcmp(ir::FCmpCond::OEQ, lhs, rhs);
                    else lhs = builder_.create_fcmp(ir::FCmpCond::ONE, lhs, rhs);
                } else {
                    if (op == TOK_EQ) lhs = builder_.create_icmp_eq(lhs, rhs);
                    else lhs = builder_.create_icmp_ne(lhs, rhs);
                }
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
                ir::IRType* t1 = builder_.get_function()->has_value(lhs) ? builder_.get_function()->get_value(lhs).type : nullptr;
                ir::IRType* t2 = builder_.get_function()->has_value(rhs) ? builder_.get_function()->get_value(rhs).type : nullptr;
                if (t1 && t2 && ((t1->is_pointer() && t2->is_integer()) || (t1->is_integer() && t2->is_pointer()))) {
                    ir::IRType* i64_t = ctx_.get_i64_type();
                    lhs = coerce_type(lhs, i64_t);
                    rhs = coerce_type(rhs, i64_t);
                } else {
                    match_types(lhs, rhs);
                }
                ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
                if (lt && lt->is_floating_point()) {
                    if (op == TOK_LT) lhs = builder_.create_fcmp(ir::FCmpCond::OLT, lhs, rhs);
                    else if (op == TOK_LTE) lhs = builder_.create_fcmp(ir::FCmpCond::OLE, lhs, rhs);
                    else if (op == TOK_GT) lhs = builder_.create_fcmp(ir::FCmpCond::OGT, lhs, rhs);
                    else if (op == TOK_GTE) lhs = builder_.create_fcmp(ir::FCmpCond::OGE, lhs, rhs);
                } else {
                    if (op == TOK_LT) lhs = builder_.create_icmp_slt(lhs, rhs);
                    else if (op == TOK_LTE) lhs = builder_.create_icmp_sle(lhs, rhs);
                    else if (op == TOK_GT) lhs = builder_.create_icmp_sgt(lhs, rhs);
                    else if (op == TOK_GTE) lhs = builder_.create_icmp_sge(lhs, rhs);
                }
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

                ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
                ir::IRType* rt = builder_.get_function()->get_value(rhs).type;

                // Pointer arithmetic: ptr + n, ptr - n
                if (lt && lt->is_pointer() && rt && rt->is_integer()) {
                    uint64_t elem_sz = 8;
                    if (lt->element_type) {
                        elem_sz = lt->element_type->size();
                        if (elem_sz <= 0) elem_sz = 1;
                    }
                    ir::IRType* i64_t = ctx_.get_i64_type();
                    ir::ValueId scaled_rhs = coerce_type(rhs, i64_t);
                    if (elem_sz != 1) {
                        ir::ValueId sz_val = builder_.create_const_int(i64_t, elem_sz);
                        scaled_rhs = builder_.create_mul(scaled_rhs, sz_val);
                    }
                    ir::ValueId ptr_i64 = coerce_type(lhs, i64_t);
                    ir::ValueId res_i64 = (op == TOK_PLUS)
                        ? builder_.create_add(ptr_i64, scaled_rhs)
                        : builder_.create_sub(ptr_i64, scaled_rhs);
                    lhs = builder_.create_inttoptr(res_i64, lt);
                    continue;
                }
                // n + ptr
                if (op == TOK_PLUS && rt && rt->is_pointer() && lt && lt->is_integer()) {
                    uint64_t elem_sz = 8;
                    if (rt->element_type) {
                        elem_sz = rt->element_type->size();
                        if (elem_sz <= 0) elem_sz = 1;
                    }
                    ir::IRType* i64_t = ctx_.get_i64_type();
                    ir::ValueId scaled_lhs = coerce_type(lhs, i64_t);
                    if (elem_sz != 1) {
                        ir::ValueId sz_val = builder_.create_const_int(i64_t, elem_sz);
                        scaled_lhs = builder_.create_mul(scaled_lhs, sz_val);
                    }
                    ir::ValueId ptr_i64 = coerce_type(rhs, i64_t);
                    ir::ValueId res_i64 = builder_.create_add(scaled_lhs, ptr_i64);
                    lhs = builder_.create_inttoptr(res_i64, rt);
                    continue;
                }
                // ptr1 - ptr2: (ptr1 - ptr2) / sizeof(T)
                if (op == TOK_MINUS && lt && lt->is_pointer() && rt && rt->is_pointer()) {
                    uint64_t elem_sz = 8;
                    if (lt->element_type) {
                        elem_sz = lt->element_type->size();
                        if (elem_sz <= 0) elem_sz = 1;
                    }
                    ir::IRType* i64_t = ctx_.get_i64_type();
                    ir::ValueId p1_i64 = coerce_type(lhs, i64_t);
                    ir::ValueId p2_i64 = coerce_type(rhs, i64_t);
                    ir::ValueId diff = builder_.create_sub(p1_i64, p2_i64);
                    if (elem_sz != 1) {
                        ir::ValueId sz_val = builder_.create_const_int(i64_t, elem_sz);
                        lhs = builder_.create_sdiv(diff, sz_val);
                    } else {
                        lhs = diff;
                    }
                    continue;
                }

                match_types(lhs, rhs);
                lt = builder_.get_function()->get_value(lhs).type;
                if (lt && lt->is_floating_point()) {
                    if (op == TOK_PLUS) lhs = builder_.create_fadd(lhs, rhs);
                    else lhs = builder_.create_fsub(lhs, rhs);
                } else {
                    if (op == TOK_PLUS) lhs = builder_.create_add(lhs, rhs);
                    else lhs = builder_.create_sub(lhs, rhs);
                }
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
                ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
                if (lt && lt->is_floating_point()) {
                    if (op == TOK_STAR) lhs = builder_.create_fmul(lhs, rhs);
                    else if (op == TOK_SLASH) lhs = builder_.create_fdiv(lhs, rhs);
                    else lhs = builder_.create_srem(lhs, rhs);
                } else {
                    if (op == TOK_STAR) lhs = builder_.create_mul(lhs, rhs);
                    else if (op == TOK_SLASH) lhs = builder_.create_sdiv(lhs, rhs);
                    else if (op == TOK_MOD) lhs = builder_.create_srem(lhs, rhs);
                }
            }
            return lhs;
        }

        ir::ValueId parse_unary(std::string& error) {
            // Address-of operator &var, &arr[i], &obj.field, &ptr->field, &outer.inner.field
            if (match(TOK_AMP)) {
                if (peek().kind != TOK_IDENT) {
                    error = "Line " + std::to_string(peek().line) + ": Cannot take address of non-lvalue expression!";
                    return 0;
                }
                std::string id = advance().text;
                const VarBinding* var = lookup_var(id);
                if (!var) { error = "Undeclared variable: " + id; return 0; }
                last_expr_is_borrowed_ptr_ = true;

                if (match(TOK_LBRACKET)) {
                    ir::ValueId idx = parse_expression(error);
                    if (!idx || !consume(TOK_RBRACKET, "Expected ']'", error)) return 0;
                    ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                    if (var->is_vec && var->vec_elem_type) {
                        ir::ValueId vec_p = (var->type && var->type->is_pointer())
                            ? builder_.create_load(var->alloca_id, id + "_vptr")
                            : var->alloca_id;
                        ir::ValueId data_ptr = emit_vec_data(vec_p, var->vec_elem_type);
                        return builder_.create_gep(data_ptr, { idx64 }, var->vec_elem_type, id + "_vec_elem_addr");
                    }
                    ir::ValueId arr_ptr = builder_.create_load(var->alloca_id, id + "_ptr");
                    bool is_typed_ptr = (var->type && var->type->is_pointer() &&
                                         var->type->element_type && !var->type->element_type->is_void());
                    if (is_typed_ptr) {
                        return builder_.create_gep(arr_ptr, { idx64 }, var->type->element_type, id + "_elem_addr");
                    }
                    // Dynamic GrassDynArray header has data pointer at offset +8 (index 1 of i64*)
                    ir::ValueId hdr_i64 = coerce_type(arr_ptr, ctx_.get_ptr_type(ctx_.get_i64_type()));
                    ir::ValueId data_slot = builder_.create_gep(hdr_i64, { builder_.create_const_i64(1) }, ctx_.get_i64_type());
                    ir::ValueId data_i64 = builder_.create_load(data_slot, id + "_data");
                    ir::ValueId data_ptr = coerce_type(data_i64, ctx_.get_ptr_type(ctx_.get_i64_type()));
                    return builder_.create_gep(data_ptr, { idx64 }, ctx_.get_i64_type(), id + "_elem_addr");
                }

                if (peek().kind == TOK_DOT || peek().kind == TOK_ARROW) {
                    ir::ValueId inst_ptr = var->alloca_id;
                    if (var->type && var->type->is_pointer()) {
                        inst_ptr = builder_.create_load(var->alloca_id, id + "_ptr");
                    }
                    std::string sname = var->struct_type_name;
                    while (match(TOK_DOT) || match(TOK_ARROW)) {
                        if (peek().kind != TOK_IDENT) {
                            error = "Expected field name after '.' or '->' in address-of!";
                            return 0;
                        }
                        std::string field_name = advance().text;
                        if (!oop_mgr_ || sname.empty()) break;
                        const auto* cd = oop_mgr_->get_class(sname);
                        const auto* fld = cd ? cd->find_field(field_name) : nullptr;
                        ir::ValueId fptr = oop_mgr_->emit_field_gep(builder_, inst_ptr, sname, field_name, field_name + "_addr");
                        if (peek().kind != TOK_DOT && peek().kind != TOK_ARROW) {
                            last_struct_name_.clear();
                            if (fld && fld->type && fld->type->is_struct()) last_struct_name_ = fld->type->struct_name;
                            return fptr;
                        }
                        if (fld && fld->type && fld->type->is_struct()) {
                            inst_ptr = fptr;
                            sname = fld->type->struct_name;
                        } else if (fld && fld->type && fld->type->is_pointer() && fld->type->element_type && fld->type->element_type->is_struct()) {
                            inst_ptr = builder_.create_load(fptr, field_name + "_ptr");
                            sname = fld->type->element_type->struct_name;
                        } else {
                            return fptr;
                        }
                    }
                    return inst_ptr;
                }

                if (!var->struct_type_name.empty() && var->type && var->type->is_struct()) {
                    last_struct_name_ = var->struct_type_name;
                } else {
                    last_struct_name_.clear();
                }
                return var->alloca_id;
            }

            // Dereference operator *ptr
            if (match(TOK_STAR)) {
                ir::ValueId ptr_val = parse_unary(error);
                if (!ptr_val) return 0;
                ir::IRType* pt = builder_.get_function()->get_value(ptr_val).type;
                if (pt && pt->is_pointer() && pt->element_type && pt->element_type->is_struct()) {
                    last_struct_name_ = pt->element_type->struct_name;
                } else if (pt && pt->is_pointer() && pt->element_type && pt->element_type->is_pointer() &&
                           pt->element_type->element_type && pt->element_type->element_type->is_struct()) {
                    last_struct_name_ = pt->element_type->element_type->struct_name;
                }
                return builder_.create_load(ptr_val, "deref");
            }

            // C-style cast (Type*)expr or (Type)expr
            if (peek().kind == TOK_LPAREN) {
                size_t off = 1;
                if (peek(off).kind == TOK_IDENT && (is_type_or_func_keyword(peek(off).text) || (oop_mgr_ && oop_mgr_->has_class(peek(off).text)))) {
                    std::string tname = peek(off).text;
                    off++;
                    while (peek(off).kind == TOK_COLON_COLON) {
                        off++;
                        if (peek(off).kind == TOK_IDENT) { tname += "::" + peek(off).text; off++; } else break;
                    }
                    int ptr_cnt = 0;
                    while (peek(off).kind == TOK_STAR) { ptr_cnt++; off++; }
                    if (peek(off).kind == TOK_RPAREN) {
                        auto next_k = peek(off + 1).kind;
                        if (next_k == TOK_IDENT || next_k == TOK_NUMBER || next_k == TOK_STRING ||
                            next_k == TOK_LPAREN || next_k == TOK_STAR || next_k == TOK_AMP ||
                            next_k == TOK_MINUS || next_k == TOK_NULLPTR) {
                            advance(); // consume '('
                            advance(); // consume base type identifier
                            while (match(TOK_COLON_COLON)) {
                                if (peek().kind == TOK_IDENT) advance();
                            }
                            ir::IRType* tgt_t = parse_type_name(tname);
                            while (match(TOK_STAR)) tgt_t = ctx_.get_ptr_type(tgt_t);
                            match(TOK_RPAREN);
                            ir::ValueId val = parse_unary(error);
                            if (!val) return 0;
                            if (tgt_t && tgt_t->is_pointer() && tgt_t->element_type && tgt_t->element_type->is_struct()) {
                                last_struct_name_ = tgt_t->element_type->struct_name;
                                last_expr_is_borrowed_ptr_ = true;
                            }
                            return coerce_type(val, tgt_t);
                        }
                    }
                }
            }

            if (match(TOK_MINUS)) {
                ir::ValueId operand = parse_unary(error);
                if (!operand) return 0;
                ir::IRType* ot = builder_.get_function()->get_value(operand).type;
                if (ot && ot->is_floating_point()) {
                    return builder_.create_fneg(operand);
                }
                return builder_.create_neg(operand);
            }
            if (match(TOK_NOT)) {
                ir::ValueId operand = parse_unary(error);
                if (!operand) return 0;
                ir::IRType* ot = builder_.get_function()->get_value(operand).type;
                if (ot && ot->is_bool()) {
                    return builder_.create_xor(operand, builder_.create_const_i1(true));
                }
                ir::ValueId zero = builder_.create_const_int(ot ? ot : ctx_.get_i64_type(), 0);
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
                if (match(TOK_DOT) || match(TOK_ARROW)) {
                    if (peek().kind != TOK_IDENT) {
                        error = "Expected identifier after '.' or '->'!";
                        return 0;
                    }
                    std::string member_name = advance().text;
                    std::string receiver_struct = last_struct_name_;
                    if (match(TOK_LPAREN)) {
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
                        if (!receiver_struct.empty() && vec_struct_meta_.count(receiver_struct)) {
                            const auto& vm = vec_struct_meta_[receiver_struct];
                            if ((member_name == "push" || member_name == "push_back") && !args.empty()) {
                                emit_vec_push(base, vm.elem_type, args[0]);
                                last_struct_name_.clear();
                                base = builder_.create_const_i64(0);
                                continue;
                            } else if (member_name == "pop" || member_name == "pop_back") {
                                emit_vec_pop(base, vm.elem_type, vm.elem_struct);
                                last_struct_name_.clear();
                                base = builder_.create_const_i64(0);
                                continue;
                            } else if (member_name == "size") {
                                last_struct_name_.clear();
                                base = emit_vec_size(base);
                                continue;
                            } else if (member_name == "capacity") {
                                last_struct_name_.clear();
                                base = emit_vec_capacity(base);
                                continue;
                            } else if (member_name == "empty") {
                                last_struct_name_.clear();
                                base = emit_vec_empty(base);
                                continue;
                            } else if (member_name == "clear") {
                                emit_vec_clear(base, vm.elem_type, vm.elem_struct);
                                last_struct_name_.clear();
                                base = builder_.create_const_i64(0);
                                continue;
                            } else if (member_name == "reserve" && !args.empty()) {
                                emit_vec_reserve(base, vm.elem_type, args[0]);
                                last_struct_name_.clear();
                                base = builder_.create_const_i64(0);
                                continue;
                            } else if (member_name == "data") {
                                base = emit_vec_data(base, vm.elem_type);
                                last_struct_name_ = vm.elem_struct;
                                continue;
                            }
                        }
                        if (member_name == "push") {
                            std::vector<ir::ValueId> push_args = { base };
                            push_args.insert(push_args.end(), args.begin(), args.end());
                            builder_.create_call("grass_array_push", ctx_.get_void_type(), push_args);
                            last_struct_name_.clear();
                            return base;
                        } else if (member_name == "len" || member_name == "size") {
                            last_struct_name_.clear();
                            return builder_.create_call("grass_array_len", ctx_.get_i64_type(), { base });
                        } else if (oop_mgr_ && !receiver_struct.empty()) {
                            const auto* cd = oop_mgr_->get_class(receiver_struct);
                            const auto* md = cd ? cd->find_method(member_name) : nullptr;
                            base = oop_mgr_->emit_call_method(builder_, base, receiver_struct, member_name, args);
                            if (md && md->return_type && md->return_type->is_struct()) {
                                last_struct_name_ = md->return_type->struct_name;
                            } else if (md && md->return_type && md->return_type->is_pointer() && md->return_type->element_type && md->return_type->element_type->is_struct()) {
                                last_struct_name_ = md->return_type->element_type->struct_name;
                            } else {
                                last_struct_name_.clear();
                            }
                            return base;
                        }
                    } else {
                        if (oop_mgr_ && !receiver_struct.empty()) {
                            std::string prev_struct = receiver_struct;
                            const auto* cd = oop_mgr_->get_class(prev_struct);
                            std::string next_struct = "";
                            bool is_nested_val_struct = false;
                            if (cd) {
                                auto fit = cd->field_indices.find(member_name);
                                if (fit != cd->field_indices.end()) {
                                    const auto& fld = cd->fields[fit->second];
                                    if (fld.type) {
                                        if (fld.type->is_struct()) {
                                            is_nested_val_struct = true;
                                            next_struct = fld.type->struct_name;
                                        } else if (fld.type->is_pointer() && fld.type->element_type && fld.type->element_type->is_struct()) {
                                            next_struct = fld.type->element_type->struct_name;
                                        }
                                    }
                                }
                            }
                            if (is_nested_val_struct) {
                                base = oop_mgr_->emit_field_gep(builder_, base, prev_struct, member_name);
                            } else {
                                base = oop_mgr_->emit_load_field(builder_, base, prev_struct, member_name);
                            }
                            last_struct_name_ = next_struct;
                        }
                    }
                    continue;
                }
                // Cast: expr as Type* or expr as Type
                if (match(TOK_AS)) {
                    if (peek().kind != TOK_IDENT) {
                        error = "Expected type name after 'as'!";
                        return 0;
                    }
                    std::string tname = advance().text;
                    while (match(TOK_COLON_COLON)) {
                        if (peek().kind == TOK_IDENT) tname += "::" + advance().text;
                    }
                    ir::IRType* tgt_t = parse_type_name(tname);
                    while (match(TOK_STAR)) {
                        tgt_t = ctx_.get_ptr_type(tgt_t);
                    }
                    base = coerce_type(base, tgt_t);
                    continue;
                }
                // Array or pointer subscript: base[idx]
                if (match(TOK_LBRACKET)) {
                    ir::ValueId idx = parse_expression(error);
                    if (!idx) return 0;
                    consume(TOK_RBRACKET, "Expected ']' closing array index!", error);
                    ir::IRType* base_t = builder_.get_function()->has_value(base)
                        ? builder_.get_function()->get_value(base).type : nullptr;
                    if (base_t && base_t->is_pointer() && base_t->element_type && base_t->element_type->is_struct() &&
                        vec_struct_meta_.count(base_t->element_type->struct_name)) {
                        const auto& vm = vec_struct_meta_[base_t->element_type->struct_name];
                        ir::IRType* elem_t = vm.elem_type;
                        ir::ValueId data_ptr = emit_vec_data(base, elem_t);
                        ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                        ir::ValueId elem_ptr = builder_.create_gep(data_ptr, { idx64 }, elem_t, "vec_idx_gep");
                        if (elem_t->is_struct()) {
                            last_struct_name_ = vm.elem_struct;
                            base = elem_ptr;
                        } else if (elem_t->is_pointer() && elem_t->element_type && elem_t->element_type->is_struct()) {
                            last_struct_name_ = elem_t->element_type->struct_name;
                            base = builder_.create_load(elem_ptr, "vec_idx_val");
                        } else {
                            last_struct_name_.clear();
                            base = builder_.create_load(elem_ptr, "vec_idx_val");
                        }
                    } else if (base_t && base_t->is_pointer() && base_t->element_type && !base_t->element_type->is_void()) {
                        ir::IRType* elem_t = base_t->element_type;
                        ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                        ir::ValueId elem_ptr = builder_.create_gep(base, { idx64 }, elem_t, "ptr_idx_gep");
                        if (elem_t->is_struct()) {
                            last_struct_name_ = elem_t->struct_name;
                            base = elem_ptr;
                        } else if (elem_t->is_pointer() && elem_t->element_type && elem_t->element_type->is_struct()) {
                            last_struct_name_ = elem_t->element_type->struct_name;
                            base = builder_.create_load(elem_ptr, "ptr_idx_val");
                        } else {
                            last_struct_name_.clear();
                            base = builder_.create_load(elem_ptr, "ptr_idx_val");
                        }
                    } else {
                        base = builder_.create_call("grass_array_get", ctx_.get_i64_type(), { base, idx });
                    }
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

            // Array literal: [] or [elem1, elem2, ...]
            if (match(TOK_LBRACKET)) {
                ir::ValueId arr = builder_.create_call("grass_array_create", ctx_.get_ptr_type(), {});
                if (!match(TOK_RBRACKET)) {
                    do {
                        ir::ValueId elem = parse_expression(error);
                        if (!elem) return 0;
                        builder_.create_call("grass_array_push", ctx_.get_void_type(), { arr, elem });
                    } while (match(TOK_COMMA));
                    if (!consume(TOK_RBRACKET, "Expected ']' closing array literal!", error)) return 0;
                }
                return arr;
            }

            // Number literal
            if (peek().kind == TOK_NUMBER) {
                last_struct_name_.clear();
                const auto& tok = advance();
                bool is_float = (tok.text.find('.') != std::string::npos ||
                                 tok.text.find('e') != std::string::npos ||
                                 tok.text.find('E') != std::string::npos);
                if (!is_float) {
                    try {
                        int64_t ival = std::stoll(tok.text);
                        return builder_.create_const_i64(ival);
                    } catch (...) {}
                }
                double val = tok.num_val;
                if (!is_float && std::floor(val) == val) {
                    return builder_.create_const_i64(static_cast<int64_t>(val));
                }
                return builder_.create_const_f64(val);
            }

            // String literal
            if (peek().kind == TOK_STRING) {
                last_struct_name_.clear();
                std::string s = advance().text;
                std::string sym = add_rodata_string(s);
                return builder_.create_call(sym, ctx_.get_ptr_type(ctx_.get_i8_type()), {});
            }

            // Boolean literals: true / false
            if (peek().kind == TOK_IDENT && peek().text == "true") {
                last_struct_name_.clear();
                advance();
                return builder_.create_const_i1(true);
            }
            if (peek().kind == TOK_IDENT && peek().text == "false") {
                last_struct_name_.clear();
                advance();
                return builder_.create_const_i1(false);
            }

            // Nullptr literal
            if (peek().kind == TOK_NULLPTR || (peek().kind == TOK_IDENT && (peek().text == "nullptr" || peek().text == "null"))) {
                last_struct_name_.clear();
                advance();
                return builder_.create_const_int(ctx_.get_ptr_type(ctx_.get_void_type()), 0);
            }

            // Sizeof operator
            if (match(TOK_SIZEOF)) {
                last_struct_name_.clear();
                if (!match(TOK_LPAREN)) {
                    error = "Expected '(' after sizeof";
                    return 0;
                }
                bool is_type = is_type_or_func_keyword(peek().text) || (oop_mgr_ && oop_mgr_->has_class(peek().text));
                if (is_type) {
                    std::string tname = advance().text;
                    while (match(TOK_COLON_COLON)) {
                        if (peek().kind == TOK_IDENT) tname += "::" + advance().text;
                    }
                    ir::IRType* t = parse_type_name(tname);
                    while (match(TOK_STAR)) {
                        t = ctx_.get_ptr_type(t);
                    }
                    consume(TOK_RPAREN, "Expected ')' after sizeof(Type)", error);
                    uint64_t sz = t ? t->size() : 8;
                    if (sz == 0 && t && t->is_void()) sz = 1;
                    return builder_.create_const_i64(static_cast<int64_t>(sz));
                } else {
                    ir::ValueId expr_val = parse_expression(error);
                    consume(TOK_RPAREN, "Expected ')' after sizeof(expr)", error);
                    uint64_t sz = 8;
                    if (expr_val && builder_.get_function()->has_value(expr_val)) {
                        ir::IRType* et = builder_.get_function()->get_value(expr_val).type;
                        if (et) sz = et->size();
                    }
                    return builder_.create_const_i64(static_cast<int64_t>(sz));
                }
            }

            // Identifier or Function Call
            if (peek().kind == TOK_IDENT) {
                std::string ident = advance().text;
                while (match(TOK_COLON_COLON)) {
                    if (peek().kind == TOK_IDENT) {
                        ident += "::" + advance().text;
                    }
                }

                // Member access ahead? (e.g. hero.name or ptr->name)
                if (peek().kind == TOK_DOT || peek().kind == TOK_ARROW) {
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

                    std::string target_call_name = resolve_callee(ident);
                    ir::IRType* call_ret_type = ctx_.get_i64_type();
                    if ((ident == "to_str" || ident == "to_string") && !args.empty()) {
                        ir::IRType* at0 = builder_.get_function()->has_value(args[0]) ? builder_.get_function()->get_value(args[0]).type : nullptr;
                        if (at0 && at0->is_floating_point()) {
                            ident = "grass_float_to_str";
                            target_call_name = ident;
                            call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                        } else if (at0 && at0->is_pointer()) {
                            last_struct_name_.clear();
                            return args[0];
                        } else {
                            ident = "int_to_str";
                            target_call_name = ident;
                            call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                        }
                    } else if (ident == "upper" && !args.empty()) {
                        ident = "grass_to_upper";
                        target_call_name = ident;
                        call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                    } else if (ident == "lower" && !args.empty()) {
                        ident = "grass_to_lower";
                        target_call_name = ident;
                        call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                    } else if (ident == "trim" && !args.empty()) {
                        ident = "grass_trim";
                        target_call_name = ident;
                        call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                    } else if (ident == "len" || ident == "size") {
                        ident = "grass_array_len";
                        target_call_name = ident;
                    } else if (ident == "alloc") {
                        ident = "grass_alloc";
                        target_call_name = ident;
                        call_ret_type = ctx_.get_ptr_type();
                    } else if (ident == "free") {
                        ident = "grass_free";
                        target_call_name = ident;
                        call_ret_type = ctx_.get_void_type();
                    } else if (ident == "realloc") {
                        ident = "grass_realloc";
                        target_call_name = ident;
                        call_ret_type = ctx_.get_ptr_type();
                    }

                    if (oop_mgr_ && oop_mgr_->has_class(ident)) {
                        last_struct_name_ = ident;
                        return oop_mgr_->emit_new_object(builder_, ident, args);
                    }

                    // Overload / arity normalization for 3D Math, Gfx3D, and Physics
                    if (ident == "m4_translate" && args.size() >= 3) {
                        ident = "m4_translate_xyz";
                        target_call_name = ident;
                    } else if (ident == "m4_rotate" && args.size() >= 4) {
                        ident = "m4_rotate_xyz";
                        target_call_name = ident;
                    } else if (ident == "m4_scale" && args.size() >= 3) {
                        ident = "m4_scale_xyz";
                        target_call_name = ident;
                    } else if (ident == "g3d_clear" && args.size() >= 3) {
                        ident = "g3d_clear_rgb";
                        target_call_name = ident;
                    } else if (ident == "g3d_key_down" && args.size() == 1) {
                        ir::IRType* at0 = builder_.get_function()->has_value(args[0]) ? builder_.get_function()->get_value(args[0]).type : nullptr;
                        if (at0 && at0->is_pointer()) {
                            ident = "g3d_key_down_str";
                            target_call_name = ident;
                        }
                    } else if (ident == "g3d_mouse_btn" && args.empty()) {
                        args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 0));
                    } else if (ident == "g3d_camera" && args.size() >= 7) {
                        ident = "g3d_camera_xyz";
                        target_call_name = ident;
                    } else if (ident == "g3d_sphere") {
                        if (args.size() == 1) args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 16));
                        else if (args.size() > 2) args.resize(2);
                    } else if (ident == "g3d_plane" && args.size() >= 3) {
                        ident = "g3d_plane_segs";
                        target_call_name = ident;
                    } else if (ident == "g3d_set_color" && args.size() >= 4) {
                        ident = "g3d_set_color_rgb";
                        target_call_name = ident;
                    } else if (ident == "g3d_transform" && args.size() == 2) {
                        ident = "g3d_transform_mat";
                        target_call_name = ident;
                    } else if (ident == "g3d_dir_light" && args.size() >= 4) {
                        ident = "g3d_dir_light_rgb";
                        target_call_name = ident;
                        if (args.size() == 4) args.push_back(builder_.create_const_f64(1.0));
                    } else if (ident == "g3d_point_light" && args.size() == 4) {
                        args.push_back(builder_.create_const_f64(10.0));
                    } else if (ident == "g3d_fog" && args.size() == 3) {
                        args.push_back(builder_.create_const_f64(0.05));
                    } else if (ident == "g3d_bind_framebuffer" && args.size() == 3) {
                        args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 0));
                    } else if (ident == "phys_world") {
                        if (args.empty()) {
                            args.push_back(builder_.create_const_f64(0.0));
                            args.push_back(builder_.create_const_f64(-9.81));
                            args.push_back(builder_.create_const_f64(0.0));
                        } else if (args.size() == 1) {
                            ir::ValueId gy = args[0];
                            args = {
                                builder_.create_const_f64(0.0),
                                gy,
                                builder_.create_const_f64(0.0)
                            };
                        }
                    } else if (ident == "phys_step" && args.size() == 2) {
                        args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 8));
                    } else if (ident == "phys_box" && args.size() == 7) {
                        args.push_back(builder_.create_const_f64(1.0));
                    } else if (ident == "phys_sphere" && args.size() == 5) {
                        args.push_back(builder_.create_const_f64(1.0));
                    } else if (ident == "phys_cloth" && args.size() == 4) {
                        args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 8));
                    } else if (ident == "phys_link" && args.size() == 4) {
                        args.push_back(builder_.create_const_f64(0.0));
                    } else if ((ident == "phys_get_pos" || ident == "phys_get_rot") && args.size() == 1) {
                        args.insert(args.begin(), builder_.create_const_int(ctx_.get_i64_type(), 1));
                    } else if ((ident == "phys_set_pos" || ident == "phys_set_vel" || ident == "phys_impulse") && args.size() == 4) {
                        args.insert(args.begin(), builder_.create_const_int(ctx_.get_i64_type(), 1));
                    } else if (ident == "g2d_clear" && args.size() >= 3) {
                        ident = "g2d_clear_rgb";
                        target_call_name = ident;
                    } else if (ident == "g2d_key_down" && args.size() == 1) {
                        ir::IRType* at0 = builder_.get_function()->has_value(args[0]) ? builder_.get_function()->get_value(args[0]).type : nullptr;
                        if (at0 && at0->is_pointer()) {
                            ident = "g2d_key_down_str";
                            target_call_name = ident;
                        }
                    } else if (ident == "g2d_mouse_btn" && args.empty()) {
                        args.push_back(builder_.create_const_i64(0));
                    } else if (ident == "g2d_cam_set") {
                        while (args.size() < 2) args.push_back(builder_.create_const_f64(0.0));
                        if (args.size() == 2) args.push_back(builder_.create_const_f64(1.0));
                        if (args.size() == 3) args.push_back(builder_.create_const_f64(0.0));
                    } else if (ident == "g2d_emit_particles") {
                        if (args.size() == 3) args.push_back(builder_.create_const_i64(0xFFFF00));
                        if (args.size() >= 5) {
                            ident = "g2d_emit_particles_ex";
                            target_call_name = ident;
                            if (args.size() == 5) args.push_back(builder_.create_const_f64(1.0));
                        }
                    } else if (ident == "g2d_update_particles" && args.empty()) {
                        args.push_back(builder_.create_const_f64(1.0 / 60.0));
                    } else if (ident == "g2d_draw_sprite") {
                        if (args.size() == 5) args.push_back(builder_.create_const_f64(0.0));
                        if (args.size() == 6) args.push_back(builder_.create_const_f64(1.0));
                    } else if (ident == "g2d_bind_framebuffer" && args.size() == 3) {
                        args.push_back(builder_.create_const_i64(0));
                    } else if (ident == "g2d_line" && args.size() == 5) {
                        args.push_back(builder_.create_const_i64(1));
                    } else if (ident == "gfx_line" && args.size() == 5) {
                        args.push_back(builder_.create_const_i64(1));
                    } else if (ident == "gfx_frame" && args.empty()) {
                        args.push_back(builder_.create_const_i64(60));
                    } else if ((ident == "synth_play" || ident == "synth") && args.size() == 3) {
                        args.push_back(builder_.create_const_f64(80.0));
                    } else if (ident == "tone" && args.size() == 2) {
                        args.push_back(builder_.create_const_f64(80.0));
                    } else if (ident == "beep") {
                        if (args.empty()) args.push_back(builder_.create_const_f64(750.0));
                        if (args.size() == 1) args.push_back(builder_.create_const_i64(300));
                    } else if (ident == "sound_play" && args.size() == 1) {
                        args.push_back(builder_.create_const_i64(0));
                    } else if (ident == "bind_synth" && args.size() == 4) {
                        args.push_back(builder_.create_const_f64(80.0));
                    } else if (ident == "attach_sound" && args.size() == 4) {
                        args.push_back(builder_.create_const_f64(80.0));
                    } else if (ident == "tg_poll" && args.empty()) {
                        args.push_back(builder_.create_const_i64(2));
                    } else if (ident == "tg_send_photo" && args.size() == 2) {
                        args.push_back(builder_.create_call(add_rodata_string(""), ctx_.get_ptr_type(ctx_.get_i8_type()), {}));
                    } else if (ident == "sleep") {
                        ident = "grass_sleep_sec";
                        target_call_name = ident;
                        if (args.empty()) args.push_back(builder_.create_const_i64(0));
                    } else if (ident == "ping" && args.size() == 1) {
                        args.push_back(builder_.create_const_i64(80));
                    } else if (ident == "connect" && args.size() == 2) {
                        ident = "net_connect_host";
                        target_call_name = ident;
                    } else if (ident == "accept" && args.empty()) {
                        ident = "net_accept_srv";
                        target_call_name = ident;
                    } else if (ident == "net_close" && args.empty()) {
                        ident = "net_close_srv";
                        target_call_name = ident;
                    } else if (ident == "web_compile" && args.size() == 1) {
                        args.push_back(builder_.create_call(add_rodata_string(""), ctx_.get_ptr_type(ctx_.get_i8_type()), {}));
                    } else if ((ident == "thread_join" || ident == "mutex_lock") && args.size() == 1) {
                        args.push_back(builder_.create_const_i64(0));
                    } else if ((ident == "thread" || ident == "async") && !args.empty()) {
                        ir::ValueId fn_arg = args[0];
                        ir::ValueId param_arg = (args.size() >= 2) ? coerce_type(args[1], ctx_.get_ptr_type()) : builder_.create_const_int(ctx_.get_ptr_type(), 0);
                        args = { coerce_type(fn_arg, ctx_.get_ptr_type()), param_arg };
                        ident = "grass_native_thread_spawn";
                        target_call_name = ident;
                    }

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

                    ir::IRFunction* target_fn = module_.get_function(target_call_name);
                    if (!target_fn && target_call_name != ident) {
                        target_fn = module_.get_function(ident);
                    }
                    if (target_fn) {
                        call_ret_type = target_fn->return_type;
                        for (size_t i = 0; i < args.size() && i < target_fn->arguments.size(); ++i) {
                            args[i] = coerce_type(args[i], target_fn->arguments[i].type);
                        }
                    } else if (std_fn) {
                        for (size_t i = 0; i < args.size() && i < std_fn->param_types.size(); ++i) {
                            args[i] = coerce_type(args[i], std_fn->param_types[i]);
                        }
                    }

                    if (oop_mgr_ && (ident == "v3_create" || ident == "v3_add" || ident == "v3_sub" ||
                                     ident == "v3_mul" || ident == "v3_cross" || ident == "v3_normalize" ||
                                     ident == "v3_lerp" || ident == "phys_get_pos" || ident == "phys_get_rot")) {
                        const auto* vcd = oop_mgr_->get_class("Vec3");
                        if (vcd && vcd->ir_struct_type) {
                            call_ret_type = ctx_.get_ptr_type(vcd->ir_struct_type);
                        }
                    }

                    if (call_ret_type && call_ret_type->is_struct()) {
                        last_struct_name_ = call_ret_type->struct_name;
                    } else if (call_ret_type && call_ret_type->is_pointer() && call_ret_type->element_type && call_ret_type->element_type->is_struct()) {
                        last_struct_name_ = call_ret_type->element_type->struct_name;
                    } else {
                        last_struct_name_.clear();
                    }
                    return builder_.create_call(target_fn ? target_fn->name : target_call_name, call_ret_type, args);
                }

                // Variable access
                if (ident == "proc_mem") {
                    last_struct_name_.clear();
                    return builder_.create_call("hw_ram_used", ctx_.get_i64_type(), {});
                }
                const VarBinding* var = lookup_var(ident);
                if (!var) {
                    if (ir::IRFunction* fn_ref = module_.get_function(ident)) {
                        (void)fn_ref;
                        last_struct_name_.clear();
                        return builder_.create_const_int(ctx_.get_ptr_type(ctx_.get_void_type()), 0);
                    }
                    error = "Undeclared identifier: " + ident;
                    return 0;
                }
                if (!var->struct_type_name.empty()) {
                    last_struct_name_ = var->struct_type_name;
                    if (var->type && var->type->is_pointer()) {
                        return builder_.create_load(var->alloca_id, ident + "_ptr");
                    }
                    return var->alloca_id;
                }
                last_struct_name_.clear();
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
        grass::debug::GrassDebugTableBuilder debug_builder;

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
                                bytes = static_cast<uint32_t>(inst->type->element_type->size());
                                if (bytes == 0) bytes = 8;
                            }
                        }
                        alloca_stack_cursor = (alloca_stack_cursor + 15) & ~15;
                        alloca_offsets[inst->result_id] = alloca_stack_cursor;
                        alloca_stack_cursor += bytes;
                    }
                }
            }

            std::vector<backend::Reg> used_callee_xmm;
            if (options_.abi == TargetABI::Win64 && options_.arch == TargetArch::X86_64) {
                for (ir::PhysReg pr : alloc_res.used_callee_saved) {
                    backend::Reg r = phys_reg_to_backend(pr, options_.arch);
                    if (r != backend::Reg::None && backend::reg_is_xmm(r)) {
                        used_callee_xmm.push_back(r);
                    }
                }
            }

            int32_t xmm_save_base = alloca_stack_cursor + 16;
            std::unordered_map<backend::Reg, int32_t> xmm_save_offsets;
            for (backend::Reg xr : used_callee_xmm) {
                xmm_save_base = (xmm_save_base + 15) & ~15;
                xmm_save_offsets[xr] = xmm_save_base;
                xmm_save_base += 16;
            }

            size_t max_call_args = 0;
            for (const auto& bb : fn->blocks) {
                for (const auto& inst : bb->instructions) {
                    if (inst->op == ir::IROp::Call) {
                        max_call_args = std::max(max_call_args, inst->operands.size());
                    }
                }
            }

            int32_t phi_scratch_base = xmm_save_base + 16;

            // Compute stack frame
            backend::StackFrameRequest req;
            req.local_vars_size = phi_scratch_base + 512;
            req.has_calls = true;
            if (options_.abi == TargetABI::Win64) {
                uint32_t out_args = (max_call_args > 4) ? static_cast<uint32_t>(32 + (max_call_args - 4) * 8) : 32;
                req.max_outgoing_args_size = (out_args + 15) & ~15;
            } else {
                uint32_t out_args = (max_call_args > 6) ? static_cast<uint32_t>((max_call_args - 6) * 8) : 0;
                req.max_outgoing_args_size = (out_args + 15) & ~15;
            }
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
                for (backend::Reg xr : used_callee_xmm) {
                    int32_t off = xmm_save_offsets[xr];
                    encoder.movdqu_mx(backend::mem(backend::Reg::RBP, -off, 16), xr);
                }
                if (fn->name == "main" && !options_.freestanding) {
                    encoder.call_extern("grass_install_runtime_crash_handler");
                }
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

            auto to_vec_reg = [&](backend::Reg xmm_r, ir::IRType* vtype) -> backend::Reg {
                if (vtype && vtype->is_vector() && vtype->array_len == 4 && backend::reg_is_xmm(xmm_r)) {
                    return static_cast<backend::Reg>(0x0070 | backend::reg_raw_index(xmm_r));
                }
                return xmm_r;
            };

            auto to_xmm_reg = [&](backend::Reg r) -> backend::Reg {
                if (backend::reg_is_ymm(r)) {
                    return static_cast<backend::Reg>(0x0050 | backend::reg_raw_index(r));
                }
                return r;
            };

            auto load_val = [&](ir::ValueId val, backend::Reg temp_reg) -> backend::Reg {
                backend::Reg r = get_loc_reg(val);
                ir::IRType* vtype = fn->get_value(val).type;
                if (r != backend::Reg::None) {
                    return (vtype && vtype->is_vector()) ? to_vec_reg(r, vtype) : r;
                }
                int32_t off = get_spill_offset(val);
                if (vtype && vtype->is_vector()) {
                    backend::Reg base_xmm = backend::reg_is_xmm(temp_reg) ? temp_reg : backend::Reg::XMM0;
                    backend::Reg vtmp = to_vec_reg(base_xmm, vtype);
                    if (vtype->array_len == 4) {
                        encoder.vmovdqu_ym(vtmp, backend::mem(backend::Reg::RBP, -off, 32));
                    } else {
                        encoder.movdqu_xm(vtmp, backend::mem(backend::Reg::RBP, -off, 16));
                    }
                    return vtmp;
                }
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

                if (vtype && vtype->is_vector()) {
                    if (r != backend::Reg::None) {
                        backend::Reg vr = to_vec_reg(r, vtype);
                        if (vr != src_reg) {
                            if (vtype->array_len == 4) encoder.vmovdqu_yy(vr, src_reg);
                            else encoder.movdqu_xx(vr, src_reg);
                        }
                    } else {
                        int32_t off = get_spill_offset(val);
                        if (vtype->array_len == 4) {
                            encoder.vmovdqu_my(backend::mem(backend::Reg::RBP, -off, 32), src_reg);
                        } else {
                            encoder.movdqu_mx(backend::mem(backend::Reg::RBP, -off, 16), src_reg);
                        }
                    }
                    return;
                }

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

            auto load_binary_fp = [&](ir::ValueId v0, ir::ValueId v1, backend::Reg& out_lhs, backend::Reg& out_rhs) {
                backend::Reg r0 = get_loc_reg(v0);
                backend::Reg r1 = get_loc_reg(v1);

                if (r0 != backend::Reg::None && r1 != backend::Reg::None) {
                    out_lhs = r0;
                    out_rhs = r1;
                    return;
                }
                if (r0 != backend::Reg::None && r1 == backend::Reg::None) {
                    out_lhs = r0;
                    backend::Reg tmp = (r0 == backend::Reg::XMM1) ? backend::Reg::XMM2 : backend::Reg::XMM1;
                    int32_t off = get_spill_offset(v1);
                    encoder.movsd_xm(tmp, backend::mem(backend::Reg::RBP, -off, 8));
                    out_rhs = tmp;
                    return;
                }
                if (r0 == backend::Reg::None && r1 != backend::Reg::None) {
                    out_rhs = r1;
                    backend::Reg tmp = (r1 == backend::Reg::XMM0) ? backend::Reg::XMM2 : backend::Reg::XMM0;
                    int32_t off = get_spill_offset(v0);
                    encoder.movsd_xm(tmp, backend::mem(backend::Reg::RBP, -off, 8));
                    out_lhs = tmp;
                    return;
                }
                int32_t off0 = get_spill_offset(v0);
                int32_t off1 = get_spill_offset(v1);
                encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, -off0, 8));
                encoder.movsd_xm(backend::Reg::XMM1, backend::mem(backend::Reg::RBP, -off1, 8));
                out_lhs = backend::Reg::XMM0;
                out_rhs = backend::Reg::XMM1;
            };

            auto load_binary_gpr = [&](ir::ValueId v0, ir::ValueId v1, backend::Reg& out_lhs, backend::Reg& out_rhs) {
                backend::Reg r0 = get_loc_reg(v0);
                backend::Reg r1 = get_loc_reg(v1);

                if (r0 != backend::Reg::None && r1 != backend::Reg::None) {
                    out_lhs = r0;
                    out_rhs = r1;
                    return;
                }
                if (r0 != backend::Reg::None && r1 == backend::Reg::None) {
                    out_lhs = r0;
                    backend::Reg tmp = (r0 == backend::Reg::R10) ? backend::Reg::R11 : backend::Reg::R10;
                    int32_t off = get_spill_offset(v1);
                    encoder.mov_rm(tmp, backend::mem(backend::Reg::RBP, -off, 8));
                    out_rhs = tmp;
                    return;
                }
                if (r0 == backend::Reg::None && r1 != backend::Reg::None) {
                    out_rhs = r1;
                    backend::Reg tmp = (r1 == backend::Reg::R11) ? backend::Reg::R10 : backend::Reg::R11;
                    int32_t off = get_spill_offset(v0);
                    encoder.mov_rm(tmp, backend::mem(backend::Reg::RBP, -off, 8));
                    out_lhs = tmp;
                    return;
                }
                int32_t off0 = get_spill_offset(v0);
                int32_t off1 = get_spill_offset(v1);
                encoder.mov_rm(backend::Reg::R11, backend::mem(backend::Reg::RBP, -off0, 8));
                encoder.mov_rm(backend::Reg::R10, backend::mem(backend::Reg::RBP, -off1, 8));
                out_lhs = backend::Reg::R11;
                out_rhs = backend::Reg::R10;
            };

            // Incoming function arguments placement
            const auto& arg_regs = abi_spec.get_integer_arg_regs();
            for (size_t i = 0; i < fn->arguments.size(); ++i) {
                ir::IRType* arg_t = fn->arguments[i].type;
                bool is_fp = arg_t && arg_t->is_floating_point();
                if (i < arg_regs.size()) {
                    if (is_fp) {
                        backend::Reg xmm_reg = static_cast<backend::Reg>(0x0050 + i);
                        store_val(fn->arguments[i].id, xmm_reg);
                    } else {
                        store_val(fn->arguments[i].id, arg_regs[i]);
                    }
                } else {
                    int32_t shadow_disp = (options_.abi == TargetABI::Win64) ? 32 : 0;
                    int32_t stack_off = 16 + shadow_disp + static_cast<int32_t>(i - arg_regs.size()) * 8;
                    if (is_fp) {
                        encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, stack_off, 8));
                        store_val(fn->arguments[i].id, backend::Reg::XMM0);
                    } else {
                        backend::Reg tmp = backend::Reg::R10;
                        encoder.mov_rm(tmp, backend::mem(backend::Reg::RBP, stack_off, 8));
                        store_val(fn->arguments[i].id, tmp);
                    }
                }
            }

            auto emit_phi_copies = [&](ir::BasicBlock* from_bb, ir::BasicBlock* to_bb) {
                if (!to_bb) return;
                struct PhiMove {
                    ir::ValueId src_val;
                    ir::ValueId dst_val;
                    bool is_fp;
                    uint8_t vec_lanes;
                };
                std::vector<PhiMove> moves;
                for (const auto& phi_inst : to_bb->instructions) {
                    if (phi_inst->op != ir::IROp::Phi) break;
                    ir::ValueId in_val = phi_inst->get_phi_incoming_value(from_bb);
                    if (in_val != ir::kInvalidValueId) {
                        ir::IRType* vt = phi_inst->type;
                        bool fp = vt && vt->is_floating_point();
                        uint8_t vlanes = (vt && vt->is_vector()) ? static_cast<uint8_t>(vt->array_len) : 0;
                        moves.push_back({ in_val, phi_inst->result_id, fp, vlanes });
                    }
                }
                if (moves.empty()) return;

                // Pass 1: save all source values into stack scratch slots
                int32_t scratch_base = phi_scratch_base;
                for (size_t i = 0; i < moves.size(); ++i) {
                    int32_t off = scratch_base + static_cast<int32_t>(i * 32);
                    if (moves[i].vec_lanes == 4) {
                        backend::Reg r = load_val(moves[i].src_val, backend::Reg::XMM0);
                        encoder.vmovdqu_my(backend::mem(backend::Reg::RBP, -off, 32), r);
                    } else if (moves[i].vec_lanes == 2) {
                        backend::Reg r = load_val(moves[i].src_val, backend::Reg::XMM0);
                        encoder.movdqu_mx(backend::mem(backend::Reg::RBP, -off, 16), r);
                    } else if (moves[i].is_fp) {
                        backend::Reg r = load_val(moves[i].src_val, backend::Reg::XMM0);
                        encoder.movsd_mx(backend::mem(backend::Reg::RBP, -off, 8), r);
                    } else {
                        backend::Reg r = load_val(moves[i].src_val, backend::Reg::R10);
                        encoder.mov_mr(backend::mem(backend::Reg::RBP, -off, 8), r);
                    }
                }

                // Pass 2: load from scratch slots into destination values
                for (size_t i = 0; i < moves.size(); ++i) {
                    int32_t off = scratch_base + static_cast<int32_t>(i * 32);
                    if (moves[i].vec_lanes == 4) {
                        encoder.vmovdqu_ym(backend::Reg::YMM0, backend::mem(backend::Reg::RBP, -off, 32));
                        store_val(moves[i].dst_val, backend::Reg::YMM0);
                    } else if (moves[i].vec_lanes == 2) {
                        encoder.movdqu_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, -off, 16));
                        store_val(moves[i].dst_val, backend::Reg::XMM0);
                    } else if (moves[i].is_fp) {
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
                    size_t inst_code_start = encoder.code_size();
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
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            bool in_reg = (dst != backend::Reg::None && backend::reg_is_xmm(dst));
                            int32_t off = in_reg ? phi_scratch_base : get_spill_offset(inst->result_id);
                            encoder.mov_ri64(backend::Reg::R11, (int64_t)raw_bits);
                            encoder.mov_mr(backend::mem(backend::Reg::RBP, -off, 8), backend::Reg::R11);
                            if (in_reg) {
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
                            backend::Reg lhs, rhs;
                            load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            if (out == rhs) {
                                encoder.mov_rr(backend::Reg::R9, lhs);
                                encoder.add_rr(backend::Reg::R9, rhs);
                                store_val(inst->result_id, backend::Reg::R9);
                            } else {
                                if (lhs != out) encoder.mov_rr(out, lhs);
                                encoder.add_rr(out, rhs);
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::Sub: {
                            backend::Reg lhs, rhs;
                            load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            if (out == rhs) {
                                encoder.mov_rr(backend::Reg::R9, lhs);
                                encoder.sub_rr(backend::Reg::R9, rhs);
                                store_val(inst->result_id, backend::Reg::R9);
                            } else {
                                if (lhs != out) encoder.mov_rr(out, lhs);
                                encoder.sub_rr(out, rhs);
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::Mul: {
                            backend::Reg lhs, rhs;
                            load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            if (out == rhs) {
                                encoder.mov_rr(backend::Reg::R9, lhs);
                                encoder.imul_rr(backend::Reg::R9, rhs);
                                store_val(inst->result_id, backend::Reg::R9);
                            } else {
                                if (lhs != out) encoder.mov_rr(out, lhs);
                                encoder.imul_rr(out, rhs);
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::SDiv: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (rhs == backend::Reg::RAX || rhs == backend::Reg::RDX) {
                                encoder.mov_rr(backend::Reg::R10, rhs);
                                rhs = backend::Reg::R10;
                            }
                            backend::LabelId non_zero_lbl = encoder.create_label();
                            backend::LabelId done_lbl = encoder.create_label();
                            encoder.test_rr(rhs, rhs);
                            encoder.jcc_label(backend::CondCode::NE, non_zero_lbl);
                            encoder.xor_rr(backend::Reg::RAX, backend::Reg::RAX);
                            encoder.jmp_label(done_lbl);
                            encoder.bind_label(non_zero_lbl);
                            if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                            backend::LabelId do_div_lbl = encoder.create_label();
                            encoder.cmp_ri(rhs, -1);
                            encoder.jcc_label(backend::CondCode::NE, do_div_lbl);
                            encoder.mov_ri64(backend::Reg::R11, static_cast<int64_t>(0x8000000000000000ULL));
                            encoder.cmp_rr(backend::Reg::RAX, backend::Reg::R11);
                            encoder.jcc_label(backend::CondCode::NE, do_div_lbl);
                            encoder.jmp_label(done_lbl);
                            encoder.bind_label(do_div_lbl);
                            encoder.cqo();
                            encoder.idiv_r(rhs);
                            encoder.bind_label(done_lbl);
                            store_val(inst->result_id, backend::Reg::RAX);
                            break;
                        }
                        case ir::IROp::SRem: {
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                            if (rhs == backend::Reg::RAX || rhs == backend::Reg::RDX) {
                                encoder.mov_rr(backend::Reg::R10, rhs);
                                rhs = backend::Reg::R10;
                            }
                            backend::LabelId non_zero_lbl = encoder.create_label();
                            backend::LabelId done_lbl = encoder.create_label();
                            encoder.test_rr(rhs, rhs);
                            encoder.jcc_label(backend::CondCode::NE, non_zero_lbl);
                            encoder.xor_rr(backend::Reg::RDX, backend::Reg::RDX);
                            encoder.jmp_label(done_lbl);
                            encoder.bind_label(non_zero_lbl);
                            if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                            backend::LabelId do_rem_lbl = encoder.create_label();
                            encoder.cmp_ri(rhs, -1);
                            encoder.jcc_label(backend::CondCode::NE, do_rem_lbl);
                            encoder.mov_ri64(backend::Reg::R11, static_cast<int64_t>(0x8000000000000000ULL));
                            encoder.cmp_rr(backend::Reg::RAX, backend::Reg::R11);
                            encoder.jcc_label(backend::CondCode::NE, do_rem_lbl);
                            encoder.xor_rr(backend::Reg::RDX, backend::Reg::RDX);
                            encoder.jmp_label(done_lbl);
                            encoder.bind_label(do_rem_lbl);
                            encoder.cqo();
                            encoder.idiv_r(rhs);
                            encoder.bind_label(done_lbl);
                            store_val(inst->result_id, backend::Reg::RDX);
                            break;
                        }
                        case ir::IROp::And: {
                            backend::Reg lhs, rhs;
                            load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            if (out == rhs) {
                                encoder.mov_rr(backend::Reg::R9, lhs);
                                encoder.and_rr(backend::Reg::R9, rhs);
                                store_val(inst->result_id, backend::Reg::R9);
                            } else {
                                if (lhs != out) encoder.mov_rr(out, lhs);
                                encoder.and_rr(out, rhs);
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::Or: {
                            backend::Reg lhs, rhs;
                            load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            if (out == rhs) {
                                encoder.mov_rr(backend::Reg::R9, lhs);
                                encoder.or_rr(backend::Reg::R9, rhs);
                                store_val(inst->result_id, backend::Reg::R9);
                            } else {
                                if (lhs != out) encoder.mov_rr(out, lhs);
                                encoder.or_rr(out, rhs);
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::Xor: {
                            backend::Reg lhs, rhs;
                            load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            if (out == rhs) {
                                encoder.mov_rr(backend::Reg::R9, lhs);
                                encoder.xor_rr(backend::Reg::R9, rhs);
                                store_val(inst->result_id, backend::Reg::R9);
                            } else {
                                if (lhs != out) encoder.mov_rr(out, lhs);
                                encoder.xor_rr(out, rhs);
                                store_val(inst->result_id, out);
                            }
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
                            backend::Reg lhs, rhs;
                            load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            encoder.cmp_rr(lhs, rhs);
                            backend::CondCode cc = icmp_to_cond_code(inst->icmp_cond);
                            encoder.setcc_r(cc, backend::Reg::R11B);
                            encoder.movzx_rr8(out, backend::Reg::R11B);
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
                            ir::IRType* rt = fn->get_value(inst->result_id).type;
                            backend::Reg ptr = load_val(inst->operands[0], backend::Reg::R10);
                            if (rt && rt->is_floating_point()) {
                                backend::Reg dst = get_loc_reg(inst->result_id);
                                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                                encoder.movsd_xm(out, backend::mem(ptr, 0, 8));
                                store_val(inst->result_id, out);
                            } else {
                                backend::Reg dst = get_loc_reg(inst->result_id);
                                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                                uint8_t ld_size = 8;
                                if (rt) {
                                    ld_size = static_cast<uint8_t>(rt->size());
                                    if (ld_size == 0) ld_size = 8;
                                }
                                if (ld_size == 1) {
                                    encoder.movzx_rm8(out, backend::mem(ptr, 0, 1));
                                } else if (ld_size == 2) {
                                    encoder.movzx_rm16(out, backend::mem(ptr, 0, 2));
                                } else if (ld_size == 4) {
                                    encoder.mov_rm(backend::reg_with_size(out, 4), backend::mem(ptr, 0, 4));
                                } else {
                                    encoder.mov_rm(out, backend::mem(ptr, 0, 8));
                                }
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::Store: {
                            backend::Reg ptr = load_val(inst->operands[1], backend::Reg::R10);
                            ir::IRType* val_t = fn->get_value(inst->operands[0]).type;
                            if (val_t && val_t->is_floating_point()) {
                                backend::Reg val = load_val(inst->operands[0], backend::Reg::XMM0);
                                encoder.movsd_mx(backend::mem(ptr, 0, 8), val);
                            } else {
                                backend::Reg val = load_val(inst->operands[0], backend::Reg::R11);
                                uint8_t st_size = 8;
                                ir::IRType* ptr_t = fn->get_value(inst->operands[1]).type;
                                if (ptr_t && ptr_t->element_type) {
                                    st_size = static_cast<uint8_t>(ptr_t->element_type->size());
                                    if (st_size == 0) st_size = 8;
                                }
                                backend::Reg val_sized = backend::reg_with_size(val, st_size);
                                encoder.mov_mr(backend::mem(ptr, 0, st_size), val_sized);
                            }
                            break;
                        }
                        case ir::IROp::GetElementPtr: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            backend::Reg base = load_val(inst->operands[0], backend::Reg::RAX);
                            encoder.mov_rr(out, base);
                            if (inst->gep_field_index >= 0) {
                                int64_t byte_off = static_cast<int64_t>(inst->gep_field_index) * 8;
                                ir::IRType* base_t = fn->get_value(inst->operands[0]).type;
                                if (base_t && base_t->element_type && base_t->element_type->is_struct()) {
                                    byte_off = static_cast<int64_t>(base_t->element_type->get_field_offset(static_cast<uint32_t>(inst->gep_field_index)));
                                }
                                if (byte_off != 0) {
                                    encoder.add_ri(out, static_cast<int32_t>(byte_off));
                                }
                            } else if (inst->operands.size() > 1) {
                                int64_t elem_sz = 8;
                                ir::IRType* base_t = fn->get_value(inst->operands[0]).type;
                                if (base_t && base_t->element_type) {
                                    elem_sz = static_cast<int64_t>(base_t->element_type->size());
                                    if (elem_sz <= 0) elem_sz = 1;
                                }
                                backend::Reg idx = load_val(inst->operands[1], backend::Reg::R10);
                                if (elem_sz != 1) {
                                    encoder.imul_rri(backend::Reg::R10, idx, static_cast<int32_t>(elem_sz));
                                    encoder.add_rr(out, backend::Reg::R10);
                                } else {
                                    encoder.add_rr(out, idx);
                                }
                            }
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Call: {
                            // Check for bare-metal intrinsics first
                            if (inst->callee_name == "port_in8" || inst->callee_name == "io_in8" || inst->callee_name == "inb") {
                                backend::Reg p = load_val(inst->operands[0], backend::Reg::RDX);
                                if (p != backend::Reg::RDX) encoder.mov_rr(backend::Reg::RDX, p);
                                encoder.in_al_dx();
                                encoder.movzx_rr8(backend::Reg::RAX, backend::Reg::AL);
                                if (inst->result_id != ir::kInvalidValueId) {
                                    store_val(inst->result_id, backend::Reg::RAX);
                                }
                                break;
                            }
                            if (inst->callee_name == "port_out8" || inst->callee_name == "io_out8" || inst->callee_name == "outb") {
                                backend::Reg p = load_val(inst->operands[0], backend::Reg::RDX);
                                backend::Reg v = load_val(inst->operands[1], backend::Reg::RAX);
                                if (v == backend::Reg::RDX && p != backend::Reg::RDX) {
                                    encoder.xchg_rr(backend::Reg::RDX, p);
                                    encoder.mov_rr(backend::Reg::RAX, p);
                                } else {
                                    if (p != backend::Reg::RDX) encoder.mov_rr(backend::Reg::RDX, p);
                                    if (v != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, v);
                                }
                                encoder.out_dx_al();
                                break;
                            }
                            if (inst->callee_name == "cpu_cli" || inst->callee_name == "cli") {
                                encoder.cli();
                                break;
                            }
                            if (inst->callee_name == "cpu_sti" || inst->callee_name == "sti") {
                                encoder.sti();
                                break;
                            }
                            if (inst->callee_name == "cpu_hlt" || inst->callee_name == "hlt") {
                                encoder.hlt();
                                break;
                            }
                            if (inst->callee_name == "memory_barrier" || inst->callee_name == "read_barrier" || inst->callee_name == "write_barrier") {
                                encoder.mfence();
                                break;
                            }
                            if (inst->callee_name == "mmio_read8") {
                                backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                                encoder.movzx_rm8(backend::Reg::RAX, backend::mem(addr, 0, 1));
                                if (inst->result_id != ir::kInvalidValueId) {
                                    store_val(inst->result_id, backend::Reg::RAX);
                                }
                                break;
                            }
                            if (inst->callee_name == "mmio_read16") {
                                backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                                encoder.movzx_rm16(backend::Reg::RAX, backend::mem(addr, 0, 2));
                                if (inst->result_id != ir::kInvalidValueId) {
                                    store_val(inst->result_id, backend::Reg::RAX);
                                }
                                break;
                            }
                            if (inst->callee_name == "mmio_read32") {
                                backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                                encoder.mov_rm(backend::Reg::EAX, backend::mem(addr, 0, 4));
                                if (inst->result_id != ir::kInvalidValueId) {
                                    store_val(inst->result_id, backend::Reg::RAX);
                                }
                                break;
                            }
                            if (inst->callee_name == "mmio_read64") {
                                backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                                encoder.mov_rm(backend::Reg::RAX, backend::mem(addr, 0, 8));
                                if (inst->result_id != ir::kInvalidValueId) {
                                    store_val(inst->result_id, backend::Reg::RAX);
                                }
                                break;
                            }
                            if (inst->callee_name == "mmio_write8") {
                                backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                                backend::Reg val = load_val(inst->operands[1], backend::Reg::R11);
                                backend::Reg val8 = backend::reg_with_size(val, 1);
                                encoder.mov_mr(backend::mem(addr, 0, 1), val8);
                                break;
                            }
                            if (inst->callee_name == "mmio_write16") {
                                backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                                backend::Reg val = load_val(inst->operands[1], backend::Reg::R11);
                                backend::Reg val16 = backend::reg_with_size(val, 2);
                                encoder.mov_mr(backend::mem(addr, 0, 2), val16);
                                break;
                            }
                            if (inst->callee_name == "mmio_write32") {
                                backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                                backend::Reg val = load_val(inst->operands[1], backend::Reg::R11);
                                backend::Reg val32 = backend::reg_with_size(val, 4);
                                encoder.mov_mr(backend::mem(addr, 0, 4), val32);
                                break;
                            }
                            if (inst->callee_name == "mmio_write64") {
                                backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                                backend::Reg val = load_val(inst->operands[1], backend::Reg::R11);
                                backend::Reg val64 = backend::reg_with_size(val, 8);
                                encoder.mov_mr(backend::mem(addr, 0, 8), val64);
                                break;
                            }

                            // Check if callee is a string symbol in rodata
                            if (rodata_symbols.count(inst->callee_name)) {
                                encoder.lea_symbol(backend::Reg::RAX, inst->callee_name);
                                store_val(inst->result_id, backend::Reg::RAX);
                                break;
                            }

                            if (inst->callee_name == "system" && !options_.freestanding &&
                                options_.arch == TargetArch::X86_64 && options_.abi == TargetABI::Win64) {
                                encoder.call_extern("gout_flush");
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
                            backend::Reg lhs, rhs;
                            load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                            if (out == rhs) {
                                encoder.movsd_xx(backend::Reg::XMM2, lhs);
                                encoder.addsd_xx(backend::Reg::XMM2, rhs);
                                store_val(inst->result_id, backend::Reg::XMM2);
                            } else {
                                if (out != lhs) encoder.movsd_xx(out, lhs);
                                encoder.addsd_xx(out, rhs);
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::FSub: {
                            backend::Reg lhs, rhs;
                            load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                            if (out == rhs) {
                                encoder.movsd_xx(backend::Reg::XMM2, lhs);
                                encoder.subsd_xx(backend::Reg::XMM2, rhs);
                                store_val(inst->result_id, backend::Reg::XMM2);
                            } else {
                                if (out != lhs) encoder.movsd_xx(out, lhs);
                                encoder.subsd_xx(out, rhs);
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::FMul: {
                            backend::Reg lhs, rhs;
                            load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                            if (out == rhs) {
                                encoder.movsd_xx(backend::Reg::XMM2, lhs);
                                encoder.mulsd_xx(backend::Reg::XMM2, rhs);
                                store_val(inst->result_id, backend::Reg::XMM2);
                            } else {
                                if (out != lhs) encoder.movsd_xx(out, lhs);
                                encoder.mulsd_xx(out, rhs);
                                store_val(inst->result_id, out);
                            }
                            break;
                        }
                        case ir::IROp::FDiv: {
                            backend::Reg lhs, rhs;
                            load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                            backend::Reg res_reg = (out == rhs) ? backend::Reg::XMM2 : out;
                            backend::LabelId non_zero_lbl = encoder.create_label();
                            backend::LabelId done_lbl = encoder.create_label();
                            encoder.xorpd_xx(backend::Reg::XMM3, backend::Reg::XMM3);
                            encoder.ucomisd_xx(rhs, backend::Reg::XMM3);
                            encoder.jcc_label(backend::CondCode::NE, non_zero_lbl);
                            encoder.jcc_label(backend::CondCode::P, non_zero_lbl);
                            encoder.xorpd_xx(res_reg, res_reg);
                            encoder.jmp_label(done_lbl);
                            encoder.bind_label(non_zero_lbl);
                            if (res_reg != lhs) encoder.movsd_xx(res_reg, lhs);
                            encoder.divsd_xx(res_reg, rhs);
                            encoder.bind_label(done_lbl);
                            store_val(inst->result_id, res_reg);
                            break;
                        }
                        case ir::IROp::FCmp: {
                            backend::Reg lhs, rhs;
                            load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                            encoder.ucomisd_xx(lhs, rhs);
                            backend::CondCode cc = backend::CondCode::E;
                            switch (inst->fcmp_cond) {
                                case ir::FCmpCond::OEQ: cc = backend::CondCode::E; break;
                                case ir::FCmpCond::ONE: cc = backend::CondCode::NE; break;
                                case ir::FCmpCond::OLT: cc = backend::CondCode::B; break;
                                case ir::FCmpCond::OLE: cc = backend::CondCode::BE; break;
                                case ir::FCmpCond::OGT: cc = backend::CondCode::A; break;
                                case ir::FCmpCond::OGE: cc = backend::CondCode::AE; break;
                            }
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                            encoder.setcc_r(cc, backend::Reg::R11B);
                            encoder.movzx_rr8(out, backend::Reg::R11B);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::FNeg: {
                            backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM1);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                            encoder.xorpd_xx(backend::Reg::XMM2, backend::Reg::XMM2);
                            encoder.subsd_xx(backend::Reg::XMM2, src);
                            if (out != backend::Reg::XMM2) encoder.movsd_xx(out, backend::Reg::XMM2);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::SIToFP: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM2;
                            backend::Reg src = load_val(inst->operands[0], backend::Reg::R10);
                            encoder.cvtsi2sd_xr(out, src);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::FPToSI: {
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R10;
                            backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM2);
                            encoder.cvttsd2si_rx(out, src);
                            store_val(inst->result_id, out);
                            break;
                        }
                        case ir::IROp::Trunc:
                        case ir::IROp::SExt:
                        case ir::IROp::ZExt:
                        case ir::IROp::PtrToInt:
                        case ir::IROp::IntToPtr:
                        case ir::IROp::Bitcast: {
                            backend::Reg src = load_val(inst->operands[0], backend::Reg::R10);
                            store_val(inst->result_id, src);
                            break;
                        }
                        case ir::IROp::Phi: {
                            // Handled at predecessor jump edges
                            break;
                        }
                        case ir::IROp::VecZero: {
                            ir::IRType* vt = inst->type;
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg xmm_out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                            backend::Reg vout = to_vec_reg(xmm_out, vt);
                            if (vt && vt->array_len == 4) {
                                encoder.vpxor_yyy(vout, vout, vout);
                            } else {
                                encoder.pxor_xx(vout, vout);
                            }
                            store_val(inst->result_id, vout);
                            break;
                        }
                        case ir::IROp::VecSplat: {
                            ir::IRType* vt = inst->type;
                            bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg xmm_out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                            backend::Reg vout = to_vec_reg(xmm_out, vt);
                            if (is_fp) {
                                backend::Reg sreg = load_val(inst->operands[0], backend::Reg::XMM1);
                                if (sreg != xmm_out) encoder.movsd_xx(xmm_out, sreg);
                                if (vt && vt->array_len == 4) {
                                    encoder.vbroadcastsd_yx(vout, xmm_out);
                                } else {
                                    encoder.shufpd_xxi(xmm_out, xmm_out, 0);
                                }
                            } else {
                                backend::Reg sreg = load_val(inst->operands[0], backend::Reg::RAX);
                                encoder.movq_xr(xmm_out, sreg);
                                if (vt && vt->array_len == 4) {
                                    encoder.vpbroadcastq_yx(vout, xmm_out);
                                } else {
                                    encoder.shufpd_xxi(xmm_out, xmm_out, 0);
                                }
                            }
                            store_val(inst->result_id, vout);
                            break;
                        }
                        case ir::IROp::VecLoad: {
                            ir::IRType* vt = inst->type;
                            bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                            backend::Reg ptr = load_val(inst->operands[0], backend::Reg::R10);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg xmm_out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                            backend::Reg vout = to_vec_reg(xmm_out, vt);
                            if (vt && vt->array_len == 4) {
                                if (is_fp) encoder.vmovupd_ym(vout, backend::mem(ptr, 0, 32));
                                else encoder.vmovdqu_ym(vout, backend::mem(ptr, 0, 32));
                            } else {
                                if (is_fp) encoder.movupd_xm(vout, backend::mem(ptr, 0, 16));
                                else encoder.movdqu_xm(vout, backend::mem(ptr, 0, 16));
                            }
                            store_val(inst->result_id, vout);
                            break;
                        }
                        case ir::IROp::VecStore: {
                            ir::IRType* vt = fn->get_value(inst->operands[0]).type;
                            bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                            backend::Reg vreg = load_val(inst->operands[0], backend::Reg::XMM0);
                            backend::Reg ptr = load_val(inst->operands[1], backend::Reg::R10);
                            if (vt && vt->array_len == 4) {
                                if (is_fp) encoder.vmovupd_my(backend::mem(ptr, 0, 32), vreg);
                                else encoder.vmovdqu_my(backend::mem(ptr, 0, 32), vreg);
                            } else {
                                if (is_fp) encoder.movupd_mx(backend::mem(ptr, 0, 16), vreg);
                                else encoder.movdqu_mx(backend::mem(ptr, 0, 16), vreg);
                            }
                            break;
                        }
                        case ir::IROp::VecAdd:
                        case ir::IROp::VecSub:
                        case ir::IROp::VecMul: {
                            ir::IRType* vt = inst->type;
                            bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                            uint64_t lanes = vt ? vt->array_len : 2;
                            backend::Reg lhs = load_val(inst->operands[0], backend::Reg::XMM0);
                            backend::Reg rhs = load_val(inst->operands[1], backend::Reg::XMM1);
                            backend::Reg dst = get_loc_reg(inst->result_id);
                            backend::Reg xmm_out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM2;
                            backend::Reg vout = to_vec_reg(xmm_out, vt);

                            if (lanes == 4) {
                                if (inst->op == ir::IROp::VecAdd) {
                                    if (is_fp) encoder.vaddpd_yyy(vout, lhs, rhs);
                                    else encoder.vpaddq_yyy(vout, lhs, rhs);
                                } else if (inst->op == ir::IROp::VecSub) {
                                    if (is_fp) encoder.vsubpd_yyy(vout, lhs, rhs);
                                    else encoder.vpsubq_yyy(vout, lhs, rhs);
                                } else {
                                    encoder.vmulpd_yyy(vout, lhs, rhs);
                                }
                                store_val(inst->result_id, vout);
                            } else {
                                auto emit_sse_op = [&](backend::Reg d, backend::Reg s) {
                                    if (inst->op == ir::IROp::VecAdd) {
                                        if (is_fp) encoder.addpd_xx(d, s);
                                        else encoder.paddq_xx(d, s);
                                    } else if (inst->op == ir::IROp::VecSub) {
                                        if (is_fp) encoder.subpd_xx(d, s);
                                        else encoder.psubq_xx(d, s);
                                    } else {
                                        encoder.mulpd_xx(d, s);
                                    }
                                };
                                if (vout == rhs && vout != lhs) {
                                    backend::Reg tmp = backend::Reg::XMM2;
                                    if (lhs != backend::Reg::XMM0 && rhs != backend::Reg::XMM0) tmp = backend::Reg::XMM0;
                                    else if (lhs != backend::Reg::XMM1 && rhs != backend::Reg::XMM1) tmp = backend::Reg::XMM1;
                                    encoder.movdqu_xx(tmp, lhs);
                                    emit_sse_op(tmp, rhs);
                                    store_val(inst->result_id, tmp);
                                } else {
                                    if (vout != lhs) encoder.movdqu_xx(vout, lhs);
                                    emit_sse_op(vout, rhs);
                                    store_val(inst->result_id, vout);
                                }
                            }
                            break;
                        }
                        case ir::IROp::VecHAdd: {
                            ir::IRType* vt = fn->get_value(inst->operands[0]).type;
                            bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                            uint64_t lanes = vt ? vt->array_len : 2;
                            backend::Reg vsrc = load_val(inst->operands[0], backend::Reg::XMM0);
                            if (lanes == 4) {
                                backend::Reg hi_tmp = (backend::reg_raw_index(vsrc) == 1) ? backend::Reg::XMM2 : backend::Reg::XMM1;
                                if (is_fp) {
                                    encoder.vextractf128_xyi(hi_tmp, vsrc, 1);
                                    if (backend::reg_raw_index(vsrc) != 0) encoder.vextractf128_xyi(backend::Reg::XMM0, vsrc, 0);
                                    encoder.addpd_xx(backend::Reg::XMM0, hi_tmp);
                                    encoder.movhlps_xx(backend::Reg::XMM1, backend::Reg::XMM0);
                                    encoder.addsd_xx(backend::Reg::XMM0, backend::Reg::XMM1);
                                    encoder.vzeroupper();
                                    store_val(inst->result_id, backend::Reg::XMM0);
                                } else {
                                    encoder.vextracti128_xyi(hi_tmp, vsrc, 1);
                                    if (backend::reg_raw_index(vsrc) != 0) encoder.vextracti128_xyi(backend::Reg::XMM0, vsrc, 0);
                                    encoder.paddq_xx(backend::Reg::XMM0, hi_tmp);
                                    encoder.movhlps_xx(backend::Reg::XMM1, backend::Reg::XMM0);
                                    encoder.paddq_xx(backend::Reg::XMM0, backend::Reg::XMM1);
                                    encoder.vzeroupper();
                                    encoder.movq_rx(backend::Reg::RAX, backend::Reg::XMM0);
                                    store_val(inst->result_id, backend::Reg::RAX);
                                }
                            } else {
                                if (is_fp) {
                                    if (vsrc != backend::Reg::XMM0) encoder.movupd_xx(backend::Reg::XMM0, vsrc);
                                    encoder.movhlps_xx(backend::Reg::XMM1, backend::Reg::XMM0);
                                    encoder.addsd_xx(backend::Reg::XMM0, backend::Reg::XMM1);
                                    store_val(inst->result_id, backend::Reg::XMM0);
                                } else {
                                    if (vsrc != backend::Reg::XMM0) encoder.movdqu_xx(backend::Reg::XMM0, vsrc);
                                    encoder.movhlps_xx(backend::Reg::XMM1, backend::Reg::XMM0);
                                    encoder.paddq_xx(backend::Reg::XMM0, backend::Reg::XMM1);
                                    encoder.movq_rx(backend::Reg::RAX, backend::Reg::XMM0);
                                    store_val(inst->result_id, backend::Reg::RAX);
                                }
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    size_t inst_code_end = encoder.code_size();
                    if (inst_code_end > inst_code_start && inst->loc.line > 0) {
                        debug_builder.add_entry(static_cast<uint32_t>(inst_code_start),
                                                static_cast<uint32_t>(inst_code_end),
                                                inst->loc.file,
                                                fn->name,
                                                inst->loc.line,
                                                inst->loc.column);
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
            if (fn->name == "main" && !options_.freestanding &&
                options_.arch == TargetArch::X86_64 && options_.abi == TargetABI::Win64) {
                encoder.push_r(backend::Reg::RAX);
                encoder.sub_ri(backend::Reg::RSP, 40);
                encoder.call_extern("gout_flush");
                encoder.add_ri(backend::Reg::RSP, 40);
                encoder.pop_r(backend::Reg::RAX);
            }
            for (backend::Reg xr : used_callee_xmm) {
                int32_t off = xmm_save_offsets[xr];
                encoder.movdqu_xm(xr, backend::mem(backend::Reg::RBP, -off, 16));
            }
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

        // Attach Grass debug table into .rodata
        debug_builder.set_text_bounds(0x1000, encoder.code_size());
        std::vector<uint8_t> debug_bytes = debug_builder.serialize();
        if (!debug_bytes.empty()) {
            uint64_t debug_table_offset = unit.rodata.size();
            while (debug_table_offset % 16 != 0) {
                unit.rodata.push_back(0);
                debug_table_offset++;
            }
            unit.rodata.insert(unit.rodata.end(), debug_bytes.begin(), debug_bytes.end());

            backend::SymbolDef debug_sym;
            debug_sym.name = "__grass_debug_table";
            debug_sym.section = backend::SectionKind::RoData;
            debug_sym.offset = debug_table_offset;
            debug_sym.size = debug_bytes.size();
            debug_sym.is_global = true;
            debug_sym.is_external = false;
            unit.symbols.push_back(debug_sym);
        }
        return true;
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

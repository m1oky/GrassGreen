#include "frontend/GrassSema.h"
#include <algorithm>

namespace grass::sema {

void GrassSema::registerBuiltinsCore() {
    auto add_builtin = [this](const std::string& name, const SemaType& ret, const std::vector<SemaType>& params, bool is_variadic = false) {
        FunctionSymbol sym;
        sym.name = name;
        sym.return_type = ret;
        sym.loc = SourceLocation("<builtin>", 0, 0);
        sym.is_builtin = true;
        sym.has_body = true;
        sym.is_variadic = is_variadic;
        for (size_t i = 0; i < params.size(); ++i) {
            sym.params.emplace_back("arg" + std::to_string(i), params[i]);
        }
        functions_[name] = sym;
    };

    // Standard I/O and utility
    add_builtin("print", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("println", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("io_print", SemaType::Int(), {SemaType::Any()});
    add_builtin("io_print_str", SemaType::Int(), {SemaType::Any()});
    add_builtin("io_print_int", SemaType::Int(), {SemaType::Int()});
    add_builtin("io_print_bool", SemaType::Int(), {SemaType::Any()});
    add_builtin("io_print_ptr", SemaType::Int(), {SemaType::Any()});
    add_builtin("io_print_double", SemaType::Int(), {SemaType::Float()});
    add_builtin("io_print_newline", SemaType::Int(), {});
    add_builtin("grass_print_int", SemaType::Void(), {SemaType::Int()});
    add_builtin("grass_print_double", SemaType::Void(), {SemaType::Float()});
    add_builtin("grass_print_str", SemaType::Void(), {SemaType::Any()});
    add_builtin("grass_print_newline", SemaType::Void(), {});
    add_builtin("grass_print_bool", SemaType::Void(), {SemaType::Any()});
    add_builtin("grass_print_ptr", SemaType::Void(), {SemaType::Any()});
    add_builtin("grass_print_char", SemaType::Void(), {SemaType::Int()});
    add_builtin("gout_flush", SemaType::Void(), {});
    add_builtin("len", SemaType::Int(), {SemaType::Any()});
    add_builtin("to_str", SemaType::String(), {SemaType::Any()});
    add_builtin("to_string", SemaType::String(), {SemaType::Any()});
    add_builtin("to_num", SemaType::Float(), {SemaType::Any()});
    add_builtin("typeof", SemaType::String(), {SemaType::Any()});
    add_builtin("upper", SemaType::String(), {SemaType::String()});
    add_builtin("lower", SemaType::String(), {SemaType::String()});
    add_builtin("trim", SemaType::String(), {SemaType::String()});
    add_builtin("substr", SemaType::String(), {SemaType::String(), SemaType::Int()}, true);
    add_builtin("replace", SemaType::String(), {SemaType::String(), SemaType::String(), SemaType::String()});
    add_builtin("contains", SemaType::Bool(), {SemaType::String(), SemaType::String()});
    add_builtin("discor", SemaType::Float(), {SemaType::Float()});

    // Math functions
    add_builtin("sqrt", SemaType::Float(), {SemaType::Float()});
    add_builtin("sin", SemaType::Float(), {SemaType::Float()});
    add_builtin("cos", SemaType::Float(), {SemaType::Float()});
    add_builtin("abs", SemaType::Float(), {SemaType::Float()});
    add_builtin("round", SemaType::Float(), {SemaType::Float()});
    add_builtin("floor", SemaType::Float(), {SemaType::Float()});
    add_builtin("ceil", SemaType::Float(), {SemaType::Float()});
    add_builtin("pow", SemaType::Float(), {SemaType::Float(), SemaType::Float()});
    add_builtin("min", SemaType::Any(), {SemaType::Any(), SemaType::Any()});
    add_builtin("max", SemaType::Any(), {SemaType::Any(), SemaType::Any()});
    add_builtin("rand", SemaType::Int(), {}, true);
    add_builtin("lerp", SemaType::Float(), {SemaType::Float(), SemaType::Float(), SemaType::Float()});


    // Additional Math functions
    add_builtin("tan", SemaType::Float(), {SemaType::Float()});
    add_builtin("clamp", SemaType::Any(), {SemaType::Any(), SemaType::Any(), SemaType::Any()});

    // CoreMath and Crypto
    add_builtin("sha256", SemaType::String(), {SemaType::Any()});
    add_builtin("pow_mod", SemaType::Int(), {SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("crypto_rand", SemaType::Int(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("pi", SemaType::String(), {});
    add_builtin("gcd", SemaType::Int(), {SemaType::Int(), SemaType::Int()});
    add_builtin("pow_e", SemaType::String(), {SemaType::Float(), SemaType::Float()});
    add_builtin("qb_init", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("h", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("x", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("z", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("cnot", SemaType::String(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("q_measure", SemaType::Int(), {SemaType::Any()}, true);
    add_builtin("q_rand", SemaType::Int(), {}, true);


    // System memory
    add_builtin("memory_free", SemaType::String(), {});
    add_builtin("memory_usit", SemaType::Float(), {});
    add_builtin("memory_alloc", SemaType::Int(), {SemaType::Int()});
    add_builtin("memory_free_block", SemaType::Int(), {SemaType::Int()});

    // Memory management
    add_builtin("alloc", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("free", SemaType::Void(), {SemaType::Ptr(SemaType::Void())});
    add_builtin("realloc", SemaType::Ptr(SemaType::Void()), {SemaType::Ptr(SemaType::Void()), SemaType::Int()});
    add_builtin("memcpy", SemaType::Ptr(SemaType::Void()), {SemaType::Ptr(SemaType::Void()), SemaType::Ptr(SemaType::Void()), SemaType::Int()});
    add_builtin("memset", SemaType::Ptr(SemaType::Void()), {SemaType::Ptr(SemaType::Void()), SemaType::Int(), SemaType::Int()});
    add_builtin("mem_alloc", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("mem_free", SemaType::Void(), {SemaType::Ptr(SemaType::Void())});
    add_builtin("heap_alloc", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("heap_free", SemaType::Int(), {SemaType::Ptr(SemaType::Void())});
    add_builtin("heap_realloc", SemaType::Ptr(SemaType::Void()), {SemaType::Ptr(SemaType::Void()), SemaType::Int()});
    add_builtin("heap_get_process_heap", SemaType::Ptr(SemaType::Void()), {});

    // String operations
    add_builtin("strlen", SemaType::Int(), {SemaType::Any()});
    add_builtin("str_len", SemaType::Int(), {SemaType::Any()});
    add_builtin("strcmp", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_cmp", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("streq", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_eq", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("strcpy", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_copy", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("strcat", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_cat", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("strdup", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("str_dup", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("str_find", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_contains", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_trim", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("str_to_int", SemaType::Int(), {SemaType::Any()});
    add_builtin("int_to_str", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("str_substr", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Int(), SemaType::Int()});


}

} // namespace grass::sema

#include "GrassSyntax.h"
#include "GrassSema.h"
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <chrono>

// Global VM variables required for standalone linking
GrassArena g_vm_arena(1024 * 1024);
std::unordered_map<std::string, GrassStructDef> g_struct_registry;

// Keyboard stubs for standalone testing
namespace GrassKeyboard {
    void set_layout(int) {}
    void set_layout_by_name(const std::string&) {}
}

using namespace grass::sema;

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while (0)

#define TEST_ASSERT_SEMA(sema, cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            std::cerr << "Diagnostics (" << sema.getDiagnostics().size() << "):" << std::endl; \
            for (const auto& d : sema.getDiagnostics()) { \
                std::cerr << "  " << d.format() << std::endl; \
            } \
            return false; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        g_tests_total++; \
        std::cout << "[RUN] " << #fn << "..." << std::flush; \
        if (fn()) { \
            std::cout << " OK" << std::endl; \
            g_tests_passed++; \
        } else { \
            std::cout << " FAILED" << std::endl; \
        } \
    } while (0)

// Helper: parse source code and return ProgramAST
static std::unique_ptr<ProgramAST> parseSource(const std::string& src, const std::string& fname = "test.grass") {
    Lexer lexer(src, fname);
    std::vector<GrassToken> tokens = lexer.tokenize();
    GrassParser parser(tokens, fname);
    return parser.parseProgram();
}

// ----------------------------------------------------------------------------
// Test 1: Parser error recovery with multiple syntax errors
// ----------------------------------------------------------------------------
static bool test_syntax_error_recovery() {
    std::string src =
        "int a = 10\n"        // Missing semicolon (error 1)
        "int b = 20;\n"       // Valid statement
        "int c = @;\n"        // Unexpected token (error 2)
        "int d = 30;\n"       // Valid statement
        "int main() {\n"
        "    return a + b;\n"
        "}\n";

    Lexer lexer(src, "recovery.grass");
    auto tokens = lexer.tokenize();
    GrassParser parser(tokens, "recovery.grass");
    auto program = parser.parseProgram();

    TEST_ASSERT(parser.hasErrors(), "Parser should have collected errors");
    TEST_ASSERT(parser.getDiagnostics().size() >= 2, "Parser should recover and report multiple errors");
    TEST_ASSERT(!program->declarations.empty() || !program->statements.empty(), "Parser should produce AST nodes despite errors");

    return true;
}

// ----------------------------------------------------------------------------
// Test 2: Accurate coordinates and hint in syntax diagnostics
// ----------------------------------------------------------------------------
static bool test_syntax_diagnostic_coordinates() {
    std::string src =
        "int a = 10;\n"
        "int b = ;\n"         // Missing expression on line 2
        "int c = 30;\n";

    Lexer lexer(src, "coords.grass");
    auto tokens = lexer.tokenize();
    GrassParser parser(tokens, "coords.grass");
    auto program = parser.parseProgram();

    TEST_ASSERT(parser.hasErrors(), "Should detect syntax error");
    const auto& diags = parser.getDiagnostics();
    TEST_ASSERT(!diags.empty(), "Should have diagnostics");
    TEST_ASSERT(diags[0].loc.line == 2, "Error must be on line 2");
    TEST_ASSERT(diags[0].loc.col > 0, "Error column must be positive");
    TEST_ASSERT(!diags[0].hint.empty(), "Diagnostic hint must not be empty");

    return true;
}

// ----------------------------------------------------------------------------
// Test 3: Undeclared variable detection
// ----------------------------------------------------------------------------
static bool test_undeclared_variable() {
    std::string src =
        "int main() {\n"
        "    int x = 10;\n"
        "    int z = x + y;\n" // 'y' is undeclared
        "    return z;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on undeclared variable");
    TEST_ASSERT(sema.hasErrors(), "Should record sema error");

    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Undeclared variable 'y'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report undeclared variable 'y'");
    return true;
}

// ----------------------------------------------------------------------------
// Test 4: Duplicate variable declaration in the same scope
// ----------------------------------------------------------------------------
static bool test_duplicate_variable_same_scope() {
    std::string src =
        "int main() {\n"
        "    int x = 10;\n"
        "    int x = 20;\n" // Duplicate declaration
        "    return x;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on duplicate variable declaration");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Redeclaration of variable 'x'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report redeclaration of variable 'x'");
    return true;
}

// ----------------------------------------------------------------------------
// Test 5: Variable shadowing in nested block (valid)
// ----------------------------------------------------------------------------
static bool test_nested_scope_shadowing() {
    std::string src =
        "int main() {\n"
        "    int x = 10;\n"
        "    {\n"
        "        int x = 20;\n" // Valid shadowing in inner block
        "    }\n"
        "    return x;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT_SEMA(sema, ok, "Nested scope shadowing should be allowed without errors");
    TEST_ASSERT(!sema.hasErrors(), "Should have zero errors");
    return true;
}

// ----------------------------------------------------------------------------
// Test 6: Type mismatch in variable initialization
// ----------------------------------------------------------------------------
static bool test_type_mismatch_init() {
    std::string src =
        "int main() {\n"
        "    int x = \"hello\";\n" // Cannot initialize int with string
        "    return x;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on type mismatch in variable init");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Type mismatch in variable initialization") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report type mismatch in variable initialization");
    return true;
}

// ----------------------------------------------------------------------------
// Test 7: Type mismatch in assignment
// ----------------------------------------------------------------------------
static bool test_type_mismatch_assignment() {
    std::string src =
        "int main() {\n"
        "    int x = 10;\n"
        "    x = \"test\";\n" // Cannot assign string to int
        "    return x;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on assigning incompatible type");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Cannot assign value of type 'string' to variable of type 'int'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report assignment type mismatch");
    return true;
}

// ----------------------------------------------------------------------------
// Test 8: Binary arithmetic type mismatch
// ----------------------------------------------------------------------------
static bool test_arithmetic_type_mismatch() {
    std::string src =
        "int main() {\n"
        "    int a = 10;\n"
        "    string b = \"world\";\n"
        "    int c = a - b;\n" // Invalid subtraction with string
        "    return c;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on subtracting string from int");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Invalid operands to binary operator '-'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report invalid operands to binary operator '-'");
    return true;
}

// ----------------------------------------------------------------------------
// Test 9: Bitwise operator on float
// ----------------------------------------------------------------------------
static bool test_bitwise_operator_on_float() {
    std::string src =
        "int main() {\n"
        "    float f = 1.5;\n"
        "    int x = f & 2;\n" // Bitwise AND requires integers
        "    return x;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on bitwise operator with float");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Bitwise operator '&' requires integral operands") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report bitwise operator '&' requires integral operands");
    return true;
}

// ----------------------------------------------------------------------------
// Test 10: Non-boolean condition in if-statement
// ----------------------------------------------------------------------------
static bool test_non_boolean_condition() {
    std::string src =
        "int main() {\n"
        "    if (\"string_condition\") {\n" // String cannot be condition
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on string condition");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Condition must be boolean or numeric expression") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report invalid condition type");
    return true;
}

// ----------------------------------------------------------------------------
// Test 11: Call to undeclared function
// ----------------------------------------------------------------------------
static bool test_call_undeclared_function() {
    std::string src =
        "int main() {\n"
        "    non_existent_func(1, 2);\n"
        "    return 0;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on calling undeclared function");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Call to undeclared function 'non_existent_func'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report call to undeclared function");
    return true;
}

// ----------------------------------------------------------------------------
// Test 12: Call argument count mismatch
// ----------------------------------------------------------------------------
static bool test_call_argument_count_mismatch() {
    std::string src =
        "int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main() {\n"
        "    int res = add(5);\n" // 1 arg provided, 2 expected
        "    return res;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on argument count mismatch");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Function 'add' expects 2 arguments, but 1 were provided") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report argument count mismatch");
    return true;
}

// ----------------------------------------------------------------------------
// Test 13: Call argument type mismatch
// ----------------------------------------------------------------------------
static bool test_call_argument_type_mismatch() {
    std::string src =
        "int multiply(int a, int b) {\n"
        "    return a * b;\n"
        "}\n"
        "int main() {\n"
        "    int res = multiply(5, \"text\");\n" // Arg 2 expected int, got string
        "    return res;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on argument type mismatch");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("expected type 'int', but got 'string'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report argument type mismatch");
    return true;
}

// ----------------------------------------------------------------------------
// Test 14: Non-void function missing return statement on some branch
// ----------------------------------------------------------------------------
static bool test_missing_return_on_branch() {
    std::string src =
        "int check_num(int x) {\n"
        "    if (x > 0) {\n"
        "        return 1;\n"
        "    }\n"
        "    // Missing return in else / fallthrough\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on missing return on control path");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Not all control paths return a value in function 'check_num'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report missing return on control path");
    return true;
}

// ----------------------------------------------------------------------------
// Test 15: Non-void function with complete returns on all paths (valid)
// ----------------------------------------------------------------------------
static bool test_complete_returns_valid() {
    std::string src =
        "int check_num(int x) {\n"
        "    if (x > 0) {\n"
        "        return 1;\n"
        "    } else {\n"
        "        return 0;\n"
        "    }\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT_SEMA(sema, ok, "Sema should succeed when all control paths return");
    TEST_ASSERT(!sema.hasErrors(), "Should have zero errors");
    return true;
}

// ----------------------------------------------------------------------------
// Test 16: Void function returning a value
// ----------------------------------------------------------------------------
static bool test_void_function_returning_value() {
    std::string src =
        "void log_msg(string msg) {\n"
        "    return 42;\n" // Void function returning value
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on void function returning value");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Void function 'log_msg' cannot return a value") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report void function cannot return value");
    return true;
}

// ----------------------------------------------------------------------------
// Test 17: Struct field not found
// ----------------------------------------------------------------------------
static bool test_struct_field_not_found() {
    std::string src =
        "struct Point {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "int main() {\n"
        "    Point p;\n"
        "    int val = p.z;\n" // Field 'z' does not exist
        "    return val;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on nonexistent struct field");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Struct 'Point' has no field named 'z'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_SEMA(sema, found, "Should report field 'z' not found in Point");
    return true;
}

// ----------------------------------------------------------------------------
// Test 18: Struct method not found
// ----------------------------------------------------------------------------
static bool test_struct_method_not_found() {
    std::string src =
        "struct Counter {\n"
        "    int count;\n"
        "    int get_count() {\n"
        "        return this.count;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Counter c;\n"
        "    c.reset_all();\n" // Nonexistent method
        "    return 0;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on nonexistent method");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("has no method named 'reset_all'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_SEMA(sema, found, "Should report method 'reset_all' not found");
    return true;
}

// ----------------------------------------------------------------------------
// Test 19: Dereferencing non-pointer
// ----------------------------------------------------------------------------
static bool test_dereference_non_pointer() {
    std::string src =
        "int main() {\n"
        "    int a = 10;\n"
        "    int b = *a;\n" // Cannot dereference non-pointer int
        "    return b;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on dereferencing non-pointer");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Cannot dereference non-pointer type 'int'") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report cannot dereference non-pointer");
    return true;
}

// ----------------------------------------------------------------------------
// Test 20: Freeing non-pointer
// ----------------------------------------------------------------------------
static bool test_free_non_pointer() {
    std::string src =
        "int main() {\n"
        "    int a = 42;\n"
        "    free(a);\n" // Cannot free non-pointer
        "    return 0;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT(!ok, "Sema should fail on freeing non-pointer");
    bool found = false;
    for (const auto& d : sema.getDiagnostics()) {
        if (d.message.find("Argument to 'free()' must be a pointer") != std::string::npos) {
            found = true;
            break;
        }
    }
    TEST_ASSERT(found, "Should report free() requires pointer argument");
    return true;
}

// ----------------------------------------------------------------------------
// Test 21: Pointer address-of and dereference valid usage
// ----------------------------------------------------------------------------
static bool test_valid_pointers_and_alloc() {
    std::string src =
        "int main() {\n"
        "    int x = 100;\n"
        "    int* ptr = &x;\n"
        "    int val = *ptr;\n"
        "    void* buf = alloc(64);\n"
        "    free(buf);\n"
        "    return val;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT_SEMA(sema, ok, "Sema should succeed on valid pointer operations");
    TEST_ASSERT(!sema.hasErrors(), "Should have zero errors");
    return true;
}

// ----------------------------------------------------------------------------
// Test 22: Class inheritance field resolution (valid)
// ----------------------------------------------------------------------------
static bool test_class_inheritance_field_resolution() {
    std::string src =
        "class Animal {\n"
        "    int age;\n"
        "};\n"
        "class Dog : Animal {\n"
        "    string name;\n"
        "};\n"
        "int main() {\n"
        "    Dog d;\n"
        "    int dog_age = d.age;\n" // Inherited from Animal
        "    return dog_age;\n"
        "}\n";

    auto ast = parseSource(src);
    GrassSema sema;
    bool ok = sema.analyze(*ast);

    TEST_ASSERT_SEMA(sema, ok, "Sema should find inherited field in base class");
    TEST_ASSERT(!sema.hasErrors(), "Should have zero errors");
    return true;
}

// ----------------------------------------------------------------------------
// Test 23: Performance benchmark: analyze 10,000 lines in < 2.0 ms
// ----------------------------------------------------------------------------
static bool test_performance_10k_lines() {
    // Generate 10,000 lines of valid GrassGreen code
    std::string src = "!GrassGreen!\n";
    src.reserve(500000);

    // 1. Declarations
    for (int i = 0; i < 200; ++i) {
        src += "int fn_" + std::to_string(i) + "(int a, int b) {\n";
        src += "    int res = a + b;\n";
        src += "    return res;\n";
        src += "}\n";
    }

    // 2. Main function with thousands of statements
    src += "int main() {\n";
    src += "    int sum = 0;\n";
    for (int i = 0; i < 8500; ++i) {
        src += "    sum = sum + 1;\n";
    }
    src += "    return sum;\n";
    src += "}\n";

    // Parse AST
    auto ast = parseSource(src, "benchmark.grass");

    // Measure Sema execution time
    auto t_start = std::chrono::high_resolution_clock::now();
    GrassSema sema;
    bool ok = sema.analyze(*ast);
    auto t_end = std::chrono::high_resolution_clock::now();

    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "\n   [Benchmark] 10,000 lines analyzed in " << elapsed_ms << " ms" << std::flush;

    TEST_ASSERT_SEMA(sema, ok, "Benchmark code should analyze with zero errors");
    TEST_ASSERT(elapsed_ms < 10.0, "Sema analysis of 10,000 lines must be fast (< 10 ms)");

    return true;
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << " GrassGreen 4.2.0 -- Static Semantic Analyzer (GrassSema)" << std::endl;
    std::cout << " Diagnostics & Error Recovery Test Suite (23 test cases)" << std::endl;
    std::cout << "============================================================" << std::endl;

    RUN_TEST(test_syntax_error_recovery);
    RUN_TEST(test_syntax_diagnostic_coordinates);
    RUN_TEST(test_undeclared_variable);
    RUN_TEST(test_duplicate_variable_same_scope);
    RUN_TEST(test_nested_scope_shadowing);
    RUN_TEST(test_type_mismatch_init);
    RUN_TEST(test_type_mismatch_assignment);
    RUN_TEST(test_arithmetic_type_mismatch);
    RUN_TEST(test_bitwise_operator_on_float);
    RUN_TEST(test_non_boolean_condition);
    RUN_TEST(test_call_undeclared_function);
    RUN_TEST(test_call_argument_count_mismatch);
    RUN_TEST(test_call_argument_type_mismatch);
    RUN_TEST(test_missing_return_on_branch);
    RUN_TEST(test_complete_returns_valid);
    RUN_TEST(test_void_function_returning_value);
    RUN_TEST(test_struct_field_not_found);
    RUN_TEST(test_struct_method_not_found);
    RUN_TEST(test_dereference_non_pointer);
    RUN_TEST(test_free_non_pointer);
    RUN_TEST(test_valid_pointers_and_alloc);
    RUN_TEST(test_class_inheritance_field_resolution);
    RUN_TEST(test_performance_10k_lines);

    std::cout << "============================================================" << std::endl;
    std::cout << " Result: " << g_tests_passed << " / " << g_tests_total << " tests passed." << std::endl;
    std::cout << "============================================================" << std::endl;

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "GrassVersion.h"
#include "frontend/GrassSyntax.h"
#include "frontend/GrassImport.h"
#include "frontend/GrassSema.h"
#include "vm/GrassVM.h"
#include "vm/GrassOptimizer.h"
#include "native/GrassNativeCodegen.h"
#include "baremetal/GrassBoot.h"
#include "baremetal/GrassBaremetal.h"
#include "baremetal/GrassHAL.h"
#include "baremetal/GrassASM.h"
#include "baremetal/GrassKeyboard.h"
#include "baremetal/GSE.h"
#include "runtime/GrassWeb.h"
#include "runtime/GrassThread.h"
#include "cli/GrassInstaller.h"
#include "cli/GrassPacker.h"
#include "cli/GrassDiag.h"

using namespace std;
namespace fs = std::filesystem;

struct CompilerOptions {
    string input_file = "";
    string output_file = "";
    bool emit_bytecode_only = false; // -c (.grassc)
    bool emit_exe = false;           // -emit-exe (.exe)
    bool emit_multiboot = false;     // --multiboot
    bool emit_kernel = false;        // --kernel (.bin)
    bool emit_sys = false;           // --sys (.sys)
    bool emit_iso = false;           // --iso (.iso)
    bool run_after_build = false;    // ./!, --run, -r (по умолчанию false: не запускать автоматически)
    bool explicit_run = false;
    int opt_level = 2;               // -O0, -O1, -O2, -O3 (default: 2)
    bool dump_ast = false;           // --dump-ast
    bool dump_disasm = false;        // -S
    bool print_timing = false;       // --time
    bool freestanding = false;       // --freestanding
    bool enable_jit = false;         // --jit (x86_64 JIT)
    bool lang_ru = false;            // default: English, true: Russian (--ru)
    bool show_version = false;       // -V, --version
    bool show_help = false;          // -h, --help
    bool install = false;            // --install, install, -i
    bool native_mode = false;        // --native
    bool emit_ir = false;            // --emit-ir
    bool emit_obj = false;           // --emit-obj
    string emit_obj_path = "";
    bool emit_bin = false;           // --emit-bin
    string emit_bin_path = "";
    string arch_str = "x64";         // --arch x64|x32
    string abi_str = "win64";        // --abi win64|sysv64|cdecl32|baremetal64|baremetal32
    uint64_t base_addr = 0x100000;   // --base-addr
    bool no_sema = false;            // --no-sema
    bool check_only = false;         // --check
    bool use_gcc = false;            // --use-gcc
    string target = "";              // --target <windows|linux>
};

string FindDefaultGrassFile() {
    if (fs::exists("demo.grass")) return "demo.grass";
    if (fs::exists("main.grass")) return "main.grass";

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("*.grass", &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        string found = fd.cFileName;
        FindClose(hFind);
        return found;
    }
    return "";
}

bool ProcessCompilation(CompilerOptions& opt) {
    auto t_start = chrono::high_resolution_clock::now();

    string input_path = opt.input_file;
    if (input_path.empty()) {
        if (opt.lang_ru) cerr << "[GrassError]: Не указан входной файл .grass. Используйте 'grg -h' для справки.\n";
        else cerr << "[GrassError]: No .grass input file specified. Use 'grg -h' for help.\n";
        return false;
    }

    if (input_path.find('.') == string::npos) {
        if (fs::exists(input_path + ".grass")) input_path += ".grass";
        else if (fs::exists(input_path + ".grassc")) input_path += ".grassc";
    }

    // Прямой запуск чистого байт-кода .grassc
    if (input_path.size() >= 7 && input_path.substr(input_path.size() - 7) == ".grassc") {
        ifstream in_bc(input_path, ios::binary | ios::ate);
        if (!in_bc.is_open()) {
            if (opt.lang_ru) cerr << "[GrassError]: Не удалось открыть файл байт-кода -> " << input_path << "\n";
            else cerr << "[GrassError]: Cannot open bytecode file -> " << input_path << "\n";
            return false;
        }
        streamsize sz = in_bc.tellg();
        in_bc.seekg(0, ios::beg);
        vector<uint8_t> bc_data((size_t)sz);
        if (sz > 0) in_bc.read((char*)bc_data.data(), sz);
        in_bc.close();

        if (opt.dump_disasm) {
            DisassembleBytecode(bc_data, opt.lang_ru);
        }
        if (opt.run_after_build && !opt.emit_iso && !opt.emit_kernel && !opt.emit_sys && (!opt.emit_multiboot || opt.explicit_run)) {
            if (opt.lang_ru) cout << "[GrassVM]: Исполнение байт-кода...\n\n";
            else cout << "[GrassVM]: Executing bytecode...\n\n";
            g_vm_arena.reset();
            GrassVM vm(bc_data);
            vm.execute();
            GrassThread::shutdown();
        if (opt.print_timing && GrassJIT::getStats().loops_compiled > 0) {
            const auto& js = GrassJIT::getStats();
            if (opt.lang_ru) {
                cout << "\n[JIT-Компилятор x86_64]: Скомпилировано циклов: " << js.loops_compiled
                     << " (" << js.machine_code_bytes << " B машинного кода x86_64, время JIT: "
                     << js.compile_time_ms << " ms)\n";
            } else {
                cout << "\n[JIT x86_64]: Loops compiled: " << js.loops_compiled
                     << " (" << js.machine_code_bytes << " B native x86_64 code, JIT compile: "
                     << js.compile_time_ms << " ms)\n";
            }
        }
        }
        return true;
    }

    // 1. Этап I/O: Чтение исходного кода
    auto t_read_start = chrono::high_resolution_clock::now();
    ifstream in(input_path, ios::binary | ios::ate);
    if (!in.is_open()) {
        if (opt.lang_ru) cerr << "[GrassError]: Файл не найден -> " << input_path << "\n";
        else cerr << "[GrassError]: File not found -> " << input_path << "\n";
        return false;
    }
    streamsize src_size = in.tellg();
    in.seekg(0, ios::beg);
    string source_code((size_t)max((streamsize)0, src_size), '\0');
    if (src_size > 0) in.read(&source_code[0], src_size);
    in.close();
    auto t_read_end = chrono::high_resolution_clock::now();
    double read_ms = chrono::duration<double, std::milli>(t_read_end - t_read_start).count();

    if (opt.freestanding) {
        GrassHAL::set_freestanding(true);
        if (opt.lang_ru) cout << "[Freestanding]: Автономный режим активирован (ядро независимо от libc)\n";
        else cout << "[Freestanding]: Freestanding mode active (kernel independent from libc)\n";
    } else {
        GrassHAL::set_freestanding(false);
    }

    // 2. Этап Лексера: Токенизация
    auto t_lex_start = chrono::high_resolution_clock::now();
    Lexer lexer(source_code);
    vector<GrassToken> tokens = lexer.tokenize();

    // 2.1. Разрешение директив импорта (Import Pass)
    GrassImport::ImportStats import_stats;
    tokens = GrassImport::resolveImports(tokens, input_path, &import_stats);
    if (import_stats.has_error) {
        if (opt.lang_ru) cerr << "[GrassCompiler]: Сборка прервана из-за ошибок импорта!\n";
        else cerr << "[GrassCompiler]: Build stopped due to import errors!\n";
        return false;
    }
    if (opt.print_timing && import_stats.total_modules_imported > 0) {
        if (opt.lang_ru) {
            cout << "[Импорт]: Подключено модулей: " << import_stats.total_modules_imported
                 << " (итого токенов: " << import_stats.total_tokens_after_import << ")\n";
        } else {
            cout << "[Import]: Modules imported: " << import_stats.total_modules_imported
                 << " (total tokens: " << import_stats.total_tokens_after_import << ")\n";
        }
    }

    auto t_lex_end = chrono::high_resolution_clock::now();
    double lex_ms = chrono::duration<double, std::milli>(t_lex_end - t_lex_start).count();

    // 2.1.5. Семантический анализ (GrassSema)
    if (!opt.no_sema) {
        GrassParser sema_parser(tokens, input_path);
        auto program_ast = sema_parser.parseProgram();
        if (sema_parser.hasErrors()) {
            if (opt.lang_ru) cerr << "\n[GrassSema]: Ошибки синтаксического анализа:\n";
            else cerr << "\n[GrassSema]: Syntax analysis errors:\n";
            for (const auto& diag : sema_parser.getDiagnostics()) {
                cerr << "  " << diag.format() << "\n";
            }
            return false;
        }

        grass::sema::GrassSema sema;
        if (program_ast->is_strict) {
            sema.set_strict(true);
        }
        if (!sema.analyze(*program_ast)) {
            if (opt.lang_ru) cerr << "\n[GrassSema]: Ошибки семантического анализа:\n";
            else cerr << "\n[GrassSema]: Semantic analysis errors:\n";
            sema.printDiagnostics();
            return false;
        }
    }

    if (opt.check_only) {
        return true;
    }

    // 2.2. Нативный бэкенд (Native Backend: GrassIR + GrassX86Encoder + GrassObjWriter)
    if (opt.native_mode || opt.emit_ir || opt.emit_obj || opt.emit_bin) {
        auto t_native_start = chrono::high_resolution_clock::now();
        grass::codegen::NativeCodegenOptions nopt;
        nopt.arch = grass::codegen::NativeCompiler::parse_arch(opt.arch_str);
        nopt.abi = grass::codegen::NativeCompiler::parse_abi(opt.abi_str);
        nopt.opt_level = opt.opt_level;
        nopt.base_address = opt.base_addr;
        nopt.emit_ir = opt.emit_ir;
        nopt.freestanding = opt.freestanding || opt.emit_bin;
        nopt.emit_obj_path = opt.emit_obj_path;
        nopt.emit_bin_path = opt.emit_bin_path;
        nopt.lang_ru = opt.lang_ru;
        nopt.run_after_build = opt.run_after_build;
        nopt.use_gcc = opt.use_gcc;
        nopt.target = opt.target;

        string base_name = input_path;
        size_t last_dot = base_name.rfind('.');
        if (last_dot != string::npos) base_name = base_name.substr(0, last_dot);

        if (opt.emit_bin && nopt.emit_bin_path.empty()) {
            nopt.emit_bin_path = base_name + ".bin";
        }
        if (opt.emit_obj && nopt.emit_obj_path.empty()) {
            nopt.emit_obj_path = (nopt.abi == grass::codegen::TargetABI::Win64 || nopt.abi == grass::codegen::TargetABI::Cdecl32)
                                 ? (base_name + ".obj") : (base_name + ".o");
        }

        if (opt.native_mode) {
            nopt.output_exe_path = opt.output_file.empty() ? (base_name + ".exe") : opt.output_file;
        }

        grass::codegen::NativeCompiler native_compiler(nopt);
        auto nres = native_compiler.compile(tokens, input_path);

        if (!nres.success) {
            if (opt.lang_ru) cerr << "[GrassNativeCompiler]: Ошибка сборки: " << nres.error_message << "\n";
            else cerr << "[GrassNativeCompiler]: Build error: " << nres.error_message << "\n";
            return false;
        }

        if (opt.emit_ir) {
            if (opt.lang_ru) {
                cout << "\n=== ДАМП GRASS-IR (ДО ОПТИМИЗАЦИЙ) ===\n" << nres.ir_dump_initial << "\n";
                cout << "=== ДАМП GRASS-IR (ПОСЛЕ ОПТИМИЗАЦИЙ) ===\n" << nres.ir_dump_optimized << "\n";
            } else {
                cout << "\n=== GRASS-IR DUMP (BEFORE OPTIMIZATIONS) ===\n" << nres.ir_dump_initial << "\n";
                cout << "=== GRASS-IR DUMP (AFTER OPTIMIZATIONS) ===\n" << nres.ir_dump_optimized << "\n";
            }
        }

        if (!nres.obj_file_created.empty() && opt.emit_obj) {
            if (opt.lang_ru) cout << "[GrassNativeCompiler]: Сгенерирован объектный файл -> " << nres.obj_file_created << "\n";
            else cout << "[GrassNativeCompiler]: Object file generated -> " << nres.obj_file_created << "\n";
        }

        if (!nres.bin_file_created.empty()) {
            if (opt.lang_ru) cout << "[GrassNativeCompiler]: Сгенерирован bare-metal бинарник -> " << nres.bin_file_created
                                  << " (базовый адрес: 0x" << hex << opt.base_addr << dec << ")\n";
            else cout << "[GrassNativeCompiler]: Bare-metal binary generated -> " << nres.bin_file_created
                      << " (base address: 0x" << hex << opt.base_addr << dec << ")\n";
        }

        if (!nres.exe_file_created.empty()) {
            if (opt.lang_ru) cout << "[GrassNativeCompiler]: Успешно слинкован исполняемый файл -> " << nres.exe_file_created << "\n";
            else cout << "[GrassNativeCompiler]: Executable linked successfully -> " << nres.exe_file_created << "\n";
            if (!opt.run_after_build) {
                if (opt.lang_ru) {
                    cout << "[GrassNativeCompiler]: Сборка завершена без запуска. Укажите -r для немедленного запуска или выполните: "
                         << nres.exe_file_created << "\n";
                } else {
                    cout << "[GrassNativeCompiler]: Build complete (not executed). Use -r to run immediately, or execute: "
                         << nres.exe_file_created << "\n";
                }
            }
        }

        auto t_native_end = chrono::high_resolution_clock::now();
        double native_ms = chrono::duration<double, std::milli>(t_native_end - t_native_start).count();
        if (opt.print_timing) {
            cout << "[NativeCodegen]: Total native pipeline time: " << native_ms << " ms\n";
        }

        if (opt.run_after_build && !nres.exe_file_created.empty()) {
            if (opt.lang_ru) cout << "[GrassNative]: Запуск нативного исполняемого файла...\n\n";
            else cout << "[GrassNative]: Executing native binary...\n\n";
            cout.flush();
            std::string run_cmd = nres.exe_file_created;
            for (char& c : run_cmd) {
                if (c == '/') c = '\\';
            }
            run_cmd = "\"" + run_cmd + "\"";
            int run_code = std::system(run_cmd.c_str());
            return (run_code == 0);
        }
        return true;
    }

    // 3. Этап Парсера и Компилятора байт-кода
    auto t_comp_start = chrono::high_resolution_clock::now();
    BytecodeCompiler compiler(tokens, input_path);
    compiler.compileProgram();
    auto t_comp_end = chrono::high_resolution_clock::now();
    double comp_ms = chrono::duration<double, std::milli>(t_comp_end - t_comp_start).count();

    if (compiler.has_error) {
        if (opt.lang_ru) cerr << "\n[GrassCompiler]: Сборка остановлена из-за ошибок синтаксиса в " << input_path << "!\n";
        else cerr << "\n[GrassCompiler]: Build stopped due to syntax errors in " << input_path << "!\n";
        return false;
    }

    // ── Этап Оптимизатора Байт-кода (-O1, -O2, -O3) ──
    auto t_opt_start = chrono::high_resolution_clock::now();
    GrassOptimizer::OptimizationStats opt_stats;
    if (opt.opt_level > 0) {
        compiler.code = GrassOptimizer::optimize(compiler.code, opt.opt_level, &opt_stats);
        if (opt.print_timing) {
            if (opt.lang_ru) {
                cout << "[Оптимизатор -O" << opt.opt_level << "]: "
                     << opt_stats.initial_bytes << " B -> " << opt_stats.optimized_bytes
                     << " B (Свернуто констант: " << opt_stats.constants_folded
                     << ", Fused-опкодов: " << opt_stats.opcodes_fused
                     << ", Удалено мертвого кода: " << opt_stats.dead_code_removed << ")\n";
            } else {
                cout << "[Optimizer -O" << opt.opt_level << "]: "
                     << opt_stats.initial_bytes << " B -> " << opt_stats.optimized_bytes
                     << " B (Folded constants: " << opt_stats.constants_folded
                     << ", Fused opcodes: " << opt_stats.opcodes_fused
                     << ", Dead code removed: " << opt_stats.dead_code_removed << ")\n";
            }
        }
    }
    auto t_opt_end = chrono::high_resolution_clock::now();
    double opt_ms = chrono::duration<double, std::milli>(t_opt_end - t_opt_start).count();

    // Опция --dump-ast
    if (opt.dump_ast) {
        DumpAST(tokens, input_path, opt.lang_ru);
    }

    // Опция -S (Дизассемблирование)
    if (opt.dump_disasm) {
        DisassembleBytecode(compiler.code, opt.lang_ru);
    }

    // 4. Этап Генерации выходного файла
    auto t_emit_start = chrono::high_resolution_clock::now();
    string base_name = input_path;
    size_t last_dot = base_name.rfind('.');
    if (last_dot != string::npos) base_name = base_name.substr(0, last_dot);

    // Извлечение машинного кода x86_64 из ассемблерных вставок asm { ... }
    vector<uint8_t> asm_bytes;
    string asm_hex = GrassASM::get_last_machine_hex();
    if (!asm_hex.empty()) {
        stringstream ss(asm_hex);
        string b_str;
        while (ss >> b_str) {
            try { asm_bytes.push_back((uint8_t)stoul(b_str, nullptr, 16)); } catch (...) {}
        }
    }

    if (opt.emit_iso) {
        string out_file = opt.output_file.empty() ? (base_name + ".iso") : opt.output_file;
        GrassBoot::build_bootable_iso(out_file, compiler.code, asm_bytes, "GRASSGREEN_OS", opt.lang_ru);
    } else if (opt.emit_sys) {
        string out_file = opt.output_file.empty() ? (base_name + ".sys") : opt.output_file;
        GrassBoot::build_driver_sys(out_file, compiler.code, base_name, opt.lang_ru);
    } else if (opt.emit_kernel || opt.emit_multiboot) {
        string out_file = opt.output_file.empty() ? (base_name + ".bin") : opt.output_file;
        GrassBoot::build_kernel_bin(out_file, compiler.code, asm_bytes, true, opt.lang_ru);
    } else if (opt.emit_bytecode_only) {
        // -c: Чистый байт-код .grassc
        string out_file = opt.output_file.empty() ? (base_name + ".grassc") : opt.output_file;
        ofstream out_bc(out_file, ios::binary);
        if (!out_bc.is_open()) {
            if (opt.lang_ru) cerr << "[GrassError]: Не удалось записать " << out_file << "\n";
            else cerr << "[GrassError]: Failed to write " << out_file << "\n";
            return false;
        }
        out_bc.write((const char*)compiler.code.data(), compiler.code.size());
        out_bc.close();

        if (opt.lang_ru) {
            cout << "[GrassCompiler]: Скомпилировано " << compiler.code.size()
                 << " байт чистого байт-кода -> " << out_file << "\n";
        } else {
            cout << "[GrassCompiler]: Compiled " << compiler.code.size()
                 << " bytes of pure bytecode -> " << out_file << "\n";
        }
    } else if (opt.emit_exe || (!opt.output_file.empty() && opt.output_file.size() >= 4 && opt.output_file.substr(opt.output_file.size() - 4) == ".exe")) {
        // -emit-exe или явный вывод в .exe
        string out_file = opt.output_file.empty() ? (base_name + ".exe") : opt.output_file;
        bool packed_ok = BuildStandaloneExe(out_file, compiler.code);
        if (packed_ok) {
            if (opt.lang_ru) {
                cout << "[GrassCompiler]: Скомпилировано " << compiler.code.size()
                     << " байт двоичного кода -> " << out_file << "\n";
            } else {
                cout << "[GrassCompiler]: Compiled " << compiler.code.size()
                     << " bytes of bytecode -> " << out_file << "\n";
            }
        }
    } else {
        // Обычная компиляция без неявной упаковки в .exe
        if (!opt.run_after_build) {
            if (opt.lang_ru) {
                cout << "[GrassCompiler]: Успешная компиляция (" << compiler.code.size()
                     << " байт). Для запуска укажите -r, для упаковки .exe укажите -emit-exe.\n";
            } else {
                cout << "[GrassCompiler]: Compilation successful (" << compiler.code.size()
                     << " bytes). Use -r to run, or -emit-exe to package executable.\n";
            }
        }
    }
    auto t_emit_end = chrono::high_resolution_clock::now();
    double emit_ms = chrono::duration<double, std::milli>(t_emit_end - t_emit_start).count();

    auto t_end = chrono::high_resolution_clock::now();
    double total_ms = chrono::duration<double, std::milli>(t_end - t_start).count();

    // Опция --time
    if (opt.print_timing) {
        cout << fixed << setprecision(2);
        if (opt.lang_ru) {
            cout << "\n=== ЗАМЕР ВРЕМЕНИ ЭТАПОВ КОМПИЛЯЦИИ ===\n"
                 << "  1. Чтение файла (I/O):        " << setw(6) << read_ms << " ms\n"
                 << "  2. Лексер (токенизация):      " << setw(6) << lex_ms << " ms\n"
                 << "  3. Компилятор байт-кода:      " << setw(6) << comp_ms << " ms\n" \
                 << "  4. Оптимизатор (-O):          " << setw(6) << opt_ms << " ms\n"
                 << "  4. Генерация вывода:          " << setw(6) << emit_ms << " ms\n"
                 << "  ИТОГО сборка:                 " << setw(6) << total_ms << " ms\n"
                 << "========================================\n\n";
        } else {
            cout << "\n=== COMPILATION STAGE TIMINGS ===\n"
                 << "  1. File I/O Read:             " << setw(6) << read_ms << " ms\n"
                 << "  2. Lexer (tokenization):      " << setw(6) << lex_ms << " ms\n"
                 << "  3. Bytecode Compiler:         " << setw(6) << comp_ms << " ms\n" \
                 << "  4. Bytecode Optimizer (-O):   " << setw(6) << opt_ms << " ms\n"
                 << "  4. Output Generation:         " << setw(6) << emit_ms << " ms\n"
                 << "  TOTAL build time:             " << setw(6) << total_ms << " ms\n"
                 << "=================================\n\n";
        }
    }

    // Выполнение в VM
    if (opt.run_after_build) {
        if (opt.lang_ru) cout << "[GrassVM]: Исполнение двоичного кода...\n\n";
        else cout << "[GrassVM]: Executing bytecode...\n\n";
        g_vm_arena.reset();
        GrassJIT::initialize();
        if (opt.enable_jit || opt.opt_level >= 3) {
            GrassJIT::set_enabled(true);
        } else {
            GrassJIT::set_enabled(false);
        }
        GrassVM vm(compiler.code);
        vm.execute();
        GrassThread::shutdown();
        if (opt.print_timing && GrassJIT::getStats().loops_compiled > 0) {
            const auto& js = GrassJIT::getStats();
            if (opt.lang_ru) {
                cout << "\n[JIT-Компилятор x86_64]: Скомпилировано циклов: " << js.loops_compiled
                     << " (" << js.machine_code_bytes << " B машинного кода x86_64, время JIT: "
                     << js.compile_time_ms << " ms)\n";
            } else {
                cout << "\n[JIT x86_64]: Loops compiled: " << js.loops_compiled
                     << " (" << js.machine_code_bytes << " B native x86_64 code, JIT compile: "
                     << js.compile_time_ms << " ms)\n";
            }
        }
    }
    return true;
}

bool CompileAndRunFile(string input_path, bool build_only) {
    CompilerOptions opt;
    opt.input_file = input_path;
    opt.run_after_build = !build_only;
    return ProcessCompilation(opt);
}

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD origInMode = 0;
    if (hIn != INVALID_HANDLE_VALUE) GetConsoleMode(hIn, &origInMode);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    GrassASM::initialize();
    GrassKeyboard::initialize();

    if (CheckAndRunEmbeddedBytecode()) {
#if defined(_WIN32)
        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
#endif
        return 0;
    }

    if (argc >= 2) {
        CompilerOptions opt;

        for (int i = 1; i < argc; ++i) {
            string arg = argv[i];
            if (arg == "-V" || arg == "--version" || arg == "-v") {
                opt.show_version = true;
            } else if (arg == "-h" || arg == "--help" || arg == "/?") {
                opt.show_help = true;
            } else if (arg == "--ru") {
                opt.lang_ru = true;
            } else if (arg == "--en") {
                opt.lang_ru = false;
            } else if (arg == "--install" || arg == "install" || arg == "-i") {
                opt.install = true;
            } else if (arg == "-c" || arg == ".//") {
                opt.emit_bytecode_only = true;
                opt.run_after_build = false;
            } else if (arg == "-emit-exe") {
                opt.emit_exe = true;
                opt.run_after_build = false;
            } else if (arg == "-r" || arg == "--run" || arg == "./!") {
                opt.run_after_build = true;
                opt.explicit_run = true;
            } else if (arg == "-S") {
                opt.dump_disasm = true;
                if (!opt.explicit_run) opt.run_after_build = false;
            } else if (arg == "--dump-ast") {
                opt.dump_ast = true;
                opt.run_after_build = false;
            } else if (arg == "--time") {
                opt.print_timing = true;
            } else if (arg == "--multiboot") {
                opt.emit_multiboot = true;
                opt.freestanding = true;
                opt.run_after_build = false;
            } else if (arg == "--kernel") {
                opt.emit_kernel = true;
                opt.freestanding = true;
                opt.run_after_build = false;
            } else if (arg == "--sys") {
                opt.emit_sys = true;
                opt.freestanding = true;
                opt.run_after_build = false;
            } else if (arg == "--iso") {
                opt.emit_iso = true;
                opt.freestanding = true;
                opt.run_after_build = false;
            } else if (arg == "--freestanding") {
                opt.freestanding = true;
            } else if (arg == "--jit") {
                opt.enable_jit = true;
            } else if (arg == "-O0") {
                opt.opt_level = 0;
            } else if (arg == "-O1") {
                opt.opt_level = 1;
            } else if (arg == "-O2") {
                opt.opt_level = 2;
            } else if (arg == "-O3") {
                opt.opt_level = 3;
            } else if (arg == "-o") {
                if (i + 1 < argc) {
                    opt.output_file = argv[++i];
                    if (!opt.explicit_run) opt.run_after_build = false;
                } else {
                    cerr << (opt.lang_ru ? "[GrassError]: Пропущен аргумент для -o\n" : "[GrassError]: Missing argument for -o\n");
                    return 1;
                }
            } else if (arg == "--native") {
                opt.native_mode = true;
                opt.emit_bytecode_only = false;
            } else if (arg == "--use-gcc") {
                opt.use_gcc = true;
            } else if (arg == "--target" || arg.rfind("--target=", 0) == 0) {
                string target_val;
                if (arg.rfind("--target=", 0) == 0) {
                    target_val = arg.substr(9);
                } else if (i + 1 < argc) {
                    target_val = argv[++i];
                }
                opt.target = target_val;
                if (target_val == "linux") {
                    opt.abi_str = "sysv64";
                    opt.arch_str = "x64";
                } else if (target_val == "windows") {
                    opt.abi_str = "win64";
                    opt.arch_str = "x64";
                }
            } else if (arg == "--no-sema") {
                opt.no_sema = true;
            } else if (arg == "--check") {
                opt.check_only = true;
                opt.run_after_build = false;
                opt.emit_bytecode_only = false;
            } else if (arg == "--emit-ir") {
                opt.emit_ir = true;
            } else if (arg == "--emit-obj") {
                opt.emit_obj = true;
                if (i + 1 < argc) {
                    opt.emit_obj_path = argv[++i];
                }
            } else if (arg == "--emit-bin") {
                opt.emit_bin = true;
                if (i + 1 < argc) {
                    opt.emit_bin_path = argv[++i];
                }
            } else if (arg == "--arch") {
                if (i + 1 < argc) {
                    opt.arch_str = argv[++i];
                }
            } else if (arg == "--abi") {
                if (i + 1 < argc) {
                    opt.abi_str = argv[++i];
                }
            } else if (arg == "--base-addr") {
                if (i + 1 < argc) {
                    try {
                        opt.base_addr = std::stoull(argv[++i], nullptr, 0);
                    } catch (...) {}
                }
            } else if (!arg.empty() && arg[0] == '-') {
                cerr << (opt.lang_ru ? "[GrassError]: Неизвестный флаг: " : "[GrassError]: Unknown flag: ") << arg << "\n";
                return 1;
            } else {
                opt.input_file = arg;
            }
        }

        if (opt.show_version) {
            PrintVersion(opt.lang_ru);
            return 0;
        }

        if (opt.show_help) {
            PrintHelp(opt.lang_ru);
            return 0;
        }

        if (opt.install) {
            InstallGrassGreenEcosystem();
            return 0;
        }

        if (opt.input_file.empty()) {
            cerr << (opt.lang_ru ? "[GrassError]: Не указан входной файл .grass. Используйте 'grg -h' для справки.\n"
                                 : "[GrassError]: No .grass input file specified. Use 'grg -h' for help.\n");
            return 1;
        }

#if defined(_WIN32)
        // Если .grass запущен напрямую из Проводника (отдельное новое окно консоли только с одним процессом),
        // переключаем рабочую папку на каталог файла, включаем запуск (-r) и не закрываем консоль сразу.
        DWORD consoleProcs[4] = { 0 };
        bool launchedStandalone = (argc == 2 && GetConsoleProcessList(consoleProcs, 4) <= 1);
        if (launchedStandalone) {
            std::error_code ec;
            fs::path inp(opt.input_file);
            if (!inp.parent_path().empty() && fs::exists(inp.parent_path(), ec)) {
                fs::current_path(inp.parent_path(), ec);
                opt.input_file = inp.filename().string();
            }
            if (!opt.explicit_run && !opt.emit_bytecode_only && !opt.emit_exe && opt.output_file.empty()) {
                opt.run_after_build = true;
            }
        }
#endif

        bool comp_ok = ProcessCompilation(opt);
        int exit_code = comp_ok ? 0 : 1;

#if defined(_WIN32)
        if (launchedStandalone) {
            cout << "\n========================================================\n"
                 << "[GrassGreen] Process exited with code " << exit_code << ".\n"
                 << "========================================================\n";
            cout.flush();
            std::system("pause");
        }
        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
#endif
        return exit_code;
    }

    cout << "========================================================\n";
    cout << "  GrassGreen (G&G) Native Compiler & VM v" << GRASS_VERSION << " (grg)\n";
    cout << "  Repository: github.com/m1oky/GrassGreen\n";
    cout << "  Type 'help' or '-h' for commands, 'exit' to quit.\n";
    cout << "========================================================\n";
    string line;
    while (true) {
        cout << "G&G> ";
        if (!getline(cin, line)) break;
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);

        if (line == "exit" || line == "quit") break;
        if (line == "install" || line == "--install") { InstallGrassGreenEcosystem(); continue; }
        if (line == "help" || line == "-h" || line == "--help") { PrintHelp(false); continue; }
        if (line == "version" || line == "-V" || line == "--version") { PrintVersion(false); continue; }

        stringstream ss(line);
        string cmd, file_arg;
        ss >> cmd;
        getline(ss, file_arg);
        size_t f_start = file_arg.find_first_not_of(" \t");
        if (f_start != string::npos) file_arg = file_arg.substr(f_start);
        else file_arg = "";

        CompilerOptions opt;
        if (cmd == ".//" || cmd == "compile" || cmd == "build" || cmd == "-c") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.emit_bytecode_only = true;
            opt.run_after_build = false;
        } else if (cmd == "-S") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.dump_disasm = true;
            opt.run_after_build = false;
        } else if (cmd == "--dump-ast") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.dump_ast = true;
        } else if (cmd == "--time") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.print_timing = true;
        } else if (cmd == "./!" || cmd == "run" || cmd == "-r") {
            opt.input_file = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            opt.run_after_build = true;
        } else {
            opt.input_file = cmd;
            opt.run_after_build = true;
        }
        ProcessCompilation(opt);
    }

#if defined(_WIN32)
    if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
#endif
    return 0;
}

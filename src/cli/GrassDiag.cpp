#include "cli/GrassDiag.h"
#include "vm/GrassVM.h"
#include "GrassVersion.h"
#include <iostream>
#include <iomanip>
#include <cstring>

using namespace std;

void PrintVersion(bool lang_ru) {
    if (lang_ru) {
        cout << "GrassGreen (G&G) v" << GRASS_VERSION << " (x86_64-win32)\n"
             << "Официальный компилятор и ядро выполнения (grg / grassc)\n"
             << "Сайт: grassgreen.info | Репозиторий: github.com/m1oky/GrassGreen\n";
    } else {
        cout << "GrassGreen (G&G) v" << GRASS_VERSION << " (x86_64-win32)\n"
             << "Official Compiler & Freestanding Execution Core (grg / grassc)\n"
             << "Domain: grassgreen.info | Repository: github.com/m1oky/GrassGreen\n";
    }
}

void PrintHelp(bool lang_ru) {
    if (lang_ru) {
        cout << "GrassGreen (G&G) v" << GRASS_VERSION << " Компилятор и Рантайм (grg / grassc)\n\n"
             << "Использование:\n"
             << "  grg [опции] <файл.grass>\n"
             << "  grassc [опции] <файл.grass>\n\n"
             << "Опции сборки и вывода:\n"
             << "  -o <файл>        Указать путь и имя выходного файла\n"
             << "  -c               Собрать только чистый бинарный байт-код (.grassc)\n"
             << "  -emit-exe        Упаковать байт-код в автономный исполняемый .exe\n"
             << "  -r, --run, ./!   Скомпилировать и немедленно запустить в VM\n"
             << "  -O0              Debug: сборка без оптимизаций\n"
             << "  -O2, -O3         Оптимизация байт-кода (по умолчанию: -O2)\n"
             << "  --jit            Включить нативный JIT-компилятор x86_64 для горячих циклов\n"
             << "  --freestanding   Автономный режим ядра (без libc/C++ рантайма)\n\n"
             << "Низкоуровневая разработка и ОС (G&G v" << GRASS_VERSION << " Bare-Metal):\n"
             << "  --multiboot      Генерация заголовка Multiboot 1/2 спецификации (GRUB / QEMU)\n"
             << "  --kernel         Выделенный режим компиляции в чистый бинарный файл ядра (.bin)\n"
             << "  --sys            Режим компиляции драйверов ядра в системный модуль (.sys)\n"
             << "  --iso            Создание готового загрузочного .iso образа (El Torito No-Emulation)\n\n"
             << "Диагностика и анализ:\n"
             << "  --dump-ast       Распечатать дерево токенов (AST) и C-ABI структуры\n"
             << "  -S               Распечатать читаемый дизассемблер байт-кода\n"
             << "  --time           Замерить время каждого этапа компиляции (мс)\n"
             << "  --check          Выполнить только синтаксический и семантический анализ\n"
             << "  --no-sema        Отключить статический семантический анализ перед сборкой\n\n"
             << "Нативная кодогенерация и контейнеры (G&G Native Backend):\n"
             << "  --native         Скомпилировать напрямую в нативный исполняемый .exe (без VM)\n"
             << "  --emit-ir        Вывести текстовый дамп SSA GrassIR до и после оптимизаций\n"
             << "  --emit-obj <путь> Сохранить объектный файл COFF (.obj) или ELF (.o)\n"
             << "  --emit-bin <путь> Сохранить плоский бинарный образ ядра (.bin)\n"
             << "  --arch <x64|x32> Целевая архитектура (по умолчанию: x64)\n"
             << "  --abi <win64|sysv64|cdecl32|baremetal64|baremetal32> Соглашение о вызовах\n"
             << "  --base-addr <hex> Базовый адрес загрузки ядра (по умолчанию: 0x100000)\n\n"
             << "Локализация и справка:\n"
             << "  --ru             Выводить сообщения компилятора на русском\n"
             << "  --en             Выводить сообщения компилятора на английском (по умолчанию)\n"
             << "  -V, --version    Показать информацию о версии\n"
             << "  -h, --help       Показать эту справку\n"
             << "  --install, -i    Установить инструментарий G&G и расширение VS Code\n";
    } else {
        cout << "GrassGreen (G&G) v" << GRASS_VERSION << " Compiler & Runner (grg / grassc)\n\n"
             << "Usage:\n"
             << "  grg [options] <source.grass>\n"
             << "  grassc [options] <source.grass>\n\n"
             << "Build & Output Options:\n"
             << "  -o <filename>    Specify output file name\n"
             << "  -c               Compile to pure bytecode (.grassc) without .exe wrapper\n"
             << "  -emit-exe        Package bytecode into standalone native Win32 .exe\n"
             << "  -r, --run, ./!   Compile and execute immediately in VM\n"
             << "  -O0              Debug: instant build without optimizations\n"
             << "  -O2, -O3         Enable bytecode optimizations (default: -O2)\n"
             << "  --jit            Enable native x86_64 JIT (Just-In-Time) compiler for loops\n"
             << "  --freestanding   Freestanding kernel build (independent of libc/C++)\n\n"
             << "Bare-Metal & OS Development (G&G v" << GRASS_VERSION << "):\n"
             << "  --multiboot      Generate Multiboot 1/2 header specification for GRUB / QEMU\n"
             << "  --kernel         Dedicated build mode to raw binary OS kernel (.bin)\n"
             << "  --sys            Device driver compilation to system kernel module (.sys)\n"
             << "  --iso            Create standalone bootable ISO CD image (El Torito No-Emulation)\n\n"
             << "Diagnostics & Inspection:\n"
             << "  --dump-ast       Dump syntax tree (AST) and C-ABI struct layouts\n"
             << "  -S               Print disassembled human-readable bytecode\n"
             << "  --time           Display execution time for each compilation stage (ms)\n"
             << "  --check          Run syntax and semantic analysis only without codegen\n"
             << "  --no-sema        Disable static semantic analysis before compilation\n\n"
             << "Native Backend & Containers (G&G Native Backend):\n"
             << "  --native         Compile directly to native standalone .exe without bytecode VM\n"
             << "  --emit-ir        Dump text SSA GrassIR before and after optimizations\n"
             << "  --emit-obj <path> Save object file COFF (.obj) or ELF (.o)\n"
             << "  --emit-bin <path> Save bare-metal flat binary kernel (.bin)\n"
             << "  --arch <x64|x32> Target architecture (default: x64)\n"
             << "  --abi <win64|sysv64|cdecl32|baremetal64|baremetal32> Calling convention\n"
             << "  --base-addr <hex> Kernel base load address (default: 0x100000)\n\n"
             << "Localization & Info:\n"
             << "  --ru             Switch compiler messages to Russian\n"
             << "  --en             Switch compiler messages to English (default)\n"
             << "  -V, --version    Show version information\n"
             << "  -h, --help       Display this help message\n"
             << "  --install, -i    Install G&G toolchain and VS Code extension\n";
    }
}

void DisassembleBytecode(const vector<uint8_t>& code, bool lang_ru) {
    if (lang_ru) {
        cout << "\n=== ДИЗАССЕМБЛЕР БАЙТ-КОДА GRASSGREEN (G&G v" << GRASS_VERSION << ") ===\n";
        cout << "Смещение  Опкод            Аргументы\n";
        cout << "--------------------------------------------------------\n";
    } else {
        cout << "\n=== GRASSGREEN (G&G v" << GRASS_VERSION << ") BYTECODE DISASSEMBLY ===\n";
        cout << "Offset    OpCode           Arguments\n";
        cout << "--------------------------------------------------------\n";
    }

    size_t ip = 0;
    while (ip < code.size()) {
        size_t cur_ip = ip;
        uint8_t op = code[ip++];
        cout << setw(6) << setfill('0') << cur_ip << "    " << setfill(' ');

        auto readU32 = [&]() -> uint32_t {
            if (ip + 4 > code.size()) { ip = code.size(); return 0; }
            uint32_t v;
            memcpy(&v, &code[ip], 4);
            ip += 4;
            return v;
        };

        auto readDouble = [&]() -> double {
            if (ip + 8 > code.size()) { ip = code.size(); return 0.0; }
            double d;
            memcpy(&d, &code[ip], 8);
            ip += 8;
            return d;
        };

        auto readString = [&]() -> string {
            uint32_t len = readU32();
            if (ip + len > code.size()) { ip = code.size(); return ""; }
            string s((const char*)&code[ip], len);
            ip += len;
            return s;
        };

        switch (op) {
            case OP_HALT:         cout << "OP_HALT\n"; break;
            case OP_CONST_NUM:    cout << "OP_CONST_NUM    " << readDouble() << "\n"; break;
            case OP_CONST_STR:    cout << "OP_CONST_STR    \"" << readString() << "\"\n"; break;
            case OP_LOAD_VAR:     cout << "OP_LOAD_VAR     \"" << readString() << "\"\n"; break;
            case OP_STORE_VAR:    cout << "OP_STORE_VAR    \"" << readString() << "\"\n"; break;
            case OP_DEF_VAR:      cout << "OP_DEF_VAR      \"" << readString() << "\"\n"; break;
            case OP_POP:          cout << "OP_POP\n"; break;
            case OP_ADD:          cout << "OP_ADD\n"; break;
            case OP_SUB:          cout << "OP_SUB\n"; break;
            case OP_MUL:          cout << "OP_MUL\n"; break;
            case OP_DIV:          cout << "OP_DIV\n"; break;
            case OP_NEG:          cout << "OP_NEG\n"; break;
            case OP_MOD:          cout << "OP_MOD\n"; break;
            case OP_BIT_AND:      cout << "OP_BIT_AND\n"; break;
            case OP_BIT_OR:       cout << "OP_BIT_OR\n"; break;
            case OP_BIT_XOR:      cout << "OP_BIT_XOR\n"; break;
            case OP_BIT_NOT:      cout << "OP_BIT_NOT\n"; break;
            case OP_CMP_EQ:       cout << "OP_CMP_EQ\n"; break;
            case OP_CMP_NEQ:      cout << "OP_CMP_NEQ\n"; break;
            case OP_CMP_LT:       cout << "OP_CMP_LT\n"; break;
            case OP_CMP_GT:       cout << "OP_CMP_GT\n"; break;
            case OP_CMP_LTE:      cout << "OP_CMP_LTE\n"; break;
            case OP_CMP_GTE:      cout << "OP_CMP_GTE\n"; break;
            case OP_AND:          cout << "OP_AND\n"; break;
            case OP_OR:           cout << "OP_OR\n"; break;
            case OP_NOT:          cout << "OP_NOT\n"; break;
            case OP_JMP:          cout << "OP_JMP          -> " << readU32() << "\n"; break;
            case OP_JMP_IF_FALSE: cout << "OP_JMP_IF_FALSE -> " << readU32() << "\n"; break;
            case OP_CALL_FUNC:    cout << "OP_CALL_FUNC    -> " << readU32() << "\n"; break;
            case OP_RET:          cout << "OP_RET\n"; break;
            case OP_LOG:          cout << "OP_LOG\n"; break;
            case OP_LOG_EMPTY:    cout << "OP_LOG_EMPTY\n"; break;
            case OP_WAIT:         cout << "OP_WAIT\n"; break;
            case OP_ASK:          cout << "OP_ASK          \"" << readString() << "\"\n"; break;
            case OP_PRINT_RAW:    cout << "OP_PRINT_RAW\n"; break;
            case OP_CALL_BUILTIN: {
                string fn = readString();
                uint8_t argc = (ip < code.size()) ? code[ip++] : 0;
                cout << "OP_CALL_BUILTIN \"" << fn << "\" (argc=" << (int)argc << ")\n";
                break;
            }
            case OP_LOAD_DB:      cout << "OP_LOAD_DB\n"; break;
            case OP_START_WEB:    cout << "OP_START_WEB    \"" << readString() << "\"\n"; break;
            case OP_UI_INIT:      cout << "OP_UI_INIT\n"; break;
            case OP_UI_ADD_ELEM:  cout << "OP_UI_ADD_ELEM\n"; break;
            case OP_UI_SHOW:      cout << "OP_UI_SHOW\n"; break;
            case OP_UI_CMD:       cout << "OP_UI_CMD       \"" << readString() << "\"\n"; break;
            case OP_NEW_ARRAY:    cout << "OP_NEW_ARRAY    count=" << readU32() << "\n"; break;
            case OP_GET_INDEX:    cout << "OP_GET_INDEX\n"; break;
            case OP_SET_INDEX:    cout << "OP_SET_INDEX\n"; break;
            case OP_ARRAY_PUSH:   cout << "OP_ARRAY_PUSH\n"; break;
            case OP_ARRAY_POP:    cout << "OP_ARRAY_POP\n"; break;
            case OP_NEW_STRUCT:   cout << "OP_NEW_STRUCT   \"" << readString() << "\"\n"; break;
            case OP_GET_FIELD:    cout << "OP_GET_FIELD    \"" << readString() << "\"\n"; break;
            case OP_SET_FIELD:    cout << "OP_SET_FIELD    \"" << readString() << "\"\n"; break;
            case OP_JUMP:         cout << "OP_JUMP         -> " << readU32() << "\n"; break;
            case OP_CALL_USER:    cout << "OP_CALL_USER    -> " << readU32() << "\n"; break;
            case OP_PARAM_PROLOGUE: cout << "OP_PARAM_PROLOGUE\n"; break;
            case OP_PARAM_EPILOGUE: cout << "OP_PARAM_EPILOGUE\n"; break;
            case OP_NOP:          cout << "OP_NOP\n"; break;
            case OP_ADD_CONST:    cout << "OP_ADD_CONST    " << readDouble() << "\n"; break;
            case OP_SUB_CONST:    cout << "OP_SUB_CONST    " << readDouble() << "\n"; break;
            case OP_MUL_CONST:    cout << "OP_MUL_CONST    " << readDouble() << "\n"; break;
            case OP_DIV_CONST:    cout << "OP_DIV_CONST    " << readDouble() << "\n"; break;
            case OP_INC_VAR:      cout << "OP_INC_VAR      \"" << readString() << "\"\n"; break;
            case OP_DEC_VAR:      cout << "OP_DEC_VAR      \"" << readString() << "\"\n"; break;
            case OP_THREAD_SPAWN: {
                uint32_t entry = readU32();
                uint8_t argc = (ip < code.size()) ? code[ip++] : 0;
                cout << "OP_THREAD_SPAWN  -> " << entry << " (argc=" << (int)argc << ")\n";
                break;
            }
            case OP_ASYNC_SPAWN: {
                uint32_t entry = readU32();
                uint8_t argc = (ip < code.size()) ? code[ip++] : 0;
                cout << "OP_ASYNC_SPAWN   -> " << entry << " (argc=" << (int)argc << ")\n";
                break;
            }
            case OP_ADDR_VAR:     cout << "OP_ADDR_VAR     \"" << readString() << "\"\n"; break;
            case OP_DEREF:        cout << "OP_DEREF\n"; break;
            case OP_STORE_DEREF:  cout << "OP_STORE_DEREF\n"; break;
            case OP_ADDR_INDEX:   cout << "OP_ADDR_INDEX   \"" << readString() << "\"\n"; break;
            case OP_ADDR_FIELD:   cout << "OP_ADDR_FIELD   \"" << readString() << "\"\n"; break;
            default:              cout << "OP_UNKNOWN_0x" << hex << (int)op << dec << "\n"; break;
        }
    }
    cout << "--------------------------------------------------------\n\n";
}

void DumpAST(const vector<GrassToken>& tokens, const string& filename, bool lang_ru) {
    if (lang_ru) {
        cout << "\n=== ДАМП СИНТАКСИЧЕСКОГО ДЕРЕВА (AST): " << filename << " ===\n";
    } else {
        cout << "\n=== ABSTRACT SYNTAX TREE (AST) DUMP: " << filename << " ===\n";
    }
    int cur_line = -1;
    for (const auto& tok : tokens) {
        if (tok.kind == TOK_EOF) break;
        if (tok.line != cur_line) {
            cur_line = tok.line;
            cout << "\n[Line " << setw(3) << setfill('0') << cur_line << "] " << setfill(' ');
        }
        cout << tok.text << " ";
    }
    cout << "\n\n";
    if (!g_struct_registry.empty()) {
        if (lang_ru) cout << "Зарегистрированные структуры (C-ABI Data Layout):\n";
        else cout << "Registered Structures (C-ABI Data Layout):\n";
        for (const auto& kv : g_struct_registry) {
            const auto& def = kv.second;
            cout << "  struct " << def.name << " (Size: " << def.total_size 
                 << " B, Alignment: " << def.alignment << " B):\n";
            for (const auto& f : def.fields) {
                cout << "    +" << f.offset << " B: " << f.name << " [size=" << f.size 
                     << " B, align=" << f.alignment << " B]\n";
            }
        }
        cout << "\n";
    }
}

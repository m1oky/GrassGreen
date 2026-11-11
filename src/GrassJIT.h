// =============================================================================
//  GrassGreen 4.0 — Minimal x86_64 JIT (Just-In-Time) Compiler
//  Файл создан Архитектором. Реализация — Кодер Ядра («Занятой-Core»).
// =============================================================================
#pragma once

#include "GrassSyntax.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <memory>

namespace GrassJIT {

    // Статистика JIT-компиляции
    struct JitStats {
        size_t loops_compiled = 0;
        size_t machine_code_bytes = 0;
        double compile_time_ms = 0.0;
        bool is_active = false;
    };

    // Контекст выполнения JIT-блока (передаётся в нативный машинный код в RCX)
    struct alignas(16) JitRuntimeContext {
        double vars[128];        // Значения численных переменных (до 128 переменных)
        int64_t iterations;      // Количество выполненных итераций
        int64_t status_flags;    // 0 = OK, 1 = Error/Bailout
    };

    // Указатель на скомпилированную функцию x86_64
    // Windows x64 ABI: первый аргумент в RCX
    typedef void (*JitFn)(JitRuntimeContext* ctx);

    // Скомпилированный нативный фрагмент
    struct CompiledJitBlock {
        void* code_ptr = nullptr;
        size_t code_size = 0;
        uint32_t start_ip = 0;
        uint32_t end_ip = 0;
        std::unordered_map<std::string, int> var_slot_map;
        bool is_valid = false;

        ~CompiledJitBlock();
    };

    // Инициализация подсистемы JIT
    void initialize();

    // Освобождение ресурсов JIT
    void shutdown();

    // BUG-03 / JIT-01 & BUG-04 / JIT-02:
    // Анализатор допустимости цикла для JIT-компиляции (проверка глубины стека <= 2 и запрет OP_MOD)
    bool isLoopJITCandidate(
        const std::vector<uint8_t>& code,
        uint32_t loop_start,
        uint32_t loop_end,
        int& max_stack_depth,
        uint32_t& loop_exit_ip,
        std::unordered_map<std::string, int>& var_slot_map
    );

    // Включение / выключение JIT в рантайме
    void set_enabled(bool enable);
    bool is_enabled();

    // Выделение исполняемой памяти (PAGE_EXECUTE_READWRITE)
    void* allocateExecutableBuffer(size_t size);
    void freeExecutableBuffer(void* ptr, size_t size);

    // Попытка скомпилировать горячий цикл байт-кода [loop_start, loop_end] в нативный код x86_64
    std::shared_ptr<CompiledJitBlock> tryCompileLoop(
        const std::vector<uint8_t>& code,
        uint32_t loop_start,
        uint32_t loop_end,
        const std::unordered_map<std::string, GrassValue>& current_vars
    );

    // Выполнение скомпилированного нативного блока с синхронизацией переменных VM
    bool executeJitBlock(
        const std::shared_ptr<CompiledJitBlock>& block,
        std::unordered_map<std::string, GrassValue>& vm_vars
    );

    // Глобальная статистика
    JitStats& getStats();

} // namespace GrassJIT

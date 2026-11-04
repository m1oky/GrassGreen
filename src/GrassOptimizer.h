// =============================================================================
//  GrassGreen 4.0 — Bytecode Optimizer (Opcode Fusion, Constant Folding, Peephole)
//  Файл создан Архитектором. Реализация — Кодер Ядра.
// =============================================================================
#ifndef GRASS_OPTIMIZER_H
#define GRASS_OPTIMIZER_H

#include <vector>
#include <cstdint>
#include <string>

namespace GrassOptimizer {

    struct OptimizationStats {
        size_t initial_bytes = 0;
        size_t optimized_bytes = 0;
        size_t constants_folded = 0;
        size_t opcodes_fused = 0;
        size_t jumps_threaded = 0;
        size_t dead_code_removed = 0;
    };

    // Главная функция оптимизации байт-кода
    std::vector<uint8_t> optimize(const std::vector<uint8_t>& bytecode, int opt_level, OptimizationStats* out_stats = nullptr);

} // namespace GrassOptimizer

#endif // GRASS_OPTIMIZER_H

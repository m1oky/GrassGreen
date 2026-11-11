// =============================================================================
//  GrassGreen 4.0 — Module Import & Dependency Resolver (Import Pass)
//  Файл создан Архитектором. Реализация — Кодер Синтаксиса и Кодер Ядра.
// =============================================================================
#ifndef GRASS_IMPORT_H
#define GRASS_IMPORT_H

#include <vector>
#include <string>
#include <unordered_set>
#include "GrassSyntax.h"

namespace GrassImport {

    struct ImportStats {
        size_t total_modules_imported = 0;
        size_t total_tokens_after_import = 0;
        bool has_error = false;
    };

    // Главная функция рекурсивного разрешения директив import / include
    std::vector<GrassToken> resolveImports(
        const std::vector<GrassToken>& root_tokens,
        const std::string& root_filepath,
        ImportStats* out_stats = nullptr
    );

} // namespace GrassImport

#endif // GRASS_IMPORT_H

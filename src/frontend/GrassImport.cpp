// =============================================================================
//  GrassGreen 4.0 — Module Import & Dependency Resolver Implementation
//  Файл создан Архитектором. Реализация — Кодер Синтаксиса и Кодер Ядра.
// =============================================================================
#include "frontend/GrassImport.h"
#include "baremetal/GrassHAL.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace GrassImport {

    static std::string normalizePath(const std::string& raw) {
        std::string p = raw;
        for (char& c : p) {
            if (c == '\\') c = '/';
        }
        return p;
    }

    static std::string getCanonicalKey(const std::string& filepath) {
        std::error_code ec;
        if (fs::exists(filepath, ec)) {
            auto canon = fs::canonical(filepath, ec);
            if (!ec) {
                std::string s = canon.string();
                for (char& c : s) {
                    if (c == '\\') c = '/';
                    c = (char)std::tolower((unsigned char)c);
                }
                return s;
            }
        }
        std::string s = normalizePath(filepath);
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    static bool fileExists(const std::string& path) {
        if (GrassHAL::is_freestanding() && GrassHAL::vfs_exists(path)) {
            return true;
        }
        std::error_code ec;
        return fs::exists(path, ec) && !fs::is_directory(path, ec);
    }

    static std::string readFileContent(const std::string& path) {
        if (GrassHAL::is_freestanding() && GrassHAL::vfs_exists(path)) {
            return GrassHAL::vfs_read(path);
        }
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return "";
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return content;
    }

    // Поиск файла модуля по списку путей
    static std::string findModuleFile(const std::string& parent_file, const std::string& raw_target) {
        std::string target = raw_target;
        // Удаляем обрамляющие кавычки, если есть
        if (target.size() >= 2 && ((target.front() == '"' && target.back() == '"') ||
                                   (target.front() == '\'' && target.back() == '\''))) {
            target = target.substr(1, target.size() - 2);
        }

        std::vector<std::string> candidates;
        fs::path parent_dir = fs::path(parent_file).parent_path();

        // 1. Относительно родительского файла
        if (!parent_dir.empty()) {
            candidates.push_back((parent_dir / target).string());
            candidates.push_back((parent_dir / (target + ".grass")).string());
        }

        // 2. Относительно текущей рабочей директории
        candidates.push_back(target);
        candidates.push_back(target + ".grass");

        // 3. Стандартные папки модулей проекта
        candidates.push_back("modules/" + target);
        candidates.push_back("modules/" + target + ".grass");
        candidates.push_back("stdlib/" + target);
        candidates.push_back("stdlib/" + target + ".grass");
        candidates.push_back("../stdlib/" + target);
        candidates.push_back("../stdlib/" + target + ".grass");
        candidates.push_back("../../stdlib/" + target);
        candidates.push_back("../../stdlib/" + target + ".grass");
        candidates.push_back("examples/modules/" + target);
        candidates.push_back("examples/modules/" + target + ".grass");
        candidates.push_back("include/" + target);
        candidates.push_back("include/" + target + ".grass");

#ifdef _WIN32
        char exe_buf[MAX_PATH];
        DWORD exe_len = GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
        if (exe_len > 0) {
            fs::path exe_dir = fs::path(exe_buf).parent_path();
            candidates.push_back((exe_dir / target).string());
            candidates.push_back((exe_dir / (target + ".grass")).string());
            candidates.push_back((exe_dir / "stdlib" / target).string());
            candidates.push_back((exe_dir / "stdlib" / (target + ".grass")).string());
            candidates.push_back((exe_dir / ".." / "stdlib" / target).string());
            candidates.push_back((exe_dir / ".." / "stdlib" / (target + ".grass")).string());
            candidates.push_back((exe_dir / ".." / target).string());
            candidates.push_back((exe_dir / ".." / (target + ".grass")).string());
        }
#endif

        for (const auto& cand : candidates) {
            std::string norm = normalizePath(cand);
            std::string lower = norm;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("windows") != std::string::npos || lower.find("system32") != std::string::npos) {
                continue;
            }
            if (fileExists(norm)) {
                return norm;
            }
        }
        return "";
    }

    static std::vector<GrassToken> resolveInternal(
        const std::vector<GrassToken>& tokens,
        const std::string& current_filepath,
        std::unordered_set<std::string>& visited,
        ImportStats& stats,
        bool is_root,
        int depth = 0
    ) {
        if (depth > 64) {
            std::cerr << "[GrassImport Error]: Превышена максимальная глубина цепочки импортов (64)!\n";
            return tokens;
        }
        std::vector<GrassToken> result;
        result.reserve(tokens.size() * 2);

        size_t i = 0;
        while (i < tokens.size()) {
            // Проверка на синтаксис: import "module" [;] или include "module" [;]
            if (tokens[i].kind == TOK_IDENT &&
                (tokens[i].text == "import" || tokens[i].text == "include")) {

                std::string target_name;
                int import_line = tokens[i].line;
                bool matched_import = false;

                if (i + 1 < tokens.size() &&
                    (tokens[i + 1].kind == TOK_STRING || tokens[i + 1].kind == TOK_IDENT)) {
                    target_name = tokens[i + 1].text;
                    i += 2;
                    matched_import = true;
                } else if (i + 2 < tokens.size() && tokens[i + 1].kind == TOK_LT) {
                    // Формат C++ style: include <math_utils.grass> или #include <common_types>
                    size_t j = i + 2;
                    while (j < tokens.size() && tokens[j].kind != TOK_GT && tokens[j].kind != TOK_SEMI && tokens[j].kind != TOK_EOF) {
                        target_name += tokens[j].text;
                        j++;
                    }
                    if (j < tokens.size() && tokens[j].kind == TOK_GT) j++;
                    i = j;
                    matched_import = true;
                }

                if (matched_import) {
                    // Пропускаем опциональную точку с запятой
                    if (i < tokens.size() && tokens[i].kind == TOK_SEMI) {
                        i++;
                    }

                    std::string resolved = findModuleFile(current_filepath, target_name);
                    if (resolved.empty()) {
                        std::cerr << "[GrassImport Error]: Файл модуля '" << target_name
                                  << "' не найден (импорт из " << current_filepath
                                  << " на строке " << import_line << ")!\n";
                        stats.has_error = true;
                        continue;
                    }

                    std::string canon_key = getCanonicalKey(resolved);
                    if (visited.count(canon_key)) {
                        // Уже был импортирован ранее (семантика #pragma once / deduplication)
                        continue;
                    }
                    visited.insert(canon_key);
                    stats.total_modules_imported++;

                    std::string mod_src = readFileContent(resolved);
                    if (mod_src.empty()) {
                        std::cerr << "[GrassImport Warning]: Импортируемый файл пуст: " << resolved << "\n";
                        continue;
                    }

                    Lexer mod_lexer(mod_src);
                    std::vector<GrassToken> mod_tokens = mod_lexer.tokenize();

                    // Рекурсивное разрешение вложенных импортов
                    mod_tokens = resolveInternal(mod_tokens, resolved, visited, stats, false, depth + 1);

                    // Очистка импортируемого модуля:
                    // 1. Убираем ВСЕ TOK_EOF из модуля, чтобы парсер не останавливался преждевременно
                    mod_tokens.erase(
                        std::remove_if(mod_tokens.begin(), mod_tokens.end(), [](const GrassToken& t) {
                            return t.kind == TOK_EOF;
                        }),
                        mod_tokens.end()
                    );

                    // 2. Убираем дублирующий заголовок !GrassGreen! если он уже есть в результирующем потоке
                    bool has_core = false;
                    for (const auto& rt : result) {
                        if (rt.kind == TOK_LIB && rt.text == "!GrassGreen!") {
                            has_core = true;
                            break;
                        }
                    }
                    if (has_core) {
                        mod_tokens.erase(
                            std::remove_if(mod_tokens.begin(), mod_tokens.end(), [](const GrassToken& t) {
                                return t.kind == TOK_LIB && t.text == "!GrassGreen!";
                            }),
                            mod_tokens.end()
                        );
                    }

                    // 3. Если модуль имеет свой main(), переименовываем его, чтобы не конфликтовать с главным файлом
                    for (size_t k = 0; k < mod_tokens.size(); ++k) {
                        if (mod_tokens[k].kind == TOK_IDENT && mod_tokens[k].text == "main" &&
                            k > 0 && mod_tokens[k - 1].kind == TOK_IDENT &&
                            (mod_tokens[k - 1].text == "int" || mod_tokens[k - 1].text == "void" || mod_tokens[k - 1].text == "take") &&
                            k + 1 < mod_tokens.size() && mod_tokens[k + 1].kind == TOK_LPAREN) {
                            mod_tokens[k].text = "__mod_main_" + std::to_string(stats.total_modules_imported);
                        }
                    }

                    // Встраиваем токены модуля в результирующий поток
                    result.insert(result.end(), mod_tokens.begin(), mod_tokens.end());
                    continue;
                }
            }

            // Пропускаем TOK_EOF внутри не-рутовых файлов
            if (!is_root && tokens[i].kind == TOK_EOF) {
                i++;
                continue;
            }

            result.push_back(tokens[i]);
            i++;
        }

        return result;
    }

    std::vector<GrassToken> resolveImports(
        const std::vector<GrassToken>& root_tokens,
        const std::string& root_filepath,
        ImportStats* out_stats
    ) {
        ImportStats stats;
        std::unordered_set<std::string> visited;
        std::string root_canon = getCanonicalKey(root_filepath);
        visited.insert(root_canon);

        // Auto-import stdlib modules when !GrassGreen! or module directives are present
        bool has_grassgreen = false;
        size_t first_lib_idx = 0;
        bool found_lib = false;
        std::vector<std::string> requested_modules;

        for (size_t k = 0; k < root_tokens.size(); ++k) {
            if (root_tokens[k].kind == TOK_LIB) {
                if (!found_lib) {
                    first_lib_idx = k;
                    found_lib = true;
                }
                const std::string& lib = root_tokens[k].text;
                if (lib == "!GrassGreen!") {
                    has_grassgreen = true;
                } else if (lib == "!time!") {
                    requested_modules.push_back("stdlib/time.grass");
                } else if (lib == "!io!" || lib == "!mmio!") {
                    requested_modules.push_back("stdlib/io.grass");
                } else if (lib == "!fs!" || lib == "!fsys!") {
                    requested_modules.push_back("stdlib/fsys.grass");
                } else if (lib == "!mem!" || lib == "!memory!") {
                    requested_modules.push_back("stdlib/memory.grass");
                } else if (lib == "!string!" || lib == "!str!") {
                    requested_modules.push_back("stdlib/string.grass");
                } else if (lib == "!math!" || lib == "!FastMath!" || lib == "!CoreMath!") {
                    requested_modules.push_back("stdlib/math.grass");
                } else if (lib == "!sys!" || lib == "!os!") {
                    requested_modules.push_back("stdlib/sys.grass");
                } else if (lib == "!net!" || lib == "!GrassNet!") {
                    requested_modules.push_back("stdlib/net.grass");
                } else if (lib == "!thread!") {
                    requested_modules.push_back("stdlib/thread.grass");
                } else if (lib == "!algo!") {
                    requested_modules.push_back("stdlib/algo.grass");
                } else if (lib == "!math3d!" || lib == "!Math3D!") {
                    requested_modules.push_back("stdlib/math3d.grass");
                } else if (lib == "!2d!" || lib == "!2D!" || lib == "!gfx2d!" || lib == "!Gfx2D!" ||
                           lib == "!2DGraphics!" || lib == "!2dgraphics!") {
                    requested_modules.push_back("stdlib/gfx2d.grass");
                } else if (lib == "!gfx3d!" || lib == "!Gfx3D!" || lib == "!3d!" || lib == "!3D!") {
                    requested_modules.push_back("stdlib/gfx3d.grass");
                } else if (lib == "!physics!" || lib == "!Physics!") {
                    requested_modules.push_back("stdlib/physics.grass");
                } else if (lib == "!baremetal!" || lib == "!bm!" || lib == "!kernel!" || lib == "!BareMetal!") {
                    requested_modules.push_back("stdlib/baremetal.grass");
                }
            }
        }

        if (has_grassgreen) {
            static const char* all_mods[] = {
                "stdlib/GrassGreen.grass",
                "stdlib/io.grass",
                "stdlib/fsys.grass",
                "stdlib/time.grass",
                "stdlib/memory.grass",
                "stdlib/string.grass",
                "stdlib/math.grass",
                "stdlib/sys.grass",
                "stdlib/net.grass",
                "stdlib/thread.grass",
                "stdlib/algo.grass",
                "stdlib/math3d.grass",
                "stdlib/gfx2d.grass",
                "stdlib/gfx3d.grass",
                "stdlib/physics.grass"
            };
            for (const char* m : all_mods) {
                if (std::find(requested_modules.begin(), requested_modules.end(), m) == requested_modules.end()) {
                    requested_modules.push_back(m);
                }
            }
        }

        std::vector<GrassToken> tokens_to_resolve = root_tokens;
        std::string lower_root = root_canon;
        bool is_stdlib_file = (lower_root.find("stdlib/grassgreen.grass") != std::string::npos ||
                               lower_root.find("stdlib/io.grass") != std::string::npos ||
                               lower_root.find("stdlib/fsys.grass") != std::string::npos ||
                               lower_root.find("stdlib/time.grass") != std::string::npos ||
                               lower_root.find("stdlib/memory.grass") != std::string::npos ||
                               lower_root.find("stdlib/string.grass") != std::string::npos ||
                               lower_root.find("stdlib/math.grass") != std::string::npos ||
                               lower_root.find("stdlib/sys.grass") != std::string::npos ||
                               lower_root.find("stdlib/net.grass") != std::string::npos ||
                               lower_root.find("stdlib/thread.grass") != std::string::npos ||
                               lower_root.find("stdlib/algo.grass") != std::string::npos ||
                               lower_root.find("stdlib/math3d.grass") != std::string::npos ||
                               lower_root.find("stdlib/gfx2d.grass") != std::string::npos ||
                               lower_root.find("stdlib/gfx3d.grass") != std::string::npos ||
                               lower_root.find("stdlib/physics.grass") != std::string::npos ||
                               lower_root.find("stdlib/baremetal.grass") != std::string::npos);

        if (!is_stdlib_file && !requested_modules.empty()) {
            std::vector<GrassToken> auto_tokens;
            for (const auto& mod : requested_modules) {
                std::string resolved = findModuleFile(root_filepath, mod);
                if (!resolved.empty()) {
                    auto_tokens.push_back(GrassToken(TOK_IDENT, "import", 0.0, 1));
                    auto_tokens.push_back(GrassToken(TOK_STRING, mod, 0.0, 1));
                    auto_tokens.push_back(GrassToken(TOK_SEMI, ";", 0.0, 1));
                }
            }
            if (!auto_tokens.empty()) {
                size_t insert_pos = found_lib ? (first_lib_idx + 1) : 0;
                tokens_to_resolve.insert(tokens_to_resolve.begin() + insert_pos, auto_tokens.begin(), auto_tokens.end());
            }
        }

        std::vector<GrassToken> resolved = resolveInternal(tokens_to_resolve, root_filepath, visited, stats, true);

        // Гарантируем, что в конце результирующего потока ровно один TOK_EOF
        while (!resolved.empty() && resolved.back().kind == TOK_EOF) {
            resolved.pop_back();
        }
        int last_line = resolved.empty() ? 1 : resolved.back().line;
        resolved.push_back(GrassToken(TOK_EOF, "", 0.0, last_line));

        stats.total_tokens_after_import = resolved.size();

        if (out_stats) *out_stats = stats;
        return resolved;
    }

} // namespace GrassImport

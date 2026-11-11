// =============================================================================
//  GrassGreen 4.0 — Module Import & Dependency Resolver Implementation
//  Файл создан Архитектором. Реализация — Кодер Синтаксиса и Кодер Ядра.
// =============================================================================
#include "GrassImport.h"
#include "GrassHAL.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

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
        candidates.push_back("examples/modules/" + target);
        candidates.push_back("examples/modules/" + target + ".grass");
        candidates.push_back("include/" + target);
        candidates.push_back("include/" + target + ".grass");

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

        std::vector<GrassToken> resolved = resolveInternal(root_tokens, root_filepath, visited, stats, true);

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

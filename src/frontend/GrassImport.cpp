// =============================================================================
//  GrassGreen 4.0 -- Module Import & Dependency Resolver Implementation
// =============================================================================
#include "frontend/GrassImport.h"
#include "baremetal/GrassHAL.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
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
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return s;
            }
        }
        std::string s = normalizePath(filepath);
        for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
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
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    static std::string getStdlibModuleForDirective(const std::string& lib) {
        if (lib == "!time!") return "stdlib/time.grass";
        if (lib == "!io!" || lib == "!mmio!") return "stdlib/io.grass";
        if (lib == "!fs!" || lib == "!fsys!") return "stdlib/fsys.grass";
        if (lib == "!mem!" || lib == "!memory!") return "stdlib/memory.grass";
        if (lib == "!string!" || lib == "!str!") return "stdlib/string.grass";
        if (lib == "!math!" || lib == "!FastMath!" || lib == "!CoreMath!") return "stdlib/math.grass";
        if (lib == "!sys!" || lib == "!os!") return "stdlib/sys.grass";
        if (lib == "!net!" || lib == "!GrassNet!") return "stdlib/net.grass";
        if (lib == "!thread!") return "stdlib/thread.grass";
        if (lib == "!algo!") return "stdlib/algo.grass";
        if (lib == "!math3d!" || lib == "!Math3D!") return "stdlib/math3d.grass";
        if (lib == "!2d!" || lib == "!2D!" || lib == "!gfx2d!" || lib == "!Gfx2D!" ||
            lib == "!2DGraphics!" || lib == "!2dgraphics!") return "stdlib/gfx2d.grass";
        if (lib == "!gfx3d!" || lib == "!Gfx3D!" || lib == "!3d!" || lib == "!3D!") return "stdlib/gfx3d.grass";
        if (lib == "!physics!" || lib == "!Physics!") return "stdlib/physics.grass";
        if (lib == "!baremetal!" || lib == "!bm!" || lib == "!kernel!" || lib == "!BareMetal!") return "stdlib/baremetal.grass";
        return "";
    }

    static bool isBuiltinEngineDirective(const std::string& lib) {
        if (lib == "!GrassGreen!" || !getStdlibModuleForDirective(lib).empty()) return true;
        static const char* const kEngineLibs[] = {
            "!interface!", "!disp!", "!Disp!", "!gui!", "!GrassGUI!",
            "!database!", "!gdb!", "!audio!", "!sound!", "!GrassAudio!",
            "!tg!", "!json!", "!hardware!", "!hw!", "!sysinfo!", "!device!",
            "!proc!", "!random!", "!vd+!", "!VD+!", "!vd!", "!VD!",
            "!asm!", "!GrassASM!", "!assembler!", "!keyboard!", "!kbd!",
            "!ps2!", "!PS2Keyboard!", "!no_window!", "!consl_window!",
            "!GrassWeb!", "!GREEN_WWW1!", "!langgen!", "!LANGGEN!", "!lang!",
            "!ffi!", "!FFI!"
        };
        for (const char* el : kEngineLibs) {
            if (lib == el) return true;
        }
        return false;
    }

    static std::string getExeDir() {
#ifdef _WIN32
        char exe_buf[MAX_PATH];
        DWORD exe_len = GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
        if (exe_len > 0) {
            return fs::path(exe_buf).parent_path().string();
        }
#else
        char buf[1024];
        ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            return fs::path(buf).parent_path().string();
        }
#endif
        return "";
    }

    // Поиск файла модуля в приоритетном порядке:
    // 1. Папка текущего файла и папки родительских импортирующих файлов
    // 2. stdlib/ рядом с grassc.exe / grg.exe
    // 3. C:\GrassGreen\stdlib\
    // 4. Резервные пути относительно рабочей директории
    static std::string findModuleFile(const std::string& current_file,
                                      const std::vector<std::string>& parent_dirs,
                                      const std::string& raw_target) {
        std::string target = raw_target;
        if (target.size() >= 2 && ((target.front() == '"' && target.back() == '"') ||
                                   (target.front() == '\'' && target.back() == '\''))) {
            target = target.substr(1, target.size() - 2);
        }
        if (target.empty()) return "";

        std::vector<std::string> candidates;
        auto push_dir_candidates = [&](const fs::path& dir) {
            if (dir.empty()) return;
            candidates.push_back((dir / target).string());
            if (fs::path(target).extension().empty()) {
                candidates.push_back((dir / (target + ".grass")).string());
            }
            candidates.push_back((dir / "modules" / target).string());
            if (fs::path(target).extension().empty()) {
                candidates.push_back((dir / "modules" / (target + ".grass")).string());
            }
        };

        // 1. Папка текущего компилируемого .grass-файла и родительских импортирующих файлов
        if (!current_file.empty()) {
            push_dir_candidates(fs::path(current_file).parent_path());
        }
        for (auto it = parent_dirs.rbegin(); it != parent_dirs.rend(); ++it) {
            push_dir_candidates(fs::path(*it));
        }

        // 2. stdlib/ рядом с исполняемым файлом (grassc.exe / grg.exe)
        std::string exe_dir_str = getExeDir();
        if (!exe_dir_str.empty()) {
            fs::path exe_dir(exe_dir_str);
            candidates.push_back((exe_dir / "stdlib" / target).string());
            candidates.push_back((exe_dir / "stdlib" / (target + ".grass")).string());
            candidates.push_back((exe_dir / ".." / "stdlib" / target).string());
            candidates.push_back((exe_dir / ".." / "stdlib" / (target + ".grass")).string());
            candidates.push_back((exe_dir / target).string());
            candidates.push_back((exe_dir / (target + ".grass")).string());
        }

        // 3. Глобальная директория стандартной библиотеки C:\GrassGreen\stdlib\ (или stdlib/)
#ifdef _WIN32
        candidates.push_back((fs::path("C:/GrassGreen/stdlib") / target).string());
        candidates.push_back((fs::path("C:/GrassGreen/stdlib") / (target + ".grass")).string());
#endif
        candidates.push_back("stdlib/" + target);
        candidates.push_back("stdlib/" + target + ".grass");
        candidates.push_back("../stdlib/" + target);
        candidates.push_back("../stdlib/" + target + ".grass");
        candidates.push_back("../../stdlib/" + target);
        candidates.push_back("../../stdlib/" + target + ".grass");

        // 4. Относительно текущей рабочей директории и служебных каталогов
        candidates.push_back(target);
        candidates.push_back(target + ".grass");
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
            if (lower.find("/windows/") != std::string::npos || lower.find("/system32/") != std::string::npos) {
                continue;
            }
            if (fileExists(norm)) {
                return norm;
            }
        }
        return "";
    }

    static bool isStdlibCanonPath(const std::string& canon_lower) {
        static const char* const kStdFiles[] = {
            "stdlib/grassgreen.grass", "stdlib/io.grass", "stdlib/fsys.grass",
            "stdlib/time.grass", "stdlib/memory.grass", "stdlib/string.grass",
            "stdlib/math.grass", "stdlib/sys.grass", "stdlib/net.grass",
            "stdlib/thread.grass", "stdlib/algo.grass", "stdlib/math3d.grass",
            "stdlib/gfx2d.grass", "stdlib/gfx3d.grass", "stdlib/physics.grass",
            "stdlib/baremetal.grass"
        };
        for (const char* sf : kStdFiles) {
            if (canon_lower.find(sf) != std::string::npos) return true;
        }
        return false;
    }

    static void rewriteAliasQualifiers(std::vector<GrassToken>& toks,
                                       const std::unordered_set<std::string>& aliases) {
        if (aliases.empty()) return;
        std::vector<GrassToken> out;
        out.reserve(toks.size());
        for (size_t i = 0; i < toks.size(); ++i) {
            if (i + 2 < toks.size() &&
                toks[i].kind == TOK_IDENT &&
                aliases.count(toks[i].text) &&
                (toks[i + 1].kind == TOK_COLON_COLON || toks[i + 1].kind == TOK_DOT)) {
                i += 1; // skip alias and '::' / '.', leaving target identifier
                continue;
            }
            out.push_back(std::move(toks[i]));
        }
        toks = std::move(out);
    }

    static std::vector<GrassToken> resolveInternal(
        const std::vector<GrassToken>& tokens,
        const std::string& current_filepath,
        std::vector<std::string>& parent_dirs,
        std::unordered_set<std::string>& visited,
        std::unordered_set<std::string>& emitted_libs,
        ImportStats& stats,
        bool is_root,
        int depth = 0
    ) {
        if (depth > 64) {
            std::cerr << "[GrassImport Error]: Превышена максимальная глубина цепочки импортов (64)!\n";
            stats.has_error = true;
            return tokens;
        }

        std::string cur_dir = current_filepath.empty() ? "" : fs::path(current_filepath).parent_path().string();
        bool pushed_dir = false;
        if (!cur_dir.empty()) {
            parent_dirs.push_back(cur_dir);
            pushed_dir = true;
        }

        auto loadAndSplice = [&](const std::string& target_name, int import_line,
                                 const std::string& source_file,
                                 std::vector<GrassToken>& out_stream) -> bool {
            std::string resolved = findModuleFile(current_filepath, parent_dirs, target_name);
            if (resolved.empty()) {
                std::cerr << "[GrassImport Error]: Файл модуля '" << target_name
                          << "' не найден (импорт из " << source_file
                          << " на строке " << import_line << ")!\n";
                stats.has_error = true;
                return false;
            }
            std::string canon_key = getCanonicalKey(resolved);
            if (visited.count(canon_key)) {
                return true; // Include guard: already imported
            }
            visited.insert(canon_key);
            stats.total_modules_imported++;

            std::string mod_src = readFileContent(resolved);
            if (mod_src.empty()) {
                return true;
            }

            Lexer mod_lexer(mod_src, resolved);
            std::vector<GrassToken> mod_tokens = mod_lexer.tokenize();
            mod_tokens = resolveInternal(mod_tokens, resolved, parent_dirs, visited, emitted_libs, stats, false, depth + 1);

            mod_tokens.erase(
                std::remove_if(mod_tokens.begin(), mod_tokens.end(), [](const GrassToken& t) {
                    return t.kind == TOK_EOF;
                }),
                mod_tokens.end()
            );

            for (size_t k = 0; k < mod_tokens.size(); ++k) {
                if (mod_tokens[k].kind == TOK_IDENT && mod_tokens[k].text == "main" &&
                    k > 0 && mod_tokens[k - 1].kind == TOK_IDENT &&
                    (mod_tokens[k - 1].text == "int" || mod_tokens[k - 1].text == "void" || mod_tokens[k - 1].text == "take") &&
                    k + 1 < mod_tokens.size() && mod_tokens[k + 1].kind == TOK_LPAREN) {
                    mod_tokens[k].text = "__mod_main_" + std::to_string(stats.total_modules_imported);
                }
            }

            out_stream.insert(out_stream.end(), mod_tokens.begin(), mod_tokens.end());
            return true;
        };

        std::vector<GrassToken> result;
        result.reserve(tokens.size() * 2);
        std::unordered_set<std::string> local_aliases;

        size_t i = 0;
        while (i < tokens.size()) {
            // 1. Директива импорта в формате !ModuleName!
            if (tokens[i].kind == TOK_LIB) {
                const GrassToken& ltok = tokens[i];
                const std::string& lib = ltok.text;
                std::string mod_name = (lib.size() >= 2) ? lib.substr(1, lib.size() - 2) : lib;
                std::string err_file = ltok.file.empty() ? current_filepath : ltok.file;

                // Проверяем: если локальный пользовательский модуль (например !Token!, !TokenType!, !Lexer!)
                // не входит во встроенные директивы движка -> ищем и подключаем <ModuleName>.grass
                if (!isBuiltinEngineDirective(lib)) {
                    i++;
                    loadAndSplice(mod_name, ltok.line, err_file, result);
                    continue;
                }

                // Встроенный модуль: сохраняем первую директиву !...! в потоке,
                // а также при необходимости подгружаем stdlib/<name>.grass с include-guard
                if (!emitted_libs.count(lib)) {
                    emitted_libs.insert(lib);
                    result.push_back(ltok);
                    if (lib == "!GrassGreen!") {
                        static const char* const kCoreMods[] = {
                            "stdlib/GrassGreen.grass", "stdlib/io.grass", "stdlib/fsys.grass",
                            "stdlib/time.grass", "stdlib/memory.grass", "stdlib/string.grass",
                            "stdlib/math.grass", "stdlib/sys.grass", "stdlib/net.grass",
                            "stdlib/thread.grass", "stdlib/algo.grass", "stdlib/math3d.grass",
                            "stdlib/gfx2d.grass", "stdlib/gfx3d.grass", "stdlib/physics.grass"
                        };
                        for (const char* cm : kCoreMods) {
                            if (!findModuleFile(current_filepath, parent_dirs, cm).empty()) {
                                loadAndSplice(cm, ltok.line, err_file, result);
                            }
                        }
                    } else {
                        std::string mapped = getStdlibModuleForDirective(lib);
                        if (!mapped.empty() && !findModuleFile(current_filepath, parent_dirs, mapped).empty()) {
                            loadAndSplice(mapped, ltok.line, err_file, result);
                        }
                    }
                }
                i++;
                continue;
            }

            // 2. Синтаксис: import "module.grass" [as alias]; или include <module>
            if (tokens[i].kind == TOK_IDENT &&
                (tokens[i].text == "import" || tokens[i].text == "include")) {
                std::string target_name;
                int import_line = tokens[i].line;
                std::string err_file = tokens[i].file.empty() ? current_filepath : tokens[i].file;
                bool matched_import = false;

                if (i + 1 < tokens.size() &&
                    (tokens[i + 1].kind == TOK_STRING || tokens[i + 1].kind == TOK_IDENT)) {
                    target_name = tokens[i + 1].text;
                    i += 2;
                    matched_import = true;
                } else if (i + 2 < tokens.size() && tokens[i + 1].kind == TOK_LT) {
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
                    if (i + 1 < tokens.size() &&
                        (tokens[i].kind == TOK_AS || (tokens[i].kind == TOK_IDENT && tokens[i].text == "as")) &&
                        tokens[i + 1].kind == TOK_IDENT) {
                        local_aliases.insert(tokens[i + 1].text);
                        i += 2;
                    }
                    if (i < tokens.size() && tokens[i].kind == TOK_SEMI) {
                        i++;
                    }
                    loadAndSplice(target_name, import_line, err_file, result);
                    continue;
                }
            }

            if (!is_root && tokens[i].kind == TOK_EOF) {
                i++;
                continue;
            }

            result.push_back(tokens[i]);
            i++;
        }

        if (pushed_dir) {
            parent_dirs.pop_back();
        }
        rewriteAliasQualifiers(result, local_aliases);
        return result;
    }

    std::vector<GrassToken> resolveImports(
        const std::vector<GrassToken>& root_tokens,
        const std::string& root_filepath,
        ImportStats* out_stats
    ) {
        ImportStats stats;
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> emitted_libs;
        std::vector<std::string> parent_dirs;

        std::string root_canon = getCanonicalKey(root_filepath);
        if (!root_canon.empty()) {
            visited.insert(root_canon);
        }

        std::vector<GrassToken> tokens_copy = root_tokens;
        if (!root_filepath.empty()) {
            for (auto& t : tokens_copy) {
                if (t.file.empty()) t.file = root_filepath;
            }
        }

        // Prevent stdlib files from recursively self-including all core modules
        if (isStdlibCanonPath(root_canon)) {
            emitted_libs.insert("!GrassGreen!");
        }

        std::vector<GrassToken> resolved = resolveInternal(
            tokens_copy, root_filepath, parent_dirs, visited, emitted_libs, stats, true, 0);

        while (!resolved.empty() && resolved.back().kind == TOK_EOF) {
            resolved.pop_back();
        }
        int last_line = resolved.empty() ? 1 : resolved.back().line;
        std::string last_file = resolved.empty() ? root_filepath : resolved.back().file;
        resolved.push_back(GrassToken(TOK_EOF, "", 0.0, last_line, 1, last_file));

        stats.total_tokens_after_import = resolved.size();
        if (out_stats) *out_stats = stats;
        return resolved;
    }

} // namespace GrassImport

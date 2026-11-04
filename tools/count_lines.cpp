// =============================================================================
//  GrassGreen 4.0 — Project Line Counter Utility (count_lines.cpp) [FIXED]
// =============================================================================

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <unordered_set>

namespace fs = std::filesystem;

struct ExtStats {
    size_t file_count = 0;
    size_t total_lines = 0;
    size_t blank_lines = 0;
    size_t code_lines = 0;
};

struct FileInfo {
    std::string path;
    size_t lines = 0;
    std::string ext;
};

bool isIgnoredDirectory(const std::string& name) {
    static const std::unordered_set<std::string> ignored_dirs = {
        ".git", "node_modules", ".cache", ".vscode", "build", "bin", "obj"
    };
    return ignored_dirs.contains(name);
}

bool isBinaryOrIgnoredFile(const fs::path& p) {
    std::string filename = p.filename().string();
    if (filename == "payload_data.h") return true;

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const std::unordered_set<std::string> ignored_exts = {
        ".exe", ".ico", ".png", ".jpg", ".jpeg", ".vsix",
        ".o", ".obj", ".gdb", ".bin", ".a", ".lib", ".dll", ".so", ".dylib"
    };

    return ignored_exts.contains(ext);
}

int main(int argc, char* argv[]) {
    fs::path root = ".";
    if (argc > 1) {
        root = argv[1];
    }

    std::map<std::string, ExtStats> stats;
    std::vector<FileInfo> all_files;
    size_t grand_total_lines = 0;
    size_t grand_total_blank = 0;
    size_t grand_total_code = 0;
    size_t grand_total_files = 0;

    std::error_code ec;
    auto it = fs::recursive_directory_iterator(
        root, 
        fs::directory_options::skip_permission_denied, 
        ec
    );

    for (auto end = fs::recursive_directory_iterator(); it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        const auto& entry = *it;

        // Отсекаем служебные тяжелые папки на корню, не углубляясь внутрь
        if (entry.is_directory()) {
            if (isIgnoredDirectory(entry.path().filename().string())) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!entry.is_regular_file()) continue;

        const fs::path& p = entry.path();
        if (isBinaryOrIgnoredFile(p)) continue;

        std::string ext = p.extension().string();
        if (ext.empty()) ext = "[no ext]";
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Открываем с binary, чтобы не спотыкаться о ^Z (0x1A) на Windows
        std::ifstream file(p, std::ios::binary);
        if (!file.is_open()) continue;

        size_t lines = 0;
        size_t blank = 0;
        std::string line;

        while (std::getline(file, line)) {
            lines++;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            bool is_blank = true;
            for (char c : line) {
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    is_blank = false;
                    break;
                }
            }
            if (is_blank) blank++;
        }

        size_t code = (lines >= blank) ? (lines - blank) : 0;

        ExtStats& es = stats[ext];
        es.file_count++;
        es.total_lines += lines;
        es.blank_lines += blank;
        es.code_lines += code;

        grand_total_files++;
        grand_total_lines += lines;
        grand_total_blank += blank;
        grand_total_code += code;

        all_files.push_back({ p.lexically_normal().string(), lines, ext });
    }

    std::vector<std::pair<std::string, ExtStats>> sorted_ext(stats.begin(), stats.end());
    std::sort(sorted_ext.begin(), sorted_ext.end(), [](const auto& a, const auto& b) {
        return a.second.total_lines > b.second.total_lines;
    });

    std::sort(all_files.begin(), all_files.end(), [](const auto& a, const auto& b) {
        return a.lines > b.lines;
    });

    std::cout << "\n";
    std::cout << "========================================================================================\n";
    std::cout << "           СТАТИСТИКА КОДА ПРОЕКТА GRASSGREEN 4.0 (G&G CODEBASE METRICS)               \n";
    std::cout << "========================================================================================\n";
    std::cout << std::left
              << std::setw(14) << "Расширение"
              << std::setw(12) << "Файлов"
              << std::setw(16) << "Всего строк"
              << std::setw(16) << "Чистый код"
              << std::setw(14) << "Пустые строки"
              << "Доля (%)\n";
    std::cout << "----------------------------------------------------------------------------------------\n";

    for (const auto& [ext, st] : sorted_ext) {
        double pct = (grand_total_lines > 0) ? (st.total_lines * 100.0 / grand_total_lines) : 0.0;
        std::cout << std::left
                  << std::setw(14) << ext
                  << std::setw(12) << st.file_count
                  << std::setw(16) << st.total_lines
                  << std::setw(16) << st.code_lines
                  << std::setw(14) << st.blank_lines
                  << std::fixed << std::setprecision(1) << pct << " %\n";
    }

    std::cout << "========================================================================================\n";
    std::cout << std::left
              << std::setw(14) << "ИТОГО:"
              << std::setw(12) << grand_total_files
              << std::setw(16) << grand_total_lines
              << std::setw(16) << grand_total_code
              << std::setw(14) << grand_total_blank
              << "100.0 %\n";
    std::cout << "========================================================================================\n\n";

    std::cout << "--- Топ-10 крупнейших исходных файлов проекта ---\n";
    size_t top_n = std::min((size_t)10, all_files.size());
    for (size_t i = 0; i < top_n; ++i) {
        std::cout << "  " << std::setw(2) << (i + 1) << ". "
                  << std::setw(50) << std::left << all_files[i].path
                  << " : " << all_files[i].lines << " строк\n";
    }
    std::cout << "\n";

    return 0;
}
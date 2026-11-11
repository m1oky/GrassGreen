#include "GrassFsys.h"
#include "GrassHAL.h"
#include <filesystem>
#include <sstream>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

static bool is_dangerous_system_path(const std::string& path_str) {
    if (path_str.empty()) return true;
    std::string s = path_str;
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    if (s.empty() || s == "." || s == "..") return true;
    if (s.size() <= 2 && s.find(':') != std::string::npos) return true;
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "c:" || lower == "c:/" || lower == "c:\\" || lower == "/" || lower == "\\") return true;
    if (lower.find("windows") != std::string::npos || lower.find("system32") != std::string::npos ||
        lower.rfind("/etc", 0) == 0 || lower.rfind("/bin", 0) == 0 ||
        lower.rfind("/usr", 0) == 0 || lower.rfind("/sbin", 0) == 0 ||
        lower.rfind("/sys", 0) == 0 || lower.rfind("/proc", 0) == 0 ||
        lower.rfind("/root", 0) == 0) {
        return true;
    }
    return false;
}

static bool is_safe_fsys_path(const std::string& path_str) {
    if (path_str.empty()) return false;
    // SEC-03: Блокировка попыток выхода наружу через '..'
    if (path_str.find("..") != std::string::npos) return false;
    if (GrassHAL::is_freestanding()) return true;

    try {
        std::error_code ec;
        fs::path cur_root = fs::current_path(ec);
        if (ec) return false;

        fs::path target(path_str);
        fs::path resolved = target.is_absolute() ? fs::weakly_canonical(target, ec)
                                                : fs::weakly_canonical(cur_root / target, ec);
        if (ec) return false;

        std::string root_s = cur_root.lexically_normal().string();
        std::string res_s = resolved.lexically_normal().string();

        #if defined(_WIN32)
        std::transform(root_s.begin(), root_s.end(), root_s.begin(), ::tolower);
        std::transform(res_s.begin(), res_s.end(), res_s.begin(), ::tolower);
        #endif

        if (res_s.rfind(root_s, 0) != 0) return false;
        return !is_dangerous_system_path(res_s);
    } catch (...) {
        return false;
    }
}

bool GrassFsys::mkdir(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_mkdir(path);
    }
    if (!is_safe_fsys_path(path)) return false;
    std::error_code ec;
    return fs::create_directories(path, ec);
}

bool GrassFsys::is_dir(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_is_dir(path);
    }
    if (!is_safe_fsys_path(path)) return false;
    std::error_code ec;
    return fs::is_directory(path, ec);
}

int64_t GrassFsys::size(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_size(path);
    }
    if (!is_safe_fsys_path(path)) return 0;
    std::error_code ec;
    if (!fs::exists(path, ec) || fs::is_directory(path, ec)) return 0;
    auto sz = fs::file_size(path, ec);
    if (ec) return 0;
    return static_cast<int64_t>(sz);
}

bool GrassFsys::copy(const std::string& src, const std::string& dst) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_copy(src, dst);
    }
    if (!is_safe_fsys_path(src) || !is_safe_fsys_path(dst)) return false;
    std::error_code ec;
    return fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

std::string GrassFsys::list(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_list(path);
    }
    if (!is_safe_fsys_path(path)) return "";
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) return "";
    std::ostringstream ss;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        ss << entry.path().filename().string() << (entry.is_directory() ? "/" : "") << "\n";
    }
    return ss.str();
}

std::string GrassFsys::read(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_read(path);
    }
    if (!is_safe_fsys_path(path)) return "";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool GrassFsys::write(const std::string& path, const std::string& content) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_write(path, content);
    }
    if (!is_safe_fsys_path(path)) return false;
    std::error_code ec;
    auto parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f << content;
    return true;
}

bool GrassFsys::exists(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_exists(path);
    }
    if (!is_safe_fsys_path(path)) return false;
    std::error_code ec;
    return fs::exists(path, ec);
}

bool GrassFsys::remove(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_remove(path);
    }
    if (!is_safe_fsys_path(path)) return false;
    std::error_code ec;
    return fs::remove_all(path, ec) > 0;
}

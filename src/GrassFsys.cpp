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
        lower == "/etc" || lower == "/bin" || lower == "/usr" || lower == "/sbin") {
        return true;
    }
    return false;
}

bool GrassFsys::mkdir(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_mkdir(path);
    }
    std::error_code ec;
    return fs::create_directories(path, ec);
}

bool GrassFsys::is_dir(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_is_dir(path);
    }
    std::error_code ec;
    return fs::is_directory(path, ec);
}

int64_t GrassFsys::size(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_size(path);
    }
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
    std::error_code ec;
    return fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

std::string GrassFsys::list(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_list(path);
    }
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
    if (is_dangerous_system_path(path)) return false;
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
    std::error_code ec;
    return fs::exists(path, ec);
}

bool GrassFsys::remove(const std::string& path) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::vfs_remove(path);
    }
    if (is_dangerous_system_path(path)) return false;
    std::error_code ec;
    return fs::remove_all(path, ec) > 0;
}

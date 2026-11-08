#include "GrassFsys.h"
#include "GrassHAL.h"
#include <filesystem>
#include <sstream>
#include <fstream>

namespace fs = std::filesystem;

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
    std::error_code ec;
    return fs::remove_all(path, ec) > 0;
}

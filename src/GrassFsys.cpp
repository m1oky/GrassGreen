#include "GrassFsys.h"
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

bool GrassFsys::mkdir(const std::string& path) {
    std::error_code ec;
    return fs::create_directories(path, ec);
}

bool GrassFsys::is_dir(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

int64_t GrassFsys::size(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec) || fs::is_directory(path, ec)) return 0;
    auto sz = fs::file_size(path, ec);
    if (ec) return 0;
    return static_cast<int64_t>(sz);
}

bool GrassFsys::copy(const std::string& src, const std::string& dst) {
    std::error_code ec;
    return fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

std::string GrassFsys::list(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) return "";
    std::ostringstream ss;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        ss << entry.path().filename().string() << (entry.is_directory() ? "/" : "") << "\n";
    }
    return ss.str();
}
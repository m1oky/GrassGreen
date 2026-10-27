#ifndef GRASS_FSYS_H
#define GRASS_FSYS_H

#include <string>
#include <cstdint>

class GrassFsys {
public:
    static bool mkdir(const std::string& path);
    static bool is_dir(const std::string& path);
    static int64_t size(const std::string& path);
    static bool copy(const std::string& src, const std::string& dst);
    static std::string list(const std::string& path);
};

#endif // GRASS_FSYS_H
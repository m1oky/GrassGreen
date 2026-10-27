#ifndef GRASS_PROC_H
#define GRASS_PROC_H

#include <string>
#include <cstdint>

class GrassProc {
public:
    static int64_t find(const std::string& name);
    static bool kill(int64_t pid);
    static int64_t mem(int64_t pid);
    static bool exists(int64_t pid);
    static std::string list();
};

#endif // GRASS_PROC_H
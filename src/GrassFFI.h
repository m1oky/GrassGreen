#ifndef GRASS_FFI_H
#define GRASS_FFI_H

#include <string>
#include <vector>
#include <cstdint>
#include "GrassSyntax.h"

namespace GrassFFI {
    uint64_t open(const std::string& lib_name);
    uint64_t sym(uint64_t handle, const std::string& func_name);
    uint64_t call(uint64_t func_ptr, const std::vector<GrassValue>& args);
    bool close(uint64_t handle);
}

#endif // GRASS_FFI_H

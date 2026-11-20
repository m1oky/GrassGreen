#include "GrassFFI.h"
#include <iostream>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace GrassFFI {

    uint64_t open(const std::string& lib_name) {
#if defined(_WIN32)
        HMODULE h = LoadLibraryA(lib_name.c_str());
        return (uint64_t)(uintptr_t)h;
#else
        void* h = dlopen(lib_name.c_str(), RTLD_NOW | RTLD_GLOBAL);
        return (uint64_t)(uintptr_t)h;
#endif
    }

    uint64_t sym(uint64_t handle, const std::string& func_name) {
        if (!handle) return 0;
#if defined(_WIN32)
        FARPROC p = GetProcAddress((HMODULE)(uintptr_t)handle, func_name.c_str());
        return (uint64_t)(uintptr_t)p;
#else
        void* p = dlsym((void*)(uintptr_t)handle, func_name.c_str());
        return (uint64_t)(uintptr_t)p;
#endif
    }

    bool close(uint64_t handle) {
        if (!handle) return false;
#if defined(_WIN32)
        return FreeLibrary((HMODULE)(uintptr_t)handle) != 0;
#else
        return dlclose((void*)(uintptr_t)handle) == 0;
#endif
    }

    typedef uintptr_t (*Fn0)();
    typedef uintptr_t (*Fn1)(uintptr_t);
    typedef uintptr_t (*Fn2)(uintptr_t, uintptr_t);
    typedef uintptr_t (*Fn3)(uintptr_t, uintptr_t, uintptr_t);
    typedef uintptr_t (*Fn4)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    typedef uintptr_t (*Fn5)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    typedef uintptr_t (*Fn6)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    typedef uintptr_t (*Fn7)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    typedef uintptr_t (*Fn8)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    typedef uintptr_t (*Fn9)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    typedef uintptr_t (*Fn10)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

    uint64_t call(uint64_t func_ptr, const std::vector<GrassValue>& args) {
        if (!func_ptr) return 0;

        // Временные буферы для передачи массивов байтов
        std::vector<std::vector<uint8_t>> temp_buffers;
        uintptr_t raw[12] = {0};

        size_t count = args.size();
        if (count > 10) count = 10;

        for (size_t i = 0; i < count; ++i) {
            const GrassValue& gv = args[i];
            if (gv.is_str()) {
                raw[i] = (uintptr_t)gv.asCStr();
            } else if (gv.is_array()) {
                GrassArray* arr = gv.asArray();
                if (arr) {
                    temp_buffers.emplace_back();
                    auto& buf = temp_buffers.back();
                    buf.reserve(arr->elements.size());
                    for (const auto& el : arr->elements) {
                        buf.push_back((uint8_t)(uint64_t)el.toNumber());
                    }
                    raw[i] = (uintptr_t)buf.data();
                } else {
                    raw[i] = 0;
                }
            } else if (gv.is_bool()) {
                raw[i] = gv.isTruthy() ? 1 : 0;
            } else if (gv.is_nil()) {
                raw[i] = 0;
            } else {
                raw[i] = (uintptr_t)(int64_t)gv.toNumber();
            }
        }

        uintptr_t ret = 0;
        switch (count) {
            case 0:  ret = ((Fn0)func_ptr)(); break;
            case 1:  ret = ((Fn1)func_ptr)(raw[0]); break;
            case 2:  ret = ((Fn2)func_ptr)(raw[0], raw[1]); break;
            case 3:  ret = ((Fn3)func_ptr)(raw[0], raw[1], raw[2]); break;
            case 4:  ret = ((Fn4)func_ptr)(raw[0], raw[1], raw[2], raw[3]); break;
            case 5:  ret = ((Fn5)func_ptr)(raw[0], raw[1], raw[2], raw[3], raw[4]); break;
            case 6:  ret = ((Fn6)func_ptr)(raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]); break;
            case 7:  ret = ((Fn7)func_ptr)(raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6]); break;
            case 8:  ret = ((Fn8)func_ptr)(raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]); break;
            case 9:  ret = ((Fn9)func_ptr)(raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8]); break;
            case 10: ret = ((Fn10)func_ptr)(raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9]); break;
        }

        return (uint64_t)ret;
    }
}

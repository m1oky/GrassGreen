#pragma once
#include "native/GrassObjWriter.h"
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace grass::backend {

    struct Win64RuntimeCtx {
        CompiledUnit& unit;
        std::unordered_set<std::string>& defined_symbols;
        const std::unordered_set<std::string>& needed_symbols;
        bool& any_injected;

        void add_rodata_str(const std::string& sym_name, const std::string& text);
        void add_data_buffer(const std::string& sym_name, size_t size_bytes);
        void add_bss_buffer(const std::string& sym_name, size_t size_bytes);
        void append_function(const std::string& name,
                             const std::vector<uint8_t>& code,
                             const std::vector<RelocEntry>& rels);
    };

    void inject_win64_rt_io(Win64RuntimeCtx& ctx);
    void inject_win64_rt_sys(Win64RuntimeCtx& ctx);

} // namespace grass::backend

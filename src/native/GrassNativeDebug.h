#pragma once
#ifndef GRASS_NATIVE_DEBUG_H
#define GRASS_NATIVE_DEBUG_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

namespace grass::debug {

    constexpr uint32_t GRASS_DEBUG_MAGIC = 0x47524442; // 'GRDB'
    constexpr uint32_t GRASS_DEBUG_VERSION = 1;

#pragma pack(push, 1)

    struct GrassDebugLineEntry {
        uint32_t code_start;
        uint32_t code_end;
        uint32_t file_offset;
        uint32_t func_offset;
        uint32_t line;
        uint32_t col;
    };

    struct GrassDebugHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t entry_count;
        uint32_t strings_size;
        uint64_t text_base_rva;
        uint64_t text_size;
    };

#pragma pack(pop)

    struct GrassDebugLocation {
        std::string file;
        std::string function;
        uint32_t line = 0;
        uint32_t col = 0;
        uint64_t rip = 0;
        bool found = false;
    };

    class GrassDebugTableBuilder {
    public:
        GrassDebugTableBuilder();

        void set_text_bounds(uint64_t text_base_rva, uint64_t text_size);
        void add_entry(uint32_t code_start, uint32_t code_end,
                       const std::string& file, const std::string& func,
                       uint32_t line, uint32_t col);

        std::vector<uint8_t> serialize() const;
        void clear();

    private:
        uint64_t text_base_rva_ = 0x1000;
        uint64_t text_size_ = 0;
        std::vector<GrassDebugLineEntry> entries_;
        std::vector<char> string_pool_;
        std::unordered_map<std::string, uint32_t> string_map_;

        uint32_t get_or_add_string(const std::string& str);
    };

    // Symbol resolution helper
    bool lookup_debug_location(const void* table_data, size_t table_size,
                               uint64_t rip, uint64_t module_base,
                               GrassDebugLocation& out_loc);

    // Native crash handler setup and control
    void install_crash_handler(const void* debug_table = nullptr);
    void uninstall_crash_handler();
    void set_active_debug_table(const void* debug_table);

    // Formatted stack unwinder from raw context
    void print_stack_trace_from_context(void* native_context, const void* debug_table = nullptr);

    // Generates embeddable standalone C runtime crash handler source
    std::string generate_runtime_crash_handler_c();

} // namespace grass::debug

#endif // GRASS_NATIVE_DEBUG_H

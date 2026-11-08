// =============================================================================
//  GrassGreen 4.3.0 (Pre-release) — Evolution of the Bare-Metal (!baremetal!)
//  Universal colors, Smooth cursor, read_key(), TUI widgets, ACPI poweroff,
//  reboot(), Virtual & Physical RamFS/VFS, terminal commands, not_clear/not_show.
// =============================================================================
#ifndef GRASS_BAREMETAL_H
#define GRASS_BAREMETAL_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace GrassBaremetal {

    // ── 1. VGA 16 Color Constants ──
    constexpr uint8_t COLOR_BLACK         = 0;
    constexpr uint8_t COLOR_BLUE          = 1;
    constexpr uint8_t COLOR_GREEN         = 2;
    constexpr uint8_t COLOR_CYAN          = 3;
    constexpr uint8_t COLOR_RED           = 4;
    constexpr uint8_t COLOR_MAGENTA       = 5;
    constexpr uint8_t COLOR_BROWN         = 6;
    constexpr uint8_t COLOR_LIGHT_GRAY    = 7;
    constexpr uint8_t COLOR_DARK_GRAY     = 8;
    constexpr uint8_t COLOR_LIGHT_BLUE    = 9;
    constexpr uint8_t COLOR_LIGHT_GREEN   = 10;
    constexpr uint8_t COLOR_LIGHT_CYAN    = 11;
    constexpr uint8_t COLOR_LIGHT_RED     = 12;
    constexpr uint8_t COLOR_LIGHT_MAGENTA = 13;
    constexpr uint8_t COLOR_YELLOW        = 14;
    constexpr uint8_t COLOR_WHITE         = 15;

    // ── Color API ──
    uint8_t parse_color(const std::string& str_or_hex);
    uint8_t hex_to_vga(const std::string& hex);
    void set_color(uint8_t fg, uint8_t bg = 0);
    void set_color_hex(const std::string& hex_fg, const std::string& hex_bg = "#000000");
    uint8_t get_current_attr();

    // ── 2. Smooth Cursor System ──
    void init_smooth_cursor();
    void tick_smooth_cursor();

    // ── 3. High-Level Keyboard Input: read_key() ──
    std::string read_key();

    // ── 4. Built-in TUI Widgets ──
    void draw_box(int x, int y, int w, int h, const std::string& title = "", int fg = COLOR_LIGHT_GREEN, int bg = COLOR_BLACK);
    int menu(const std::string& title, const std::vector<std::string>& options);
    void progress_bar(int x, int y, int width, int percent, int fg = COLOR_LIGHT_GREEN, int bg = COLOR_BLACK);

    // ── 5. Hardware Power Management ──
    void reboot();
    void poweroff();

    // ── 6. Virtual & Physical VFS (RamFS) Structure ──
    struct VFSNode {
        std::string name;
        bool is_dir = false;
        bool hidden = false;
        std::string content;
        std::vector<std::shared_ptr<VFSNode>> children;
        std::weak_ptr<VFSNode> parent;
    };

    void vfs_init();
    bool vfs_has_struct();
    void vfs_set_has_struct(bool b);
    bool vfs_physical_struct(const std::string& host_path);
    std::string vfs_ls(const std::string& flags = "");
    std::string vfs_pwd();
    bool vfs_cd(const std::string& path);
    bool vfs_copy(const std::string& src, const std::string& dst);
    bool vfs_move(const std::string& src, const std::string& dst);
    bool vfs_delete(const std::string& path);
    bool vfs_cr_folder(const std::string& name);
    bool vfs_cr_file(const std::string& name, const std::string& content = "");
    bool vfs_not_show(const std::string& name);
    void vfs_specSign_show(const std::string& flag);
    std::string vfs_read_file(const std::string& path);

    // ── 7. Screen Clearing & Protection ──
    void clear();
    void not_clear_on();
    void not_clear_off();
}

#endif // GRASS_BAREMETAL_H

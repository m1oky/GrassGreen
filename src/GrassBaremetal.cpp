// =============================================================================
//  GrassGreen 4.3.0 (Pre-release) — Evolution of the Bare-Metal (!baremetal!)
//  Implementation of Universal colors, Smooth cursor, read_key(), TUI widgets,
//  Power management, RamFS/VFS, terminal commands, not_clear/not_show.
// =============================================================================

#include "GrassBaremetal.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <cctype>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace fs = std::filesystem;

namespace GrassBaremetal {

    static uint8_t g_current_fg = COLOR_LIGHT_GREEN;
    static uint8_t g_current_bg = COLOR_BLACK;
    static bool g_not_clear_active = false;
    static std::vector<std::string> g_protected_lines;
    static bool g_has_vfs_structure = false;
    static bool g_show_hidden_files = false;

    // VGA 16 Palette RGB Reference
    static const uint8_t VGA_PALETTE[16][3] = {
        {0,   0,   0},   // 0 Black
        {0,   0,   170}, // 1 Blue
        {0,   170, 0},   // 2 Green
        {0,   170, 170}, // 3 Cyan
        {170, 0,   0},   // 4 Red
        {170, 0,   170}, // 5 Magenta
        {170, 85,  0},   // 6 Brown
        {170, 170, 170}, // 7 Light Gray
        {85,  85,  85},  // 8 Dark Gray
        {85,  85,  255}, // 9 Light Blue
        {85,  255, 85},  // 10 Light Green
        {85,  255, 255}, // 11 Light Cyan
        {255, 85,  85},  // 12 Light Red
        {255, 85,  255}, // 13 Light Magenta
        {255, 255, 85},  // 14 Yellow
        {255, 255, 255}  // 15 White
    };

    static std::string to_lower_str(std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    uint8_t hex_to_vga(const std::string& hex_in) {
        std::string hex = hex_in;
        if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
        if (hex.size() == 3) {
            std::string expanded;
            for (char c : hex) { expanded += c; expanded += c; }
            hex = expanded;
        }
        if (hex.size() < 6) return COLOR_LIGHT_GRAY;

        uint32_t val = 0;
        std::stringstream ss;
        ss << std::hex << hex.substr(0, 6);
        ss >> val;

        uint8_t r = (val >> 16) & 0xFF;
        uint8_t g = (val >> 8) & 0xFF;
        uint8_t b = val & 0xFF;

        int best_idx = 0;
        int min_dist = 1000000;
        for (int i = 0; i < 16; ++i) {
            int dr = (int)r - (int)VGA_PALETTE[i][0];
            int dg = (int)g - (int)VGA_PALETTE[i][1];
            int db = (int)b - (int)VGA_PALETTE[i][2];
            int dist = dr*dr + dg*dg + db*db;
            if (dist < min_dist) {
                min_dist = dist;
                best_idx = i;
            }
        }
        return (uint8_t)best_idx;
    }

    uint8_t parse_color(const std::string& str_or_hex) {
        if (str_or_hex.empty()) return COLOR_LIGHT_GRAY;
        if (str_or_hex[0] == '#') return hex_to_vga(str_or_hex);

        std::string name = to_lower_str(str_or_hex);
        if (name == "black" || name == "0") return COLOR_BLACK;
        if (name == "blue" || name == "1") return COLOR_BLUE;
        if (name == "green" || name == "2") return COLOR_GREEN;
        if (name == "cyan" || name == "aqua" || name == "3") return COLOR_CYAN;
        if (name == "red" || name == "4") return COLOR_RED;
        if (name == "magenta" || name == "purple" || name == "5") return COLOR_MAGENTA;
        if (name == "brown" || name == "6") return COLOR_BROWN;
        if (name == "light_gray" || name == "gray" || name == "7") return COLOR_LIGHT_GRAY;
        if (name == "dark_gray" || name == "8") return COLOR_DARK_GRAY;
        if (name == "light_blue" || name == "9") return COLOR_LIGHT_BLUE;
        if (name == "light_green" || name == "10") return COLOR_LIGHT_GREEN;
        if (name == "light_cyan" || name == "11") return COLOR_LIGHT_CYAN;
        if (name == "light_red" || name == "12") return COLOR_LIGHT_RED;
        if (name == "light_magenta" || name == "13") return COLOR_LIGHT_MAGENTA;
        if (name == "yellow" || name == "14") return COLOR_YELLOW;
        if (name == "white" || name == "15") return COLOR_WHITE;

        // Try number
        char* endp = nullptr;
        long n = strtol(str_or_hex.c_str(), &endp, 10);
        if (endp != str_or_hex.c_str() && n >= 0 && n <= 15) return (uint8_t)n;

        return COLOR_LIGHT_GRAY;
    }

    void set_color(uint8_t fg, uint8_t bg) {
        g_current_fg = fg & 0x0F;
        g_current_bg = bg & 0x0F;
#if defined(_WIN32)
        WORD attr = (WORD)((g_current_bg << 4) | g_current_fg);
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attr);
#else
        static const int ANSI_FG[16] = {30, 34, 32, 36, 31, 35, 33, 37, 90, 94, 92, 96, 91, 95, 93, 97};
        static const int ANSI_BG[16] = {40, 44, 42, 46, 41, 45, 43, 47, 100, 104, 102, 106, 101, 105, 103, 107};
        std::cout << "\033[" << ANSI_FG[g_current_fg] << ";" << ANSI_BG[g_current_bg] << "m";
#endif
    }

    void set_color_hex(const std::string& hex_fg, const std::string& hex_bg) {
        set_color(hex_to_vga(hex_fg), hex_to_vga(hex_bg));
    }

    uint8_t get_current_attr() {
        return (uint8_t)((g_current_bg << 4) | g_current_fg);
    }

    // ── 2. Smooth Cursor System ──
    void init_smooth_cursor() {
#if defined(_WIN32)
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO cci;
        if (GetConsoleCursorInfo(hOut, &cci)) {
            cci.bVisible = TRUE;
            cci.dwSize = 25; // Nice solid block / smooth bar
            SetConsoleCursorInfo(hOut, &cci);
        }
#endif
    }

    void tick_smooth_cursor() {
        // Kept lightweight for 60+ FPS synchronization loop
    }

    // ── 3. High-Level Keyboard Input: read_key() ──
    std::string read_key() {
#if defined(_WIN32)
        int c = _getch();
        if (c == 0 || c == 224) {
            int ext = _getch();
            switch (ext) {
                case 72: return "UP";
                case 80: return "DOWN";
                case 75: return "LEFT";
                case 77: return "RIGHT";
                case 71: return "HOME";
                case 79: return "END";
                case 83: return "DELETE";
                case 73: return "PAGE_UP";
                case 81: return "PAGE_DOWN";
                default: return "EXT_" + std::to_string(ext);
            }
        }
        if (c == 13) return "ENTER";
        if (c == 27) return "ESC";
        if (c == 32) return "SPACE";
        if (c == 8)  return "BACKSPACE";
        if (c == 9)  return "TAB";
        return std::string(1, (char)c);
#else
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);

        int ch = getchar();
        if (ch == 27) { // ESC sequence
            int ch2 = getchar();
            if (ch2 == '[') {
                int ch3 = getchar();
                tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
                switch (ch3) {
                    case 'A': return "UP";
                    case 'B': return "DOWN";
                    case 'C': return "RIGHT";
                    case 'D': return "LEFT";
                    default: return "ESC";
                }
            }
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            return "ESC";
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (ch == 10 || ch == 13) return "ENTER";
        if (ch == 32) return "SPACE";
        if (ch == 127 || ch == 8) return "BACKSPACE";
        if (ch == 9) return "TAB";
        return std::string(1, (char)ch);
#endif
    }

    // ── 4. Built-in TUI Widgets ──
    static void set_pos(int x, int y) {
#if defined(_WIN32)
        COORD c = {(SHORT)x, (SHORT)y};
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
#else
        std::cout << "\033[" << (y + 1) << ";" << (x + 1) << "H";
#endif
    }

    void draw_box(int x, int y, int w, int h, const std::string& title, int fg, int bg) {
        if (w < 2 || h < 2) return;
        set_color((uint8_t)fg, (uint8_t)bg);

        // Top line
        set_pos(x, y);
        std::string top = "+";
        int inner_w = w - 2;
        if (!title.empty() && (int)title.size() <= inner_w) {
            top += "[ " + title + " ]";
            int rem = inner_w - (int)title.size() - 4;
            if (rem > 0) top += std::string(rem, '-');
        } else {
            top += std::string(inner_w, '-');
        }
        top += "+";
        std::cout << top;

        // Sides
        for (int row = 1; row < h - 1; ++row) {
            set_pos(x, y + row);
            std::cout << "|" << std::string(inner_w, ' ') << "|";
        }

        // Bottom line
        set_pos(x, y + h - 1);
        std::cout << "+" << std::string(inner_w, '-') << "+";
        std::cout.flush();
    }

    int menu(const std::string& title, const std::vector<std::string>& options) {
        if (options.empty()) return -1;
        int selected = 0;
        int w = (int)title.size() + 6;
        for (const auto& opt : options) {
            if ((int)opt.size() + 6 > w) w = (int)opt.size() + 6;
        }
        if (w < 24) w = 24;
        int h = (int)options.size() + 4;
        int x = 2;
        int y = 2;

        while (true) {
            draw_box(x, y, w, h, title, COLOR_LIGHT_CYAN, COLOR_BLACK);
            for (int i = 0; i < (int)options.size(); ++i) {
                set_pos(x + 2, y + 2 + i);
                if (i == selected) {
                    set_color(COLOR_BLACK, COLOR_LIGHT_GREEN);
                    std::cout << " > " << options[i] << " ";
                } else {
                    set_color(COLOR_WHITE, COLOR_BLACK);
                    std::cout << "   " << options[i] << " ";
                }
            }
            std::cout.flush();

            std::string k = read_key();
            if (k == "UP") {
                selected = (selected > 0) ? selected - 1 : (int)options.size() - 1;
            } else if (k == "DOWN") {
                selected = (selected + 1 < (int)options.size()) ? selected + 1 : 0;
            } else if (k == "ENTER" || k == "SPACE") {
                set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
                return selected;
            } else if (k == "ESC") {
                return -1;
            }
        }
    }

    void progress_bar(int x, int y, int width, int percent, int fg, int bg) {
        if (width < 6) width = 6;
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;

        set_pos(x, y);
        set_color((uint8_t)fg, (uint8_t)bg);
        int bar_len = width - 7; // reserve 7 chars for "[...] 99%"
        int filled = (bar_len * percent) / 100;
        std::cout << "[";
        for (int i = 0; i < filled; ++i) std::cout << "=";
        for (int i = filled; i < bar_len; ++i) std::cout << ".";
        std::cout << "] " << percent << "%";
        std::cout.flush();
    }

    // ── 5. Hardware Power Management ──
    void reboot() {
#if defined(_WIN32)
        std::cout << "\n[GrassGreen Baremetal]: System reboot initiated...\n";
#else
        std::cout << "\n[GrassGreen Baremetal]: System reboot initiated...\n";
#endif
    }

    void poweroff() {
        std::cout << "\n[GrassGreen Baremetal]: System poweroff...\n";
        exit(0);
    }

    // ── 6. Virtual & Physical VFS (RamFS) Structure ──
    static std::shared_ptr<VFSNode> g_vfs_root = nullptr;
    static std::shared_ptr<VFSNode> g_vfs_cwd = nullptr;

    void vfs_init() {
        if (!g_vfs_root) {
            g_vfs_root = std::make_shared<VFSNode>();
            g_vfs_root->name = "/";
            g_vfs_root->is_dir = true;
            g_vfs_cwd = g_vfs_root;
        }
    }

    bool vfs_has_struct() {
        return g_has_vfs_structure;
    }

    void vfs_set_has_struct(bool b) {
        g_has_vfs_structure = b;
        vfs_init();
    }

    static std::shared_ptr<VFSNode> find_child(std::shared_ptr<VFSNode> parent, const std::string& name) {
        if (!parent || !parent->is_dir) return nullptr;
        for (auto& ch : parent->children) {
            if (ch->name == name) return ch;
        }
        return nullptr;
    }

    bool vfs_cr_folder(const std::string& name) {
        vfs_init();
        if (name.empty() || name == "." || name == "..") return false;
        if (find_child(g_vfs_cwd, name)) return false;

        auto node = std::make_shared<VFSNode>();
        node->name = name;
        node->is_dir = true;
        node->parent = g_vfs_cwd;
        g_vfs_cwd->children.push_back(node);
        return true;
    }

    bool vfs_cr_file(const std::string& name, const std::string& content) {
        vfs_init();
        if (name.empty()) return false;
        auto existing = find_child(g_vfs_cwd, name);
        if (existing) {
            if (existing->is_dir) return false;
            existing->content = content;
            return true;
        }

        auto node = std::make_shared<VFSNode>();
        node->name = name;
        node->is_dir = false;
        node->content = content;
        node->parent = g_vfs_cwd;
        g_vfs_cwd->children.push_back(node);
        return true;
    }

    bool vfs_physical_struct(const std::string& host_path) {
        vfs_init();
        g_has_vfs_structure = true;
        if (!fs::exists(host_path)) return false;

        // Recursive import
        auto import_dir = [](auto self, const fs::path& p, std::shared_ptr<VFSNode> target_vfs) -> void {
            try {
                for (const auto& entry : fs::directory_iterator(p)) {
                    std::string ename = entry.path().filename().string();
                    if (entry.is_directory()) {
                        auto dnode = std::make_shared<VFSNode>();
                        dnode->name = ename;
                        dnode->is_dir = true;
                        dnode->parent = target_vfs;
                        target_vfs->children.push_back(dnode);
                        self(self, entry.path(), dnode);
                    } else if (entry.is_regular_file()) {
                        std::ifstream file(entry.path(), std::ios::binary);
                        std::string cnt((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                        auto fnode = std::make_shared<VFSNode>();
                        fnode->name = ename;
                        fnode->is_dir = false;
                        fnode->content = cnt;
                        fnode->parent = target_vfs;
                        target_vfs->children.push_back(fnode);
                    }
                }
            } catch (...) {}
        };

        if (fs::is_directory(host_path)) {
            import_dir(import_dir, fs::path(host_path), g_vfs_cwd);
        }
        return true;
    }

    std::string vfs_pwd() {
        vfs_init();
        if (g_vfs_cwd == g_vfs_root) return "/";

        std::vector<std::string> parts;
        auto cur = g_vfs_cwd;
        while (cur && cur != g_vfs_root) {
            parts.push_back(cur->name);
            cur = cur->parent.lock();
        }
        std::string res;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            res += "/" + *it;
        }
        return res;
    }

    std::string vfs_ls(const std::string& flags) {
        vfs_init();
        bool show_hidden = g_show_hidden_files || (flags.find("-a") != std::string::npos);

        std::string res;
        for (const auto& ch : g_vfs_cwd->children) {
            if (ch->hidden && !show_hidden) continue;
            if (!res.empty()) res += "  ";
            if (ch->is_dir) res += "[" + ch->name + "]";
            else res += ch->name;
        }
        return res;
    }

    bool vfs_cd(const std::string& path) {
        vfs_init();
        if (path.empty() || path == ".") return true;
        if (path == "/") {
            g_vfs_cwd = g_vfs_root;
            return true;
        }
        if (path == "..") {
            auto p = g_vfs_cwd->parent.lock();
            if (p) g_vfs_cwd = p;
            return true;
        }

        std::stringstream ss(path);
        std::string token;
        auto cur = (path[0] == '/') ? g_vfs_root : g_vfs_cwd;

        while (std::getline(ss, token, '/')) {
            if (token.empty() || token == ".") continue;
            if (token == "..") {
                auto p = cur->parent.lock();
                if (p) cur = p;
            } else {
                auto ch = find_child(cur, token);
                if (!ch || !ch->is_dir) return false;
                cur = ch;
            }
        }
        g_vfs_cwd = cur;
        return true;
    }

    bool vfs_copy(const std::string& src, const std::string& dst) {
        vfs_init();
        auto snode = find_child(g_vfs_cwd, src);
        if (!snode || snode->is_dir) return false;
        return vfs_cr_file(dst, snode->content);
    }

    bool vfs_move(const std::string& src, const std::string& dst) {
        vfs_init();
        auto snode = find_child(g_vfs_cwd, src);
        if (!snode) return false;
        if (vfs_copy(src, dst)) {
            return vfs_delete(src);
        }
        return false;
    }

    bool vfs_delete(const std::string& path) {
        vfs_init();
        auto& list = g_vfs_cwd->children;
        for (auto it = list.begin(); it != list.end(); ++it) {
            if ((*it)->name == path) {
                list.erase(it);
                return true;
            }
        }
        return false;
    }

    bool vfs_not_show(const std::string& name) {
        vfs_init();
        auto node = find_child(g_vfs_cwd, name);
        if (node) {
            node->hidden = true;
            return true;
        }
        return false;
    }

    void vfs_specSign_show(const std::string& flag) {
        if (flag == "-a" || flag == "a") {
            g_show_hidden_files = true;
        } else if (flag == "-h" || flag == "hide") {
            g_show_hidden_files = false;
        }
    }

    std::string vfs_read_file(const std::string& path) {
        vfs_init();
        auto node = find_child(g_vfs_cwd, path);
        if (node && !node->is_dir) return node->content;
        return "";
    }

    // ── 7. Screen Clearing & Protection ──
    void clear() {
#if defined(_WIN32)
        system("cls");
#else
        std::cout << "\033[2J\033[H";
#endif
        for (const auto& line : g_protected_lines) {
            std::cout << line << "\n";
        }
    }

    void not_clear_on() {
        g_not_clear_active = true;
    }

    void not_clear_off() {
        g_not_clear_active = false;
    }
}

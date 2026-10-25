#ifndef GRASSLIB_H
#define GRASSLIB_H

#include <string>
#include <vector>
#include <map>
#include <windows.h>

struct UIElement {
    std::string type;
    std::string id;
    std::wstring text;
    int x = 0, y = 0;
    int w = 150, h = 35;
    int align = 0;
    COLORREF txt_color = RGB(0, 255, 204);
    int f_size = 18;
    std::wstring f_style = L"Segoe UI";
    HWND hwnd = NULL;
    HFONT hfont = NULL;

    bool is_glass = false;
    bool has_run_action = false;
    std::string run_target;

    bool is_hovered = false;
    bool is_placeholder = true;
};

extern std::map<std::string, std::wstring> variables_cache;

std::wstring utf8_to_wstring(const std::string& str);
std::string wstring_to_utf8(const std::wstring& wstr);
COLORREF hex_to_rgb(const std::string& hex);

void GrassLib_Load(const std::string& lib_name);
bool GrassStorageEngine_Load(const std::string& filename);

namespace GrassGUI {
    void InitWindow(const std::string& title, int w, int h, const std::string& bg_hex = "#0f0f14", const std::string& bg_img = "");
    void AddElement(const std::string& type, const std::string& id, const std::string& text,
                    int x, int y, int w, int h, int align, const std::string& color_hex,
                    int font_size, const std::string& font_family, bool glass, const std::string& run_cmd);
    void Show();
}

#endif // GRASSLIB_H
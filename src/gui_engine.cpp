#include "gui_engine.h"
#include "GrassAudio.h"
#include "GrassHAL.h"
#include "GrassMMIO.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#include <commctrl.h>
#include <urlmon.h>
#include <gdiplus.h>
#include <mmsystem.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;
#else
#include <cstdlib>
#include <cwchar>
namespace Gdiplus {
    typedef void Image;
    typedef void Bitmap;
}
#endif

using namespace std;

extern map<string, wstring> variables_cache;

namespace GrassGUI {

    SurfaceMode current_surface_mode = SurfaceMode::Auto;
    uint64_t freestanding_fb_addr = 0xE0000000;
    int freestanding_fb_pitch = 800 * 4;

    bool vd_plus_mode = true;
    int focused_elem_idx = -1;
    float caret_pulse_phase = 0.0f; // Плавная дыхательная анимация каретки

    bool g_auto_size_display = false;
    bool g_dpi_awareness_initialized = false;
    int app_border_radius = 0;

    // Шапка окна (VD+ Header System)
    HeaderStyle current_header_style = HeaderStyle::Modern;
    bool user_specified_header_style = false;
    COLORREF header_bg_color = RGB(15, 23, 42); // Slate 900
    bool has_custom_header_color = false;
    COLORREF header_txt_color = RGB(244, 244, 245);
    int header_bar_height = 38;
    string header_buttons_mode = "all";
    string header_buttons_pos = "auto"; // "auto", "left", "right", "center"
    int header_buttons_offset = 0;      // Смещение кнопок по горизонтали в px
    string header_title_align = "auto"; // "auto", "left", "center", "right"
    bool is_dragging_header_buttons = false;
    int drag_buttons_start_x = 0;
    int drag_buttons_initial_offset = 0;

    struct HeaderLayout {
        string resolved_pos; // "left" or "right" or "center"
        int btn_start_x = 0;
        int btn_w = 48;
        int btn_h = 12;
        int btn_y = 12;

        // Для Dots:
        int dot_red_x = 0;
        int dot_yel_x = 0;
        int dot_grn_x = 0;

        // Для Modern/Glass/Emerald:
        int min_btn_x = 0;
        int max_btn_x = 0;
        int cls_btn_x = 0;
        int btn_size_w = 36;

        // Для заголовка:
        REAL title_x = 18.0f;
    };

    HeaderLayout compute_header_layout(int cw, float measured_title_w = 0.0f) {
        HeaderLayout hl;
        bool isDots = (current_header_style == HeaderStyle::Dots || current_header_style == HeaderStyle::Minimal);

        if (header_buttons_pos == "auto") {
            hl.resolved_pos = isDots ? "left" : "right";
        } else {
            hl.resolved_pos = header_buttons_pos;
        }

        hl.btn_y = (header_bar_height - 12) / 2;
        hl.btn_h = 12;

        if (isDots) {
            hl.btn_w = 48; // 3 точки по 12px с шагом 18px
            if (hl.resolved_pos == "right") {
                hl.btn_start_x = cw - 65 + header_buttons_offset;
            } else if (hl.resolved_pos == "center") {
                hl.btn_start_x = (cw - hl.btn_w) / 2 + header_buttons_offset;
            } else {
                hl.btn_start_x = 15 + header_buttons_offset;
            }
            hl.dot_red_x = hl.btn_start_x;
            hl.dot_yel_x = hl.btn_start_x + 18;
            hl.dot_grn_x = hl.btn_start_x + 36;
        } else {
            hl.btn_size_w = 36;
            hl.btn_w = 112; // 3 кнопки: min, max, close
            hl.btn_y = 4;
            hl.btn_h = header_bar_height - 8;
            if (hl.resolved_pos == "left") {
                hl.cls_btn_x = 10 + header_buttons_offset;
                hl.max_btn_x = 10 + 38 + header_buttons_offset;
                hl.min_btn_x = 10 + 76 + header_buttons_offset;
                hl.btn_start_x = 10 + header_buttons_offset;
            } else if (hl.resolved_pos == "center") {
                hl.btn_start_x = (cw - hl.btn_w) / 2 + header_buttons_offset;
                hl.min_btn_x = hl.btn_start_x;
                hl.max_btn_x = hl.btn_start_x + 38;
                hl.cls_btn_x = hl.btn_start_x + 76;
            } else {
                hl.btn_start_x = cw - 118 + header_buttons_offset;
                hl.min_btn_x = hl.btn_start_x;
                hl.max_btn_x = hl.btn_start_x + 38;
                hl.cls_btn_x = hl.btn_start_x + 76;
            }
        }

        // Позиция заголовка окна
        if (header_title_align == "center") {
            hl.title_x = max(10.0f, (cw - measured_title_w) / 2.0f);
        } else if (header_title_align == "right") {
            hl.title_x = max(10.0f, (REAL)(cw - measured_title_w - 20));
        } else if (header_title_align == "left") {
            if (hl.resolved_pos == "left") {
                hl.title_x = (REAL)(hl.btn_start_x + hl.btn_w + 14);
            } else {
                hl.title_x = isDots ? 18.0f : 34.0f;
            }
        } else {
            // "auto"
            if (hl.resolved_pos == "left") {
                hl.title_x = (REAL)(hl.btn_start_x + hl.btn_w + 14);
            } else {
                hl.title_x = isDots ? 18.0f : 34.0f;
            }
        }

        return hl;
    }

    // Вкладки (Tabs System)
    string current_active_tab = "main";
    string current_parsing_tab = "main";

    // Частицы (Particle Engine)
    bool particles_active = false;
    vector<Particle> g_particles;
    COLORREF particle_color = RGB(34, 197, 94);
    float particle_speed_mult = 1.0f;
    int base_particle_count = 35;

    // Режим полного экрана
    bool start_in_fullscreen = false;

    // Автоматическое центрирование элементов окна
    bool window_auto_center_elements = false;

    // Z-Index и слои окон (Z-Order)
    bool window_is_topmost = false;
    int window_z_index = 0;
    HWND g_hwnd_main = NULL;

    void set_window_topmost(bool topmost) {
        window_is_topmost = topmost;
        if (g_hwnd_main && IsWindow(g_hwnd_main)) {
            SetWindowPos(g_hwnd_main, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
    }

    bool is_window_topmost() {
        return window_is_topmost;
    }

    void set_window_z_index(int z) {
        window_z_index = z;
        set_window_topmost(z > 0);
    }

    int get_window_z_index() {
        return window_z_index;
    }

    void bring_window_to_front() {
        if (g_hwnd_main && IsWindow(g_hwnd_main)) {
            BringWindowToTop(g_hwnd_main);
            SetForegroundWindow(g_hwnd_main);
            SetWindowPos(g_hwnd_main, (window_is_topmost || window_z_index > 0) ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
    }

    // Рамка окна
    int win_border_width = 0;
    COLORREF win_border_color = RGB(39, 39, 42);

    wstring window_title = L"GrassGreen UI";
    int win_w = 640, win_h = 480;
    COLORREF bg_color = RGB(9, 9, 11);
    bool window_is_glass = false;

    wstring bg_image_path = L"";
    wstring app_icon_path = L"";
    Gdiplus::Image* pBgImage = NULL;

    vector<UIElement> elements;
    map<string, string> input_values_utf8;

    int last_idx = -1;
    bool trigger_show = false;
    string pending_switch_file = "";

    // --- Вспомогательные функции ---

    wstring utf8_to_wstring(const string& str);
    string wstring_to_utf8(const wstring& wstr);

    void cleanParentheses(string& str) {
        string res;
        for (char c : str) if (c != '(' && c != ')') res += c;
        str = res;
    }

    void cleanCarriageReturn(string& str) {
        if (!str.empty() && str.back() == '\r') str.pop_back();
    }

    string trim(const string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    string stripQuotes(string val) {
        val = trim(val);
        if (val.length() >= 4 && val.substr(0, 2) == "!!" && val.substr(val.length() - 2) == "!!") {
            return val.substr(2, val.length() - 4);
        }
        if (val.length() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            return val.substr(1, val.length() - 2);
        }
        return val;
    }

    wstring format_current_time(const SYSTEMTIME& st, const string& fmt) {
        string f = fmt.empty() ? "HH:mm:ss" : fmt;
        char buf[128];
        if (f == "HH:mm:ss" || f == "time") {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
            return utf8_to_wstring(buf);
        } else if (f == "HH:mm") {
            snprintf(buf, sizeof(buf), "%02d:%02d", st.wHour, st.wMinute);
            return utf8_to_wstring(buf);
        } else if (f == "date" || f == "DD.MM.YYYY") {
            snprintf(buf, sizeof(buf), "%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);
            return utf8_to_wstring(buf);
        } else if (f == "datetime") {
            snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
            return utf8_to_wstring(buf);
        } else {
            string res = f;
            auto replace_all = [](string& str, const string& from, const string& to) {
                size_t pos = 0;
                while ((pos = str.find(from, pos)) != string::npos) {
                    str.replace(pos, from.length(), to);
                    pos += to.length();
                }
            };
            char h_buf[8], m_buf[8], s_buf[8], y_buf[8], mo_buf[8], d_buf[8];
            snprintf(h_buf, sizeof(h_buf), "%02d", st.wHour);
            snprintf(m_buf, sizeof(m_buf), "%02d", st.wMinute);
            snprintf(s_buf, sizeof(s_buf), "%02d", st.wSecond);
            snprintf(y_buf, sizeof(y_buf), "%04d", st.wYear);
            snprintf(mo_buf, sizeof(mo_buf), "%02d", st.wMonth);
            snprintf(d_buf, sizeof(d_buf), "%02d", st.wDay);
            replace_all(res, "YYYY", y_buf);
            replace_all(res, "MM", mo_buf);
            replace_all(res, "DD", d_buf);
            replace_all(res, "HH", h_buf);
            replace_all(res, "mm", m_buf);
            replace_all(res, "ss", s_buf);
            return utf8_to_wstring(res);
        }
    }

    void set_surface_mode(SurfaceMode mode) {
        current_surface_mode = mode;
    }

    void set_surface_mode(const string& raw_mode) {
        string m = trim(raw_mode);
        cleanParentheses(m);
        m = stripQuotes(m);
        for (char& c : m) c = (char)::tolower(c);
        if (m == "auto") current_surface_mode = SurfaceMode::Auto;
        else if (m == "windowed" || m == "window") current_surface_mode = SurfaceMode::Windowed;
        else if (m == "freestanding" || m == "baremetal") current_surface_mode = SurfaceMode::Freestanding;
        else if (m == "borderless" || m == "overlay") current_surface_mode = SurfaceMode::Borderless;
    }

    SurfaceMode get_surface_mode() {
        return current_surface_mode;
    }

    SurfaceMode resolve_active_mode() {
        if (current_surface_mode == SurfaceMode::Auto) {
            if (GrassHAL::is_freestanding()) return SurfaceMode::Freestanding;
            return SurfaceMode::Windowed;
        }
        return current_surface_mode;
    }

    string get_surface_mode_name() {
        switch (resolve_active_mode()) {
            case SurfaceMode::Windowed: return "windowed";
            case SurfaceMode::Freestanding: return "freestanding";
            case SurfaceMode::Borderless: return "borderless";
            default: return "auto";
        }
    }

    void set_header_style(HeaderStyle style) {
        current_header_style = style;
        user_specified_header_style = true;
    }

    void set_header_style(const string& raw_style) {
        string s = trim(raw_style);
        cleanParentheses(s);
        s = stripQuotes(s);
        for (char& c : s) c = (char)::tolower(c);
        user_specified_header_style = true;
        if (s == "dots" || s == "mac" || s == "minimal" || s == "apple") {
            current_header_style = HeaderStyle::Dots;
        } else if (s == "modern" || s == "dark" || s == "win11" || s == "figma") {
            current_header_style = HeaderStyle::Modern;
        } else if (s == "glass" || s == "acrylic") {
            current_header_style = HeaderStyle::Glass;
        } else if (s == "emerald" || s == "grass") {
            current_header_style = HeaderStyle::Emerald;
        } else if (s == "none" || s == "off" || s == "0") {
            current_header_style = HeaderStyle::None;
        } else if (s == "native" || s == "os" || s == "win32") {
            current_header_style = HeaderStyle::Native;
        } else {
            current_header_style = HeaderStyle::Modern;
        }
    }

    HeaderStyle get_header_style() {
        return current_header_style;
    }

    void set_header_color(COLORREF col) {
        header_bg_color = col;
        has_custom_header_color = true;
    }

    void set_header_text_color(COLORREF col) {
        header_txt_color = col;
    }

    void set_header_height(int h) {
        header_bar_height = max(24, min(80, h));
    }

    void set_header_buttons(const string& mode) {
        string m = stripQuotes(mode);
        for (char& c : m) c = (char)::tolower(c);
        if (m == "left" || m == "right" || m == "center") {
            header_buttons_pos = m;
        } else {
            header_buttons_mode = m;
        }
    }

    void set_header_buttons_pos(const string& pos) {
        string p = stripQuotes(pos);
        for (char& c : p) c = (char)::tolower(c);
        header_buttons_pos = p;
    }

    string get_header_buttons_pos() {
        return header_buttons_pos;
    }

    void set_header_buttons_offset(int offset) {
        header_buttons_offset = offset;
    }

    int get_header_buttons_offset() {
        return header_buttons_offset;
    }

    void set_header_title_align(const string& align) {
        string a = stripQuotes(align);
        for (char& c : a) c = (char)::tolower(c);
        header_title_align = a;
    }

    string get_header_title_align() {
        return header_title_align;
    }

    void set_window_border(int width, COLORREF col) {
        win_border_width = max(0, width);
        win_border_color = col;
    }

    void set_current_tab(const string& tab_name) {
        current_active_tab = stripQuotes(tab_name);
    }

    string get_current_tab() {
        return current_active_tab;
    }

    void switch_tab(const string& tab_name) {
        current_active_tab = stripQuotes(tab_name);
        // Запуск анимации появления элементов для новой вкладки
        for (auto& elem : elements) {
            if (elem.tab_name == current_active_tab && elem.animated) {
                if (elem.anim_type == "pulse" || elem.anim_type == "glow") {
                    elem.alpha = 255;
                    elem.anim_progress = 1.0f;
                } else {
                    elem.alpha = 0;
                    elem.anim_progress = 0.0f;
                    if (elem.anim_type == "slide-up") elem.y = elem.orig_y + 15;
                }
            }
        }
    }

    void enable_particles(int count, COLORREF col, float speed) {
        particles_active = true;
        particle_color = col;
        particle_speed_mult = speed;
        base_particle_count = max(15, count);
        int target = (int)((float)base_particle_count * (float)(win_w * win_h) / (720.0f * 560.0f));
        target = max(base_particle_count, min(250, target));
        g_particles.clear();
        g_particles.resize(target);
        for (size_t i = 0; i < g_particles.size(); ++i) {
            auto& p = g_particles[i];
            p.x = (float)(rand() % max(100, win_w));
            p.y = (float)(rand() % max(100, win_h));
            p.vx = ((float)(rand() % 100 - 50) / 100.0f) * speed * 0.8f;
            p.vy = ((float)(rand() % 100 - 50) / 100.0f) * speed * 0.8f;
            p.size = (float)(rand() % 4 + 2);
            p.base_alpha = (float)(rand() % 90 + 50);
            p.alpha = p.base_alpha;
            p.pulse_speed = (float)(rand() % 40 + 20) / 1000.0f;
            p.pulse_t = (float)(rand() % 314) / 100.0f;
        }
    }

    void update_particles_for_size(int new_w, int new_h) {
        if (!particles_active) return;
        int target = (int)((float)base_particle_count * (float)(new_w * new_h) / (720.0f * 560.0f));
        target = max(base_particle_count, min(250, target));

        // Если размер окна существенно увеличился (например развернули на 2K/4K экран),
        // масштабируем координаты существующих частиц, чтобы они не скапливались в левом верхнем углу
        if (win_w > 100 && win_h > 100 && (new_w > win_w * 1.15f || new_h > win_h * 1.15f)) {
            float scale_x = (float)new_w / (float)win_w;
            float scale_y = (float)new_h / (float)win_h;
            for (auto& p : g_particles) {
                p.x = fmodf(p.x * scale_x, (float)new_w);
                p.y = fmodf(p.y * scale_y, (float)new_h);
            }
        }

        size_t old_sz = g_particles.size();
        if ((int)old_sz < target) {
            g_particles.resize(target);
            for (size_t i = old_sz; i < g_particles.size(); ++i) {
                auto& p = g_particles[i];
                p.x = (float)(rand() % max(100, new_w));
                p.y = (float)(rand() % max(100, new_h));
                p.vx = ((float)(rand() % 100 - 50) / 100.0f) * particle_speed_mult * 0.8f;
                p.vy = ((float)(rand() % 100 - 50) / 100.0f) * particle_speed_mult * 0.8f;
                p.size = (float)(rand() % 4 + 2);
                p.base_alpha = (float)(rand() % 90 + 50);
                p.alpha = p.base_alpha;
                p.pulse_speed = (float)(rand() % 40 + 20) / 1000.0f;
                p.pulse_t = (float)(rand() % 314) / 100.0f;
            }
        }
    }

    void set_fullscreen(bool enable) {
        start_in_fullscreen = enable;
    }

    bool is_fullscreen() {
        return start_in_fullscreen;
    }

    void set_auto_center_elements(bool enable) {
        window_auto_center_elements = enable;
    }

    bool is_auto_center_enabled() {
        return window_auto_center_elements;
    }

    bool are_particles_enabled() {
        return particles_active;
    }

    void set_vd_plus(bool enable) {
        vd_plus_mode = enable;
    }

    bool is_vd_plus_enabled() {
        return vd_plus_mode;
    }

    void set_freestanding_framebuffer(uint64_t physical_addr, int width, int height, int pitch) {
        freestanding_fb_addr = physical_addr;
        if (width > 0) win_w = width;
        if (height > 0) win_h = height;
        if (pitch > 0) freestanding_fb_pitch = pitch;
    }

    uint64_t get_freestanding_framebuffer_addr() {
        return freestanding_fb_addr;
    }

    void enable_high_dpi_awareness() {
        if (g_dpi_awareness_initialized) return;
        g_dpi_awareness_initialized = true;

        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) {
            typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(HANDLE);
            SetProcessDpiAwarenessContextProc pSetContext = 
                (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
            if (pSetContext) {
                pSetContext((HANDLE)-4);
                return;
            }
        }
        HMODULE hShcore = LoadLibraryW(L"shcore.dll");
        if (hShcore) {
            typedef HRESULT(WINAPI* SetProcessDpiAwarenessProc)(int);
            SetProcessDpiAwarenessProc pSetAwareness = 
                (SetProcessDpiAwarenessProc)GetProcAddress(hShcore, "SetProcessDpiAwareness");
            if (pSetAwareness) {
                pSetAwareness(2);
                FreeLibrary(hShcore);
                return;
            }
            FreeLibrary(hShcore);
        }
        SetProcessDPIAware();
    }

    float get_dpi_scale() {
        enable_high_dpi_awareness();
        HDC hdc = GetDC(NULL);
        if (!hdc) return 1.0f;
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        float scale = 1.0f;
        if (dpiX > 0) scale = (float)dpiX / 96.0f;
        if (scale <= 1.05f) {
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            if (screenW >= 3840) scale = 2.0f;
            else if (screenW >= 2560) scale = 1.25f;
        }
        return scale;
    }

    void auto_size_display(bool enable) {
        g_auto_size_display = enable;
        if (enable) enable_high_dpi_awareness();
    }

    bool is_auto_size_display_enabled() {
        return g_auto_size_display;
    }

    void set_app_border_radius(int radius) {
        app_border_radius = max(0, radius);
    }

    int get_app_border_radius() {
        return app_border_radius;
    }

    wstring utf8_to_wstring(const string& str) {
        if (str.empty()) return L"";
#if defined(_WIN32)
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        if (len <= 0) return L"";
        wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
        if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
        return wstr;
#else
        size_t len = mbstowcs(nullptr, str.c_str(), 0);
        if (len == (size_t)-1) return L"";
        wstring wstr(len, 0);
        mbstowcs(&wstr[0], str.c_str(), len);
        return wstr;
#endif

using namespace std;
    }

    string wstring_to_utf8(const wstring& wstr) {
        if (wstr.empty()) return "";
#if defined(_WIN32)
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (len <= 0) return "";
        string str(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
        if (!str.empty() && str.back() == '\0') str.pop_back();
        return str;
#else
        size_t len = wcstombs(nullptr, wstr.c_str(), 0);
        if (len == (size_t)-1) return "";
        string str(len, 0);
        wcstombs(&str[0], wstr.c_str(), len);
        return str;
#endif

using namespace std;
    }

    void unpack_cached_vars(wstring& text) {
        size_t start = text.find(L"{");
        while (start != wstring::npos) {
            size_t end = text.find(L"}", start);
            if (end != wstring::npos) {
                wstring var_name = text.substr(start + 1, end - start - 1);
                string utf8_var = wstring_to_utf8(var_name);
                if (variables_cache.count(utf8_var)) {
                    wstring val = variables_cache[utf8_var];
                    text.replace(start, (end - start) + 1, val);
                    start = text.find(L"{", start + val.length());
                } else {
                    start = text.find(L"{", end + 1);
                }
            } else {
                break;
            }
        }
    }

    COLORREF hex_to_rgb(string hex) {
        cleanParentheses(hex);
        hex = stripQuotes(hex);
        if (hex.empty()) return RGB(255, 255, 255);
        if (hex[0] == '#') hex = hex.substr(1);
        if (hex.length() == 3) {
            string expanded = "";
            for (char c : hex) { expanded += c; expanded += c; }
            hex = expanded;
        }
        if (hex.length() < 6) return RGB(255, 255, 255);
        try {
            unsigned int r = 0, g = 0, b = 0;
            stringstream ss;
            ss << std::hex << hex.substr(0, 2); ss >> r; ss.clear();
            ss << std::hex << hex.substr(2, 2); ss >> g; ss.clear();
            ss << std::hex << hex.substr(4, 2); ss >> b;
            return RGB(r, g, b);
        } catch (...) {
            return RGB(255, 255, 255);
        }
    }

    void parse_two_args_fixed(string str, int& a1, int& a2) {
        cleanParentheses(str);
        for (char& c : str) if (c == ',') c = ' ';
        stringstream ss(str);
        ss >> a1 >> a2;
    }

    int find_target_index(string arg) {
        cleanParentheses(arg);
        arg = stripQuotes(arg);
        if (arg.empty()) return last_idx;
        for (size_t i = 0; i < elements.size(); i++) {
            if (elements[i].id == arg) return (int)i;
        }
        return last_idx;
    }

#if defined(_WIN32)
    void MakeRoundedRectPath(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL radius) {
        path.Reset();
        if (radius <= 0.5f || w <= 0.0f || h <= 0.0f) {
            path.AddRectangle(RectF(x, y, w, h));
            return;
        }
        REAL max_r = (w < h ? w : h) / 2.0f;
        if (radius > max_r) radius = max_r;
        REAL dia = radius * 2.0f;
        REAL right = x + w;
        REAL bottom = y + h;

        path.AddArc(x, y, dia, dia, 180.0f, 90.0f);
        path.AddLine(x + radius, y, right - radius, y);
        path.AddArc(right - dia, y, dia, dia, 270.0f, 90.0f);
        path.AddLine(right, y + radius, right, bottom - radius);
        path.AddArc(right - dia, bottom - dia, dia, dia, 0.0f, 90.0f);
        path.AddLine(right - radius, bottom, x + radius, bottom);
        path.AddArc(x, bottom - dia, dia, dia, 90.0f, 90.0f);
        path.AddLine(x, bottom - radius, x, y + radius);
        path.CloseFigure();
    }

    void DrawSmoothRoundedBorder(Graphics& g, REAL x, REAL y, REAL w, REAL h, REAL radius, REAL strokeW, Color color) {
        if (strokeW <= 0.0f || w <= 0.0f || h <= 0.0f) return;
        REAL half = strokeW * 0.5f;
        GraphicsPath sPath;
        MakeRoundedRectPath(sPath, x + half, y + half, w - strokeW, h - strokeW, max(0.0f, radius - half));
        Pen pen(color, strokeW);
        g.DrawPath(&pen, &sPath);
    }

    void DrawVDSoftShadow(Graphics& g, REAL x, REAL y, REAL w, REAL h, REAL radius, int blur, COLORREF col = RGB(0,0,0)) {
        if (w <= 0 || h <= 0) return;
        BYTE cr = GetRValue(col), cg = GetGValue(col), cb = GetBValue(col);
        int steps = 4;
        REAL step_expand = (REAL)max(4, blur) / (REAL)steps;
        BYTE base_alpha = 20;

        for (int s = steps; s >= 1; --s) {
            REAL exp = step_expand * s;
            REAL sx = x - exp * 0.5f;
            REAL sy = y + (exp * 0.45f);
            REAL sw = w + exp;
            REAL sh = h + exp;
            REAL sr = radius + exp * 0.5f;

            BYTE alpha = (BYTE)(base_alpha * (steps - s + 1) / steps);
            GraphicsPath sPath;
            MakeRoundedRectPath(sPath, sx, sy, sw, sh, sr);
            SolidBrush sBrush(Color(alpha, cr, cg, cb));
            g.FillPath(&sBrush, &sPath);
        }
    }

    void DrawVDGlow(Graphics& g, REAL x, REAL y, REAL w, REAL h, REAL radius, int glow_rad, COLORREF col) {
        if (glow_rad <= 0 || w <= 0 || h <= 0) return;
        BYTE cr = GetRValue(col), cg = GetGValue(col), cb = GetBValue(col);
        int steps = 3;
        for (int i = steps; i >= 1; --i) {
            REAL exp = (REAL)glow_rad * ((REAL)i / (REAL)steps);
            BYTE alpha = (BYTE)(60 / i);
            GraphicsPath gPath;
            MakeRoundedRectPath(gPath, x - exp, y - exp, w + exp * 2.0f, h + exp * 2.0f, radius + exp);
            SolidBrush gBrush(Color(alpha, cr, cg, cb));
            g.FillPath(&gBrush, &gPath);
        }
    }

    // ── Векторный рендерер SVG ──
    static string getSvgAttr(const string& tag, const string& attr) {
        string pattern = attr + "=\"";
        size_t p = tag.find(pattern);
        if (p == string::npos) {
            pattern = attr + "='";
            p = tag.find(pattern);
            if (p == string::npos) return "";
        }
        size_t start = p + pattern.length();
        size_t end = tag.find(tag[start - 1], start);
        if (end == string::npos) return "";
        return tag.substr(start, end - start);
    }

    static Color parseSvgColor(const string& val, const map<string, pair<Color, Color>>& grads, bool& is_grad, pair<Color, Color>& grad_pair) {
        is_grad = false;
        string v = trim(val);
        if (v.empty() || v == "none") return Color(0, 0, 0, 0);
        if (v.find("url(#") == 0) {
            size_t end = v.find(')');
            if (end != string::npos) {
                string gid = v.substr(5, end - 5);
                if (grads.count(gid)) {
                    is_grad = true;
                    grad_pair = grads.at(gid);
                    return grad_pair.first;
                }
            }
        }
        if (v[0] == '#') {
            COLORREF rgb = hex_to_rgb(v);
            return Color(255, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
        }
        if (v == "white") return Color(255, 255, 255, 255);
        if (v == "black") return Color(255, 0, 0, 0);
        if (v == "red") return Color(255, 239, 68, 68);
        if (v == "green") return Color(255, 34, 197, 94);
        if (v == "blue") return Color(255, 59, 130, 246);
        return Color(255, 244, 244, 245);
    }

    void RenderSVG(Graphics& g, const wstring& svg_path, REAL targetX, REAL targetY, REAL targetW, REAL targetH) {
        string path_utf8 = wstring_to_utf8(svg_path);
        ifstream in(path_utf8);
        if (!in.is_open()) return;
        string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
        in.close();

        REAL vbX = 0, vbY = 0, vbW = 100, vbH = 100;
        size_t svgTagPos = content.find("<svg");
        if (svgTagPos != string::npos) {
            size_t svgTagEnd = content.find('>', svgTagPos);
            if (svgTagEnd != string::npos) {
                string svgHeader = content.substr(svgTagPos, svgTagEnd - svgTagPos);
                string vb = getSvgAttr(svgHeader, "viewBox");
                if (!vb.empty()) {
                    for (char& c : vb) if (c == ',') c = ' ';
                    stringstream ss(vb);
                    ss >> vbX >> vbY >> vbW >> vbH;
                } else {
                    string sw = getSvgAttr(svgHeader, "width");
                    string sh = getSvgAttr(svgHeader, "height");
                    try { if (!sw.empty()) vbW = stof(sw); } catch(...) {}
                    try { if (!sh.empty()) vbH = stof(sh); } catch(...) {}
                }
            }
        }
        if (vbW <= 0) vbW = 100;
        if (vbH <= 0) vbH = 100;

        auto mapX = [&](REAL x) -> REAL { return targetX + (x - vbX) * (targetW / vbW); };
        auto mapY = [&](REAL y) -> REAL { return targetY + (y - vbY) * (targetH / vbH); };
        auto mapD = [&](REAL d) -> REAL { return d * (targetW / vbW); };

        map<string, pair<Color, Color>> gradients;
        size_t gradPos = 0;
        while ((gradPos = content.find("<linearGradient", gradPos)) != string::npos) {
            size_t gradEnd = content.find("</linearGradient>", gradPos);
            if (gradEnd == string::npos) break;
            string gBlock = content.substr(gradPos, gradEnd - gradPos);
            string gid = getSvgAttr(gBlock, "id");
            Color c1(255, 0, 179, 89), c2(255, 0, 255, 204);
            size_t s1 = gBlock.find("<stop");
            if (s1 != string::npos) {
                string col1 = getSvgAttr(gBlock.substr(s1), "stop-color");
                if (!col1.empty()) {
                    COLORREF r = hex_to_rgb(col1);
                    c1 = Color(255, GetRValue(r), GetGValue(r), GetBValue(r));
                }
                size_t s2 = gBlock.find("<stop", s1 + 5);
                if (s2 != string::npos) {
                    string col2 = getSvgAttr(gBlock.substr(s2), "stop-color");
                    if (!col2.empty()) {
                        COLORREF r = hex_to_rgb(col2);
                        c2 = Color(255, GetRValue(r), GetGValue(r), GetBValue(r));
                    }
                }
            }
            if (!gid.empty()) gradients[gid] = { c1, c2 };
            gradPos = gradEnd + 17;
        }

        RectF tRect(targetX, targetY, targetW, targetH);
        auto makeBrush = [&](const string& fillStr) -> Brush* {
            bool isG = false; pair<Color, Color> gp;
            Color c = parseSvgColor(fillStr, gradients, isG, gp);
            if (c.GetAlpha() == 0) return NULL;
            if (isG) return new LinearGradientBrush(tRect, gp.first, gp.second, 45.0f);
            return new SolidBrush(c);
        };

        auto makePen = [&](const string& strokeStr, REAL sw, const string& cap) -> Pen* {
            if (strokeStr.empty() || strokeStr == "none") return NULL;
            bool isG = false; pair<Color, Color> gp;
            Color c = parseSvgColor(strokeStr, gradients, isG, gp);
            REAL realW = max(1.0f, mapD(sw));
            Pen* p = NULL;
            if (isG) {
                LinearGradientBrush lgb(tRect, gp.first, gp.second, 45.0f);
                p = new Pen(&lgb, realW);
            } else {
                p = new Pen(c, realW);
            }
            if (cap == "round") {
                p->SetStartCap(LineCapRound);
                p->SetEndCap(LineCapRound);
                p->SetLineJoin(LineJoinRound);
            }
            return p;
        };

        size_t tagPos = 0;
        while ((tagPos = content.find('<', tagPos)) != string::npos) {
            size_t tagEnd = content.find('>', tagPos);
            if (tagEnd == string::npos) break;
            string tag = content.substr(tagPos, tagEnd - tagPos + 1);
            tagPos = tagEnd + 1;

            if (tag.find("<rect") == 0) {
                REAL rx = 0, ry = 0, rw = 0, rh = 0, rrad = 0;
                try {
                    string sx = getSvgAttr(tag, "x"); if (!sx.empty()) rx = stof(sx);
                    string sy = getSvgAttr(tag, "y"); if (!sy.empty()) ry = stof(sy);
                    string sw = getSvgAttr(tag, "width"); if (!sw.empty()) rw = stof(sw);
                    string sh = getSvgAttr(tag, "height"); if (!sh.empty()) rh = stof(sh);
                    string sr = getSvgAttr(tag, "rx"); if (!sr.empty()) rrad = stof(sr);
                } catch(...) {}

                Brush* br = makeBrush(getSvgAttr(tag, "fill"));
                REAL sw = 1.0f;
                try { string s = getSvgAttr(tag, "stroke-width"); if (!s.empty()) sw = stof(s); } catch(...) {}
                Pen* pen = makePen(getSvgAttr(tag, "stroke"), sw, getSvgAttr(tag, "stroke-linecap"));

                GraphicsPath rPath;
                MakeRoundedRectPath(rPath, mapX(rx), mapY(ry), mapD(rw), mapD(rh), mapD(rrad));
                if (br) { g.FillPath(br, &rPath); delete br; }
                if (pen) { g.DrawPath(pen, &rPath); delete pen; }
            }
            else if (tag.find("<circle") == 0) {
                REAL cx = 0, cy = 0, cr = 0;
                try {
                    string scx = getSvgAttr(tag, "cx"); if (!scx.empty()) cx = stof(scx);
                    string scy = getSvgAttr(tag, "cy"); if (!scy.empty()) cy = stof(scy);
                    string scr = getSvgAttr(tag, "r"); if (!scr.empty()) cr = stof(scr);
                } catch(...) {}

                Brush* br = makeBrush(getSvgAttr(tag, "fill"));
                REAL sw = 1.0f;
                try { string s = getSvgAttr(tag, "stroke-width"); if (!s.empty()) sw = stof(s); } catch(...) {}
                Pen* pen = makePen(getSvgAttr(tag, "stroke"), sw, getSvgAttr(tag, "stroke-linecap"));

                REAL ex = mapX(cx - cr), ey = mapY(cy - cr), ed = mapD(cr * 2.0f);
                if (br) { g.FillEllipse(br, ex, ey, ed, ed); delete br; }
                if (pen) { g.DrawEllipse(pen, ex, ey, ed, ed); delete pen; }
            }
            else if (tag.find("<ellipse") == 0) {
                REAL cx = 0, cy = 0, rx = 0, ry = 0;
                try {
                    string scx = getSvgAttr(tag, "cx"); if (!scx.empty()) cx = stof(scx);
                    string scy = getSvgAttr(tag, "cy"); if (!scy.empty()) cy = stof(scy);
                    string srx = getSvgAttr(tag, "rx"); if (!srx.empty()) rx = stof(srx);
                    string sry = getSvgAttr(tag, "ry"); if (!sry.empty()) ry = stof(sry);
                } catch(...) {}

                Brush* br = makeBrush(getSvgAttr(tag, "fill"));
                REAL sw = 1.0f;
                try { string s = getSvgAttr(tag, "stroke-width"); if (!s.empty()) sw = stof(s); } catch(...) {}
                Pen* pen = makePen(getSvgAttr(tag, "stroke"), sw, getSvgAttr(tag, "stroke-linecap"));

                REAL ex = mapX(cx - rx), ey = mapY(cy - ry), ew = mapD(rx * 2.0f), eh = mapD(ry * 2.0f);
                if (br) { g.FillEllipse(br, ex, ey, ew, eh); delete br; }
                if (pen) { g.DrawEllipse(pen, ex, ey, ew, eh); delete pen; }
            }
            else if (tag.find("<path") == 0) {
                string d = getSvgAttr(tag, "d");
                if (!d.empty()) {
                    GraphicsPath pPath;
                    for (char& c : d) if (c == ',') c = ' ';
                    stringstream ss(d);
                    char cmd = 0;
                    REAL curX = 0, curY = 0;

                    while (ss >> cmd) {
                        if (isalpha(cmd)) {
                            if (cmd == 'M' || cmd == 'm') {
                                REAL px = 0, py = 0;
                                if (ss >> px >> py) {
                                    if (cmd == 'm') { curX += px; curY += py; }
                                    else { curX = px; curY = py; }
                                    pPath.StartFigure();
                                }
                            }
                            else if (cmd == 'L' || cmd == 'l') {
                                REAL px = 0, py = 0;
                                while (ss >> px >> py) {
                                    REAL nextX = (cmd == 'l') ? curX + px : px;
                                    REAL nextY = (cmd == 'l') ? curY + py : py;
                                    pPath.AddLine(mapX(curX), mapY(curY), mapX(nextX), mapY(nextY));
                                    curX = nextX; curY = nextY;
                                    if (ss.peek() == EOF || isalpha(ss.peek())) break;
                                }
                            }
                            else if (cmd == 'H' || cmd == 'h') {
                                REAL px = 0;
                                if (ss >> px) {
                                    REAL nextX = (cmd == 'h') ? curX + px : px;
                                    pPath.AddLine(mapX(curX), mapY(curY), mapX(nextX), mapY(curY));
                                    curX = nextX;
                                }
                            }
                            else if (cmd == 'V' || cmd == 'v') {
                                REAL py = 0;
                                if (ss >> py) {
                                    REAL nextY = (cmd == 'v') ? curY + py : py;
                                    pPath.AddLine(mapX(curX), mapY(curY), mapX(curX), mapY(nextY));
                                    curY = nextY;
                                }
                            }
                            else if (cmd == 'C' || cmd == 'c') {
                                REAL x1, y1, x2, y2, x, y;
                                while (ss >> x1 >> y1 >> x2 >> y2 >> x >> y) {
                                    REAL p1x = (cmd == 'c') ? curX + x1 : x1;
                                    REAL p1y = (cmd == 'c') ? curY + y1 : y1;
                                    REAL p2x = (cmd == 'c') ? curX + x2 : x2;
                                    REAL p2y = (cmd == 'c') ? curY + y2 : y2;
                                    REAL ex = (cmd == 'c') ? curX + x : x;
                                    REAL ey = (cmd == 'c') ? curY + y : y;
                                    pPath.AddBezier(mapX(curX), mapY(curY), mapX(p1x), mapY(p1y), mapX(p2x), mapY(p2y), mapX(ex), mapY(ey));
                                    curX = ex; curY = ey;
                                    if (ss.peek() == EOF || isalpha(ss.peek())) break;
                                }
                            }
                            else if (cmd == 'Z' || cmd == 'z') {
                                pPath.CloseFigure();
                            }
                        }
                    }

                    Brush* br = makeBrush(getSvgAttr(tag, "fill"));
                    REAL sw = 1.0f;
                    try { string s = getSvgAttr(tag, "stroke-width"); if (!s.empty()) sw = stof(s); } catch(...) {}
                    Pen* pen = makePen(getSvgAttr(tag, "stroke"), sw, getSvgAttr(tag, "stroke-linecap"));

                    if (br) { g.FillPath(br, &pPath); delete br; }
                    if (pen) { g.DrawPath(pen, &pPath); delete pen; }
                }
            }
        }
    }

        int base_design_w = 1140;
    int base_design_h = 760;

    void apply_responsive_scaling(int new_w, int new_h) {
        if (new_w <= 0 || new_h <= 0) return;
        if (base_design_w <= 0 || base_design_h <= 0) return;

        float scale_x = (float)new_w / (float)base_design_w;
        float scale_y = (float)new_h / (float)base_design_h;
        float scale_font = min(scale_x, scale_y);

        for (auto& elem : elements) {
            if (elem.orig_design_w < 0) {
                elem.orig_design_x = elem.x;
                elem.orig_design_y = elem.y;
                elem.orig_design_w = elem.w;
                elem.orig_design_h = elem.h;
                elem.orig_design_f_size = elem.f_size;
            }

            // 1. Full-width bars (e.g. top status bar)
            if (elem.orig_design_w >= base_design_w - 30) {
                elem.x = 0;
                elem.w = new_w;
                elem.y = 0;
                elem.h = max(34, (int)(elem.orig_design_h * scale_y));
                continue;
            }

            // 2. Right-anchored status elements in top bar (wifi, volume, battery, user, clock)
            if (elem.orig_design_y < 45 && elem.orig_design_x > base_design_w - 450) {
                int dist_from_right = base_design_w - elem.orig_design_x;
                elem.x = new_w - dist_from_right;
                elem.y = elem.orig_design_y;
                elem.w = elem.orig_design_w;
                elem.h = elem.orig_design_h;
                continue;
            }

            // 3. Top-right toast notification
            if (elem.orig_design_y < 120 && elem.orig_design_x > base_design_w - 450) {
                int dist_from_right = base_design_w - elem.orig_design_x;
                elem.x = new_w - dist_from_right - 20;
                elem.y = (int)(elem.orig_design_y * scale_y);
                continue;
            }

            // 4. Bottom floating dock (anchored to bottom center)
            if (elem.orig_design_y >= 650) {
                int dock_center_offset = elem.orig_design_x - (base_design_w / 2);
                elem.x = (new_w / 2) + dock_center_offset;
                elem.y = new_h - (base_design_h - elem.orig_design_y) - 16;
                continue;
            }

            // 5. Main cards and general elements: proportional scaling
            elem.x = (int)(elem.orig_design_x * scale_x);
            elem.y = (int)(elem.orig_design_y * scale_y);
            elem.w = (int)(elem.orig_design_w * scale_x);
            elem.h = (int)(elem.orig_design_h * scale_y);
            elem.f_size = max(11, (int)(elem.orig_design_f_size * scale_font));
        }
    }

    // --- Обработчик оконных сообщений Win32 ---

    LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp) {
        SurfaceMode active_mode = resolve_active_mode();
        bool is_modern_frameless = (active_mode == SurfaceMode::Borderless) ||
                                   (app_border_radius > 0) ||
                                   (current_header_style != HeaderStyle::Native);

        switch (uMsg) {
            case WM_MOUSEACTIVATE: {
                BringWindowToTop(hwnd);
                SetForegroundWindow(hwnd);
                SetWindowPos(hwnd, (window_is_topmost || window_z_index > 0) ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                return MA_ACTIVATE;
            }

            case WM_ACTIVATE: {
                if (LOWORD(wp) != WA_INACTIVE) {
                    BringWindowToTop(hwnd);
                    SetForegroundWindow(hwnd);
                    SetWindowPos(hwnd, (window_is_topmost || window_z_index > 0) ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                }
                return 0;
            }

            case WM_TIMER: {
                if (wp == 1) { // 60 FPS Анимации и Частицы
                    caret_pulse_phase += 0.14f;

                    // Обновление частиц
                    if (particles_active) {
                        for (auto& p : g_particles) {
                            p.x += p.vx;
                            p.y += p.vy;
                            if (p.x < 0) p.x = (float)win_w;
                            else if (p.x > win_w) p.x = 0;
                            if (p.y < 0) p.y = (float)win_h;
                            else if (p.y > win_h) p.y = 0;
                            p.pulse_t += p.pulse_speed;
                            p.alpha = p.base_alpha * (0.6f + 0.4f * sinf(p.pulse_t));
                        }
                    }

                    // Обновление анимаций элементов
                    bool need_redraw = particles_active || (focused_elem_idx != -1);

                    // Обновление виджетов часов (Clock widget) раз в секунду
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    static int last_clock_second = -1;
                    if (st.wSecond != last_clock_second) {
                        last_clock_second = st.wSecond;
                        for (auto& elem : elements) {
                            if (elem.is_clock) {
                                elem.text = format_current_time(st, elem.clock_format);
                                need_redraw = true;
                            }
                        }
                    }

                    for (auto& elem : elements) {
                        if (elem.animated) {
                            if (elem.anim_progress < 1.0f) {
                                elem.anim_progress = min(1.0f, elem.anim_progress + 0.05f);
                                elem.alpha = (int)(255 * elem.anim_progress);
                                if (elem.anim_type == "slide-up") {
                                    elem.y = (int)(elem.orig_y + 15 * (1.0f - elem.anim_progress));
                                }
                                need_redraw = true;
                            }
                            if (elem.anim_type == "pulse" || elem.anim_type == "glow") {
                                elem.pulse_phase += 0.08f;
                                need_redraw = true;
                            }
                        }
                    }

                    if (need_redraw) {
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                break;
            }

            case WM_NCCALCSIZE: {
                if (is_modern_frameless) {
                    if (wp == TRUE) return 0; // Вся область окна — клиентская!
                }
                return DefWindowProcW(hwnd, uMsg, wp, lp);
            }

            case WM_GETMINMAXINFO: {
                if (is_modern_frameless) {
                    MINMAXINFO* mmi = (MINMAXINFO*)lp;
                    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi = { sizeof(MONITORINFO) };
                    if (GetMonitorInfoW(hMon, &mi)) {
                        mmi->ptMaxPosition.x = abs(mi.rcWork.left - mi.rcMonitor.left);
                        mmi->ptMaxPosition.y = abs(mi.rcWork.top - mi.rcMonitor.top);
                        mmi->ptMaxSize.x = abs(mi.rcWork.right - mi.rcWork.left);
                        mmi->ptMaxSize.y = abs(mi.rcWork.bottom - mi.rcWork.top);
                    }
                    mmi->ptMinTrackSize.x = 360;
                    mmi->ptMinTrackSize.y = 240;
                    return 0;
                }
                return DefWindowProcW(hwnd, uMsg, wp, lp);
            }

            case WM_NCHITTEST: {
                if (!is_modern_frameless) return DefWindowProcW(hwnd, uMsg, wp, lp);

                POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
                ScreenToClient(hwnd, &pt);

                // Если окно не распахнуто на весь экран — обрабатываем 8px рамку для плавного изменения размера
                if (!IsZoomed(hwnd)) {
                    const int bSize = 8;
                    bool on_left   = (pt.x < bSize);
                    bool on_right  = (pt.x >= win_w - bSize);
                    bool on_top    = (pt.y < bSize);
                    bool on_bottom = (pt.y >= win_h - bSize);

                    if (on_top && on_left)     return HTTOPLEFT;
                    if (on_top && on_right)    return HTTOPRIGHT;
                    if (on_bottom && on_left)  return HTBOTTOMLEFT;
                    if (on_bottom && on_right) return HTBOTTOMRIGHT;
                    if (on_left)   return HTLEFT;
                    if (on_right)  return HTRIGHT;
                    if (on_top)    return HTTOP;
                    if (on_bottom) return HTBOTTOM;
                }

                return HTCLIENT;
            }

            case WM_ERASEBKGND:
                return 1; // Предотвращает мерцание при плавном ресайзе

            case WM_SIZE: {
                int new_w = LOWORD(lp);
                int new_h = HIWORD(lp);
                if (new_w > 0 && new_h > 0) {
                    // Адаптивное масштабирование и распределение частиц на весь экран
                    update_particles_for_size(new_w, new_h);

                    win_w = new_w;
                    win_h = new_h;
                    apply_responsive_scaling(new_w, new_h);

                    for (auto& elem : elements) {
                        if (elem.align == 1 || (window_auto_center_elements && elem.align != 2 && elem.align != 3)) {
                            elem.x = (win_w - elem.w) / 2;
                        } else if (elem.align == 3) {
                            int margin = (int)(24 * (g_auto_size_display ? get_dpi_scale() : 1.0f));
                            elem.x = win_w - elem.w - margin;
                        }
                    }

                    if (app_border_radius > 0) {
                        if (wp == SIZE_MAXIMIZED) {
                            SetWindowRgn(hwnd, NULL, TRUE);
                        } else {
                            HRGN hRgn = CreateRoundRectRgn(0, 0, win_w + 1, win_h + 1, app_border_radius * 2, app_border_radius * 2);
                            if (hRgn) SetWindowRgn(hwnd, hRgn, TRUE);
                        }
                    }

                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            case WM_LBUTTONDBLCLK: {
                int mx = LOWORD(lp);
                int my = HIWORD(lp);
                if (is_modern_frameless && current_header_style != HeaderStyle::None && my < header_bar_height) {
                    HeaderLayout hl = compute_header_layout(win_w);
                    bool on_btns = (mx >= hl.btn_start_x - 4 && mx <= hl.btn_start_x + hl.btn_w + 4);
                    if (!on_btns) {
                        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                        return 0;
                    }
                }
                break;
            }

            case WM_MBUTTONDOWN: {
                BringWindowToTop(hwnd);
                SetForegroundWindow(hwnd);
                SetWindowPos(hwnd, (window_is_topmost || window_z_index > 0) ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                break;
            }

            case WM_RBUTTONDOWN: {
                BringWindowToTop(hwnd);
                SetForegroundWindow(hwnd);
                SetWindowPos(hwnd, (window_is_topmost || window_z_index > 0) ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

                int mx = LOWORD(lp);
                int my = HIWORD(lp);
                if (is_modern_frameless && current_header_style != HeaderStyle::None && my < header_bar_height) {
                    HeaderLayout hl = compute_header_layout(win_w);
                    if (mx >= hl.btn_start_x - 10 && mx <= hl.btn_start_x + hl.btn_w + 10) {
                        is_dragging_header_buttons = true;
                        drag_buttons_start_x = mx;
                        drag_buttons_initial_offset = header_buttons_offset;
                        SetCapture(hwnd);
                        return 0;
                    }
                }
                break;
            }

            case WM_RBUTTONUP: {
                if (is_dragging_header_buttons) {
                    is_dragging_header_buttons = false;
                    ReleaseCapture();
                    return 0;
                }
                break;
            }

            case WM_MOUSEMOVE: {
                int mx = LOWORD(lp);
                int my = HIWORD(lp);
                bool need_redraw = false;
                bool is_hand = false;
                bool is_ibeam = false;

                if (is_dragging_header_buttons) {
                    header_buttons_offset = drag_buttons_initial_offset + (mx - drag_buttons_start_x);
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                }

                if (is_modern_frameless && current_header_style != HeaderStyle::None && my < header_bar_height) {
                    HeaderLayout hl = compute_header_layout(win_w);
                    if (current_header_style == HeaderStyle::Dots || current_header_style == HeaderStyle::Minimal) {
                        int dotY = (header_bar_height - 12) / 2;
                        if (my >= dotY - 2 && my <= dotY + 14 && mx >= hl.btn_start_x - 2 && mx <= hl.btn_start_x + hl.btn_w + 2) {
                            is_hand = true;
                        }
                    } else if (header_buttons_mode != "none") {
                        if (mx >= hl.btn_start_x && mx <= hl.btn_start_x + hl.btn_w) {
                            is_hand = true;
                        }
                    }
                }

                for (auto& elem : elements) {
                    if (!elem.tab_name.empty() && elem.tab_name != "all" && elem.tab_name != current_active_tab) continue;
                    bool inside = (mx >= elem.x && mx <= elem.x + elem.w && my >= elem.y && my <= elem.y + elem.h);
                    if (elem.is_hovered != inside) {
                        elem.is_hovered = inside;
                        need_redraw = true;
                    }
                    if (inside) {
                        if (elem.type == "button") is_hand = true;
                        else if (elem.type == "enter") is_ibeam = true;
                    }
                }

                if (is_hand) SetCursor(LoadCursor(NULL, IDC_HAND));
                else if (is_ibeam) SetCursor(LoadCursor(NULL, IDC_IBEAM));
                else SetCursor(LoadCursor(NULL, IDC_ARROW));

                if (need_redraw) InvalidateRect(hwnd, NULL, FALSE);
                break;
            }

            case WM_SETCURSOR: {
                if (LOWORD(lp) == HTCLIENT) return TRUE;
                break;
            }

            case WM_LBUTTONDOWN: {
                BringWindowToTop(hwnd);
                SetForegroundWindow(hwnd);
                SetWindowPos(hwnd, (window_is_topmost || window_z_index > 0) ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

                int mx = LOWORD(lp);
                int my = HIWORD(lp);

                // 1. Обработка шапки окна: системные кнопки и перетаскивание
                if (is_modern_frameless && current_header_style != HeaderStyle::None && my < header_bar_height) {
                    HeaderLayout hl = compute_header_layout(win_w);

                    // Интерактивное перетаскивание кнопок мышью при зажатом Shift или Ctrl
                    if ((GetKeyState(VK_SHIFT) & 0x8000) || (GetKeyState(VK_CONTROL) & 0x8000)) {
                        if (mx >= hl.btn_start_x - 10 && mx <= hl.btn_start_x + hl.btn_w + 10) {
                            is_dragging_header_buttons = true;
                            drag_buttons_start_x = mx;
                            drag_buttons_initial_offset = header_buttons_offset;
                            SetCapture(hwnd);
                            return 0;
                        }
                    }

                    bool hit_sys_btn = false;

                    if (current_header_style == HeaderStyle::Dots || current_header_style == HeaderStyle::Minimal) {
                        int dotY = (header_bar_height - 12) / 2;
                        if (my >= dotY - 2 && my <= dotY + 14) {
                            // Красная точка: Закрыть
                            if (mx >= hl.dot_red_x - 2 && mx <= hl.dot_red_x + 14) {
                                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                                return 0;
                            }
                            // Желтая точка: Свернуть
                            if (mx >= hl.dot_yel_x - 2 && mx <= hl.dot_yel_x + 14) {
                                ShowWindow(hwnd, SW_MINIMIZE);
                                return 0;
                            }
                            // Зеленая точка: Развернуть / Восстановить (SW_MAXIMIZE / SW_RESTORE)
                            if (mx >= hl.dot_grn_x - 2 && mx <= hl.dot_grn_x + 14) {
                                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                                return 0;
                            }
                            hit_sys_btn = true;
                        }
                    } else if (header_buttons_mode != "none") {
                        if (my >= 4 && my <= header_bar_height - 4) {
                            // Кнопка Закрыть [X]
                            if (mx >= hl.cls_btn_x && mx <= hl.cls_btn_x + hl.btn_size_w) {
                                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                                return 0;
                            }
                            // Кнопка Развернуть [□]
                            if (mx >= hl.max_btn_x && mx <= hl.max_btn_x + hl.btn_size_w) {
                                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                                return 0;
                            }
                            // Кнопка Свернуть [-]
                            if (mx >= hl.min_btn_x && mx <= hl.min_btn_x + hl.btn_size_w) {
                                ShowWindow(hwnd, SW_MINIMIZE);
                                return 0;
                            }
                            hit_sys_btn = true;
                        }
                    }

                    if (!hit_sys_btn) {
                        ReleaseCapture();
                        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                        return 0;
                    }
                }

                // 2. Обработка клика по интерактивным элементам активной вкладки
                bool hit_input = false;
                bool hit_button = false;

                for (int i = (int)elements.size() - 1; i >= 0; --i) {
                    auto& elem = elements[i];
                    if (!elem.tab_name.empty() && elem.tab_name != "all" && elem.tab_name != current_active_tab) continue;

                    if (mx >= elem.x && mx <= elem.x + elem.w && my >= elem.y && my <= elem.y + elem.h) {
                        if (elem.type == "button") {
                            hit_button = true;
                            elem.is_pressed = true;
                            GrassAudio::tone(950, 12, 25);

                            // Переключение вкладки (tab switch)
                            if (elem.has_tab_switch && !elem.target_tab_name.empty()) {
                                GrassAudio::tone(640, 20, 35);
                                switch_tab(elem.target_tab_name);
                                InvalidateRect(hwnd, NULL, FALSE);
                                return 0;
                            }

                            if (elem.has_run_action && !elem.run_target.empty()) {
                                ShellExecuteA(NULL, "open", elem.run_target.c_str(), NULL, NULL, SW_SHOWNORMAL);
                            }
                            if (elem.has_switch_event && !elem.switch_target.empty()) {
                                pending_switch_file = elem.switch_target;
                                PostMessage(hwnd, WM_CLOSE, 0, 0);
                                return 0;
                            }
                            if (elem.has_click_action) {
                                wstring transfer_text = L"";
                                if (!elem.target_input_id.empty()) {
                                    for (auto& inp : elements) {
                                        if (inp.id == elem.target_input_id && inp.type == "enter") {
                                            transfer_text = inp.text;
                                            if (elem.trigger_clear_on_click) {
                                                inp.text.clear();
                                                inp.caret_pos = 0;
                                                input_values_utf8[inp.id] = "";
                                            }
                                            break;
                                        }
                                    }
                                }
                                if (!elem.target_label_id.empty() && !transfer_text.empty()) {
                                    for (auto& lbl : elements) {
                                        if (lbl.id == elem.target_label_id && lbl.type == "label") {
                                            lbl.text = transfer_text;
                                            break;
                                        }
                                    }
                                }
                            }
                            InvalidateRect(hwnd, NULL, FALSE);
                            break;
                        } else if (elem.type == "enter") {
                            hit_input = true;
                            if (focused_elem_idx != i) {
                                if (focused_elem_idx >= 0 && focused_elem_idx < (int)elements.size()) {
                                    elements[focused_elem_idx].is_focused = false;
                                }
                                focused_elem_idx = i;
                                elem.is_focused = true;
                                elem.caret_pos = elem.text.length();
                                caret_pulse_phase = 0.0f;
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                            break;
                        }
                    }
                }

                if (!hit_input && focused_elem_idx != -1) {
                    if (focused_elem_idx >= 0 && focused_elem_idx < (int)elements.size()) {
                        elements[focused_elem_idx].is_focused = false;
                    }
                    focused_elem_idx = -1;
                    InvalidateRect(hwnd, NULL, FALSE);
                }

                // 3. Если шапка "none" или пустая область кликнута — позволяем плавно двигать окно за любое пустое место!
                if (!hit_input && !hit_button && (current_header_style == HeaderStyle::None || my < header_bar_height)) {
                    ReleaseCapture();
                    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                    return 0;
                }
                break;
            }

            case WM_LBUTTONUP: {
                if (is_dragging_header_buttons) {
                    is_dragging_header_buttons = false;
                    ReleaseCapture();
                }
                for (auto& elem : elements) elem.is_pressed = false;
                InvalidateRect(hwnd, NULL, FALSE);
                break;
            }

            case WM_CHAR: {
                if (focused_elem_idx >= 0 && focused_elem_idx < (int)elements.size()) {
                    auto& elem = elements[focused_elem_idx];
                    if (elem.type == "enter") {
                        if (wp == VK_BACK) {
                            if (elem.caret_pos > 0 && !elem.text.empty()) {
                                elem.text.erase(elem.caret_pos - 1, 1);
                                elem.caret_pos--;
                                caret_pulse_phase = 0.0f;
                                if (!elem.id.empty()) input_values_utf8[elem.id] = wstring_to_utf8(elem.text);
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                        } else if (wp >= 32) {
                            elem.text.insert(elem.caret_pos, 1, (wchar_t)wp);
                            elem.caret_pos++;
                            caret_pulse_phase = 0.0f;
                            if (!elem.id.empty()) input_values_utf8[elem.id] = wstring_to_utf8(elem.text);
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                break;
            }

            case WM_KEYDOWN: {
                if (focused_elem_idx >= 0 && focused_elem_idx < (int)elements.size()) {
                    auto& elem = elements[focused_elem_idx];
                    if (elem.type == "enter") {
                        if (wp == VK_LEFT && elem.caret_pos > 0) {
                            elem.caret_pos--; caret_pulse_phase = 0.0f; InvalidateRect(hwnd, NULL, FALSE);
                        } else if (wp == VK_RIGHT && elem.caret_pos < elem.text.length()) {
                            elem.caret_pos++; caret_pulse_phase = 0.0f; InvalidateRect(hwnd, NULL, FALSE);
                        } else if (wp == VK_HOME) {
                            elem.caret_pos = 0; caret_pulse_phase = 0.0f; InvalidateRect(hwnd, NULL, FALSE);
                        } else if (wp == VK_END) {
                            elem.caret_pos = elem.text.length(); caret_pulse_phase = 0.0f; InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                }
                break;
            }

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);

                RECT cr;
                GetClientRect(hwnd, &cr);
                int cw = cr.right - cr.left;
                int ch = cr.bottom - cr.top;

                HDC memDC = CreateCompatibleDC(hdc);
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = cw;
                bmi.bmiHeader.biHeight = -ch;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = DIB_RGB_COLORS;

                void* pVirtualPixels = NULL;
                HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pVirtualPixels, NULL, 0);
                HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

                {
                    Graphics graphics(memDC);
                    graphics.SetSmoothingMode(SmoothingMode::SmoothingModeHighQuality);
                    graphics.SetTextRenderingHint(TextRenderingHint::TextRenderingHintClearTypeGridFit);
                    graphics.SetInterpolationMode(InterpolationMode::InterpolationModeHighQualityBicubic);
                    graphics.SetPixelOffsetMode(PixelOffsetMode::PixelOffsetModeHighQuality);

                    GraphicsPath winPath;
                    if (app_border_radius > 0) {
                        MakeRoundedRectPath(winPath, 0, 0, (REAL)cw, (REAL)ch, (REAL)app_border_radius);
                        graphics.SetClip(&winPath);
                    }

                    // 1. Отрисовка фонового холста
                    if (pBgImage && pBgImage->GetLastStatus() == Ok) {
                        graphics.DrawImage(pBgImage, 0, 0, cw, ch);
                    } else {
                        RectF bgRect(0, 0, (REAL)cw, (REAL)ch);
                        Color topBg(255, min(255, GetRValue(bg_color) + 5), min(255, GetGValue(bg_color) + 5), min(255, GetBValue(bg_color) + 6));
                        Color botBg(255, GetRValue(bg_color), GetGValue(bg_color), GetBValue(bg_color));
                        LinearGradientBrush bgGrad(bgRect, topBg, botBg, 90.0f);
                        graphics.FillRectangle(&bgGrad, bgRect);
                    }

                    // 2. Отрисовка живых частиц (Particle Engine)
                    if (particles_active && !g_particles.empty()) {
                        for (const auto& p : g_particles) {
                            BYTE pa = (BYTE)max(0.0f, min(255.0f, p.alpha));
                            SolidBrush pBrush(Color(pa, GetRValue(particle_color), GetGValue(particle_color), GetBValue(particle_color)));
                            graphics.FillEllipse(&pBrush, p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size);
                        }
                    }

                    // 3. Отрисовка кастомной шапки окна (VD+ Header System)
                    if (is_modern_frameless && current_header_style != HeaderStyle::None) {
                        REAL hH = (REAL)header_bar_height;
                        RectF hRect(0, 0, (REAL)cw, hH);

                        if (current_header_style == HeaderStyle::Emerald) {
                            Color c1(255, 6, 78, 59);  // #064e3b
                            Color c2(255, 2, 44, 34);  // #022c22
                            LinearGradientBrush emGrad(hRect, c1, c2, 90.0f);
                            graphics.FillRectangle(&emGrad, hRect);
                        } else if (current_header_style == HeaderStyle::Glass) {
                            // Настоящий CSS Glassmorphism: акриловый градиент + верхний зеркальный блик
                            Color gTop(220, 30, 41, 59); // Slate 800
                            Color gBot(190, 15, 23, 42); // Slate 900
                            LinearGradientBrush glassGrad(hRect, gTop, gBot, 90.0f);
                            graphics.FillRectangle(&glassGrad, hRect);

                            // Зеркальный блик сверху шапки
                            Pen glassSpec(Color(70, 255, 255, 255), 1.0f);
                            graphics.DrawLine(&glassSpec, 0.0f, 1.0f, (REAL)cw, 1.0f);
                        } else {
                            // Modern / Dots
                            Color hCol = has_custom_header_color ?
                                Color(255, GetRValue(header_bg_color), GetGValue(header_bg_color), GetBValue(header_bg_color)) :
                                Color(255, 15, 23, 42);
                            SolidBrush headerBrush(hCol);
                            graphics.FillRectangle(&headerBrush, hRect);
                        }

                        // Разделительная полоса под шапкой
                        Pen divPen(Color(160, 39, 39, 42), 1.0f);
                        graphics.DrawLine(&divPen, 0.0f, hH, (REAL)cw, hH);

                        FontFamily hFontFam(L"Segoe UI");
                        Gdiplus::Font hFont(&hFontFam, 12, FontStyleBold, UnitPixel);
                        RectF layoutRect(0, 0, 10000.0f, (REAL)header_bar_height);
                        RectF titleBounds;
                        StringFormat sfmt(StringFormat::GenericTypographic());
                        graphics.MeasureString(window_title.c_str(), -1, &hFont, layoutRect, &sfmt, &titleBounds);

                        HeaderLayout hl = compute_header_layout(cw, titleBounds.Width);

                        POINT cursorPt;
                        GetCursorPos(&cursorPt);
                        ScreenToClient(hwnd, &cursorPt);

                        if (current_header_style == HeaderStyle::Dots || current_header_style == HeaderStyle::Minimal) {
                            // ── СТИЛЬ DOTS / MAC: 3 цветные круглые точки ──
                            int dotY = (header_bar_height - 12) / 2;
                            bool closeHover = (cursorPt.y >= dotY - 2 && cursorPt.y <= dotY + 14 && cursorPt.x >= hl.dot_red_x - 2 && cursorPt.x <= hl.dot_red_x + 14);
                            bool minHover   = (cursorPt.y >= dotY - 2 && cursorPt.y <= dotY + 14 && cursorPt.x >= hl.dot_yel_x - 2 && cursorPt.x <= hl.dot_yel_x + 14);
                            bool maxHover   = (cursorPt.y >= dotY - 2 && cursorPt.y <= dotY + 14 && cursorPt.x >= hl.dot_grn_x - 2 && cursorPt.x <= hl.dot_grn_x + 14);

                            SolidBrush redDot(closeHover ? Color(255, 248, 113, 113) : Color(255, 239, 68, 68));   // #ef4444
                            SolidBrush yelDot(minHover ? Color(255, 251, 191, 36) : Color(255, 245, 158, 11));    // #f59e0b
                            SolidBrush grnDot(maxHover ? Color(255, 52, 211, 153) : Color(255, 16, 185, 129));    // #10b981

                            graphics.FillEllipse(&redDot, hl.dot_red_x, dotY, 12, 12);
                            graphics.FillEllipse(&yelDot, hl.dot_yel_x, dotY, 12, 12);
                            graphics.FillEllipse(&grnDot, hl.dot_grn_x, dotY, 12, 12);

                            // Символы внутри точек при наведении (как в macOS)
                            if (closeHover) {
                                Pen symPen(Color(180, 0, 0, 0), 1.2f);
                                graphics.DrawLine(&symPen, (REAL)hl.dot_red_x + 3.0f, (REAL)dotY + 3.0f, (REAL)hl.dot_red_x + 9.0f, (REAL)dotY + 9.0f);
                                graphics.DrawLine(&symPen, (REAL)hl.dot_red_x + 9.0f, (REAL)dotY + 3.0f, (REAL)hl.dot_red_x + 3.0f, (REAL)dotY + 9.0f);
                            }
                            if (minHover) {
                                Pen symPen(Color(180, 0, 0, 0), 1.2f);
                                graphics.DrawLine(&symPen, (REAL)hl.dot_yel_x + 3.0f, (REAL)dotY + 6.0f, (REAL)hl.dot_yel_x + 9.0f, (REAL)dotY + 6.0f);
                            }
                            if (maxHover) {
                                Pen symPen(Color(180, 0, 0, 0), 1.2f);
                                if (IsZoomed(hwnd)) {
                                    graphics.DrawLine(&symPen, (REAL)hl.dot_grn_x + 3.0f, (REAL)dotY + 6.0f, (REAL)hl.dot_grn_x + 9.0f, (REAL)dotY + 6.0f);
                                } else {
                                    graphics.DrawLine(&symPen, (REAL)hl.dot_grn_x + 3.0f, (REAL)dotY + 6.0f, (REAL)hl.dot_grn_x + 9.0f, (REAL)dotY + 6.0f);
                                    graphics.DrawLine(&symPen, (REAL)hl.dot_grn_x + 6.0f, (REAL)dotY + 3.0f, (REAL)hl.dot_grn_x + 6.0f, (REAL)dotY + 9.0f);
                                }
                            }

                            SolidBrush hTxtBrush(Color(255, GetRValue(header_txt_color), GetGValue(header_txt_color), GetBValue(header_txt_color)));
                            graphics.DrawString(window_title.c_str(), -1, &hFont, PointF(hl.title_x, (REAL)((header_bar_height - 16) / 2)), &hTxtBrush);
                        } else {
                            // ── СТИЛЬ MODERN / EMERALD / GLASS: изумрудный логотип + стильные кнопки ──
                            if (hl.resolved_pos == "right") {
                                SolidBrush dotBrush(Color(255, 34, 197, 94));
                                graphics.FillEllipse(&dotBrush, 16, (header_bar_height - 9) / 2, 9, 9);
                            }

                            SolidBrush hTxtBrush(Color(255, GetRValue(header_txt_color), GetGValue(header_txt_color), GetBValue(header_txt_color)));
                            graphics.DrawString(window_title.c_str(), -1, &hFont, PointF(hl.title_x, (REAL)((header_bar_height - 16) / 2)), &hTxtBrush);

                            if (header_buttons_mode != "none") {
                                REAL minMidY = (REAL)(header_bar_height / 2);

                                // Кнопка Свернуть [-]
                                bool minHov = (cursorPt.y >= hl.btn_y && cursorPt.y <= hl.btn_y + hl.btn_h && cursorPt.x >= hl.min_btn_x && cursorPt.x <= hl.min_btn_x + hl.btn_size_w);
                                if (minHov) {
                                    GraphicsPath minP;
                                    MakeRoundedRectPath(minP, (REAL)hl.min_btn_x, (REAL)hl.btn_y, (REAL)hl.btn_size_w, (REAL)hl.btn_h, 6.0f);
                                    SolidBrush hMin(Color(60, 255, 255, 255));
                                    graphics.FillPath(&hMin, &minP);
                                }
                                Pen ctrlPen(minHov ? Color(255, 255, 255, 255) : Color(255, 161, 161, 170), 2.0f);
                                graphics.DrawLine(&ctrlPen, (REAL)hl.min_btn_x + 11.0f, minMidY, (REAL)hl.min_btn_x + 25.0f, minMidY);

                                // Кнопка Развернуть [□]
                                bool maxHov = (cursorPt.y >= hl.btn_y && cursorPt.y <= hl.btn_y + hl.btn_h && cursorPt.x >= hl.max_btn_x && cursorPt.x <= hl.max_btn_x + hl.btn_size_w);
                                if (maxHov) {
                                    GraphicsPath maxP;
                                    MakeRoundedRectPath(maxP, (REAL)hl.max_btn_x, (REAL)hl.btn_y, (REAL)hl.btn_size_w, (REAL)hl.btn_h, 6.0f);
                                    SolidBrush hMax(Color(60, 255, 255, 255));
                                    graphics.FillPath(&hMax, &maxP);
                                }
                                Pen maxPen(maxHov ? Color(255, 255, 255, 255) : Color(255, 161, 161, 170), 1.5f);
                                REAL maxMidX = (REAL)hl.max_btn_x + (REAL)hl.btn_size_w / 2.0f;
                                if (IsZoomed(hwnd)) {
                                    graphics.DrawRectangle(&maxPen, maxMidX - 4.0f, minMidY - 6.0f, 9.0f, 9.0f);
                                    graphics.DrawRectangle(&maxPen, maxMidX - 6.0f, minMidY - 4.0f, 9.0f, 9.0f);
                                } else {
                                    graphics.DrawRectangle(&maxPen, maxMidX - 5.0f, minMidY - 5.0f, 10.0f, 10.0f);
                                }

                                // Кнопка Закрыть [X]
                                bool closeHov = (cursorPt.y >= hl.btn_y && cursorPt.y <= hl.btn_y + hl.btn_h && cursorPt.x >= hl.cls_btn_x && cursorPt.x <= hl.cls_btn_x + hl.btn_size_w);
                                if (closeHov) {
                                    GraphicsPath clsP;
                                    MakeRoundedRectPath(clsP, (REAL)hl.cls_btn_x, (REAL)hl.btn_y, (REAL)hl.btn_size_w, (REAL)hl.btn_h, 6.0f);
                                    SolidBrush hClose(Color(230, 239, 68, 68)); // Red 500
                                    graphics.FillPath(&hClose, &clsP);
                                }
                                Pen clsPen(closeHov ? Color(255, 255, 255, 255) : Color(255, 161, 161, 170), 2.0f);
                                REAL cMidX = (REAL)hl.cls_btn_x + (REAL)hl.btn_size_w / 2.0f;
                                graphics.DrawLine(&clsPen, cMidX - 5.0f, minMidY - 5.0f, cMidX + 5.0f, minMidY + 5.0f);
                                graphics.DrawLine(&clsPen, cMidX + 5.0f, minMidY - 5.0f, cMidX - 5.0f, minMidY + 5.0f);
                            }
                        }
                    }

                    // 4. Отрисовка элементов активной вкладки через композитор VD+
                    for (auto& elem : elements) {
                        // Фильтр вкладок (Tabs System): рисуем только элементы текущей вкладки
                        if (!elem.tab_name.empty() && elem.tab_name != "all" && elem.tab_name != current_active_tab) {
                            continue;
                        }

                        REAL ex = (REAL)elem.x;
                        REAL ey = (REAL)elem.y;
                        REAL ew = (REAL)elem.w;
                        REAL eh = (REAL)elem.h;
                        REAL er = (REAL)elem.radius;

                        // Динамическая анимация пульсации (pulse)
                        if (elem.animated && elem.anim_type == "pulse") {
                            float pScale = 1.0f + 0.03f * sinf(elem.pulse_phase);
                            REAL oldW = ew, oldH = eh;
                            ew *= pScale; eh *= pScale;
                            ex -= (ew - oldW) * 0.5f;
                            ey -= (eh - oldH) * 0.5f;
                        }

                        if (elem.has_shadow) {
                            DrawVDSoftShadow(graphics, ex, ey, ew, eh, er, elem.shadow_blur, elem.shadow_color);
                        }

                        if (elem.has_glow || (elem.animated && elem.anim_type == "glow")) {
                            int gRad = elem.glow_radius;
                            if (elem.animated && elem.anim_type == "glow") {
                                gRad = (int)(elem.glow_radius * (0.8f + 0.4f * sinf(elem.pulse_phase)));
                            }
                            DrawVDGlow(graphics, ex, ey, ew, eh, er, gRad, elem.glow_color);
                        }

                        GraphicsPath path;
                        MakeRoundedRectPath(path, ex, ey, ew, eh, er);

                        if (elem.type == "button") {
                            RectF bRect(ex, ey, ew, eh);
                            Color c1, c2;
                            if (elem.has_gradient) {
                                c1 = Color(255, GetRValue(elem.grad_color1), GetGValue(elem.grad_color1), GetBValue(elem.grad_color1));
                                c2 = Color(255, GetRValue(elem.grad_color2), GetGValue(elem.grad_color2), GetBValue(elem.grad_color2));
                            } else if (elem.has_custom_btn_color) {
                                c1 = Color(255, GetRValue(elem.btn_color), GetGValue(elem.btn_color), GetBValue(elem.btn_color));
                                c2 = Color(255, max(0, GetRValue(elem.btn_color) - 20), max(0, GetGValue(elem.btn_color) - 20), max(0, GetBValue(elem.btn_color) - 20));
                            } else {
                                c1 = Color(255, 34, 197, 94);
                                c2 = Color(255, 22, 163, 74);
                            }

                            if (elem.is_hovered) {
                                if (elem.has_hover_color) {
                                    c1 = Color(255, GetRValue(elem.hover_color), GetGValue(elem.hover_color), GetBValue(elem.hover_color));
                                    c2 = Color(255, max(0, GetRValue(elem.hover_color) - 15), max(0, GetGValue(elem.hover_color) - 15), max(0, GetBValue(elem.hover_color) - 15));
                                } else {
                                    c1 = Color(255, min(255, c1.GetR() + 25), min(255, c1.GetG() + 25), min(255, c1.GetB() + 25));
                                    c2 = Color(255, min(255, c2.GetR() + 25), min(255, c2.GetG() + 25), min(255, c2.GetB() + 25));
                                }
                                DrawVDGlow(graphics, ex, ey, ew, eh, er, 6, RGB(34, 197, 94));
                            }

                            if (elem.is_pressed) {
                                if (elem.has_active_color) {
                                    c1 = Color(255, GetRValue(elem.active_color), GetGValue(elem.active_color), GetBValue(elem.active_color));
                                    c2 = c1;
                                } else {
                                    c1 = Color(255, max(0, c1.GetR() - 30), max(0, c1.GetG() - 30), max(0, c1.GetB() - 30));
                                    c2 = Color(255, max(0, c2.GetR() - 30), max(0, c2.GetG() - 30), max(0, c2.GetB() - 30));
                                }
                            }

                            LinearGradientBrush btnBrush(bRect, c1, c2, elem.grad_angle);
                            graphics.FillPath(&btnBrush, &path);

                            REAL bWidth = (REAL)max(1, elem.border_width);
                            Color bColor = elem.has_border ?
                                Color(255, GetRValue(elem.border_color), GetGValue(elem.border_color), GetBValue(elem.border_color)) :
                                (elem.is_hovered ? Color(90, 255, 255, 255) : Color(25, 255, 255, 255));
                            DrawSmoothRoundedBorder(graphics, ex, ey, ew, eh, er, bWidth, bColor);

                            FontFamily fontFamily(elem.f_style.c_str());
                            Gdiplus::Font font(&fontFamily, (REAL)elem.f_size, elem.font_bold ? FontStyleBold : FontStyleRegular, UnitPixel);
                            SolidBrush textBrush(Color(255, GetRValue(elem.txt_color), GetGValue(elem.txt_color), GetBValue(elem.txt_color)));
                            StringFormat format;
                            format.SetAlignment(StringAlignmentCenter);
                            format.SetLineAlignment(StringAlignmentCenter);
                            RectF textRect(ex + (REAL)elem.pad, ey + (elem.is_pressed ? 1.0f : 0.0f), ew - (REAL)(elem.pad * 2), eh);
                            graphics.DrawString(elem.text.c_str(), -1, &font, textRect, &format, &textBrush);
                        }
                        else if (elem.type == "enter") {
                            SolidBrush inputBg(Color(255, 24, 24, 27));
                            graphics.FillPath(&inputBg, &path);

                            REAL bWidth = elem.is_focused ? 2.0f : (REAL)max(1, elem.border_width);
                            Color inBorder = elem.is_focused ? Color(255, 34, 197, 94) :
                                (elem.has_border ? Color(255, GetRValue(elem.border_color), GetGValue(elem.border_color), GetBValue(elem.border_color)) : Color(255, 63, 63, 70));
                            DrawSmoothRoundedBorder(graphics, ex, ey, ew, eh, er, bWidth, inBorder);

                            if (elem.is_focused) {
                                DrawVDGlow(graphics, ex, ey, ew, eh, er, 4, RGB(34, 197, 94));
                            }

                            FontFamily fontFamily(elem.f_style.c_str());
                            Gdiplus::Font font(&fontFamily, (REAL)elem.f_size, elem.font_bold ? FontStyleBold : FontStyleRegular, UnitPixel);

                            REAL padX = (REAL)max(10, elem.pad);
                            REAL textX = ex + padX;
                            REAL textY = ey + (eh - (REAL)elem.f_size) / 2.0f - 1.0f;

                            // ── СТРОГАЯ ТИПОГРАФИЧЕСКАЯ СИНХРОНИЗАЦИЯ ТЕКСТА И КАРЕТКИ ──
                            StringFormat sfmt(StringFormat::GenericTypographic());
                            sfmt.SetFormatFlags(sfmt.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoFitBlackBox);

                            if (elem.text.empty() && !elem.placeholder.empty()) {
                                SolidBrush phBrush(Color(255, GetRValue(elem.placeholder_color), GetGValue(elem.placeholder_color), GetBValue(elem.placeholder_color)));
                                graphics.DrawString(elem.placeholder.c_str(), -1, &font, PointF(textX, textY), &sfmt, &phBrush);
                            } else if (!elem.text.empty()) {
                                SolidBrush valBrush(Color(255, GetRValue(elem.txt_color), GetGValue(elem.txt_color), GetBValue(elem.txt_color)));
                                graphics.DrawString(elem.text.c_str(), -1, &font, PointF(textX, textY), &sfmt, &valBrush);
                            }

                            // ── ПЛАВНАЯ НЕОНОВАЯ ДЫШАЩАЯ КАРЕТКА (НЕ НАЛЕЗАЕТ НА БУКВЫ) ──
                            if (elem.is_focused) {
                                REAL caretX = textX;
                                if (elem.caret_pos > 0 && !elem.text.empty()) {
                                    wstring prefix = elem.text.substr(0, elem.caret_pos);
                                    RectF layoutRect(0, 0, 10000.0f, eh);
                                    RectF boundingBox;
                                    graphics.MeasureString(prefix.c_str(), -1, &font, layoutRect, &sfmt, &boundingBox);
                                    caretX += boundingBox.Width + 2.0f; // 2px аккуратный отступ вправо от буквы
                                } else {
                                    caretX += 1.0f;
                                }

                                float pulse = (sinf(caret_pulse_phase) * 0.5f + 0.5f);
                                BYTE caret_a = (BYTE)(80 + 175 * pulse);

                                // Неоновое микросвечение курсора
                                Pen caretGlow(Color((BYTE)(caret_a / 4), 34, 197, 94), 4.0f);
                                graphics.DrawLine(&caretGlow, caretX, textY, caretX, textY + (REAL)elem.f_size + 2.0f);

                                Pen caretPen(Color(caret_a, 34, 197, 94), 2.0f);
                                graphics.DrawLine(&caretPen, caretX, textY, caretX, textY + (REAL)elem.f_size + 2.0f);
                            }
                        }
                        else if (elem.type == "label" || elem.type == "clock") {
                            BYTE a = elem.is_glass ? 140 : (BYTE)elem.alpha;
                            if (elem.has_custom_btn_color || elem.is_glass || elem.radius > 0) {
                                Color cardBg = elem.is_glass ?
                                    Color(90, GetRValue(elem.btn_color), GetGValue(elem.btn_color), GetBValue(elem.btn_color)) :
                                    Color(220, GetRValue(elem.btn_color), GetGValue(elem.btn_color), GetBValue(elem.btn_color));
                                SolidBrush cardBrush(cardBg);
                                graphics.FillPath(&cardBrush, &path);

                                if (elem.has_border || elem.border_width > 0) {
                                    Color cardBorder = elem.has_border ?
                                        Color(255, GetRValue(elem.border_color), GetGValue(elem.border_color), GetBValue(elem.border_color)) :
                                        Color(elem.is_glass ? 70 : 180, 39, 39, 42);
                                    DrawSmoothRoundedBorder(graphics, ex, ey, ew, eh, er, (REAL)max(1, elem.border_width), cardBorder);
                                }
                            }

                            FontFamily fontFamily(elem.f_style.c_str());
                            Gdiplus::Font font(&fontFamily, (REAL)elem.f_size, elem.font_bold ? FontStyleBold : FontStyleRegular, UnitPixel);
                            SolidBrush textBrush(Color(a, GetRValue(elem.txt_color), GetGValue(elem.txt_color), GetBValue(elem.txt_color)));

                            StringFormat format;
                            if (elem.align == 1) format.SetAlignment(StringAlignmentCenter);
                            else if (elem.align == 3) format.SetAlignment(StringAlignmentFar);
                            else format.SetAlignment(StringAlignmentNear);
                            format.SetLineAlignment(StringAlignmentCenter);

                            RectF rectF(ex + (REAL)elem.pad, ey, ew - (REAL)(elem.pad * 2), eh);
                            graphics.DrawString(elem.text.c_str(), -1, &font, rectF, &format, &textBrush);
                        }
                        else if (elem.type == "icon" || elem.type == "svg") {
                            if (!elem.media_path.empty()) {
                                wstring ext = elem.media_path.substr(elem.media_path.find_last_of(L".") + 1);
                                transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                if (ext == L"svg") {
                                    RenderSVG(graphics, elem.media_path, ex, ey, ew, eh);
                                } else {
                                    Image img(elem.media_path.c_str());
                                    if (img.GetLastStatus() == Ok) {
                                        graphics.DrawImage(&img, ex, ey, ew, eh);
                                    }
                                }
                            }
                        }
                    }

                    // 5. Отрисовка кастомной внешней рамки окна
                    if (win_border_width > 0) {
                        graphics.ResetClip();
                        Color wbColor(255, GetRValue(win_border_color), GetGValue(win_border_color), GetBValue(win_border_color));
                        DrawSmoothRoundedBorder(graphics, 0, 0, (REAL)cw, (REAL)ch, (REAL)app_border_radius, (REAL)win_border_width, wbColor);
                    }
                }

                BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);

                SelectObject(memDC, hOldBitmap);
                DeleteObject(hBitmap);
                DeleteDC(memDC);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_DESTROY:
                KillTimer(hwnd, 1);
                if (pBgImage) { delete pBgImage; pBgImage = NULL; }
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, uMsg, wp, lp);
    }
#endif

using namespace std;

    // --- Публичные функции API ---

    void reset() {
        window_title = L"GrassGreen UI";
        win_w = 640; win_h = 480;
        bg_color = RGB(9, 9, 11);
        window_is_glass = false;
        bg_image_path.clear();
        app_icon_path.clear();
        if (pBgImage) { delete pBgImage; pBgImage = NULL; }
        elements.clear();
        last_idx = -1;
        focused_elem_idx = -1;
        caret_pulse_phase = 0.0f;
        trigger_show = false;
        pending_switch_file.clear();
        current_header_style = HeaderStyle::Modern;
        user_specified_header_style = false;
        header_bar_height = 38;
        header_buttons_mode = "all";
        header_buttons_pos = "auto";
        header_buttons_offset = 0;
        header_title_align = "auto";
        is_dragging_header_buttons = false;
        win_border_width = 0;
        app_border_radius = 0;
        current_active_tab = "main";
        current_parsing_tab = "main";
        particles_active = false;
        g_particles.clear();
        start_in_fullscreen = false;
        window_auto_center_elements = false;
        window_is_topmost = false;
        window_z_index = 0;
        g_hwnd_main = NULL;
    }

    void set_variable(const string& name, const string& val) {
        variables_cache[name] = utf8_to_wstring(val);
    }

    string get_input_value(const string& element_id) {
        for (const auto& elem : elements) {
            if (elem.id == element_id && elem.type == "enter") {
                return wstring_to_utf8(elem.text);
            }
        }
        if (input_values_utf8.count(element_id)) return input_values_utf8[element_id];
        return "";
    }

    void exec_command(const string& raw_line) {
        string line = raw_line;
        cleanCarriageReturn(line);
        string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '*' || trimmed.substr(0, 2) == "//") return;

        if (trimmed.find("set ") == 0) {
            stringstream vss(trimmed.substr(4));
            string name, eq, val;
            vss >> name >> eq;
            getline(vss, val);
            val = stripQuotes(val);
            variables_cache[name] = utf8_to_wstring(val);
            return;
        }

        if (trimmed.find("animate-") == 0) {
            string target = trimmed.substr(8);
            cleanParentheses(target);
            target = stripQuotes(target);
            int idx = find_target_index(target);
            if (idx != -1) {
                elements[idx].animated = true;
                elements[idx].alpha = 0;
            }
            return;
        }

        string command;
        string inline_arg = "";
        size_t space_pos = trimmed.find(' ');
        size_t paren_pos = trimmed.find('(');

        if (paren_pos != string::npos && (space_pos == string::npos || paren_pos < space_pos)) {
            command = trimmed.substr(0, paren_pos);
            inline_arg = trimmed.substr(paren_pos);
        } else if (space_pos != string::npos) {
            command = trimmed.substr(0, space_pos);
            inline_arg = trimmed.substr(space_pos + 1);
        } else {
            command = trimmed;
        }

        if (command == "surface") {
            set_surface_mode(inline_arg);
        }
        else if (command == "auto_size_display" || command == "auto-size-display") {
            auto_size_display(true);
        }
        else if (command == "border-radius-app" || command == "border_radius_app") {
            cleanParentheses(inline_arg);
            try { set_app_border_radius(stoi(inline_arg)); } catch (...) { set_app_border_radius(12); }
        }
        else if (command == "header") {
            set_header_style(inline_arg);
        }
        else if (command == "header-color" || command == "header_color") {
            cleanParentheses(inline_arg);
            set_header_color(hex_to_rgb(inline_arg));
        }
        else if (command == "header-text-color" || command == "header_text_color") {
            cleanParentheses(inline_arg);
            set_header_text_color(hex_to_rgb(inline_arg));
        }
        else if (command == "header-height" || command == "header_height") {
            cleanParentheses(inline_arg);
            try { set_header_height(stoi(inline_arg)); } catch(...) {}
        }
        else if (command == "header-buttons-pos" || command == "header_buttons_pos") {
            cleanParentheses(inline_arg);
            set_header_buttons_pos(inline_arg);
        }
        else if (command == "header-buttons-offset" || command == "header_buttons_offset") {
            cleanParentheses(inline_arg);
            try { set_header_buttons_offset(stoi(inline_arg)); } catch(...) {}
        }
        else if (command == "header-title-align" || command == "header_title_align" || command == "header-align" || command == "header_align") {
            cleanParentheses(inline_arg);
            set_header_title_align(inline_arg);
        }
        else if (command == "header-buttons" || command == "header_buttons") {
            cleanParentheses(inline_arg);
            set_header_buttons(inline_arg);
        }
        else if (command == "fullscreen" || command == "fullscrean" || command == "maximize" || command == "maximized") {
            set_fullscreen(true);
        }
        else if (command == "topmost") {
            cleanParentheses(inline_arg);
            string val = stripQuotes(inline_arg);
            bool enable = (val.empty() || val == "true" || val == "1" || val == "on");
            set_window_topmost(enable);
        }
        else if (command == "z-index" || command == "z_index" || command == "layer") {
            cleanParentheses(inline_arg);
            string val = stripQuotes(inline_arg);
            try {
                int z = stoi(val);
                set_window_z_index(z);
            } catch (...) {
                set_window_topmost(true);
            }
        }
        else if (command == "clock") {
            cleanParentheses(inline_arg);
            string val = stripQuotes(inline_arg);
            if (val.empty()) val = "HH:mm:ss";
            if (last_idx != -1) {
                elements[last_idx].is_clock = true;
                elements[last_idx].clock_format = val;
                if (elements[last_idx].type == "label") elements[last_idx].type = "clock";
            } else {
                UIElement elem;
                elem.type = "clock";
                elem.is_clock = true;
                elem.clock_format = val;
                elem.text = L"--:--:--";
                elem.tab_name = current_parsing_tab;
                elem.txt_color = RGB(34, 197, 94);
                elem.radius = 6;
                elem.w = 110;
                elem.h = 28;
                elem.f_size = 13;
                elem.font_bold = true;
                elements.push_back(elem);
                last_idx = (int)elements.size() - 1;
            }
        }
        else if (command == "clock-format" || command == "clock_format" || command == "time-format" || command == "time_format") {
            cleanParentheses(inline_arg);
            string val = stripQuotes(inline_arg);
            if (last_idx != -1 && !val.empty()) {
                elements[last_idx].is_clock = true;
                elements[last_idx].clock_format = val;
            }
        }
        else if (command == "layout" || command == "align-all" || command == "align_all" || command == "center-all" || command == "center_all") {
            cleanParentheses(inline_arg);
            string a = stripQuotes(inline_arg);
            if (a.empty() || a == "center" || a == "middle" || a == "1") {
                set_auto_center_elements(true);
            }
        }
        else if (command == "align") {
            cleanParentheses(inline_arg);
            string a = stripQuotes(inline_arg);
            int idx = last_idx;
            if (idx != -1) {
                if (a == "center" || a == "middle") {
                    elements[idx].align = 1;
                    elements[idx].x = (win_w - elements[idx].w) / 2;
                } else if (a == "left") {
                    elements[idx].align = 2;
                } else if (a == "right") {
                    elements[idx].align = 3;
                }
            }
        }
        else if (command == "window-border" || command == "window_border") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream ss(inline_arg);
            int bw = 1; string bcol = "#27272a";
            if (ss >> bw) ss >> bcol;
            set_window_border(bw, hex_to_rgb(bcol));
        }
        else if (command == "tab") {
            cleanParentheses(inline_arg);
            string tname = stripQuotes(inline_arg);
            if (!tname.empty()) current_parsing_tab = tname;
        }
        else if (command == "switch" || command == "switch_tab") {
            cleanParentheses(inline_arg);
            string target = stripQuotes(inline_arg);
            if (last_idx != -1 && elements[last_idx].type == "button") {
                elements[last_idx].has_tab_switch = true;
                elements[last_idx].target_tab_name = target;
            } else {
                switch_tab(target);
            }
        }
        else if (command == "click-switch" || command == "click_switch") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].has_tab_switch = true;
                elements[last_idx].target_tab_name = stripQuotes(inline_arg);
            }
        }
        else if (command == "particles") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream pss(inline_arg);
            int cnt = 35; string col = "#22c55e"; float spd = 1.0f;
            if (pss >> cnt) { pss >> col; pss >> spd; }
            enable_particles(cnt, hex_to_rgb(col), spd);
        }
        else if (command == "animate") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream ass(inline_arg);
            string atype = "fade-in"; int dur = 300;
            if (ass >> atype) ass >> dur;
            atype = stripQuotes(atype);
            if (last_idx != -1) {
                elements[last_idx].animated = true;
                elements[last_idx].anim_type = atype;
                elements[last_idx].anim_duration = dur;
                if (atype == "pulse" || atype == "glow") {
                    elements[last_idx].anim_progress = 1.0f;
                    elements[last_idx].alpha = 255;
                } else {
                    elements[last_idx].anim_progress = 0.0f;
                    elements[last_idx].alpha = 0;
                    if (atype == "slide-up") elements[last_idx].y = elements[last_idx].orig_y + 15;
                }
            }
        }
        else if (command == "title" || command == "name") {
            string val = inline_arg.empty() ? trimmed.substr(command.length()) : inline_arg;
            cleanParentheses(val);
            val = stripQuotes(val);
            if (command == "name" && last_idx != -1 && !inline_arg.empty()) {
                elements[last_idx].id = val;
            } else {
                window_title = utf8_to_wstring(val);
                unpack_cached_vars(window_title);
            }
        }
        else if (command == "size") {
            cleanParentheses(inline_arg);
            if (inline_arg.find(',') != string::npos) {
                int w = 640, h = 480;
                parse_two_args_fixed(inline_arg, w, h);
                if (last_idx != -1) {
                    elements[last_idx].w = w; elements[last_idx].h = h;
                    elements[last_idx].orig_design_w = w; elements[last_idx].orig_design_h = h;
                } else {
                    win_w = w; win_h = h;
                    base_design_w = w; base_design_h = h;
                }
            } else {
                try {
                    int val = stoi(inline_arg);
                    if (last_idx != -1) elements[last_idx].w = val;
                    else win_w = val;
                } catch (...) {}
            }
        }
        else if (command == "size-top" || command == "height") {
            cleanParentheses(inline_arg);
            try {
                int val = stoi(inline_arg);
                if (last_idx != -1) elements[last_idx].h = val;
                else win_h = val;
            } catch (...) {}
        }
        else if (command == "color" || command == "bg" || command == "background") {
            cleanParentheses(inline_arg);
            bg_color = hex_to_rgb(inline_arg);
        }
        else if (command == "theme") {
            cleanParentheses(inline_arg);
            string th = stripQuotes(inline_arg);
            if (th == "light") bg_color = RGB(244, 244, 245);
            else if (th == "emerald" || th == "grass") bg_color = RGB(6, 26, 18);
            else if (th == "cyber") bg_color = RGB(15, 8, 28);
            else bg_color = RGB(9, 9, 11);
        }
        else if (command == "fon") {
            cleanParentheses(inline_arg);
            bg_image_path = utf8_to_wstring(stripQuotes(inline_arg));
            if (pBgImage) delete pBgImage;
            pBgImage = Gdiplus::Image::FromFile(bg_image_path.c_str());
        }
        else if (command == "iconP") {
            cleanParentheses(inline_arg);
            app_icon_path = utf8_to_wstring(stripQuotes(inline_arg));
        }
        else if (command == "button" || command == "label" || command == "enter" || command == "clock") {
            string val = stripQuotes(inline_arg);
            UIElement elem;
            elem.type = command;
            elem.tab_name = current_parsing_tab;
            if (command == "clock") {
                elem.is_clock = true;
                elem.clock_format = val.empty() ? "HH:mm:ss" : val;
                elem.text = L"--:--:--";
                elem.txt_color = RGB(34, 197, 94);
                elem.radius = 6;
                elem.w = 110;
                elem.h = 28;
                elem.f_size = 13;
                elem.font_bold = true;
            } else {
                elem.text = utf8_to_wstring(val);
                unpack_cached_vars(elem.text);
            }
            if (command == "button") {
                elem.has_shadow = true;
                elem.shadow_blur = 10;
                elem.radius = 8;
            } else if (command == "enter") {
                elem.has_border = true;
                elem.border_width = 1;
                elem.border_color = RGB(39, 39, 42);
                elem.radius = 8;
            } else if (command == "label") {
                elem.radius = 0;
            }
            elements.push_back(elem);
            last_idx = (int)elements.size() - 1;
        }
        else if (command == "icon" || command == "video" || command == "svg") {
            cleanParentheses(inline_arg);
            string val = stripQuotes(inline_arg);
            UIElement elem;
            elem.type = command;
            elem.media_path = utf8_to_wstring(val);
            elem.tab_name = current_parsing_tab;
            elem.w = 200; elem.h = 150;
            if (command == "svg") elem.is_svg = true;
            elements.push_back(elem);
            last_idx = (int)elements.size() - 1;
        }
        else if (command == "id" || command == "name_el") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].id = stripQuotes(inline_arg);
        }
        else if (command == "window-ob" || command == "pos" || command == "position") {
            int arg1 = 0, arg2 = 0;
            parse_two_args_fixed(inline_arg, arg1, arg2);
            if (last_idx != -1) {
                elements[last_idx].x = arg1;
                elements[last_idx].y = arg2;
                elements[last_idx].orig_y = arg2;
                elements[last_idx].orig_design_x = arg1;
                elements[last_idx].orig_design_y = arg2;
            }
        }
        else if (command == "middle" || command == "center") {
            int idx = find_target_index(inline_arg);
            if (idx != -1) elements[idx].align = 1;
        }
        else if (command == "left") {
            int idx = find_target_index(inline_arg);
            if (idx != -1) elements[idx].align = 2;
        }
        else if (command == "right") {
            int idx = find_target_index(inline_arg);
            if (idx != -1) elements[idx].align = 3;
        }
        else if (command == "text-color" || command == "text_color") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].txt_color = hex_to_rgb(inline_arg);
        }
        else if (command == "button-color" || command == "button_color") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].btn_color = hex_to_rgb(inline_arg);
                elements[last_idx].has_custom_btn_color = true;
            }
        }
        else if (command == "gradient") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream gss(inline_arg);
            string c1, c2;
            float angle = 90.0f;
            gss >> c1 >> c2;
            if (gss >> angle) {}
            if (last_idx != -1 && !c1.empty()) {
                elements[last_idx].has_gradient = true;
                elements[last_idx].grad_color1 = hex_to_rgb(c1);
                elements[last_idx].grad_color2 = c2.empty() ? elements[last_idx].grad_color1 : hex_to_rgb(c2);
                elements[last_idx].grad_angle = angle;
            }
        }
        else if (command == "glow") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream gss(inline_arg);
            string col;
            int rad = 8;
            gss >> col;
            if (gss >> rad) {}
            if (last_idx != -1) {
                elements[last_idx].has_glow = true;
                if (!col.empty()) elements[last_idx].glow_color = hex_to_rgb(col);
                elements[last_idx].glow_radius = rad;
            }
        }
        else if (command == "border") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream bss(inline_arg);
            int w = 1;
            string col = "#27272a";
            if (bss >> w) {
                bss >> col;
            }
            if (last_idx != -1) {
                elements[last_idx].has_border = true;
                elements[last_idx].border_width = w;
                elements[last_idx].border_color = hex_to_rgb(col);
            }
        }
        else if (command == "hover-color" || command == "hover_color") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].has_hover_color = true;
                elements[last_idx].hover_color = hex_to_rgb(inline_arg);
            }
        }
        else if (command == "active-color" || command == "active_color") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].has_active_color = true;
                elements[last_idx].active_color = hex_to_rgb(inline_arg);
            }
        }
        else if (command == "placeholder") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].placeholder = utf8_to_wstring(stripQuotes(inline_arg));
            }
        }
        else if (command == "placeholder-color" || command == "placeholder_color") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].placeholder_color = hex_to_rgb(inline_arg);
            }
        }
        else if (command == "font-size" || command == "font_size") {
            cleanParentheses(inline_arg);
            try { if (last_idx != -1) elements[last_idx].f_size = stoi(inline_arg); } catch (...) {}
        }
        else if (command == "font-style" || command == "font_style") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].f_style = utf8_to_wstring(stripQuotes(inline_arg));
        }
        else if (command == "font-weight" || command == "font_weight") {
            cleanParentheses(inline_arg);
            string w = stripQuotes(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].font_bold = (w != "normal" && w != "regular" && w != "400");
            }
        }
        else if (command == "radius") {
            cleanParentheses(inline_arg);
            try { if (last_idx != -1) elements[last_idx].radius = stoi(inline_arg); } catch (...) {}
        }
        else if (command == "pad") {
            cleanParentheses(inline_arg);
            try { if (last_idx != -1) elements[last_idx].pad = stoi(inline_arg); } catch (...) {}
        }
        else if (command == "shadow") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream sss(inline_arg);
            int blur = 12;
            string col = "#000000";
            if (sss >> blur) {
                sss >> col;
            }
            if (last_idx != -1) {
                elements[last_idx].has_shadow = true;
                elements[last_idx].shadow_blur = blur;
                elements[last_idx].shadow_color = hex_to_rgb(col);
            }
        }
        else if (command == "glass") {
            if (last_idx != -1) elements[last_idx].is_glass = true;
            else window_is_glass = true;
        }
        else if (command == "click-ob") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream ss(inline_arg);
            string in_id, lbl_id;
            ss >> in_id >> lbl_id;
            if (last_idx != -1) {
                elements[last_idx].has_click_action = true;
                elements[last_idx].target_input_id = in_id;
                elements[last_idx].target_label_id = lbl_id;
            }
        }
        else if (command == "switch_file") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].has_switch_event = true;
                elements[last_idx].switch_target = stripQuotes(inline_arg);
            }
        }
        else if (command == "run") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].has_run_action = true;
                elements[last_idx].run_target = stripQuotes(inline_arg);
            }
        }
        else if (command == "show") {
            trigger_show = true;
        }
    }

    int show_window() {
        SurfaceMode active_mode = resolve_active_mode();

        if (active_mode == SurfaceMode::Freestanding) {
            std::cout << "[GrassGUI Freestanding]: Universal Surface Engine active in Freestanding mode.\n";
            std::cout << "[GrassGUI Freestanding]: Target Physical Video Memory: 0x" << std::hex << freestanding_fb_addr << std::dec
                      << " (" << win_w << "x" << win_h << " 32-bpp)\n";

            uint32_t c_bg = ((GetRValue(bg_color)) << 16) | ((GetGValue(bg_color)) << 8) | (GetBValue(bg_color));
            for (int y = 0; y < win_h; ++y) {
                for (int x = 0; x < win_w; ++x) {
                    GrassMMIO::write32(freestanding_fb_addr + (uint64_t)y * freestanding_fb_pitch + (uint64_t)x * 4, c_bg);
                }
            }

            for (const auto& elem : elements) {
                if (!elem.tab_name.empty() && elem.tab_name != "all" && elem.tab_name != current_active_tab) continue;
                uint32_t c_elem = ((GetRValue(elem.btn_color)) << 16) | ((GetGValue(elem.btn_color)) << 8) | (GetBValue(elem.btn_color));
                int ey_end = min(win_h, elem.y + elem.h);
                int ex_end = min(win_w, elem.x + elem.w);
                for (int y = max(0, elem.y); y < ey_end; ++y) {
                    for (int x = max(0, elem.x); x < ex_end; ++x) {
                        GrassMMIO::write32(freestanding_fb_addr + (uint64_t)y * freestanding_fb_pitch + (uint64_t)x * 4, c_elem);
                    }
                }
            }
            std::cout << "[GrassGUI Freestanding]: Successfully projected UI elements to hardware framebuffer.\n";
            return 0;
        }

#if defined(_WIN32)
        GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        float dpi_scale = 1.0f;
        if (g_auto_size_display) {
            dpi_scale = get_dpi_scale();
            if (dpi_scale > 1.05f) {
                win_w = (int)(win_w * dpi_scale);
                win_h = (int)(win_h * dpi_scale);
                header_bar_height = (int)(header_bar_height * dpi_scale);
                if (app_border_radius > 0) app_border_radius = (int)(app_border_radius * dpi_scale);

                for (auto& elem : elements) {
                    elem.x = (int)(elem.x * dpi_scale);
                    elem.y = (int)(elem.y * dpi_scale);
                    elem.orig_y = elem.y;
                    elem.w = (int)(elem.w * dpi_scale);
                    elem.h = (int)(elem.h * dpi_scale);
                    elem.f_size = (int)(elem.f_size * dpi_scale);
                    elem.radius = (int)(elem.radius * dpi_scale);
                    elem.pad = (int)(elem.pad * dpi_scale);
                }
            }
        }

        // Инициализация отображения времени для всех виджетов часов перед показом
        SYSTEMTIME init_st;
        GetLocalTime(&init_st);
        for (auto& elem : elements) {
            if (elem.is_clock) {
                elem.text = format_current_time(init_st, elem.clock_format);
            }
        }

        bool is_modern_frameless = (active_mode == SurfaceMode::Borderless) ||
                                   (app_border_radius > 0) ||
                                   (current_header_style != HeaderStyle::Native);

        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc = MainWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"GrassUIMaximumClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(bg_color);
        RegisterClassExW(&wc);

        DWORD dwStyle = 0;
        DWORD dwExStyle = 0;

        if (is_modern_frameless) {
            dwStyle = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN;
            dwExStyle = WS_EX_APPWINDOW;
            // Слоистый стиль включается ТОЛЬКО если все окно явно задано как стекло (window_is_glass)
            if (window_is_glass) dwExStyle |= WS_EX_LAYERED;
        } else {
            dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
            dwExStyle = window_is_glass ? WS_EX_LAYERED : 0;
        }

        if (window_is_topmost || window_z_index > 0) {
            dwExStyle |= WS_EX_TOPMOST;
        }

        RECT wr = { 0, 0, win_w, win_h };
        if (!is_modern_frameless) {
            AdjustWindowRectEx(&wr, dwStyle, FALSE, dwExStyle);
        }

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int final_w = wr.right - wr.left;
        int final_h = wr.bottom - wr.top;
        int win_x = (screenW > final_w) ? (screenW - final_w) / 2 : CW_USEDEFAULT;
        int win_y = (screenH > final_h) ? (screenH - final_h) / 2 : CW_USEDEFAULT;

        HWND hwndMain = CreateWindowExW(
            dwExStyle, L"GrassUIMaximumClass", window_title.c_str(), dwStyle,
            win_x, win_y, final_w, final_h,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );
        if (!hwndMain) {
            GdiplusShutdown(gdiplusToken);
            return 1;
        }
        g_hwnd_main = hwndMain;

        if (app_border_radius > 0 || is_modern_frameless) {
            HMODULE hDwmapi = LoadLibraryW(L"dwmapi.dll");
            if (hDwmapi) {
                typedef HRESULT(WINAPI* DwmSetWindowAttributeProc)(HWND, DWORD, LPCVOID, DWORD);
                DwmSetWindowAttributeProc pDwmSet = (DwmSetWindowAttributeProc)GetProcAddress(hDwmapi, "DwmSetWindowAttribute");
                if (pDwmSet && app_border_radius > 0) {
                    int pref = (app_border_radius <= 8) ? 3 : 2;
                    pDwmSet(hwndMain, 33, &pref, sizeof(pref));
                }
                struct DwmMargins { int cxLeftWidth; int cxRightWidth; int cyTopHeight; int cyBottomHeight; };
                typedef HRESULT(WINAPI* DwmExtendFrameProc)(HWND, const DwmMargins*);
                DwmExtendFrameProc pDwmExtend = (DwmExtendFrameProc)GetProcAddress(hDwmapi, "DwmExtendFrameIntoClientArea");
                if (pDwmExtend) {
                    DwmMargins m = { 1, 1, 1, 1 };
                    pDwmExtend(hwndMain, &m);
                }
                FreeLibrary(hDwmapi);
            }

            if (app_border_radius > 0) {
                RECT winRect;
                GetWindowRect(hwndMain, &winRect);
                int ww = winRect.right - winRect.left;
                int wh = winRect.bottom - winRect.top;
                HRGN hRgn = CreateRoundRectRgn(0, 0, ww + 1, wh + 1, app_border_radius * 2, app_border_radius * 2);
                if (hRgn) {
                    SetWindowRgn(hwndMain, hRgn, TRUE);
                }
            }
        }

        if (window_is_glass && (dwExStyle & WS_EX_LAYERED)) {
            SetLayeredWindowAttributes(hwndMain, 0, 240, LWA_ALPHA);
        }

        if (!app_icon_path.empty()) {
            Bitmap* iconBmp = Bitmap::FromFile(app_icon_path.c_str());
            if (iconBmp && iconBmp->GetLastStatus() == Ok) {
                HICON hIcon = NULL;
                iconBmp->GetHICON(&hIcon);
                if (hIcon) {
                    SendMessageW(hwndMain, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                    SendMessageW(hwndMain, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
                }
                delete iconBmp;
            }
        }

        for (size_t i = 0; i < elements.size(); i++) {
            auto& elem = elements[i];
            if (elem.align == 1 || (window_auto_center_elements && elem.align != 2 && elem.align != 3)) {
                elem.align = 1;
                elem.x = (win_w - elem.w) / 2;
            }
            else if (elem.align == 2) elem.x = (int)(40 * (g_auto_size_display ? dpi_scale : 1.0f));
            else if (elem.align == 3) elem.x = win_w - elem.w - (int)(24 * (g_auto_size_display ? dpi_scale : 1.0f));

            if (elem.type == "video" && !elem.media_path.empty()) {
                wstring openCmd = L"open \"" + elem.media_path + L"\" alias grass_vid";
                mciSendStringW(openCmd.c_str(), NULL, 0, NULL);
                wstring winCmd = L"window grass_vid handle " + to_wstring((uintptr_t)hwndMain);
                mciSendStringW(winCmd.c_str(), NULL, 0, NULL);
                wstring rectCmd = L"put grass_vid window at " + to_wstring(elem.x) + L" " +
                    to_wstring(elem.y) + L" " + to_wstring(elem.w) + L" " + to_wstring(elem.h);
                mciSendStringW(rectCmd.c_str(), NULL, 0, NULL);
                mciSendStringW(L"play grass_vid repeat", NULL, 0, NULL);
                continue;
            }
        }

        // Запуск таймера отрисовки 60 FPS (анимации, частицы, дышащая каретка)
        SetTimer(hwndMain, 1, 16, NULL);

        if (start_in_fullscreen) {
            ShowWindow(hwndMain, SW_MAXIMIZE);
            RECT rc;
            GetClientRect(hwndMain, &rc);
            if (rc.right > 0 && rc.bottom > 0) {
                win_w = rc.right;
                win_h = rc.bottom;
                apply_responsive_scaling(win_w, win_h);
            }
        } else {
            ShowWindow(hwndMain, SW_SHOWNORMAL);
        }
        UpdateWindow(hwndMain);
        bring_window_to_front();

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        mciSendStringW(L"close grass_vid", NULL, 0, NULL);
        GdiplusShutdown(gdiplusToken);

        if (!pending_switch_file.empty()) {
            string next_file = pending_switch_file;
            return run_from_file(next_file);
        }
        return 0;
#else
        std::cout << "[GrassGUI]: Visual surface active in Linux console/freestanding mode: " << wstring_to_utf8(window_title) << "\n";
        std::cout << "[GrassGUI]: Canvas: " << win_w << "x" << win_h << " px | " << elements.size() << " UI elements.\n";
        for (const auto& elem : elements) {
            std::cout << "  - (" << elem.type << ") '" << wstring_to_utf8(elem.text) << "' at " << elem.x << "," << elem.y << " size " << elem.w << "x" << elem.h << "\n";
        }
        return 0;
#endif

using namespace std;
    }

    int run_from_lines(const vector<string>& lines) {
        reset();
        for (const string& line : lines) {
            exec_command(line);
        }
        if (trigger_show) return show_window();
        return 0;
    }

    int run_from_file(const string& filepath) {
        ifstream file(filepath);
        if (!file.is_open()) return 1;
        reset();
        string line;
        while (getline(file, line)) {
            exec_command(line);
        }
        file.close();
        return show_window();
    }
}

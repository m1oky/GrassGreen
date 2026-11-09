#include "Grass2DGraphics.h"
#include "GrassHAL.h"
#include <chrono>
#include <thread>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <cmath>

HWND Grass2DGraphics::hwnd = nullptr;
HDC Grass2DGraphics::hdc_screen = nullptr;
HDC Grass2DGraphics::hdc_mem = nullptr;
HBITMAP Grass2DGraphics::hbm_mem = nullptr;
HBITMAP Grass2DGraphics::hbm_old = nullptr;
int Grass2DGraphics::win_width = 800;
int Grass2DGraphics::win_height = 600;
bool Grass2DGraphics::is_running = false;
uint64_t Grass2DGraphics::last_frame_time = 0;

static uint64_t getNowMs() {
    if (GrassHAL::is_freestanding()) {
        return (uint64_t)GrassHAL::get_elapsed_ms(0);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static uint32_t parseColor32(const std::string& hex) {
    std::string s = hex;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    uint32_t val = 0xFFFFFFFF;
    try {
        val = std::stoul(s, nullptr, 16);
        if ((val & 0xFF000000) == 0) val |= 0xFF000000;
    } catch (...) {
        return 0xFFFFFFFF;
    }
    return val;
}

COLORREF Grass2DGraphics::parseColor(const std::string& hex) {
    std::string s = hex;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    uint32_t val = 0;
    try {
        val = std::stoul(s, nullptr, 16);
    } catch (...) {
        return RGB(255, 255, 255);
    }
    int r = (val >> 16) & 0xFF;
    int g = (val >> 8) & 0xFF;
    int b = val & 0xFF;
    return RGB(r, g, b);
}

int Grass2DGraphics::parseVkKey(const std::string& key) {
    std::string k = key;
    std::transform(k.begin(), k.end(), k.begin(), ::toupper);
    if (k == "SPACE") return VK_SPACE;
    if (k == "LEFT") return VK_LEFT;
    if (k == "RIGHT") return VK_RIGHT;
    if (k == "UP") return VK_UP;
    if (k == "DOWN") return VK_DOWN;
    if (k == "ESC" || k == "ESCAPE") return VK_ESCAPE;
    if (k == "ENTER") return VK_RETURN;
    if (k == "SHIFT") return VK_SHIFT;
    if (k == "CTRL") return VK_CONTROL;
    if (k.length() == 1) return k[0];
    return 0;
}

LRESULT CALLBACK Grass2DGraphics::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            is_running = false;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            is_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1; // Предотвращаем мерцание белого фона при рендере
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

bool Grass2DGraphics::init(const std::string& title, int width, int height) {
    if (is_running) close();

    win_width = width;
    win_height = height;

    if (GrassHAL::is_freestanding()) {
        GrassHAL::g_software_fb.init(width, height);
        is_running = true;
        last_frame_time = getNowMs();
        return true;
    }

#if defined(_WIN32)
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "Grass2D_Engine_Class";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExA(&wc);

    RECT rc = {0, 0, width, height};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    int posX = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;

    hwnd = CreateWindowExA(
        0, "Grass2D_Engine_Class", title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        posX, posY, rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (!hwnd) return false;

    hdc_screen = GetDC(hwnd);
    hdc_mem = CreateCompatibleDC(hdc_screen);
    hbm_mem = CreateCompatibleBitmap(hdc_screen, width, height);
    hbm_old = (HBITMAP)SelectObject(hdc_mem, hbm_mem);

    SetBkMode(hdc_mem, TRANSPARENT);
    is_running = true;
    last_frame_time = getNowMs();
    return true;
#else
    GrassHAL::g_software_fb.init(width, height);
    is_running = true;
    last_frame_time = getNowMs();
    return true;
#endif
}

bool Grass2DGraphics::running() {
    if (!is_running) return false;
    if (GrassHAL::is_freestanding()) {
        return is_running;
    }
#if defined(_WIN32)
    if (!hwnd) return false;

    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            is_running = false;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return is_running;
#else
    return is_running;
#endif
}

void Grass2DGraphics::clear(const std::string& hex_color) {
    if (GrassHAL::is_freestanding()) {
        GrassHAL::g_software_fb.clear(parseColor32(hex_color));
        return;
    }
#if defined(_WIN32)
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    HBRUSH brush = CreateSolidBrush(col);
    RECT rc = {0, 0, win_width, win_height};
    FillRect(hdc_mem, &rc, brush);
    DeleteObject(brush);
#endif
}

void Grass2DGraphics::rect(int x, int y, int w, int h, const std::string& hex_color) {
    if (GrassHAL::is_freestanding()) {
        GrassHAL::g_software_fb.draw_rect(x, y, w, h, parseColor32(hex_color));
        return;
    }
#if defined(_WIN32)
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    HBRUSH brush = CreateSolidBrush(col);
    RECT rc = {x, y, x + w, y + h};
    FillRect(hdc_mem, &rc, brush);
    DeleteObject(brush);
#endif
}

void Grass2DGraphics::circle(int x, int y, int r, const std::string& hex_color) {
    if (GrassHAL::is_freestanding()) {
        GrassHAL::g_software_fb.draw_circle(x, y, r, parseColor32(hex_color));
        return;
    }
#if defined(_WIN32)
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    HBRUSH brush = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ old_brush = SelectObject(hdc_mem, brush);
    HGDIOBJ old_pen = SelectObject(hdc_mem, pen);

    Ellipse(hdc_mem, x - r, y - r, x + r, y + r);

    SelectObject(hdc_mem, old_brush);
    SelectObject(hdc_mem, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
#endif
}

void Grass2DGraphics::line(int x1, int y1, int x2, int y2, const std::string& hex_color, int width) {
    if (GrassHAL::is_freestanding()) {
        GrassHAL::g_software_fb.draw_line(x1, y1, x2, y2, parseColor32(hex_color));
        return;
    }
#if defined(_WIN32)
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    HPEN pen = CreatePen(PS_SOLID, width, col);
    HGDIOBJ old_pen = SelectObject(hdc_mem, pen);

    MoveToEx(hdc_mem, x1, y1, NULL);
    LineTo(hdc_mem, x2, y2);

    SelectObject(hdc_mem, old_pen);
    DeleteObject(pen);
#endif
}

void Grass2DGraphics::text(int x, int y, const std::string& str, int size, const std::string& hex_color) {
    if (GrassHAL::is_freestanding()) {
        // В автономном режиме выводим текстовые метки в консольный журнал
        return;
    }
#if defined(_WIN32)
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    SetTextColor(hdc_mem, col);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wlen);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();

    HFONT hFont = CreateFontW(
        size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    HGDIOBJ old_font = SelectObject(hdc_mem, hFont);

    TextOutW(hdc_mem, x, y, wstr.c_str(), (int)wstr.length());

    SelectObject(hdc_mem, old_font);
    DeleteObject(hFont);
#endif
}

static std::string get_svg_attr(const std::string& tag, const std::string& attr) {
    std::string needle = attr + "=\"";
    size_t pos = tag.find(needle);
    if (pos == std::string::npos) {
        needle = attr + "='";
        pos = tag.find(needle);
        if (pos == std::string::npos) return "";
    }
    size_t start = pos + needle.length();
    char quote = tag[pos + needle.length() - 1];
    size_t end = tag.find(quote, start);
    if (end == std::string::npos) return "";
    return tag.substr(start, end - start);
}

void Grass2DGraphics::draw_svg(const std::string& path, int targetX, int targetY, int targetW, int targetH) {
    std::ifstream in(path);
    if (!in.is_open()) {
        in.open("assets/" + path);
        if (!in.is_open()) {
            in.open("../assets/" + path);
            if (!in.is_open()) {
                in.open("assets/icons/" + path);
                if (!in.is_open()) return;
            }
        }
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    float vbX = 0, vbY = 0, vbW = 100, vbH = 100;
    size_t svgPos = content.find("<svg");
    if (svgPos != std::string::npos) {
        size_t svgEnd = content.find('>', svgPos);
        if (svgEnd != std::string::npos) {
            std::string header = content.substr(svgPos, svgEnd - svgPos);
            std::string vb = get_svg_attr(header, "viewBox");
            if (!vb.empty()) {
                for (char& c : vb) if (c == ',') c = ' ';
                std::stringstream ss(vb);
                ss >> vbX >> vbY >> vbW >> vbH;
            } else {
                std::string sw = get_svg_attr(header, "width");
                std::string sh = get_svg_attr(header, "height");
                try { if (!sw.empty()) vbW = std::stof(sw); } catch(...) {}
                try { if (!sh.empty()) vbH = std::stof(sh); } catch(...) {}
            }
        }
    }
    if (vbW <= 0) vbW = 100;
    if (vbH <= 0) vbH = 100;

    auto mapX = [&](float x) -> int { return targetX + (int)((x - vbX) * (targetW / vbW)); };
    auto mapY = [&](float y) -> int { return targetY + (int)((y - vbY) * (targetH / vbH)); };
    auto mapW = [&](float w_val) -> int { return std::max(1, (int)(w_val * (targetW / vbW))); };
    auto mapH = [&](float h_val) -> int { return std::max(1, (int)(h_val * (targetH / vbH))); };

    std::map<std::string, std::string> grad_colors;
    size_t gpos = 0;
    while ((gpos = content.find("<linearGradient", gpos)) != std::string::npos) {
        size_t gend = content.find("</linearGradient>", gpos);
        if (gend == std::string::npos) break;
        std::string block = content.substr(gpos, gend - gpos);
        std::string gid = get_svg_attr(block, "id");
        size_t s1 = block.find("<stop");
        if (s1 != std::string::npos) {
            std::string col = get_svg_attr(block.substr(s1), "stop-color");
            if (!col.empty()) grad_colors[gid] = col;
        }
        gpos = gend + 17;
    }

    auto resolve_col = [&](const std::string& raw) -> std::string {
        if (raw.empty() || raw == "none") return "";
        if (raw.find("url(#") == 0) {
            std::string id = raw.substr(5);
            if (!id.empty() && id.back() == ')') id.pop_back();
            if (grad_colors.count(id)) return grad_colors[id];
            return "#22c55e";
        }
        return raw;
    };

    size_t tagPos = 0;
    while ((tagPos = content.find('<', tagPos)) != std::string::npos) {
        size_t tagEnd = content.find('>', tagPos);
        if (tagEnd == std::string::npos) break;
        std::string tag = content.substr(tagPos, tagEnd - tagPos + 1);
        tagPos = tagEnd + 1;

        if (tag.rfind("<rect", 0) == 0) {
            float rx = 0, ry = 0, rw = 0, rh = 0;
            try {
                std::string s = get_svg_attr(tag, "x"); if (!s.empty()) rx = std::stof(s);
                s = get_svg_attr(tag, "y"); if (!s.empty()) ry = std::stof(s);
                s = get_svg_attr(tag, "width"); if (!s.empty()) rw = std::stof(s);
                s = get_svg_attr(tag, "height"); if (!s.empty()) rh = std::stof(s);
            } catch (...) {}
            std::string fillCol = resolve_col(get_svg_attr(tag, "fill"));
            std::string strokeCol = resolve_col(get_svg_attr(tag, "stroke"));
            if (!fillCol.empty()) rect(mapX(rx), mapY(ry), mapW(rw), mapH(rh), fillCol);
            if (!strokeCol.empty()) {
                int px = mapX(rx), py = mapY(ry), pw = mapW(rw), ph = mapH(rh);
                line(px, py, px + pw, py, strokeCol, 1);
                line(px, py + ph, px + pw, py + ph, strokeCol, 1);
                line(px, py, px, py + ph, strokeCol, 1);
                line(px + pw, py, px + pw, py + ph, strokeCol, 1);
            }
        } else if (tag.rfind("<circle", 0) == 0) {
            float cx = 0, cy = 0, cr = 0;
            try {
                std::string s = get_svg_attr(tag, "cx"); if (!s.empty()) cx = std::stof(s);
                s = get_svg_attr(tag, "cy"); if (!s.empty()) cy = std::stof(s);
                s = get_svg_attr(tag, "r"); if (!s.empty()) cr = std::stof(s);
            } catch (...) {}
            std::string fillCol = resolve_col(get_svg_attr(tag, "fill"));
            std::string strokeCol = resolve_col(get_svg_attr(tag, "stroke"));
            int rad = std::max(1, (int)(cr * (targetW / vbW)));
            if (!fillCol.empty()) circle(mapX(cx), mapY(cy), rad, fillCol);
            else if (!strokeCol.empty()) circle(mapX(cx), mapY(cy), rad, strokeCol);
        } else if (tag.rfind("<line", 0) == 0) {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0, sw = 1;
            try {
                std::string s = get_svg_attr(tag, "x1"); if (!s.empty()) x1 = std::stof(s);
                s = get_svg_attr(tag, "y1"); if (!s.empty()) y1 = std::stof(s);
                s = get_svg_attr(tag, "x2"); if (!s.empty()) x2 = std::stof(s);
                s = get_svg_attr(tag, "y2"); if (!s.empty()) y2 = std::stof(s);
                s = get_svg_attr(tag, "stroke-width"); if (!s.empty()) sw = std::stof(s);
            } catch (...) {}
            std::string strokeCol = resolve_col(get_svg_attr(tag, "stroke"));
            if (strokeCol.empty()) strokeCol = "#ffffff";
            line(mapX(x1), mapY(y1), mapX(x2), mapY(y2), strokeCol, std::max(1, (int)sw));
        } else if (tag.rfind("<path", 0) == 0) {
            std::string d = get_svg_attr(tag, "d");
            std::string fillCol = resolve_col(get_svg_attr(tag, "fill"));
            std::string strokeCol = resolve_col(get_svg_attr(tag, "stroke"));
            std::string col = !fillCol.empty() ? fillCol : strokeCol;
            if (col.empty()) col = "#22c55e";

            std::stringstream ss(d);
            char cmd = 0;
            float curX = 0, curY = 0, startX = 0, startY = 0;
            while (ss >> cmd) {
                if (cmd == 'M' || cmd == 'm') {
                    float px, py;
                    if (ss >> px >> py) {
                        curX = px; curY = py; startX = px; startY = py;
                    }
                } else if (cmd == 'L' || cmd == 'l') {
                    float px, py;
                    if (ss >> px >> py) {
                        line(mapX(curX), mapY(curY), mapX(px), mapY(py), col, 2);
                        curX = px; curY = py;
                    }
                } else if (cmd == 'Z' || cmd == 'z') {
                    line(mapX(curX), mapY(curY), mapX(startX), mapY(startY), col, 2);
                    curX = startX; curY = startY;
                } else {
                    float dummy;
                    while (ss.peek() == ' ' || ss.peek() == ',') ss.ignore();
                    if (isdigit(ss.peek()) || ss.peek() == '-') ss >> dummy;
                }
            }
        }
    }
}

bool Grass2DGraphics::key(const std::string& key_name) {
    if (GrassHAL::is_freestanding()) {
        return false;
    }
#if defined(_WIN32)
    int vk = parseVkKey(key_name);
    if (!vk) return false;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    return false;
#endif
}

int Grass2DGraphics::mouse_x() {
    if (GrassHAL::is_freestanding()) return 0;
#if defined(_WIN32)
    if (!hwnd) return 0;
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    return pt.x;
#else
    return 0;
#endif
}

int Grass2DGraphics::mouse_y() {
    if (GrassHAL::is_freestanding()) return 0;
#if defined(_WIN32)
    if (!hwnd) return 0;
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    return pt.y;
#else
    return 0;
#endif
}

bool Grass2DGraphics::mouse_down(const std::string& btn) {
    if (GrassHAL::is_freestanding()) return false;
#if defined(_WIN32)
    if (btn == "right") return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#else
    return false;
#endif
}

bool Grass2DGraphics::collide(double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

void Grass2DGraphics::frame(int target_fps) {
    if (GrassHAL::is_freestanding()) {
        if (target_fps > 0) {
            uint32_t target_ms = 1000 / target_fps;
            GrassHAL::delay_ms(target_ms);
        }
        return;
    }
#if defined(_WIN32)
    if (!hdc_screen || !hdc_mem) return;

    // Мгновенный сброс буфера на монитор (Double Buffering)
    BitBlt(hdc_screen, 0, 0, win_width, win_height, hdc_mem, 0, 0, SRCCOPY);

    // Ограничение частоты кадров
    if (target_fps > 0) {
        uint64_t target_ms = 1000 / target_fps;
        uint64_t now = getNowMs();
        uint64_t elapsed = now - last_frame_time;
        if (elapsed < target_ms) {
            Sleep((DWORD)(target_ms - elapsed));
        }
        last_frame_time = getNowMs();
    }
#endif
}

void Grass2DGraphics::close() {
    is_running = false;
    if (GrassHAL::is_freestanding()) return;
#if defined(_WIN32)
    if (hdc_mem && hbm_old) SelectObject(hdc_mem, hbm_old);
    if (hbm_mem) { DeleteObject(hbm_mem); hbm_mem = nullptr; }
    if (hdc_mem) { DeleteDC(hdc_mem); hdc_mem = nullptr; }
    if (hwnd && hdc_screen) { ReleaseDC(hwnd, hdc_screen); hdc_screen = nullptr; }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
#endif
}

#include "Grass2DGraphics.h"
#include <chrono>
#include <thread>
#include <algorithm>

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
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
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
}

bool Grass2DGraphics::running() {
    if (!is_running || !hwnd) return false;

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
}

void Grass2DGraphics::clear(const std::string& hex_color) {
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    HBRUSH brush = CreateSolidBrush(col);
    RECT rc = {0, 0, win_width, win_height};
    FillRect(hdc_mem, &rc, brush);
    DeleteObject(brush);
}

void Grass2DGraphics::rect(int x, int y, int w, int h, const std::string& hex_color) {
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    HBRUSH brush = CreateSolidBrush(col);
    RECT rc = {x, y, x + w, y + h};
    FillRect(hdc_mem, &rc, brush);
    DeleteObject(brush);
}

void Grass2DGraphics::circle(int x, int y, int r, const std::string& hex_color) {
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
}

void Grass2DGraphics::line(int x1, int y1, int x2, int y2, const std::string& hex_color, int width) {
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    HPEN pen = CreatePen(PS_SOLID, width, col);
    HGDIOBJ old_pen = SelectObject(hdc_mem, pen);

    MoveToEx(hdc_mem, x1, y1, NULL);
    LineTo(hdc_mem, x2, y2);

    SelectObject(hdc_mem, old_pen);
    DeleteObject(pen);
}

void Grass2DGraphics::text(int x, int y, const std::string& str, int size, const std::string& hex_color) {
    if (!hdc_mem) return;
    COLORREF col = parseColor(hex_color);
    SetTextColor(hdc_mem, col);

    HFONT hFont = CreateFontA(
        size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI"
    );
    HGDIOBJ old_font = SelectObject(hdc_mem, hFont);

    TextOutA(hdc_mem, x, y, str.c_str(), (int)str.length());

    SelectObject(hdc_mem, old_font);
    DeleteObject(hFont);
}

bool Grass2DGraphics::key(const std::string& key_name) {
    int vk = parseVkKey(key_name);
    if (!vk) return false;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

int Grass2DGraphics::mouse_x() {
    if (!hwnd) return 0;
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    return pt.x;
}

int Grass2DGraphics::mouse_y() {
    if (!hwnd) return 0;
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    return pt.y;
}

bool Grass2DGraphics::mouse_down(const std::string& btn) {
    if (btn == "right") return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

bool Grass2DGraphics::collide(double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}

void Grass2DGraphics::frame(int target_fps) {
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
}

void Grass2DGraphics::close() {
    if (hdc_mem && hbm_old) SelectObject(hdc_mem, hbm_old);
    if (hbm_mem) { DeleteObject(hbm_mem); hbm_mem = nullptr; }
    if (hdc_mem) { DeleteDC(hdc_mem); hdc_mem = nullptr; }
    if (hwnd && hdc_screen) { ReleaseDC(hwnd, hdc_screen); hdc_screen = nullptr; }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
    is_running = false;
}
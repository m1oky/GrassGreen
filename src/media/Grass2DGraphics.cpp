#include "media/Grass2DGraphics.h"
#include <map>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace std;

namespace Grass2D {

static int g_width = 800;
static int g_height = 600;
static int g_pitch = 800 * 4;
static uint32_t* g_framebuffer = nullptr;
static bool g_is_open = false;
static float g_dt = 1.0f / 60.0f;
static auto g_last_time = chrono::high_resolution_clock::now();

static float g_cam_x = 0.0f;
static float g_cam_y = 0.0f;
static float g_cam_zoom = 1.0f;
static float g_cam_angle = 0.0f;

static float g_shake_intensity = 0.0f;
static float g_shake_duration = 0.0f;

static vector<G2DParticle> g_particles;
static map<uint32_t, G2DTexture> g_textures;
static uint32_t g_next_tex_id = 1;

#if defined(_WIN32)
static HWND g_hwnd = NULL;
static HDC g_hdc = NULL;
static BITMAPINFO g_bmi = {};

static LRESULT CALLBACK WndProc2D(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            g_is_open = false;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_is_open = false;
            g_hwnd = NULL;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_is_open = false;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_framebuffer && g_width > 0 && g_height > 0) {
                RECT cr;
                GetClientRect(hwnd, &cr);
                StretchDIBits(
                    hdc,
                    0, 0, cr.right - cr.left, cr.bottom - cr.top,
                    0, 0, g_width, g_height,
                    g_framebuffer,
                    &g_bmi,
                    DIB_RGB_COLORS,
                    SRCCOPY
                );
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
#endif

void bind_framebuffer(void* addr, int w, int h, int pitch) {
    g_framebuffer = (uint32_t*)addr;
    g_width = w;
    g_height = h;
    g_pitch = (pitch > 0) ? pitch : w * 4;
}

bool init_window(int w, int h, const string& title) {
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;
    g_width = w;
    g_height = h;
    g_pitch = w * 4;

    if (!g_framebuffer) {
        g_framebuffer = new uint32_t[w * h];
    }

#if defined(_WIN32)
    HINSTANCE hInst = GetModuleHandleA(NULL);
    const char* className = "GrassGreen_2D_Window";

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc2D;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = className;

    RegisterClassExA(&wc);

    RECT rc = { 0, 0, w, h };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int win_w = rc.right - rc.left;
    int win_h = rc.bottom - rc.top;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int win_x = (screen_w > win_w) ? (screen_w - win_w) / 2 : CW_USEDEFAULT;
    int win_y = (screen_h > win_h) ? (screen_h - win_h) / 2 : CW_USEDEFAULT;

    string win_title = title.empty() ? "GrassGreen 2D Window" : title;

    g_hwnd = CreateWindowExA(
        0,
        className,
        win_title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        win_x, win_y,
        win_w, win_h,
        NULL, NULL, hInst, NULL
    );

    if (!g_hwnd) {
        g_is_open = false;
        return false;
    }

    g_hdc = GetDC(g_hwnd);

    ZeroMemory(&g_bmi, sizeof(BITMAPINFO));
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = w;
    g_bmi.bmiHeader.biHeight = -h; // Top-down
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetForegroundWindow(g_hwnd);
#endif

    g_is_open = true;
    g_last_time = chrono::high_resolution_clock::now();
    g_dt = 1.0f / 60.0f;
    return true;
}

bool is_open() {
#if defined(_WIN32)
    if (g_hwnd) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_is_open = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    } else {
        g_is_open = false;
    }
#endif
    return g_is_open;
}

void clear(float r, float g, float b) {
    if (!g_framebuffer) return;
    uint32_t col = ((uint8_t)(r * 255.0f) << 16) | ((uint8_t)(g * 255.0f) << 8) | (uint8_t)(b * 255.0f);
    int total = g_width * g_height;
    for (int i = 0; i < total; i++) {
        g_framebuffer[i] = col;
    }
}

void present() {
    update_particles(g_dt);
#if defined(_WIN32)
    if (g_hwnd && g_hdc && g_framebuffer && g_width > 0 && g_height > 0) {
        RECT clientRect;
        GetClientRect(g_hwnd, &clientRect);
        int cw = clientRect.right - clientRect.left;
        int ch = clientRect.bottom - clientRect.top;

        StretchDIBits(
            g_hdc,
            0, 0, cw, ch,
            0, 0, g_width, g_height,
            g_framebuffer,
            &g_bmi,
            DIB_RGB_COLORS,
            SRCCOPY
        );
    }
#endif

    // High precision ~60 FPS frame limiting and delta time calculation
    const float target_frame_time = 1.0f / 60.0f; // ~0.016667s
    auto now = chrono::high_resolution_clock::now();
    float elapsed = chrono::duration<float>(now - g_last_time).count();

    if (elapsed < target_frame_time) {
        float remaining = target_frame_time - elapsed;
        int sleep_ms = (int)(remaining * 1000.0f);
        if (sleep_ms > 0) {
#if defined(_WIN32)
            Sleep((DWORD)sleep_ms);
#else
            this_thread::sleep_for(chrono::milliseconds(sleep_ms));
#endif
        }
        while (chrono::duration<float>(chrono::high_resolution_clock::now() - g_last_time).count() < target_frame_time) {
            // spin wait
        }
        now = chrono::high_resolution_clock::now();
        elapsed = chrono::duration<float>(now - g_last_time).count();
    }

    g_dt = elapsed;
    if (g_dt > 0.1f) g_dt = 0.1f;
    g_last_time = now;
}

float dt() {
    return (g_dt > 0.0001f && g_dt < 0.2f) ? g_dt : (1.0f / 60.0f);
}

bool key_down(const string& key) {
#if defined(_WIN32)
    char* endp = nullptr;
    long vk = strtol(key.c_str(), &endp, 10);
    if (endp && *endp == '\0' && vk > 0 && vk < 256) {
        return (GetAsyncKeyState((int)vk) & 0x8000) != 0;
    }
    if (key == "SPACE" || key == "space") return (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (key == "ENTER" || key == "enter") return (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    if (key == "ESCAPE" || key == "ESC" || key == "esc") return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (key == "UP" || key == "up")    return (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    if (key == "DOWN" || key == "down")  return (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    if (key == "LEFT" || key == "left")  return (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    if (key == "RIGHT" || key == "right") return (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
    if (key == "W" || key == "w") return (GetAsyncKeyState('W') & 0x8000) != 0;
    if (key == "A" || key == "a") return (GetAsyncKeyState('A') & 0x8000) != 0;
    if (key == "S" || key == "s") return (GetAsyncKeyState('S') & 0x8000) != 0;
    if (key == "D" || key == "d") return (GetAsyncKeyState('D') & 0x8000) != 0;
    if (key.size() == 1) {
        char c = (char)toupper((unsigned char)key[0]);
        return (GetAsyncKeyState(c) & 0x8000) != 0;
    }
#endif
    return false;
}

int mouse_x() {
#if defined(_WIN32)
    POINT p;
    if (GetCursorPos(&p) && g_hwnd) {
        ScreenToClient(g_hwnd, &p);
        return p.x;
    }
#endif
    return 0;
}

int mouse_y() {
#if defined(_WIN32)
    POINT p;
    if (GetCursorPos(&p) && g_hwnd) {
        ScreenToClient(g_hwnd, &p);
        return p.y;
    }
#endif
    return 0;
}

bool mouse_btn(int btn) {
#if defined(_WIN32)
    if (btn == 0) return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (btn == 1) return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
#endif
    return false;
}

uint32_t load_tex(const string& path) {
    uint32_t id = g_next_tex_id++;
    G2DTexture tex;
    tex.id = id;
    tex.width = 64;
    tex.height = 64;
    tex.pixels.resize(64 * 64, 0xFF22C55E); // Default green tile pattern

    // Procedural grid icon
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            if (x == 0 || x == 63 || y == 0 || y == 63 || x == y) {
                tex.pixels[y * 64 + x] = 0xFFFFFFFF;
            }
        }
    }

    g_textures[id] = tex;
    return id;
}

void cam_set(float x, float y, float zoom, float angle) {
    g_cam_x = x;
    g_cam_y = y;
    g_cam_zoom = zoom > 0.01f ? zoom : 1.0f;
    g_cam_angle = angle;
}

void cam_shake(float intensity, float duration) {
    g_shake_intensity = intensity;
    g_shake_duration = duration;
}

void draw_sprite(uint32_t tex_id, float x, float y, float w, float h, float angle, float alpha) {
    if (!g_framebuffer || !g_textures.count(tex_id)) return;
    const G2DTexture& tex = g_textures[tex_id];

    float shake_x = 0, shake_y = 0;
    if (g_shake_duration > 0.0f) {
        shake_x = ((float)(rand() % 100) / 50.0f - 1.0f) * g_shake_intensity;
        shake_y = ((float)(rand() % 100) / 50.0f - 1.0f) * g_shake_intensity;
        g_shake_duration -= g_dt;
    }

    // Apply camera
    float sx = (x - g_cam_x + shake_x) * g_cam_zoom + g_width * 0.5f;
    float sy = (y - g_cam_y + shake_y) * g_cam_zoom + g_height * 0.5f;
    float sw = w * g_cam_zoom;
    float sh = h * g_cam_zoom;

    int min_x = max(0, (int)(sx - sw * 0.5f));
    int max_x = min(g_width - 1, (int)(sx + sw * 0.5f));
    int min_y = max(0, (int)(sy - sh * 0.5f));
    int max_y = min(g_height - 1, (int)(sy + sh * 0.5f));

    for (int py = min_y; py <= max_y; py++) {
        for (int px = min_x; px <= max_x; px++) {
            float u = (px - (sx - sw * 0.5f)) / sw;
            float v = (py - (sy - sh * 0.5f)) / sh;
            int tx = min(tex.width - 1, max(0, (int)(u * tex.width)));
            int ty = min(tex.height - 1, max(0, (int)(v * tex.height)));
            uint32_t c = tex.pixels[ty * tex.width + tx];

            // Alpha blending
            int idx = py * g_width + px;
            uint32_t bg = g_framebuffer[idx];
            float a = ((c >> 24) & 0xFF) / 255.0f * alpha;
            uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * (1.0f - a));
            uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * (1.0f - a));
            uint8_t b = (uint8_t)((c & 0xFF) * a + (bg & 0xFF) * (1.0f - a));
            g_framebuffer[idx] = (r << 16) | (g << 8) | b;
        }
    }
}

void draw_frame(uint32_t tex_id, float fx, float fy, float fw, float fh, float x, float y, float w, float h) {
    if (!g_framebuffer || !g_textures.count(tex_id)) return;
    const G2DTexture& tex = g_textures[tex_id];

    float sx = (x - g_cam_x) * g_cam_zoom + g_width * 0.5f;
    float sy = (y - g_cam_y) * g_cam_zoom + g_height * 0.5f;
    float sw = w * g_cam_zoom;
    float sh = h * g_cam_zoom;

    int min_x = max(0, (int)(sx - sw * 0.5f));
    int max_x = min(g_width - 1, (int)(sx + sw * 0.5f));
    int min_y = max(0, (int)(sy - sh * 0.5f));
    int max_y = min(g_height - 1, (int)(sy + sh * 0.5f));

    for (int py = min_y; py <= max_y; py++) {
        for (int px = min_x; px <= max_x; px++) {
            float u = (px - (sx - sw * 0.5f)) / sw;
            float v = (py - (sy - sh * 0.5f)) / sh;
            int tx = min(tex.width - 1, max(0, (int)(fx + u * fw)));
            int ty = min(tex.height - 1, max(0, (int)(fy + v * fh)));
            uint32_t c = tex.pixels[ty * tex.width + tx];
            g_framebuffer[py * g_width + px] = c;
        }
    }
}

void emit_particles(float x, float y, int count, uint32_t color, float speed, float lifetime) {
    for (int i = 0; i < count; i++) {
        G2DParticle p;
        p.x = x;
        p.y = y;
        float ang = (float)(rand() % 360) * 3.14159f / 180.0f;
        float spd = speed * (0.5f + (float)(rand() % 100) / 100.0f);
        p.vx = cos(ang) * spd;
        p.vy = sin(ang) * spd;
        p.life = lifetime * (0.8f + (float)(rand() % 40) / 100.0f);
        p.max_life = p.life;
        p.color = color;
        g_particles.push_back(p);
    }
}

void update_particles(float delta) {
    if (!g_framebuffer) return;
    for (size_t i = 0; i < g_particles.size();) {
        auto& p = g_particles[i];
        p.life -= delta;
        if (p.life <= 0.0f) {
            g_particles[i] = g_particles.back();
            g_particles.pop_back();
            continue;
        }
        p.x += p.vx * delta;
        p.y += p.vy * delta;
        p.vy += 50.0f * delta; // particle gravity

        // Render particle dot
        int px = (int)((p.x - g_cam_x) * g_cam_zoom + g_width * 0.5f);
        int py = (int)((p.y - g_cam_y) * g_cam_zoom + g_height * 0.5f);
        if (px >= 0 && px < g_width && py >= 0 && py < g_height) {
            g_framebuffer[py * g_width + px] = p.color;
        }
        i++;
    }
}

static uint32_t g_active_fill_color = 0xFFFFFF;

static uint32_t parse_hex_color_u32(const std::string& hex) {
    if (hex.empty()) return 0xFFFFFF;
    const char* s = hex.c_str();
    if (s[0] == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return static_cast<uint32_t>(strtoul(s, nullptr, 16)) & 0x00FFFFFFu;
}

static inline void put_pixel_2d(int x, int y, uint32_t color) {
    if (!g_framebuffer || x < 0 || x >= g_width || y < 0 || y >= g_height) return;
    g_framebuffer[y * g_width + x] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!g_framebuffer || w <= 0 || h <= 0) return;
    int x0 = max(0, x);
    int y0 = max(0, y);
    int x1 = min(g_width, x + w);
    int y1 = min(g_height, y + h);
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            g_framebuffer[py * g_width + px] = color;
        }
    }
}

void draw_circle(int cx, int cy, int r, uint32_t color) {
    if (!g_framebuffer || r <= 0) return;
    int r2 = r * r;
    int y0 = max(0, cy - r);
    int y1 = min(g_height - 1, cy + r);
    int x0 = max(0, cx - r);
    int x1 = min(g_width - 1, cx + r);
    for (int py = y0; py <= y1; ++py) {
        int dy = py - cy;
        for (int px = x0; px <= x1; ++px) {
            int dx = px - cx;
            if (dx * dx + dy * dy <= r2) {
                g_framebuffer[py * g_width + px] = color;
            }
        }
    }
}

void draw_line(int x1, int y1, int x2, int y2, uint32_t color, int width) {
    if (!g_framebuffer) return;
    int dx = abs(x2 - x1), sx = (x1 < x2) ? 1 : -1;
    int dy = -abs(y2 - y1), sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;
    int half = max(0, (width - 1) / 2);
    while (true) {
        if (half == 0) {
            put_pixel_2d(x1, y1, color);
        } else {
            draw_rect(x1 - half, y1 - half, width, width, color);
        }
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void draw_text(int x, int y, const std::string& str, int size, uint32_t color) {
#if defined(_WIN32)
    if (g_hdc && !str.empty()) {
        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;
        SetBkMode(g_hdc, TRANSPARENT);
        SetTextColor(g_hdc, RGB(r, g, b));
        HFONT hFont = CreateFontA(-(size > 0 ? size : 16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Consolas");
        HFONT oldFont = (HFONT)SelectObject(g_hdc, hFont);
        TextOutA(g_hdc, x, y, str.c_str(), (int)str.size());
        SelectObject(g_hdc, oldFont);
        DeleteObject(hFont);
    }
#else
    (void)x; (void)y; (void)str; (void)size; (void)color;
#endif
}

void close_window() {
    g_is_open = false;
#if defined(_WIN32)
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
#endif
}

bool create_canvas(int w, int h) {
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;
    g_width = w;
    g_height = h;
    g_pitch = w * 4;
    if (g_framebuffer) {
        delete[] g_framebuffer;
    }
    g_framebuffer = new uint32_t[w * h];
    clear(0.0f, 0.0f, 0.0f);
    return true;
}

void fill_color(const std::string& hex_color) {
    g_active_fill_color = parse_hex_color_u32(hex_color);
}

void canvas_rect(int x, int y, int w, int h) {
    draw_rect(x, y, w, h, g_active_fill_color);
}

void canvas_circle(int cx, int cy, int r) {
    draw_circle(cx, cy, r, g_active_fill_color);
}

void canvas_line(int x1, int y1, int x2, int y2) {
    draw_line(x1, y1, x2, y2, g_active_fill_color, 1);
}

bool save_image(const std::string& path) {
    if (!g_framebuffer || g_width <= 0 || g_height <= 0 || path.empty()) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t row_size = (uint32_t)(g_width * 3);
    uint32_t pad = (4 - (row_size % 4)) % 4;
    uint32_t data_size = (row_size + pad) * (uint32_t)g_height;
    uint32_t file_size = 54 + data_size;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size & 0xFF; hdr[3] = (file_size >> 8) & 0xFF;
    hdr[4] = (file_size >> 16) & 0xFF; hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = g_width & 0xFF; hdr[19] = (g_width >> 8) & 0xFF;
    hdr[20] = (g_width >> 16) & 0xFF; hdr[21] = (g_width >> 24) & 0xFF;
    hdr[22] = g_height & 0xFF; hdr[23] = (g_height >> 8) & 0xFF;
    hdr[24] = (g_height >> 16) & 0xFF; hdr[25] = (g_height >> 24) & 0xFF;
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    uint8_t zeros[3] = {0, 0, 0};
    for (int y = g_height - 1; y >= 0; --y) {
        for (int x = 0; x < g_width; ++x) {
            uint32_t c = g_framebuffer[y * g_width + x];
            uint8_t bgr[3] = { (uint8_t)(c & 0xFF), (uint8_t)((c >> 8) & 0xFF), (uint8_t)((c >> 16) & 0xFF) };
            fwrite(bgr, 1, 3, f);
        }
        if (pad > 0) fwrite(zeros, 1, pad, f);
    }
    fclose(f);
    return true;
}

}

// Backward compatibility implementations
bool Grass2DGraphics::init(const std::string& title, int width, int height) {
    return Grass2D::init_window(width, height, title);
}
bool Grass2DGraphics::running() { return Grass2D::is_open(); }
void Grass2DGraphics::clear(const std::string& hex) {
    uint32_t c = Grass2D::parse_hex_color_u32(hex);
    Grass2D::clear(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f);
}
void Grass2DGraphics::rect(int x, int y, int w, int h, const std::string& hex) {
    Grass2D::draw_rect(x, y, w, h, Grass2D::parse_hex_color_u32(hex));
}
void Grass2DGraphics::circle(int x, int y, int r, const std::string& hex) {
    Grass2D::draw_circle(x, y, r, Grass2D::parse_hex_color_u32(hex));
}
void Grass2DGraphics::line(int x1, int y1, int x2, int y2, const std::string& hex, int w) {
    Grass2D::draw_line(x1, y1, x2, y2, Grass2D::parse_hex_color_u32(hex), w);
}
void Grass2DGraphics::text(int x, int y, const std::string& str, int sz, const std::string& hex) {
    Grass2D::draw_text(x, y, str, sz, Grass2D::parse_hex_color_u32(hex));
}
void Grass2DGraphics::draw_svg(const std::string& p, int x, int y, int w, int h) {
    (void)p;
    Grass2D::draw_rect(x, y, w, h, Grass2D::g_active_fill_color);
}
bool Grass2DGraphics::key(const std::string& k) { return Grass2D::key_down(k); }
int Grass2DGraphics::mouse_x() { return Grass2D::mouse_x(); }
int Grass2DGraphics::mouse_y() { return Grass2D::mouse_y(); }
bool Grass2DGraphics::mouse_down(const std::string& b) { return Grass2D::mouse_btn(b == "right" ? 1 : 0); }
bool Grass2DGraphics::collide(double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}
void Grass2DGraphics::frame(int target_fps) { (void)target_fps; Grass2D::present(); }
void Grass2DGraphics::close() { Grass2D::close_window(); }

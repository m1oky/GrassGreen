#include "Grass2DGraphics.h"
#include <map>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <algorithm>

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
static float g_dt = 0.016f;
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
static HDC g_mem_dc = NULL;
static HBITMAP g_dib = NULL;

static LRESULT CALLBACK WndProc2D(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        g_is_open = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif

void bind_framebuffer(void* addr, int w, int h, int pitch) {
    g_framebuffer = (uint32_t*)addr;
    g_width = w;
    g_height = h;
    g_pitch = (pitch > 0) ? pitch : w * 4;
}

bool init_window(int w, int h, const string& title) {
    g_width = w;
    g_height = h;
    g_pitch = w * 4;

#if defined(_WIN32)
    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc2D;
    wc.hInstance = hInst;
    wc.lpszClassName = "Grass2DClass";
    RegisterClass(&wc);

    RECT rc = {0, 0, w, h};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindow("Grass2DClass", title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                          NULL, NULL, hInst, NULL);
    if (!g_hwnd) return false;

    g_hdc = GetDC(g_hwnd);
    g_mem_dc = CreateCompatibleDC(g_hdc);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    g_dib = CreateDIBSection(g_mem_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(g_mem_dc, g_dib);
    g_framebuffer = (uint32_t*)bits;
#else
    if (!g_framebuffer) {
        g_framebuffer = new uint32_t[w * h];
    }
#endif

    g_is_open = true;
    g_last_time = chrono::high_resolution_clock::now();
    return true;
}

bool is_open() {
#if defined(_WIN32)
    if (g_hwnd) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
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
    if (g_hwnd && g_mem_dc && g_hdc) {
        BitBlt(g_hdc, 0, 0, g_width, g_height, g_mem_dc, 0, 0, SRCCOPY);
    }
#endif
    auto now = chrono::high_resolution_clock::now();
    g_dt = chrono::duration<float>(now - g_last_time).count();
    g_last_time = now;
}

float dt() {
    return g_dt > 0.0001f ? g_dt : 0.016f;
}

bool key_down(const string& key) {
#if defined(_WIN32)
    if (key == "SPACE") return (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (key == "ENTER") return (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    if (key == "UP")    return (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    if (key == "DOWN")  return (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    if (key == "LEFT")  return (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    if (key == "RIGHT") return (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
    if (key == "W" || key == "w") return (GetAsyncKeyState('W') & 0x8000) != 0;
    if (key == "A" || key == "a") return (GetAsyncKeyState('A') & 0x8000) != 0;
    if (key == "S" || key == "s") return (GetAsyncKeyState('S') & 0x8000) != 0;
    if (key == "D" || key == "d") return (GetAsyncKeyState('D') & 0x8000) != 0;
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

}

// Backward compatibility implementations
bool Grass2DGraphics::init(const std::string& title, int width, int height) {
    return Grass2D::init_window(width, height, title);
}
bool Grass2DGraphics::running() { return Grass2D::is_open(); }
void Grass2DGraphics::clear(const std::string& hex) { Grass2D::clear(0, 0, 0); }
void Grass2DGraphics::rect(int x, int y, int w, int h, const std::string& hex) {}
void Grass2DGraphics::circle(int x, int y, int r, const std::string& hex) {}
void Grass2DGraphics::line(int x1, int y1, int x2, int y2, const std::string& hex, int w) {}
void Grass2DGraphics::text(int x, int y, const std::string& str, int sz, const std::string& hex) {}
void Grass2DGraphics::draw_svg(const std::string& p, int x, int y, int w, int h) {}
bool Grass2DGraphics::key(const std::string& k) { return Grass2D::key_down(k); }
int Grass2DGraphics::mouse_x() { return Grass2D::mouse_x(); }
int Grass2DGraphics::mouse_y() { return Grass2D::mouse_y(); }
bool Grass2DGraphics::mouse_down(const std::string& b) { return Grass2D::mouse_btn(b == "right" ? 1 : 0); }
bool Grass2DGraphics::collide(double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2) {
    return (x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2);
}
void Grass2DGraphics::frame(int target_fps) { Grass2D::present(); }
void Grass2DGraphics::close() {}

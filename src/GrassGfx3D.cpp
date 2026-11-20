#include "GrassGfx3D.h"
#include <map>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace std;

namespace GrassGfx3D {

static int g_width = 800;
static int g_height = 600;
static bool g_is_open = false;
static float g_dt = 1.0f / 60.0f;
static auto g_last_time = chrono::high_resolution_clock::now();

static uint32_t* g_framebuffer = nullptr;
static float* g_zbuffer = nullptr;
static bool g_custom_fb = false;
static int g_pitch = 0;

static GrassVec3 g_cam_pos(0, 2, 5);
static GrassVec3 g_cam_target(0, 0, 0);
static float g_cam_fov = 60.0f;

static GfxLight g_dir_light{true, GrassVec3(0.5f, 1.0f, 0.5f), GrassVec3(1, 1, 1), 1.0f};
static GfxLight g_point_light{false, GrassVec3(0, 3, 0), GrassVec3(1, 1, 0.8f), 10.0f};
static float g_fog_density = 0.0f;
static GrassVec3 g_fog_color(0.5f, 0.6f, 0.7f);
static uint32_t g_skybox_top = 0x223355;
static uint32_t g_skybox_bottom = 0x111122;

static map<uint32_t, GfxMesh> g_meshes;
static uint32_t g_next_mesh_id = 1;

#if defined(_WIN32)
static HWND g_hwnd = NULL;
static HDC g_hdc = NULL;
static BITMAPINFO g_bmi = {};

static LRESULT CALLBACK WndProc3D(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
                int cw = cr.right - cr.left;
                int ch = cr.bottom - cr.top;
                StretchDIBits(
                    hdc,
                    0, 0, cw, ch,
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
    g_custom_fb = true;
    delete[] g_zbuffer;
    g_zbuffer = new float[w * h];
}

bool init_window(int w, int h, const string& title) {
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;
    g_width = w;
    g_height = h;
    g_pitch = w * 4;

    delete[] g_zbuffer;
    g_zbuffer = new float[w * h];

    if (!g_custom_fb) {
        delete[] g_framebuffer;
        g_framebuffer = new uint32_t[w * h];
    }

#if defined(_WIN32)
    HINSTANCE hInst = GetModuleHandleA(NULL);
    const char* className = "GrassGreen_Gfx3D_Window";

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc3D;
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

    string win_title = title.empty() ? "GrassGreen 3D Window" : title;

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
    int total_pixels = g_width * g_height;
    for (int i = 0; i < total_pixels; i++) {
        g_framebuffer[i] = col;
        g_zbuffer[i] = 10000.0f;
    }
}

void present() {
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

void set_camera(const GrassVec3& pos, const GrassVec3& target, float fov_deg) {
    g_cam_pos = pos;
    g_cam_target = target;
    g_cam_fov = fov_deg;
}

bool key_down(int vk) {
#if defined(_WIN32)
    if (vk >= 'a' && vk <= 'z') vk = toupper(vk);
    if (vk > 0 && vk < 256) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
#elif defined(__linux__)
    return false;
#else
    return false;
#endif
    return false;
}

bool key_down_str(const string& key) {
#if defined(_WIN32)
    char* endp = nullptr;
    long vk = strtol(key.c_str(), &endp, 10);
    if (endp && *endp == '\0' && vk > 0 && vk < 256) {
        return (GetAsyncKeyState((int)vk) & 0x8000) != 0;
    }
    if (key == "SPACE" || key == "space") return (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (key == "SHIFT" || key == "shift") return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (key == "ENTER" || key == "enter") return (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    if (key == "ESCAPE" || key == "ESC" || key == "esc") return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (key == "UP" || key == "up") return (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    if (key == "DOWN" || key == "down") return (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    if (key == "LEFT" || key == "left") return (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    if (key == "RIGHT" || key == "right") return (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
    if (key == "W" || key == "w") return (GetAsyncKeyState('W') & 0x8000) != 0;
    if (key == "A" || key == "a") return (GetAsyncKeyState('A') & 0x8000) != 0;
    if (key == "S" || key == "s") return (GetAsyncKeyState('S') & 0x8000) != 0;
    if (key == "D" || key == "d") return (GetAsyncKeyState('D') & 0x8000) != 0;
    if (key == "R" || key == "r") return (GetAsyncKeyState('R') & 0x8000) != 0;
    if (key.size() == 1) {
        char c = (char)toupper((unsigned char)key[0]);
        return (GetAsyncKeyState(c) & 0x8000) != 0;
    }
#elif defined(__linux__)
    return false;
#else
    return false;
#endif
    return false;
}

int mouse_x() {
#if defined(_WIN32)
    POINT p;
    if (g_hwnd && GetCursorPos(&p)) {
        ScreenToClient(g_hwnd, &p);
        RECT cr;
        GetClientRect(g_hwnd, &cr);
        int cw = cr.right - cr.left;
        if (cw > 0 && g_width > 0) {
            return (int)((float)p.x * ((float)g_width / (float)cw));
        }
        return p.x;
    }
#elif defined(__linux__)
    return 0;
#else
    return 0;
#endif
    return 0;
}

int mouse_y() {
#if defined(_WIN32)
    POINT p;
    if (g_hwnd && GetCursorPos(&p)) {
        ScreenToClient(g_hwnd, &p);
        RECT cr;
        GetClientRect(g_hwnd, &cr);
        int ch = cr.bottom - cr.top;
        if (ch > 0 && g_height > 0) {
            return (int)((float)p.y * ((float)g_height / (float)ch));
        }
        return p.y;
    }
#elif defined(__linux__)
    return 0;
#else
    return 0;
#endif
    return 0;
}

bool mouse_btn(int btn) {
#if defined(_WIN32)
    int vk = (btn == 0) ? VK_LBUTTON : ((btn == 1) ? VK_RBUTTON : VK_MBUTTON);
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#elif defined(__linux__)
    return false;
#else
    return false;
#endif
}

uint32_t create_box(float sx, float sy, float sz) {
    uint32_t id = g_next_mesh_id++;
    GfxMesh mesh;
    mesh.id = id;
    mesh.scale = GrassVec3(1, 1, 1);
    mesh.color = 0x22c55e;
    float hx = sx * 0.5f;
    float hy = sy * 0.5f;
    float hz = sz * 0.5f;

    // 8 box vertices
    GrassVec3 p[8] = {
        {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, {-hx,  hy, -hz},
        {-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz}
    };
    uint32_t ind[36] = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6,
        4,5,1, 4,1,0, 3,2,6, 3,6,7,
        1,5,6, 1,6,2, 4,0,3, 4,3,7
    };
    for (int i = 0; i < 36; i++) {
        GfxVertex v;
        v.pos = p[ind[i]];
        v.normal = GrassMath3D::normalize(v.pos);
        v.u = 0; v.v = 0;
        v.color = mesh.color;
        mesh.vertices.push_back(v);
        mesh.indices.push_back(i);
    }
    g_meshes[id] = mesh;
    return id;
}

uint32_t create_cube(float size) {
    return create_box(size, size, size);
}

uint32_t create_sphere(float radius, int segments) {
    uint32_t id = g_next_mesh_id++;
    GfxMesh mesh;
    mesh.id = id;
    mesh.scale = GrassVec3(1, 1, 1);
    mesh.color = 0x3b82f6;

    if (segments < 4) segments = 8;
    for (int j = 0; j <= segments; j++) {
        float theta = (float)j * 3.14159f / (float)segments;
        float sinT = sin(theta);
        float cosT = cos(theta);
        for (int i = 0; i <= segments; i++) {
            float phi = (float)i * 6.28318f / (float)segments;
            GfxVertex v;
            v.normal = GrassVec3(sinT * cos(phi), cosT, sinT * sin(phi));
            v.pos = GrassMath3D::mul_scalar(v.normal, radius);
            v.u = (float)i / (float)segments;
            v.v = (float)j / (float)segments;
            v.color = mesh.color;
            mesh.vertices.push_back(v);
        }
    }
    for (int j = 0; j < segments; j++) {
        for (int i = 0; i < segments; i++) {
            uint32_t a = j * (segments + 1) + i;
            uint32_t b = a + 1;
            uint32_t c = (j + 1) * (segments + 1) + i;
            uint32_t d = c + 1;
            mesh.indices.push_back(a); mesh.indices.push_back(c); mesh.indices.push_back(b);
            mesh.indices.push_back(b); mesh.indices.push_back(c); mesh.indices.push_back(d);
        }
    }
    g_meshes[id] = mesh;
    return id;
}

uint32_t create_plane(float w, float d, int segments) {
    if (segments < 1) segments = 1;
    if (segments > 64) segments = 64;

    uint32_t id = g_next_mesh_id++;
    GfxMesh mesh;
    mesh.id = id;
    mesh.scale = GrassVec3(1, 1, 1);
    mesh.color = 0x475569;

    float hw = w * 0.5f;
    float hd = d * 0.5f;

    for (int j = 0; j <= segments; j++) {
        float fz = (float)j / (float)segments;
        float z = -hd + fz * d;
        for (int i = 0; i <= segments; i++) {
            float fx = (float)i / (float)segments;
            float x = -hw + fx * w;
            GfxVertex v;
            v.pos = GrassVec3(x, 0.0f, z);
            v.normal = GrassVec3(0.0f, 1.0f, 0.0f);
            v.u = fx;
            v.v = fz;
            v.color = mesh.color;
            mesh.vertices.push_back(v);
        }
    }

    for (int j = 0; j < segments; j++) {
        for (int i = 0; i < segments; i++) {
            uint32_t v0 = (uint32_t)(j * (segments + 1) + i);
            uint32_t v1 = v0 + 1;
            uint32_t v2 = (uint32_t)((j + 1) * (segments + 1) + i);
            uint32_t v3 = v2 + 1;

            mesh.indices.push_back(v0);
            mesh.indices.push_back(v2);
            mesh.indices.push_back(v1);

            mesh.indices.push_back(v1);
            mesh.indices.push_back(v2);
            mesh.indices.push_back(v3);
        }
    }

    g_meshes[id] = mesh;
    return id;
}

uint32_t load_obj(const string& path) {
    uint32_t id = g_next_mesh_id++;
    GfxMesh mesh;
    mesh.id = id;
    mesh.scale = GrassVec3(1, 1, 1);
    mesh.color = 0xcccccc;

    ifstream ifs(path);
    if (!ifs.is_open()) {
        return create_cube(1.0f);
    }
    vector<GrassVec3> positions;
    string line;
    while (getline(ifs, line)) {
        stringstream ss(line);
        string prefix;
        ss >> prefix;
        if (prefix == "v") {
            float x, y, z; ss >> x >> y >> z;
            positions.emplace_back(x, y, z);
        } else if (prefix == "f") {
            string s1, s2, s3; ss >> s1 >> s2 >> s3;
            auto parseIdx = [](const string& s) -> int {
                return stoi(s.substr(0, s.find('/'))) - 1;
            };
            int i1 = parseIdx(s1), i2 = parseIdx(s2), i3 = parseIdx(s3);
            if (i1 >= 0 && i1 < (int)positions.size() &&
                i2 >= 0 && i2 < (int)positions.size() &&
                i3 >= 0 && i3 < (int)positions.size()) {
                GfxVertex v1{positions[i1], GrassVec3(0,1,0), 0,0, mesh.color};
                GfxVertex v2{positions[i2], GrassVec3(0,1,0), 0,0, mesh.color};
                GfxVertex v3{positions[i3], GrassVec3(0,1,0), 0,0, mesh.color};
                uint32_t base = (uint32_t)mesh.vertices.size();
                mesh.vertices.push_back(v1); mesh.vertices.push_back(v2); mesh.vertices.push_back(v3);
                mesh.indices.push_back(base); mesh.indices.push_back(base + 1); mesh.indices.push_back(base + 2);
            }
        }
    }
    g_meshes[id] = mesh;
    return id;
}

void set_texture(uint32_t mesh_id, const string& path) {
    if (g_meshes.count(mesh_id)) g_meshes[mesh_id].texture_path = path;
}

void set_color(uint32_t mesh_id, float r, float g, float b) {
    if (g_meshes.count(mesh_id)) {
        g_meshes[mesh_id].color = ((uint8_t)(r * 255) << 16) | ((uint8_t)(g * 255) << 8) | (uint8_t)(b * 255);
    }
}

void transform(uint32_t mesh_id, const GrassVec3& pos, const GrassVec3& rot, const GrassVec3& scale) {
    if (g_meshes.count(mesh_id)) {
        g_meshes[mesh_id].pos = pos;
        g_meshes[mesh_id].rot = rot;
        g_meshes[mesh_id].scale = scale;
        g_meshes[mesh_id].has_custom_matrix = false;
    }
}

void transform_mat(uint32_t mesh_id, const GrassMat4& mat) {
    if (g_meshes.count(mesh_id)) {
        g_meshes[mesh_id].model_matrix = mat;
        g_meshes[mesh_id].has_custom_matrix = true;
    }
}

void set_dir_light(const GrassVec3& dir, float r, float g, float b, float intensity) {
    g_dir_light.pos_or_dir = GrassMath3D::normalize(dir);
    g_dir_light.color = GrassVec3(r, g, b);
    g_dir_light.intensity_or_range = intensity;
}

void set_point_light(const GrassVec3& pos, float r, float g, float b, float range) {
    g_point_light.pos_or_dir = pos;
    g_point_light.color = GrassVec3(r, g, b);
    g_point_light.intensity_or_range = range;
}

void set_fog(float r, float g, float b, float density) {
    g_fog_color = GrassVec3(r, g, b);
    g_fog_density = density;
}

void set_skybox(uint32_t top_rgb, uint32_t bottom_rgb) {
    g_skybox_top = top_rgb;
    g_skybox_bottom = bottom_rgb;
}

void draw(uint32_t mesh_id) {
    if (!g_framebuffer || !g_zbuffer || !g_meshes.count(mesh_id)) return;
    const GfxMesh& mesh = g_meshes[mesh_id];

    GrassMat4 model;
    if (mesh.has_custom_matrix) {
        model = mesh.model_matrix;
    } else {
        model = GrassMath3D::multiply(
            GrassMath3D::translate(mesh.pos),
            GrassMath3D::multiply(
                GrassMath3D::rotate(mesh.rot.y, GrassVec3(0, 1, 0)),
                GrassMath3D::scale(mesh.scale)
            )
        );
    }
    GrassMat4 view = GrassMath3D::look_at(g_cam_pos, g_cam_target, GrassVec3(0, 1, 0));
    float aspect = (float)g_width / (float)(g_height > 0 ? g_height : 1);
    GrassMat4 proj = GrassMath3D::perspective(g_cam_fov * 3.14159f / 180.0f, aspect, 0.1f, 100.0f);
    GrassMat4 mvp = GrassMath3D::multiply(proj, GrassMath3D::multiply(view, model));

    float hw = g_width * 0.5f;
    float hh = g_height * 0.5f;

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        GrassVec4 v0 = GrassMath3D::transform_vec4(mvp, GrassVec4(mesh.vertices[mesh.indices[i]].pos.x, mesh.vertices[mesh.indices[i]].pos.y, mesh.vertices[mesh.indices[i]].pos.z, 1.0f));
        GrassVec4 v1 = GrassMath3D::transform_vec4(mvp, GrassVec4(mesh.vertices[mesh.indices[i+1]].pos.x, mesh.vertices[mesh.indices[i+1]].pos.y, mesh.vertices[mesh.indices[i+1]].pos.z, 1.0f));
        GrassVec4 v2 = GrassMath3D::transform_vec4(mvp, GrassVec4(mesh.vertices[mesh.indices[i+2]].pos.x, mesh.vertices[mesh.indices[i+2]].pos.y, mesh.vertices[mesh.indices[i+2]].pos.z, 1.0f));

        const float w_near = 0.05f;

        // 1. Trivial rejection ONLY when ALL THREE vertices are outside the same frustum plane
        if (v0.w < w_near && v1.w < w_near && v2.w < w_near) continue;
        if (v0.x < -v0.w && v1.x < -v1.w && v2.x < -v2.w) continue;
        if (v0.x >  v0.w && v1.x >  v1.w && v2.x >  v2.w) continue;
        if (v0.y < -v0.w && v1.y < -v1.w && v2.y < -v2.w) continue;
        if (v0.y >  v0.w && v1.y >  v1.w && v2.y >  v2.w) continue;

        // 2. Shading with face normal calculation and ambient illumination
        GrassVec3 p0 = mesh.vertices[mesh.indices[i]].pos;
        GrassVec3 p1 = mesh.vertices[mesh.indices[i+1]].pos;
        GrassVec3 p2 = mesh.vertices[mesh.indices[i+2]].pos;

        GrassVec4 w0 = GrassMath3D::transform_vec4(model, GrassVec4(p0.x, p0.y, p0.z, 1.0f));
        GrassVec4 w1 = GrassMath3D::transform_vec4(model, GrassVec4(p1.x, p1.y, p1.z, 1.0f));
        GrassVec4 w2 = GrassMath3D::transform_vec4(model, GrassVec4(p2.x, p2.y, p2.z, 1.0f));

        GrassVec3 e1(w1.x - w0.x, w1.y - w0.y, w1.z - w0.z);
        GrassVec3 e2(w2.x - w0.x, w2.y - w0.y, w2.z - w0.z);
        GrassVec3 face_norm = GrassMath3D::cross(e1, e2);
        float norm_len = GrassMath3D::length(face_norm);
        GrassVec3 norm = (norm_len > 0.0001f) ? GrassMath3D::mul_scalar(face_norm, 1.0f / norm_len) : mesh.vertices[mesh.indices[i]].normal;

        // Ambient Light ~0.28 ensures shadow sides remain clear and readable
        float ambient = 0.28f;
        GrassVec3 light_dir = GrassMath3D::normalize(g_dir_light.pos_or_dir);
        if (light_dir.y < 0.0f) light_dir = GrassMath3D::mul_scalar(light_dir, -1.0f);
        float NdotL = GrassMath3D::dot(norm, light_dir);
        if (NdotL < 0.0f) NdotL = 0.0f;
        float diff = ambient + (1.0f - ambient) * (NdotL * g_dir_light.intensity_or_range);
        if (diff > 1.0f) diff = 1.0f;

        uint8_t cr = (uint8_t)(min(255.0f, ((mesh.color >> 16) & 0xFF) * diff));
        uint8_t cg = (uint8_t)(min(255.0f, ((mesh.color >> 8) & 0xFF) * diff));
        uint8_t cb = (uint8_t)(min(255.0f, (mesh.color & 0xFF) * diff));
        uint32_t final_color = (cr << 16) | (cg << 8) | cb;

        // 3. Sutherland-Hodgman clipping against near plane (w >= w_near)
        bool in0 = (v0.w >= w_near);
        bool in1 = (v1.w >= w_near);
        bool in2 = (v2.w >= w_near);
        int in_count = (in0 ? 1 : 0) + (in1 ? 1 : 0) + (in2 ? 1 : 0);

        if (in_count == 0) continue;

        auto clip_edge = [w_near](const GrassVec4& a, const GrassVec4& b) -> GrassVec4 {
            float denom = b.w - a.w;
            float t = (std::fabs(denom) > 1e-6f) ? ((w_near - a.w) / denom) : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            return GrassVec4(
                a.x + t * (b.x - a.x),
                a.y + t * (b.y - a.y),
                a.z + t * (b.z - a.z),
                w_near
            );
        };

        struct ClippedTri {
            GrassVec4 a, b, c;
        };
        ClippedTri clipped_tris[2];
        int num_clipped = 0;

        if (in_count == 3) {
            clipped_tris[0] = {v0, v1, v2};
            num_clipped = 1;
        } else if (in_count == 1) {
            GrassVec4 vin, vout1, vout2;
            if (in0)      { vin = v0; vout1 = v1; vout2 = v2; }
            else if (in1) { vin = v1; vout1 = v2; vout2 = v0; }
            else          { vin = v2; vout1 = v0; vout2 = v1; }

            GrassVec4 i1 = clip_edge(vin, vout1);
            GrassVec4 i2 = clip_edge(vout2, vin);
            clipped_tris[0] = {vin, i1, i2};
            num_clipped = 1;
        } else if (in_count == 2) {
            GrassVec4 vin1, vin2, vout;
            if (!in0)      { vin1 = v1; vin2 = v2; vout = v0; }
            else if (!in1) { vin1 = v2; vin2 = v0; vout = v1; }
            else           { vin1 = v0; vin2 = v1; vout = v2; }

            GrassVec4 i1 = clip_edge(vin2, vout);
            GrassVec4 i2 = clip_edge(vout, vin1);
            clipped_tris[0] = {vin1, vin2, i1};
            clipped_tris[1] = {vin1, i1, i2};
            num_clipped = 2;
        }

        // 4. Perspective divide and rasterization of clipped triangles
        for (int t = 0; t < num_clipped; t++) {
            const GrassVec4& c0 = clipped_tris[t].a;
            const GrassVec4& c1 = clipped_tris[t].b;
            const GrassVec4& c2 = clipped_tris[t].c;

            float inv_w0 = 1.0f / c0.w;
            float sx0 = (c0.x * inv_w0) * hw + hw;
            float sy0 = -(c0.y * inv_w0) * hh + hh;
            float z0 = (c0.z * inv_w0) * 0.5f + 0.5f;

            float inv_w1 = 1.0f / c1.w;
            float sx1 = (c1.x * inv_w1) * hw + hw;
            float sy1 = -(c1.y * inv_w1) * hh + hh;
            float z1 = (c1.z * inv_w1) * 0.5f + 0.5f;

            float inv_w2 = 1.0f / c2.w;
            float sx2 = (c2.x * inv_w2) * hw + hw;
            float sy2 = -(c2.y * inv_w2) * hh + hh;
            float z2 = (c2.z * inv_w2) * 0.5f + 0.5f;

            float area = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
            if (std::fabs(area) < 0.0001f) continue;

            int min_x = max(0, (int)floor(min({sx0, sx1, sx2})));
            int max_x = min(g_width - 1, (int)ceil(max({sx0, sx1, sx2})));
            int min_y = max(0, (int)floor(min({sy0, sy1, sy2})));
            int max_y = min(g_height - 1, (int)ceil(max({sy0, sy1, sy2})));
            if (min_x > max_x || min_y > max_y) continue;

            float inv_area = 1.0f / area;
            for (int y = min_y; y <= max_y; y++) {
                for (int x = min_x; x <= max_x; x++) {
                    float px = (float)x + 0.5f;
                    float py = (float)y + 0.5f;
                    float b0 = ((sx1 - px) * (sy2 - py) - (sy1 - py) * (sx2 - px)) * inv_area;
                    float b1 = ((sx2 - px) * (sy0 - py) - (sy2 - py) * (sx0 - px)) * inv_area;
                    float b2 = 1.0f - b0 - b1;
                    if (b0 >= 0.0f && b1 >= 0.0f && b2 >= 0.0f) {
                        float z = b0 * z0 + b1 * z1 + b2 * z2;
                        int idx = y * g_width + x;
                        if (z < g_zbuffer[idx] && z >= 0.0f) {
                            g_zbuffer[idx] = z;
                            g_framebuffer[idx] = final_color;
                        }
                    }
                }
            }
        }
    }
}

}

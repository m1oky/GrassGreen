#ifndef GRASS_2D_GRAPHICS_H
#define GRASS_2D_GRAPHICS_H

#include <windows.h>
#include <string>
#include <cstdint>

class Grass2DGraphics {
private:
    static HWND hwnd;
    static HDC hdc_screen;
    static HDC hdc_mem;
    static HBITMAP hbm_mem;
    static HBITMAP hbm_old;
    static int win_width;
    static int win_height;
    static bool is_running;
    static uint64_t last_frame_time;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static COLORREF parseColor(const std::string& hex);
    static int parseVkKey(const std::string& key);

public:
    static bool init(const std::string& title, int width, int height);
    static bool running();
    static void clear(const std::string& hex_color);
    static void rect(int x, int y, int w, int h, const std::string& hex_color);
    static void circle(int x, int y, int r, const std::string& hex_color);
    static void line(int x1, int y1, int x2, int y2, const std::string& hex_color, int width);
    static void text(int x, int y, const std::string& str, int size, const std::string& hex_color);
    static bool key(const std::string& key_name);
    static int mouse_x();
    static int mouse_y();
    static bool mouse_down(const std::string& btn);
    static bool collide(double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2);
    static void frame(int target_fps);
    static void close();
};

#endif // GRASS_2D_GRAPHICS_H
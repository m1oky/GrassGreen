#ifndef GRASS_2D_GRAPHICS_H
#define GRASS_2D_GRAPHICS_H

#include <string>
#include <vector>
#include <cstdint>

struct G2DParticle {
    float x, y;
    float vx, vy;
    float life;
    float max_life;
    uint32_t color;
};

struct G2DTexture {
    uint32_t id;
    int width;
    int height;
    std::vector<uint32_t> pixels;
};

namespace Grass2D {
    bool init_window(int w, int h, const std::string& title);
    bool is_open();
    void clear(float r, float g, float b);
    void present();
    float dt();
    bool key_down(const std::string& key);
    int mouse_x();
    int mouse_y();
    bool mouse_btn(int btn);

    uint32_t load_tex(const std::string& path);
    void draw_sprite(uint32_t tex_id, float x, float y, float w, float h, float angle, float alpha);
    void draw_frame(uint32_t tex_id, float fx, float fy, float fw, float fh, float x, float y, float w, float h);

    void cam_set(float x, float y, float zoom, float angle);
    void cam_shake(float intensity, float duration);
    void emit_particles(float x, float y, int count, uint32_t color, float speed, float lifetime);
    void update_particles(float delta);
    void bind_framebuffer(void* addr, int w, int h, int pitch);

    void draw_rect(int x, int y, int w, int h, uint32_t color);
    void draw_circle(int cx, int cy, int r, uint32_t color);
    void draw_line(int x1, int y1, int x2, int y2, uint32_t color, int width = 1);
    void draw_text(int x, int y, const std::string& str, int size, uint32_t color);
    void close_window();

    bool create_canvas(int w, int h);
    void fill_color(const std::string& hex_color);
    void canvas_rect(int x, int y, int w, int h);
    void canvas_circle(int cx, int cy, int r);
    void canvas_line(int x1, int y1, int x2, int y2);
    bool save_image(const std::string& path);
}

// Backward compatibility class wrapper
class Grass2DGraphics {
public:
    static bool init(const std::string& title, int width, int height);
    static bool running();
    static void clear(const std::string& hex_color);
    static void rect(int x, int y, int w, int h, const std::string& hex_color);
    static void circle(int x, int y, int r, const std::string& hex_color);
    static void line(int x1, int y1, int x2, int y2, const std::string& hex_color, int width = 1);
    static void text(int x, int y, const std::string& str, int size, const std::string& hex_color);
    static void draw_svg(const std::string& path, int x, int y, int w, int h);
    static bool key(const std::string& key_name);
    static int mouse_x();
    static int mouse_y();
    static bool mouse_down(const std::string& btn);
    static bool collide(double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2);
    static void frame(int target_fps);
    static void close();
};

#endif // GRASS_2D_GRAPHICS_H

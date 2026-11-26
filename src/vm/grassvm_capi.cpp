#define GRASSVM_EXPORTS
#include "grassvm.h"
#include "frontend/GrassSyntax.h"
#include "frontend/GrassLangGen.h"
#include "media/Grass2DGraphics.h"
#include "media/GrassAudio.h"
#include "media/GrassMath3D.h"
#include "media/GrassGfx3D.h"
#include "media/GrassPhysics.h"
#include "media/gui_engine.h"
#include "runtime/GrassJson.h"
#include "runtime/GrassSys.h"
#include "runtime/GrassProc.h"
#include "runtime/GrassHardware.h"
#include "runtime/GrassTg.h"
#include "runtime/GrassNet.h"
#include "runtime/GrassTime.h"
#include "runtime/GrassFsys.h"
#include "runtime/GrassWeb.h"
#include "vm/GrassMemory.h"
#include "baremetal/GrassHAL.h"
#include "baremetal/GrassMMIO.h"
#include "baremetal/GrassBaremetal.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#if defined(_WIN32)
#include <windows.h>
#endif

GrassArena g_vm_arena(1024 * 1024);
std::map<std::string, std::wstring> variables_cache;

static const char* capi_intern_str(const std::string& s) {
    static std::string pool[1024];
    static size_t idx = 0;
    std::string& slot = pool[idx++ & 1023];
    slot = s;
    return slot.c_str();
}

struct GrgVM {
    std::string last_error;
    std::vector<uint8_t> bytecode;
};

extern "C" {

GRG_API GrgVM* grg_vm_create(void) {
    return new GrgVM();
}

GRG_API void grg_vm_destroy(GrgVM* vm) {
    if (vm) delete vm;
}

GRG_API int grg_vm_load_file(GrgVM* vm, const char* path) {
    if (!vm || !path) return 1;
#if defined(_WIN32)
    std::string cmd = "grg.exe -c \"" + std::string(path) + "\"";
#else
    std::string cmd = "grg -c \"" + std::string(path) + "\"";
#endif
    int res = system(cmd.c_str());
    if (res != 0) {
        vm->last_error = "Failed to load and compile script: " + std::string(path);
        return res;
    }
    return 0;
}

GRG_API int grg_vm_eval(GrgVM* vm, const char* code) {
    if (!vm || !code) return 1;
    // Write temporary script and execute
    const char* tmp_file = "_tmp_eval.grass";
    FILE* f = fopen(tmp_file, "w");
    if (!f) {
        vm->last_error = "Failed to create temp script file";
        return 1;
    }
    fputs(code, f);
    fclose(f);

#if defined(_WIN32)
    std::string cmd = "grg.exe -r " + std::string(tmp_file);
#else
    std::string cmd = "grg -r " + std::string(tmp_file);
#endif
    int res = system(cmd.c_str());
    remove(tmp_file);
    if (res != 0) {
        vm->last_error = "Script execution returned non-zero code";
    }
    return res;
}

GRG_API int grg_vm_set_int(GrgVM* vm, const char* name, int64_t val) {
    if (!vm || !name) return 1;
    return 0;
}

GRG_API int grg_vm_set_str(GrgVM* vm, const char* name, const char* val) {
    if (!vm || !name || !val) return 1;
    return 0;
}

GRG_API int grg_vm_call(GrgVM* vm, const char* func_name) {
    if (!vm || !func_name) return 1;
    std::string eval_code = "!GrassGreen!\nint main() { " + std::string(func_name) + "(); return 0; }\n";
    return grg_vm_eval(vm, eval_code.c_str());
}

GRG_API const char* grg_vm_get_error(GrgVM* vm) {
    return vm ? vm->last_error.c_str() : "Invalid VM pointer";
}

// =========================================================================
// Native C-API Exports for !math3d!, !gfx3d!, and !physics!
// =========================================================================

struct NativeVec3 {
    double x;
    double y;
    double z;
};

struct NativeMat4 {
    double m[16];
};

static NativeVec3* alloc_native_vec3(const GrassVec3& v) {
    static NativeVec3 pool[4096];
    static size_t idx = 0;
    NativeVec3* p = &pool[idx++ & 4095];
    p->x = static_cast<double>(v.x);
    p->y = static_cast<double>(v.y);
    p->z = static_cast<double>(v.z);
    return p;
}

static GrassVec3 to_grass_vec3(const NativeVec3* p) {
    if (!p) return GrassVec3(0.0f, 0.0f, 0.0f);
    return GrassVec3(static_cast<float>(p->x), static_cast<float>(p->y), static_cast<float>(p->z));
}

static NativeMat4* alloc_native_mat4(const GrassMat4& mat) {
    static NativeMat4 pool[1024];
    static size_t idx = 0;
    NativeMat4* p = &pool[idx++ & 1023];
    for (int i = 0; i < 16; ++i) p->m[i] = static_cast<double>(mat.m[i]);
    return p;
}

static GrassMat4 to_grass_mat4(const NativeMat4* p) {
    GrassMat4 res;
    if (!p) return res;
    for (int i = 0; i < 16; ++i) res.m[i] = static_cast<float>(p->m[i]);
    return res;
}

// --- Math3D ---
GRG_API NativeVec3* v3_create(double x, double y, double z) {
    return alloc_native_vec3(GrassVec3((float)x, (float)y, (float)z));
}
GRG_API NativeVec3* v3_add(const NativeVec3* a, const NativeVec3* b) {
    return alloc_native_vec3(GrassMath3D::add(to_grass_vec3(a), to_grass_vec3(b)));
}
GRG_API NativeVec3* v3_sub(const NativeVec3* a, const NativeVec3* b) {
    return alloc_native_vec3(GrassMath3D::sub(to_grass_vec3(a), to_grass_vec3(b)));
}
GRG_API NativeVec3* v3_mul(const NativeVec3* a, double s) {
    return alloc_native_vec3(GrassMath3D::mul_scalar(to_grass_vec3(a), (float)s));
}
GRG_API double v3_dot(const NativeVec3* a, const NativeVec3* b) {
    return (double)GrassMath3D::dot(to_grass_vec3(a), to_grass_vec3(b));
}
GRG_API NativeVec3* v3_cross(const NativeVec3* a, const NativeVec3* b) {
    return alloc_native_vec3(GrassMath3D::cross(to_grass_vec3(a), to_grass_vec3(b)));
}
GRG_API double v3_length(const NativeVec3* a) {
    return (double)GrassMath3D::length(to_grass_vec3(a));
}
GRG_API NativeVec3* v3_normalize(const NativeVec3* a) {
    return alloc_native_vec3(GrassMath3D::normalize(to_grass_vec3(a)));
}
GRG_API NativeVec3* v3_lerp(const NativeVec3* a, const NativeVec3* b, double t) {
    return alloc_native_vec3(GrassMath3D::lerp(to_grass_vec3(a), to_grass_vec3(b), (float)t));
}

GRG_API NativeMat4* m4_identity(void) {
    return alloc_native_mat4(GrassMath3D::identity());
}
GRG_API NativeMat4* m4_translate(const NativeVec3* v) {
    return alloc_native_mat4(GrassMath3D::translate(to_grass_vec3(v)));
}
GRG_API NativeMat4* m4_translate_xyz(double x, double y, double z) {
    return alloc_native_mat4(GrassMath3D::translate(GrassVec3((float)x, (float)y, (float)z)));
}
GRG_API NativeMat4* m4_rotate(double angle, const NativeVec3* axis) {
    return alloc_native_mat4(GrassMath3D::rotate((float)angle, to_grass_vec3(axis)));
}
GRG_API NativeMat4* m4_rotate_xyz(double angle, double ax, double ay, double az) {
    return alloc_native_mat4(GrassMath3D::rotate((float)angle, GrassVec3((float)ax, (float)ay, (float)az)));
}
GRG_API NativeMat4* m4_scale(const NativeVec3* v) {
    return alloc_native_mat4(GrassMath3D::scale(to_grass_vec3(v)));
}
GRG_API NativeMat4* m4_scale_xyz(double sx, double sy, double sz) {
    return alloc_native_mat4(GrassMath3D::scale(GrassVec3((float)sx, (float)sy, (float)sz)));
}
GRG_API NativeMat4* m4_look_at(const NativeVec3* eye, const NativeVec3* target, const NativeVec3* up) {
    return alloc_native_mat4(GrassMath3D::look_at(to_grass_vec3(eye), to_grass_vec3(target), to_grass_vec3(up)));
}
GRG_API NativeMat4* m4_perspective(double fov, double aspect, double near_z, double far_z) {
    return alloc_native_mat4(GrassMath3D::perspective((float)fov, (float)aspect, (float)near_z, (float)far_z));
}
GRG_API NativeMat4* m4_mul(const NativeMat4* a, const NativeMat4* b) {
    return alloc_native_mat4(GrassMath3D::multiply(to_grass_mat4(a), to_grass_mat4(b)));
}

// --- 3D Graphics ---
GRG_API int64_t g3d_window(const char* title, int64_t w, int64_t h) {
    std::string t = title ? title : "GrassGreen 3D Window";
    return GrassGfx3D::init_window((int)w, (int)h, t) ? 1 : 0;
}
GRG_API int64_t g3d_is_open(void) {
    return GrassGfx3D::is_open() ? 1 : 0;
}
GRG_API void g3d_clear(int64_t hex) {
    uint32_t h = (uint32_t)hex;
    GrassGfx3D::clear(((h >> 16) & 0xFF) / 255.0f, ((h >> 8) & 0xFF) / 255.0f, (h & 0xFF) / 255.0f);
}
GRG_API void g3d_clear_rgb(double r, double g, double b) {
    GrassGfx3D::clear((float)r, (float)g, (float)b);
}
GRG_API void g3d_present(void) {
    GrassGfx3D::present();
}
GRG_API double g3d_dt(void) {
    return (double)GrassGfx3D::dt();
}
GRG_API int64_t g3d_key_down(int64_t key) {
    return GrassGfx3D::key_down((int)key) ? 1 : 0;
}
GRG_API int64_t g3d_key_down_str(const char* key) {
    return (key && GrassGfx3D::key_down_str(key)) ? 1 : 0;
}
GRG_API int64_t g3d_mouse_x(void) {
    return (int64_t)GrassGfx3D::mouse_x();
}
GRG_API int64_t g3d_mouse_y(void) {
    return (int64_t)GrassGfx3D::mouse_y();
}
GRG_API int64_t g3d_mouse_btn(int64_t btn) {
    return GrassGfx3D::mouse_btn((int)btn) ? 1 : 0;
}
GRG_API void g3d_dir_light(const NativeVec3* dir, int64_t hex) {
    uint32_t h = (uint32_t)hex;
    GrassGfx3D::set_dir_light(to_grass_vec3(dir), ((h >> 16) & 0xFF) / 255.0f, ((h >> 8) & 0xFF) / 255.0f, (h & 0xFF) / 255.0f, 1.0f);
}
GRG_API void g3d_dir_light_rgb(const NativeVec3* dir, double r, double g, double b, double intensity) {
    GrassGfx3D::set_dir_light(to_grass_vec3(dir), (float)r, (float)g, (float)b, (float)intensity);
}
GRG_API void g3d_point_light(const NativeVec3* pos, double r, double g, double b, double range) {
    GrassGfx3D::set_point_light(to_grass_vec3(pos), (float)r, (float)g, (float)b, (float)range);
}
GRG_API void g3d_fog(double r, double g, double b, double density) {
    GrassGfx3D::set_fog((float)r, (float)g, (float)b, (float)density);
}
GRG_API void g3d_skybox(int64_t top_rgb, int64_t bottom_rgb) {
    GrassGfx3D::set_skybox((uint32_t)top_rgb, (uint32_t)bottom_rgb);
}
GRG_API void g3d_bind_framebuffer(void* addr, int64_t w, int64_t h, int64_t pitch) {
    GrassGfx3D::bind_framebuffer(addr, (int)w, (int)h, (int)pitch);
}
GRG_API void g3d_camera(const NativeVec3* pos, const NativeVec3* target, double fov) {
    GrassGfx3D::set_camera(to_grass_vec3(pos), to_grass_vec3(target), (float)fov);
}
GRG_API void g3d_camera_xyz(double px, double py, double pz, double tx, double ty, double tz, double fov) {
    GrassGfx3D::set_camera(GrassVec3((float)px, (float)py, (float)pz), GrassVec3((float)tx, (float)ty, (float)tz), (float)fov);
}
GRG_API int64_t g3d_cube(double size) {
    return (int64_t)GrassGfx3D::create_cube((float)size);
}
GRG_API int64_t g3d_box(double w, double h, double d) {
    return (int64_t)GrassGfx3D::create_box((float)w, (float)h, (float)d);
}
GRG_API int64_t g3d_sphere(double r, int64_t segs) {
    return (int64_t)GrassGfx3D::create_sphere((float)r, (int)segs);
}
GRG_API int64_t g3d_plane(double w, double d) {
    return (int64_t)GrassGfx3D::create_plane((float)w, (float)d, 10);
}
GRG_API int64_t g3d_plane_segs(double w, double d, int64_t segs) {
    return (int64_t)GrassGfx3D::create_plane((float)w, (float)d, (int)segs);
}
GRG_API int64_t g3d_load_obj(const char* path) {
    return path ? (int64_t)GrassGfx3D::load_obj(path) : 0;
}
GRG_API void g3d_set_texture(int64_t mesh_id, const char* path) {
    if (path) GrassGfx3D::set_texture((uint32_t)mesh_id, path);
}
GRG_API void g3d_set_color(int64_t mesh_id, int64_t hex) {
    uint32_t h = (uint32_t)hex;
    GrassGfx3D::set_color((uint32_t)mesh_id, ((h >> 16) & 0xFF) / 255.0f, ((h >> 8) & 0xFF) / 255.0f, (h & 0xFF) / 255.0f);
}
GRG_API void g3d_set_color_rgb(int64_t mesh_id, double r, double g, double b) {
    GrassGfx3D::set_color((uint32_t)mesh_id, (float)r, (float)g, (float)b);
}
GRG_API void g3d_transform(int64_t mesh_id, const NativeVec3* pos, const NativeVec3* rot, const NativeVec3* scale) {
    GrassGfx3D::transform((uint32_t)mesh_id, to_grass_vec3(pos), to_grass_vec3(rot), scale ? to_grass_vec3(scale) : GrassVec3(1, 1, 1));
}
GRG_API void g3d_transform_mat(int64_t mesh_id, const NativeMat4* mat) {
    GrassGfx3D::transform_mat((uint32_t)mesh_id, to_grass_mat4(mat));
}
GRG_API void g3d_draw(int64_t mesh_id) {
    GrassGfx3D::draw((uint32_t)mesh_id);
}

// --- Physics ---
GRG_API int64_t phys_world(double /*gx*/, double gy, double /*gz*/) {
    return (int64_t)GrassPhysics::create_world((float)gy);
}
GRG_API void phys_step(int64_t world, double dt, int64_t substeps) {
    int s = (int)substeps;
    if (s < 8) s = 8;
    GrassPhysics::step_world((uint32_t)world, (float)dt, s);
}
GRG_API int64_t phys_box(int64_t world, double px, double py, double pz, double sx, double sy, double sz, double mass) {
    return (int64_t)GrassPhysics::add_box((uint32_t)world, (float)px, (float)py, (float)pz, (float)sx, (float)sy, (float)sz, (float)mass);
}
GRG_API int64_t phys_sphere(int64_t world, double px, double py, double pz, double r, double mass) {
    return (int64_t)GrassPhysics::add_sphere((uint32_t)world, (float)px, (float)py, (float)pz, (float)r, (float)mass);
}
GRG_API int64_t phys_cloth(int64_t world, const NativeVec3* pos, double w, double h, int64_t segs) {
    GrassVec3 p = to_grass_vec3(pos);
    return (int64_t)GrassPhysics::add_cloth((uint32_t)world, p.x, p.y, p.z, (float)w, (float)h, (int)segs);
}
GRG_API void phys_link(int64_t world, int64_t a, int64_t b, double stiff, double brk) {
    GrassPhysics::add_link((uint32_t)world, (uint32_t)a, (uint32_t)b, (float)stiff, (float)brk);
}
GRG_API NativeVec3* phys_get_pos(int64_t world, int64_t body) {
    return alloc_native_vec3(GrassPhysics::get_pos((uint32_t)world, (uint32_t)body));
}
GRG_API NativeVec3* phys_get_rot(int64_t world, int64_t body) {
    return alloc_native_vec3(GrassPhysics::get_rot((uint32_t)world, (uint32_t)body));
}
GRG_API void phys_set_pos(int64_t world, int64_t body, double x, double y, double z) {
    GrassPhysics::set_pos((uint32_t)world, (uint32_t)body, (float)x, (float)y, (float)z);
}
GRG_API void phys_set_vel(int64_t world, int64_t body, double vx, double vy, double vz) {
    GrassPhysics::set_vel((uint32_t)world, (uint32_t)body, (float)vx, (float)vy, (float)vz);
}
GRG_API void phys_impulse(int64_t world, int64_t body, double fx, double fy, double fz) {
    GrassPhysics::apply_impulse((uint32_t)world, (uint32_t)body, (float)fx, (float)fy, (float)fz);
}
GRG_API int64_t phys_raycast(int64_t world, const NativeVec3* o, const NativeVec3* d, double max_dist) {
    GrassVec3 vo = to_grass_vec3(o);
    GrassVec3 vd = to_grass_vec3(d);
    PhysRayHit hit = GrassPhysics::raycast((uint32_t)world, vo.x, vo.y, vo.z, vd.x, vd.y, vd.z, (float)max_dist);
    return hit.hit ? (int64_t)hit.body_id : 0;
}

// =========================================================================
// Native C-API Exports for 2D Graphics (!2d! / !gfx2d! / !2DGraphics!)
// =========================================================================

GRG_API int64_t g2d_window(const char* title, int64_t w, int64_t h) {
    std::string t = title ? title : "GrassGreen 2D Window";
    return Grass2D::init_window((int)w, (int)h, t) ? 1 : 0;
}
GRG_API int64_t g2d_is_open(void) {
    return Grass2D::is_open() ? 1 : 0;
}
GRG_API void g2d_clear(int64_t hex) {
    uint32_t h = (uint32_t)hex;
    Grass2D::clear(((h >> 16) & 0xFF) / 255.0f, ((h >> 8) & 0xFF) / 255.0f, (h & 0xFF) / 255.0f);
}
GRG_API void g2d_clear_rgb(double r, double g, double b) {
    Grass2D::clear((float)r, (float)g, (float)b);
}
GRG_API void g2d_present(void) {
    Grass2D::present();
}
GRG_API double g2d_dt(void) {
    return (double)Grass2D::dt();
}
GRG_API int64_t g2d_key_down(int64_t key) {
    return Grass2D::key_down(std::to_string((int)key)) ? 1 : 0;
}
GRG_API int64_t g2d_key_down_str(const char* key) {
    return (key && Grass2D::key_down(key)) ? 1 : 0;
}
GRG_API int64_t g2d_mouse_x(void) {
    return (int64_t)Grass2D::mouse_x();
}
GRG_API int64_t g2d_mouse_y(void) {
    return (int64_t)Grass2D::mouse_y();
}
GRG_API int64_t g2d_mouse_btn(int64_t btn) {
    return Grass2D::mouse_btn((int)btn) ? 1 : 0;
}
GRG_API void g2d_cam_set(double x, double y, double zoom, double angle) {
    Grass2D::cam_set((float)x, (float)y, (float)zoom, (float)angle);
}
GRG_API void g2d_cam_shake(double intensity, double duration) {
    Grass2D::cam_shake((float)intensity, (float)duration);
}
GRG_API void g2d_emit_particles(double x, double y, int64_t count, int64_t color) {
    Grass2D::emit_particles((float)x, (float)y, (int)count, (uint32_t)color, 100.0f, 1.0f);
}
GRG_API void g2d_emit_particles_ex(double x, double y, int64_t count, int64_t color, double speed, double lifetime) {
    Grass2D::emit_particles((float)x, (float)y, (int)count, (uint32_t)color, (float)speed, (float)lifetime);
}
GRG_API void g2d_update_particles(double delta) {
    Grass2D::update_particles((float)delta);
}
GRG_API int64_t g2d_load_tex(const char* path) {
    return path ? (int64_t)Grass2D::load_tex(path) : 0;
}
GRG_API void g2d_draw_sprite(int64_t tex, double x, double y, double w, double h, double angle, double alpha) {
    Grass2D::draw_sprite((uint32_t)tex, (float)x, (float)y, (float)w, (float)h, (float)angle, (float)alpha);
}
GRG_API void g2d_draw_frame(int64_t tex, double fx, double fy, double fw, double fh, double x, double y, double w, double h) {
    Grass2D::draw_frame((uint32_t)tex, (float)fx, (float)fy, (float)fw, (float)fh, (float)x, (float)y, (float)w, (float)h);
}
GRG_API void g2d_bind_framebuffer(void* addr, int64_t w, int64_t h, int64_t pitch) {
    Grass2D::bind_framebuffer(addr, (int)w, (int)h, (int)pitch);
}
GRG_API void g2d_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t hex) {
    Grass2D::draw_rect((int)x, (int)y, (int)w, (int)h, (uint32_t)hex);
}
GRG_API void g2d_circle(int64_t cx, int64_t cy, int64_t r, int64_t hex) {
    Grass2D::draw_circle((int)cx, (int)cy, (int)r, (uint32_t)hex);
}
GRG_API void g2d_line(int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t hex, int64_t width) {
    Grass2D::draw_line((int)x1, (int)y1, (int)x2, (int)y2, (uint32_t)hex, (int)width);
}
GRG_API void g2d_text(int64_t x, int64_t y, const char* str, int64_t size, int64_t hex) {
    if (str) Grass2D::draw_text((int)x, (int)y, str, (int)size, (uint32_t)hex);
}
GRG_API void g2d_close(void) {
    Grass2D::close_window();
}

// Legacy !2DGraphics! gfx_* & canvas wrappers
GRG_API int64_t gfx_init(const char* title, int64_t w, int64_t h) {
    return Grass2DGraphics::init(title ? title : "GrassGreen 2D", (int)w, (int)h) ? 1 : 0;
}
GRG_API int64_t gfx_running(void) {
    return Grass2DGraphics::running() ? 1 : 0;
}
GRG_API void gfx_clear(const char* hex) {
    Grass2DGraphics::clear(hex ? hex : "#000000");
}
GRG_API void gfx_rect(int64_t x, int64_t y, int64_t w, int64_t h, const char* hex) {
    Grass2DGraphics::rect((int)x, (int)y, (int)w, (int)h, hex ? hex : "#FFFFFF");
}
GRG_API void gfx_circle(int64_t x, int64_t y, int64_t r, const char* hex) {
    Grass2DGraphics::circle((int)x, (int)y, (int)r, hex ? hex : "#FFFFFF");
}
GRG_API void gfx_line(int64_t x1, int64_t y1, int64_t x2, int64_t y2, const char* hex, int64_t w) {
    Grass2DGraphics::line((int)x1, (int)y1, (int)x2, (int)y2, hex ? hex : "#FFFFFF", (int)w);
}
GRG_API void gfx_text(int64_t x, int64_t y, const char* str, int64_t sz, const char* hex) {
    Grass2DGraphics::text((int)x, (int)y, str ? str : "", (int)sz, hex ? hex : "#FFFFFF");
}
GRG_API int64_t gfx_key(const char* k) {
    return (k && Grass2DGraphics::key(k)) ? 1 : 0;
}
GRG_API int64_t gfx_mouse_x(void) { return (int64_t)Grass2DGraphics::mouse_x(); }
GRG_API int64_t gfx_mouse_y(void) { return (int64_t)Grass2DGraphics::mouse_y(); }
GRG_API int64_t gfx_mouse_down(const char* b) { return Grass2DGraphics::mouse_down(b ? b : "left") ? 1 : 0; }
GRG_API int64_t gfx_collide(double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2) {
    return Grass2DGraphics::collide(x1, y1, w1, h1, x2, y2, w2, h2) ? 1 : 0;
}
GRG_API void gfx_frame(int64_t fps) { Grass2DGraphics::frame((int)fps); }
GRG_API void gfx_close(void) { Grass2DGraphics::close(); }

GRG_API int64_t create_canvas(int64_t w, int64_t h) { return Grass2D::create_canvas((int)w, (int)h) ? 1 : 0; }
GRG_API void fill_color(const char* hex) { Grass2D::fill_color(hex ? hex : "#FFFFFF"); }
GRG_API void draw_rectangle(int64_t x, int64_t y, int64_t w, int64_t h) { Grass2D::canvas_rect((int)x, (int)y, (int)w, (int)h); }
GRG_API void draw_circle(int64_t cx, int64_t cy, int64_t r) { Grass2D::canvas_circle((int)cx, (int)cy, (int)r); }
GRG_API void draw_line(int64_t x1, int64_t y1, int64_t x2, int64_t y2) { Grass2D::canvas_line((int)x1, (int)y1, (int)x2, (int)y2); }
GRG_API int64_t save_image(const char* path) { return Grass2D::save_image(path ? path : "output.png") ? 1 : 0; }

// =========================================================================
// Audio (!audio!)
// =========================================================================
GRG_API void audio_init(void) { GrassAudio::init(); }
GRG_API void synth_play(const char* wave, double freq, int64_t dur_ms, double vol) {
    GrassAudio::synth_play(wave ? wave : "sine", freq, (int)dur_ms, vol);
}
GRG_API void synth(const char* wave, double freq, int64_t dur_ms, double vol) {
    GrassAudio::synth_play(wave ? wave : "sine", freq, (int)dur_ms, vol);
}
GRG_API void tone(double freq, int64_t dur_ms, double vol) { GrassAudio::tone(freq, (int)dur_ms, vol); }
GRG_API void beep(double freq, int64_t dur_ms) { GrassAudio::beep(freq, (int)dur_ms); }
GRG_API int64_t sound_play(const char* path, int64_t loop) { return (path && GrassAudio::sound_play(path, loop != 0)) ? 1 : 0; }
GRG_API void sound_stop(void) { GrassAudio::sound_stop(); }
GRG_API void sound_pause(void) { GrassAudio::sound_pause(); }
GRG_API void sound_resume(void) { GrassAudio::sound_resume(); }
GRG_API void sound_volume(int64_t vol) { GrassAudio::sound_volume((int)vol); }
GRG_API int64_t sound_is_playing(void) { return GrassAudio::sound_is_playing() ? 1 : 0; }
GRG_API void bind_synth(const char* target, const char* wave, double freq, int64_t dur, double vol) {
    (void)target; (void)wave; (void)freq; (void)dur; (void)vol;
}
GRG_API void bind_sound(const char* target, const char* path) { (void)target; (void)path; }
GRG_API void attach_sound(const char* fn, const char* wave, double freq, int64_t dur, double vol) {
    (void)fn; (void)wave; (void)freq; (void)dur; (void)vol;
}

// =========================================================================
// JSON (!json!)
// =========================================================================
GRG_API void json_clear(void) { GrassJson::builder_clear(); }
GRG_API void json_add(const char* key, const char* val) { GrassJson::builder_add_str(key ? key : "", val ? val : ""); }
GRG_API void json_add_num(const char* key, double val) { GrassJson::builder_add_num(key ? key : "", val); }
GRG_API const char* json_build(void) { return capi_intern_str(GrassJson::builder_stringify()); }
GRG_API int64_t json_valid(const char* s) { return GrassJson::is_valid(s ? s : "") ? 1 : 0; }
GRG_API const char* json_get(const char* s, const char* key) { return capi_intern_str(GrassJson::get(s ? s : "", key ? key : "")); }
GRG_API const char* json_set(const char* s, const char* key, const char* val) { return capi_intern_str(GrassJson::set(s ? s : "", key ? key : "", val ? val : "")); }
GRG_API const char* json_minify(const char* s) { return capi_intern_str(GrassJson::minify(s ? s : "")); }

// =========================================================================
// Sys (!sys!) & Proc (!proc!)
// =========================================================================
GRG_API const char* pc_name(void) { return capi_intern_str(GrassSys::pc_name()); }
GRG_API const char* user_name(void) { return capi_intern_str(GrassSys::user_name()); }
GRG_API int64_t screen_w(void) { return (int64_t)GrassSys::screen_w(); }
GRG_API int64_t screen_h(void) { return (int64_t)GrassSys::screen_h(); }
GRG_API int64_t cpu_cores(void) { return (int64_t)GrassSys::cpu_cores(); }
GRG_API double ram_total_mb(void) { return GrassSys::ram_total_mb(); }
GRG_API double ram_free_mb(void) { return GrassSys::ram_free_mb(); }
GRG_API double ram_used_mb(void) { return GrassSys::ram_total_mb() - GrassSys::ram_free_mb(); }
GRG_API double uptime_sec(void) { return GrassSys::uptime_sec(); }
GRG_API int64_t clip_set(const char* text) { return GrassSys::clip_set(text ? text : "") ? 1 : 0; }
GRG_API const char* clip_get(void) { return capi_intern_str(GrassSys::clip_get()); }
GRG_API const char* get_env(const char* key) { return capi_intern_str(GrassSys::get_env(key ? key : "")); }

GRG_API int64_t proc_find(const char* name) { return GrassProc::find(name ? name : ""); }
GRG_API int64_t proc_kill(int64_t pid) { return GrassProc::kill(pid) ? 1 : 0; }
GRG_API int64_t proc_mem(int64_t pid) { return GrassProc::mem(pid); }
GRG_API int64_t proc_exists(int64_t pid) { return GrassProc::exists(pid) ? 1 : 0; }
GRG_API const char* proc_list(void) { return capi_intern_str(GrassProc::list()); }
GRG_API int64_t find(const char* name) { return GrassProc::find(name ? name : ""); }
GRG_API int64_t kill(int64_t pid) { return GrassProc::kill(pid) ? 1 : 0; }
GRG_API int64_t mem(int64_t pid) { return GrassProc::mem(pid); }
GRG_API const char* list(void) { return capi_intern_str(GrassProc::list()); }

// =========================================================================
// Hardware (!hardware!)
// =========================================================================
GRG_API const char* hw_cpu_vendor(void) { return capi_intern_str(GrassHardware::cpu_vendor()); }
GRG_API const char* hw_cpu_brand(void) { return capi_intern_str(GrassHardware::cpu_brand()); }
GRG_API int64_t hw_cpu_cores(void) { return GrassHardware::cpu_cores(); }
GRG_API int64_t hw_cpu_threads(void) { return GrassHardware::cpu_threads(); }
GRG_API int64_t hw_cpu_sockets(void) { return GrassHardware::cpu_sockets(); }
GRG_API int64_t hw_cpu_family(void) { return GrassHardware::cpu_family(); }
GRG_API int64_t hw_cpu_freq(void) { return GrassHardware::cpu_freq_mhz(); }
GRG_API int64_t hw_cpu_base_freq(void) { return GrassHardware::cpu_base_freq_mhz(); }
GRG_API int64_t hw_cpu_usage(void) { return GrassHardware::cpu_usage_percent(); }
GRG_API int64_t hw_cpu_temp(void) { return GrassHardware::cpu_temp(); }
GRG_API int64_t hw_cpu_cache_l1(void) { return GrassHardware::cpu_cache_l1_kb(); }
GRG_API int64_t hw_cpu_cache_l2(void) { return GrassHardware::cpu_cache_l2_kb(); }
GRG_API int64_t hw_cpu_cache_l3(void) { return GrassHardware::cpu_cache_l3_kb(); }
GRG_API const char* hw_cpu_virt(void) { return capi_intern_str(GrassHardware::cpu_virt()); }
GRG_API int64_t hw_uptime_sec(void) { return GrassHardware::uptime_sec(); }
GRG_API const char* hw_uptime_str(void) { return capi_intern_str(GrassHardware::uptime_str()); }

GRG_API int64_t hw_ram_total(void) { return GrassHardware::ram_total_mb(); }
GRG_API int64_t hw_ram_used(void) { return GrassHardware::ram_used_mb(); }
GRG_API int64_t hw_ram_free(void) { return GrassHardware::ram_free_mb(); }
GRG_API int64_t hw_ram_cached(void) { return GrassHardware::ram_cached_mb(); }
GRG_API int64_t hw_ram_speed(void) { return GrassHardware::ram_speed_mhz(); }
GRG_API int64_t hw_ram_temp(void) { return GrassHardware::ram_temp(); }
GRG_API const char* hw_ram_model(void) { return capi_intern_str(GrassHardware::ram_model()); }
GRG_API const char* hw_ram_form(void) { return capi_intern_str(GrassHardware::ram_form()); }

GRG_API int64_t hw_disk_size(void) { return GrassHardware::disk_size_gb(); }
GRG_API int64_t hw_disk_free(void) { return GrassHardware::disk_free_gb(); }
GRG_API const char* hw_disk_format(void) { return capi_intern_str(GrassHardware::disk_format()); }
GRG_API const char* hw_disk_model(void) { return capi_intern_str(GrassHardware::disk_model()); }
GRG_API const char* hw_disk_type(void) { return capi_intern_str(GrassHardware::disk_type()); }
GRG_API int64_t hw_disk_temp(void) { return GrassHardware::disk_temp(); }
GRG_API int64_t hw_disk_read_speed(void) { return GrassHardware::disk_read_speed(); }
GRG_API int64_t hw_disk_write_speed(void) { return GrassHardware::disk_write_speed(); }
GRG_API int64_t hw_disk_resp_time(void) { return GrassHardware::disk_resp_time(); }
GRG_API int64_t hw_disk_usage(void) { return GrassHardware::disk_usage_percent(); }
GRG_API int64_t hw_disk_is_system(void) { return GrassHardware::disk_is_system(); }
GRG_API int64_t hw_pagefile_total(void) { return GrassHardware::pagefile_total_mb(); }
GRG_API int64_t hw_pagefile_used(void) { return GrassHardware::pagefile_used_mb(); }

GRG_API const char* hw_net_adapter(void) { return capi_intern_str(GrassHardware::net_adapter()); }
GRG_API const char* hw_net_type(void) { return capi_intern_str(GrassHardware::net_type()); }
GRG_API const char* hw_net_ipv4(void) { return capi_intern_str(GrassHardware::net_ipv4()); }
GRG_API const char* hw_net_ipv6(void) { return capi_intern_str(GrassHardware::net_ipv6()); }
GRG_API int64_t hw_net_recv_speed(void) { return GrassHardware::net_recv_speed_kb(); }
GRG_API int64_t hw_net_send_speed(void) { return GrassHardware::net_send_speed_kb(); }
GRG_API int64_t hw_net_recv_total(void) { return GrassHardware::net_recv_total_mb(); }
GRG_API int64_t hw_net_send_total(void) { return GrassHardware::net_send_total_mb(); }

GRG_API const char* hw_gpu_name(void) { return capi_intern_str(GrassHardware::gpu_name()); }
GRG_API int64_t hw_gpu_usage(void) { return GrassHardware::gpu_usage_percent(); }
GRG_API int64_t hw_gpu_temp(void) { return GrassHardware::gpu_temp(); }
GRG_API int64_t hw_gpu_vram_total(void) { return GrassHardware::gpu_vram_total_mb(); }
GRG_API int64_t hw_gpu_vram_used(void) { return GrassHardware::gpu_vram_used_mb(); }
GRG_API int64_t hw_gpu_vram_shared(void) { return GrassHardware::gpu_vram_shared_mb(); }

GRG_API const char* hw_board_vendor(void) { return capi_intern_str(GrassHardware::board_vendor()); }
GRG_API const char* hw_board_model(void) { return capi_intern_str(GrassHardware::board_model()); }
GRG_API const char* hw_bios_vendor(void) { return capi_intern_str(GrassHardware::bios_vendor()); }
GRG_API const char* hw_bios_date(void) { return capi_intern_str(GrassHardware::bios_date()); }
GRG_API const char* hw_bios_version(void) { return capi_intern_str(GrassHardware::bios_version()); }
GRG_API int64_t hw_pci_count(void) { return GrassHardware::pci_count(); }
GRG_API int64_t hw_rdtsc(void) { return (int64_t)GrassHardware::rdtsc(); }
GRG_API void hw_reboot(void) { GrassHardware::reboot(); }

// =========================================================================
// Telegram (!tg!)
// =========================================================================
GRG_API void tg_init(const char* token) { GrassTg::init(token ? token : ""); }
GRG_API const char* tg_poll(int64_t timeout_sec) { return capi_intern_str(GrassTg::poll((int)timeout_sec)); }
GRG_API const char* tg_get_user(void) { return capi_intern_str(GrassTg::get_user()); }
GRG_API const char* tg_get_chat_id(void) { return capi_intern_str(GrassTg::get_chat_id()); }
GRG_API const char* tg_get_msg_id(void) { return capi_intern_str(GrassTg::get_msg_id()); }
GRG_API const char* tg_get_callback(void) { return capi_intern_str(GrassTg::get_callback()); }
GRG_API int64_t tg_send(const char* chat_id, const char* text) { return GrassTg::send(chat_id ? chat_id : "", text ? text : "") ? 1 : 0; }
GRG_API int64_t tg_reply(const char* text) { return GrassTg::reply(text ? text : "") ? 1 : 0; }
GRG_API int64_t tg_send_btn(const char* chat_id, const char* text, const char* btns) {
    return GrassTg::send_btn(chat_id ? chat_id : "", text ? text : "", btns ? btns : "") ? 1 : 0;
}
GRG_API int64_t tg_send_inline(const char* chat_id, const char* text, const char* btns) {
    return GrassTg::send_inline(chat_id ? chat_id : "", text ? text : "", btns ? btns : "") ? 1 : 0;
}
GRG_API int64_t tg_send_webapp(const char* chat_id, const char* text, const char* title, const char* url) {
    return GrassTg::send_webapp(chat_id ? chat_id : "", text ? text : "", title ? title : "", url ? url : "") ? 1 : 0;
}
GRG_API int64_t tg_send_photo(const char* chat_id, const char* url, const char* cap) {
    return GrassTg::send_photo(chat_id ? chat_id : "", url ? url : "", cap ? cap : "") ? 1 : 0;
}
GRG_API int64_t tg_send_dice(const char* chat_id, const char* emoji) {
    return GrassTg::send_dice(chat_id ? chat_id : "", emoji ? emoji : "") ? 1 : 0;
}
GRG_API int64_t tg_edit(const char* chat_id, const char* msg_id, const char* txt) {
    return GrassTg::edit(chat_id ? chat_id : "", msg_id ? msg_id : "", txt ? txt : "") ? 1 : 0;
}
GRG_API int64_t tg_del(const char* chat_id, const char* msg_id) {
    return GrassTg::del(chat_id ? chat_id : "", msg_id ? msg_id : "") ? 1 : 0;
}
GRG_API void telegram_init(const char* token) { GrassTg::init(token ? token : ""); }
GRG_API int64_t send_message(int64_t chat_id, const char* text) {
    return GrassTg::send(std::to_string(chat_id), text ? text : "") ? 1 : 0;
}
GRG_API int64_t send_photo(int64_t chat_id, const char* url) {
    return GrassTg::send_photo(std::to_string(chat_id), url ? url : "", "") ? 1 : 0;
}
GRG_API int64_t send_document(int64_t chat_id, const char* path) {
    return GrassTg::send(std::to_string(chat_id), path ? path : "") ? 1 : 0;
}

// =========================================================================
// LangGen (!langgen!)
// =========================================================================
GRG_API int64_t lang_new(const char* name) { return GrassLangGen::lang_new(name ? name : "DSL"); }
GRG_API int64_t lang_comment(int64_t id, const char* prefix) { return GrassLangGen::lang_comment(id, prefix ? prefix : "//") ? 1 : 0; }
GRG_API int64_t lang_rule(int64_t id, const char* rname, const char* pat) { return GrassLangGen::lang_rule(id, rname ? rname : "", pat ? pat : "") ? 1 : 0; }
GRG_API int64_t lang_parse(int64_t id, const char* src) { return GrassLangGen::lang_parse(id, src ? src : ""); }
GRG_API int64_t lang_count(int64_t prog_id) { return GrassLangGen::lang_count(prog_id); }
GRG_API const char* lang_cmd(int64_t prog_id, int64_t idx) { return capi_intern_str(GrassLangGen::lang_cmd(prog_id, idx)); }
GRG_API const char* lang_arg(int64_t prog_id, int64_t idx, int64_t arg_idx) { return capi_intern_str(GrassLangGen::lang_arg(prog_id, idx, arg_idx)); }
GRG_API int64_t lang_eval(int64_t id, const char* expr) { return (int64_t)GrassLangGen::lang_eval(id, expr ? expr : "").toNumber(); }
GRG_API void lang_set_var(int64_t id, const char* name, int64_t val) { GrassLangGen::lang_set_var(id, name ? name : "", GrassValue::Num((double)val)); }
GRG_API int64_t lang_get_var(int64_t id, const char* name) { return (int64_t)GrassLangGen::lang_get_var(id, name ? name : "").toNumber(); }

// =========================================================================
// Net, Backlog, MMIO, GUI, Web
// =========================================================================
GRG_API const char* local_ip(void) { return capi_intern_str(GrassNet::get_local_ip()); }
GRG_API const char* resolve_ip(const char* host) { return capi_intern_str(GrassNet::resolve_ip(host ? host : "")); }
GRG_API double ping(const char* host, int64_t port) { return GrassNet::ping(host ? host : "127.0.0.1", (int)port); }
GRG_API int64_t net_status(void) { return 1; }
GRG_API int64_t net_set_agent(const char* browser, const char* device) { GrassNet::set_agent(browser ? browser : "", device ? device : ""); return 1; }
GRG_API void API(const char* key) { GrassNet::set_api(key ? key : ""); }
GRG_API int64_t create_serv(const char* ip, int64_t port, const char* pass, const char* certain) {
    return GrassNet::create_server(ip ? ip : "127.0.0.1", (int)port, pass ? pass : "", certain ? certain : "") ? 1 : 0;
}
GRG_API int64_t net_connect_host(const char* host, int64_t port) { return GrassNet::connect_host(host ? host : "", (int)port) ? 1 : 0; }
GRG_API const char* net_accept_srv(void) { return capi_intern_str(GrassNet::accept_request()); }
GRG_API void net_close_srv(void) { GrassNet::close_all(); }
GRG_API int64_t download(const char* url, const char* path) { return GrassNet::download_file(url ? url : "", path ? path : "") ? 1 : 0; }

GRG_API int64_t fat_status(void) { return 1; }
GRG_API int64_t ticks(void) {
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#else
    return (int64_t)GrassTime::now_ms();
#endif
}
GRG_API int64_t threads(void) { return 1; }
GRG_API void grass_sleep_sec(int64_t sec) {
    if (sec > 0) GrassTime::sleep_ms((int)(sec * 1000));
}
GRG_API const char* get_date(void) { return capi_intern_str(GrassTime::get_date()); }
GRG_API const char* get_time_str(void) { return capi_intern_str(GrassTime::get_time_str()); }

GRG_API int64_t fs_mkdir(const char* path) { return (path && GrassFsys::mkdir(path)) ? 1 : 0; }
GRG_API int64_t fs_is_dir(const char* path) { return (path && GrassFsys::is_dir(path)) ? 1 : 0; }
GRG_API int64_t fs_size(const char* path) { return path ? GrassFsys::size(path) : 0; }
GRG_API int64_t fs_copy(const char* src, const char* dst) { return (src && dst && GrassFsys::copy(src, dst)) ? 1 : 0; }
GRG_API const char* fs_list(const char* path) { return capi_intern_str(GrassFsys::list(path ? path : ".")); }
GRG_API const char* fs_read(const char* path) { return capi_intern_str(GrassFsys::read(path ? path : "")); }
GRG_API int64_t fs_write(const char* path, const char* txt) { return (path && GrassFsys::write(path, txt ? txt : "")) ? 1 : 0; }
GRG_API int64_t fs_exists(const char* path) { return (path && GrassFsys::exists(path)) ? 1 : 0; }
GRG_API int64_t fs_remove(const char* path) { return (path && GrassFsys::remove(path)) ? 1 : 0; }

GRG_API double memory_usit(void) { return GrassMemory::usit(); }
GRG_API const char* memory_free(void) { return capi_intern_str(GrassMemory::get_system_free()); }
GRG_API void memory_clean(void) { GrassMemory::clean(); }

GRG_API void clear(void) {}
GRG_API void not_clear_on(void) { GrassBaremetal::not_clear_on(); }
GRG_API void not_clear_off(void) { GrassBaremetal::not_clear_off(); }

GRG_API int64_t inw(int64_t port) { return (int64_t)GrassMMIO::in16((uint16_t)port); }
GRG_API int64_t inl(int64_t port) { return (int64_t)GrassMMIO::in32((uint16_t)port); }
GRG_API void outw(int64_t port, int64_t val) { GrassMMIO::out16((uint16_t)port, (uint16_t)val); }
GRG_API void outl(int64_t port, int64_t val) { GrassMMIO::out32((uint16_t)port, (uint32_t)val); }
GRG_API int64_t inb(int64_t port) { return (int64_t)GrassMMIO::in8((uint16_t)port); }
GRG_API void outb(int64_t port, int64_t val) { GrassMMIO::out8((uint16_t)port, (uint8_t)val); }
GRG_API int64_t mmio_read8(int64_t addr) { return (int64_t)GrassMMIO::read8((uint64_t)addr); }
GRG_API int64_t mmio_read16(int64_t addr) { return (int64_t)GrassMMIO::read16((uint64_t)addr); }
GRG_API int64_t mmio_read32(int64_t addr) { return (int64_t)GrassMMIO::read32((uint64_t)addr); }
GRG_API int64_t mmio_read64(int64_t addr) { return (int64_t)GrassMMIO::read64((uint64_t)addr); }
GRG_API void mmio_write8(int64_t addr, int64_t val) { GrassMMIO::write8((uint64_t)addr, (uint8_t)val); }
GRG_API void mmio_write16(int64_t addr, int64_t val) { GrassMMIO::write16((uint64_t)addr, (uint16_t)val); }
GRG_API void mmio_write32(int64_t addr, int64_t val) { GrassMMIO::write32((uint64_t)addr, (uint32_t)val); }
GRG_API void mmio_write64(int64_t addr, int64_t val) { GrassMMIO::write64((uint64_t)addr, (uint64_t)val); }
GRG_API void memory_barrier(void) { GrassMMIO::memory_barrier(); }
GRG_API void read_barrier(void) { GrassMMIO::read_barrier(); }
GRG_API void write_barrier(void) { GrassMMIO::write_barrier(); }

GRG_API void gui_cmd(const char* cmd) { if (cmd) GrassGUI::exec_command(cmd); }
GRG_API void gui_show(void) { GrassGUI::show_window(); }
GRG_API const char* gui_get(const char* id) { return capi_intern_str(GrassGUI::get_input_value(id ? id : "")); }
GRG_API void gui_set(const char* k, const char* v) { GrassGUI::set_variable(k ? k : "", v ? v : ""); }
GRG_API const char* grass_float_to_str(double v) {
    std::ostringstream ss;
    ss << v;
    return capi_intern_str(ss.str());
}
GRG_API const char* grass_to_upper(const char* s) {
    std::string res = s ? s : "";
    for (char& c : res) c = (char)std::toupper((unsigned char)c);
    return capi_intern_str(res);
}
GRG_API const char* grass_to_lower(const char* s) {
    std::string res = s ? s : "";
    for (char& c : res) c = (char)std::tolower((unsigned char)c);
    return capi_intern_str(res);
}
GRG_API const char* grass_trim(const char* s) {
    std::string res = s ? s : "";
    size_t first = res.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return capi_intern_str("");
    size_t last = res.find_last_not_of(" \t\r\n");
    return capi_intern_str(res.substr(first, last - first + 1));
}
GRG_API void surface(const char* mode) { GrassGUI::set_surface_mode(mode ? mode : "auto"); }
GRG_API void auto_size_display(void) { GrassGUI::auto_size_display(true); }
GRG_API void set_text(const char* id, const char* txt) { GrassGUI::set_text(id ? id : "", txt ? txt : ""); }
GRG_API const char* get_text(const char* id) { return capi_intern_str(GrassGUI::get_text(id ? id : "")); }
GRG_API void set_val(const char* id, const char* val) { GrassGUI::set_val(id ? id : "", val ? val : ""); }
GRG_API double get_val(const char* id) { return GrassGUI::get_val(id ? id : ""); }
GRG_API void set_color(const char* id, const char* col) { GrassGUI::set_color(id ? id : "", col ? col : ""); }
GRG_API void set_bg(const char* id, const char* col) { GrassGUI::set_bg(id ? id : "", col ? col : ""); }
GRG_API void set_visible(const char* id, int64_t vis) { GrassGUI::set_visible(id ? id : "", vis != 0); }
GRG_API int64_t is_checked(const char* id) { return GrassGUI::is_checked(id ? id : "") ? 1 : 0; }
GRG_API void add_item(const char* id, const char* item) { GrassGUI::add_list_item(id ? id : "", item ? item : ""); }
GRG_API void clear_list(const char* id) { GrassGUI::clear_list(id ? id : ""); }
GRG_API const char* get_selected(const char* id) { return capi_intern_str(GrassGUI::get_list_selected(id ? id : "")); }
GRG_API void switch_tab(const char* tab) { GrassGUI::switch_tab(tab ? tab : ""); }
GRG_API void fullscreen(void) { GrassGUI::set_fullscreen(true); }
GRG_API void web_start(const char* path) { if (path) GrassWeb_LoadPage(path); }
GRG_API const char* web_compile(const char* src_path, const char* out_path) {
    std::string html = CompileGrassToHTML(src_path ? src_path : "");
    if (out_path && out_path[0]) {
        std::ofstream out(out_path, std::ios::trunc);
        out << html;
    }
    return capi_intern_str(html);
}

GRG_API int64_t grass_native_thread_spawn(void* fn_ptr, void* arg_ptr) {
#if defined(_WIN32)
    HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn_ptr, arg_ptr, 0, NULL);
    return (int64_t)(uintptr_t)h;
#else
    (void)fn_ptr; (void)arg_ptr; return 0;
#endif
}
GRG_API int64_t grass_native_thread_id(void) {
#if defined(_WIN32)
    return (int64_t)GetCurrentThreadId();
#else
    return 0;
#endif
}

}

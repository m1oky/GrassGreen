#define GRASSVM_EXPORTS
#include "grassvm.h"
#include "frontend/GrassSyntax.h"
#include "media/GrassMath3D.h"
#include "media/GrassGfx3D.h"
#include "media/GrassPhysics.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <cstring>

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

}

/*
 * GrassGreen Standalone Native Runtime Header
 * Standard Library & FFI definitions
 * Zero external dependencies
 */

#ifndef GRASS_NATIVE_RUNTIME_H
#define GRASS_NATIVE_RUNTIME_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* 1. Memory Management & Dynamic Arrays                                     */
/* ========================================================================= */

void* grass_alloc(size_t size);
void grass_free(void* ptr);
void* grass_realloc(void* ptr, size_t new_size);
void grass_mem_barrier(void);

int64_t memory_alloc(int64_t size);
int64_t memory_free_block(int64_t block_id);
double memory_usit(void);
const char* memory_free(void);
void memory_clean(void);

void* grass_array_create(void);
void grass_array_push(void* ptr, int64_t val);
int64_t grass_array_get(void* ptr, int64_t idx);
void grass_array_set(void* ptr, int64_t idx, int64_t val);
int64_t grass_array_len(void* ptr);
int64_t len(void* ptr);

/* ========================================================================= */
/* 2. String Operations                                                      */
/* ========================================================================= */

const char* grass_str_concat(const char* a, const char* b);
int64_t grass_str_len(const char* s);
int64_t grass_str_cmp(const char* a, const char* b);
const char* grass_str_from_int(int64_t v);
const char* grass_str_from_double(double v);
const char* grass_str_from_bool(int64_t b);
const char* grass_get_str_addr(const char* s);

/* ========================================================================= */
/* 3. Console I/O (gout / gin)                                               */
/* ========================================================================= */

void grass_print_int(int64_t v);
void grass_print_double(double v);
void grass_print_str(const char* s);
void grass_print_newline(void);
void grass_print_bool(int64_t b);
int64_t grass_read_int(void);
const char* grass_read_str(void);

/* ========================================================================= */
/* 4. Time & Chrono                                                          */
/* ========================================================================= */

void start_time(void);
double stop_time(void);
const char* conclus_time(void);
const char* get_date(void);
const char* get_time_str(void);
double now_ms(void);
double timestamp(void);
void sleep_ms(int64_t ms);
void grass_sleep(int64_t ms);
int64_t grass_time(void);
double clock_ms(void);

/* ========================================================================= */
/* 5. Math & FastMath & 3D Math                                              */
/* ========================================================================= */

double discor(double x);
double grass_sqrt(double x);
double lerp(double a, double b, double t);
double grass_sin(double x);
double grass_cos(double x);
double grass_abs(double x);
double grass_round(double x);
double grass_floor(double x);
double grass_ceil(double x);
double grass_pow(double b, double e);
double grass_clamp(double v, double min_v, double max_v);
double grass_min(double a, double b);
double grass_max(double a, double b);
int64_t grass_rand(int64_t min_v, int64_t max_v);
int64_t pow_mod(int64_t base, int64_t exp, int64_t mod);
const char* sha256(const char* input);
int64_t crypto_rand(int64_t min_v, int64_t max_v);
const char* pi(void);
int64_t gcd(int64_t a, int64_t b);
const char* pow_e(double a, double b);
const char* qb_init(int64_t id);
const char* h(int64_t id);
const char* x(int64_t id);
const char* z(int64_t id);
const char* cnot(int64_t ctl, int64_t tgt);
int64_t q_measure(int64_t id);
int64_t q_rand(void);

void* v3_create(double x, double y, double z);
void* v3_add(void* a, void* b);
void* v3_sub(void* a, void* b);
void* v3_mul(void* a, double s);
double v3_dot(void* a, void* b);
void* v3_cross(void* a, void* b);
double v3_length(void* a);
void* v3_normalize(void* a);
void* v3_lerp(void* a, void* b, double t);

void* m4_identity(void);
void* m4_translate(void* v);
void* m4_rotate(double angle, void* axis);
void* m4_scale(void* v);
void* m4_look_at(void* eye, void* target, void* up);
void* m4_perspective(double fov, double aspect, double near_p, double far_p);
void* m4_mul(void* a, void* b);

/* ========================================================================= */
/* 6. File System (fsys)                                                     */
/* ========================================================================= */

int64_t fs_mkdir(const char* path);
int64_t fs_is_dir(const char* path);
int64_t fs_size(const char* path);
int64_t fs_copy(const char* src, const char* dst);
const char* fs_list(const char* path);
const char* fs_read(const char* path);
int64_t fs_write(const char* path, const char* content);
int64_t fs_exists(const char* path);
int64_t fs_remove(const char* path);
void* fs_read_bytes(const char* path);
int64_t fs_write_bytes(const char* path, void* bytes, int64_t len);

int64_t creat(const char* path);
int64_t redact(const char* path, const char* content);
const char* read(const char* path);
int64_t exists(const char* path);

/* ========================================================================= */
/* 7. System Diagnostics (sys) & Process Management (proc)                   */
/* ========================================================================= */

const char* pc_name(void);
const char* user_name(void);
int64_t screen_w(void);
int64_t screen_h(void);
int64_t cpu_cores(void);
double ram_total_mb(void);
double ram_free_mb(void);
double ram_used_mb(void);
double uptime_sec(void);
const char* get_env(const char* key);
int64_t clip_set(const char* text);
const char* clip_get(void);
int64_t msgbox(const char* title, const char* msg);
int64_t grass_system(const char* cmd);

int64_t proc_find(const char* name);
int64_t proc_kill(int64_t pid);
int64_t proc_mem(int64_t pid);
int64_t proc_exists(int64_t pid);
const char* proc_list(void);

int64_t find(const char* name);
int64_t kill(int64_t pid);
int64_t mem(int64_t pid);
const char* list(void);

/* ========================================================================= */
/* 8. Network Suite (GrassNet)                                               */
/* ========================================================================= */

const char* local_ip(void);
const char* resolve_ip(const char* host);
int64_t connect(const char* host, int64_t port);
double ping(const char* host, int64_t port);
int64_t net_set_agent(const char* browser, const char* device);
const char* API(const char* key);
int64_t create_serv(const char* ip, int64_t port, const char* pass, const char* certain);
const char* accept(void);
const char* send(const char* url, const char* method, const char* body);
int64_t download(const char* url, const char* filepath);
int64_t net_status(void);
int64_t net_close(void);

/* ========================================================================= */
/* 9. JSON Parser & Builder                                                  */
/* ========================================================================= */

void json_clear(void);
void json_add(const char* key, const char* val);
void json_add_num(const char* key, double val);
const char* json_build(void);
int64_t json_valid(const char* json_str);
const char* json_get(const char* json_str, const char* key);
const char* json_set(const char* json_str, const char* key, const char* val);
const char* json_minify(const char* json_str);

/* ========================================================================= */
/* 10. 2D Game Graphics (2DGraphics / g2d_)                                  */
/* ========================================================================= */

int64_t g2d_window(const char* title, int64_t w, int64_t h);
int64_t g2d_is_open(void);
void g2d_clear(int64_t hex);
void g2d_present(void);
double g2d_dt(void);
int64_t g2d_key_down(int64_t key);
int64_t g2d_mouse_x(void);
int64_t g2d_mouse_y(void);
int64_t g2d_mouse_btn(int64_t btn);
void g2d_cam_shake(double intensity, double duration);
void g2d_emit_particles(double x, double y, int64_t count, int64_t color);
void g2d_update_particles(double dt);
void g2d_cam_set(double x, double y, double zoom, double angle);
int64_t g2d_load_tex(const char* path);
void g2d_draw_sprite(int64_t tex, double x, double y, double w, double h, double angle, double alpha);
void g2d_draw_frame(int64_t tex, double fx, double fy, double fw, double fh, double x, double y, double w, double h);

/* ========================================================================= */
/* 11. 3D Graphics & XPBD Physics (gfx3d / physics3d)                        */
/* ========================================================================= */

int64_t g3d_window(const char* title, int64_t w, int64_t h);
int64_t g3d_is_open(void);
void g3d_clear(int64_t hex);
void g3d_present(void);
double g3d_dt(void);
int64_t g3d_key_down(int64_t key);
int64_t g3d_mouse_x(void);
int64_t g3d_mouse_y(void);
int64_t g3d_mouse_btn(int64_t btn);
void g3d_dir_light(void* dir_v3, int64_t hex);
void g3d_point_light(void* pos_v3, double r, double g, double b, double range);
void g3d_fog(double r, double g, double b, double density);
void g3d_camera(void* pos, void* target, double fov);
int64_t g3d_cube(double size);
int64_t g3d_box(double w, double h, double d);
int64_t g3d_sphere(double r, int64_t segs);
int64_t g3d_plane(double w, double d);
void g3d_set_color(int64_t mesh_id, int64_t hex);
void g3d_transform(int64_t mesh_id, void* pos, void* rot, void* scale);
void g3d_draw(int64_t mesh_id);

int64_t phys_world(double gx, double gy, double gz);
void phys_step(int64_t world, double dt, int64_t substeps);
int64_t phys_box(int64_t world, double px, double py, double pz, double sx, double sy, double sz, double mass);
int64_t phys_sphere(int64_t world, double px, double py, double pz, double r, double mass);
void* phys_get_pos(int64_t world, int64_t body);
void* phys_get_rot(int64_t world, int64_t body);
void phys_set_pos(int64_t world, int64_t body, double x, double y, double z);
void phys_set_vel(int64_t world, int64_t body, double vx, double vy, double vz);
void phys_impulse(int64_t world, int64_t body, double fx, double fy, double fz);
int64_t phys_raycast(int64_t world, void* o, void* d, double max_dist);

/* ========================================================================= */
/* 12. Multithreading & Synchronization (thread)                             */
/* ========================================================================= */

int64_t thread_join(int64_t tid);
int64_t thread_detach(int64_t tid);
int64_t thread_id(void);
void thread_sleep(int64_t ms);
int64_t mutex_create(void);
int64_t mutex_lock(int64_t mid);
int64_t mutex_unlock(int64_t mid);
int64_t mutex_destroy(int64_t mid);

/* ========================================================================= */
/* 13. Dynamic FFI (extern)                                                  */
/* ========================================================================= */

int64_t ffi_open(const char* lib_name);
int64_t ffi_sym(int64_t handle, const char* func_name);
int64_t ffi_call(int64_t func_ptr, void* args);
int64_t ffi_close(int64_t handle);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_NATIVE_RUNTIME_H */

#include "native/GrassNativeStdlib.h"
#include <algorithm>

namespace grass::native {

    void GrassNativeStdlib::populate_disp(ir::IRContext& ctx) {
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* str_t = ctx.get_ptr_type(ctx.get_i8_type());
        register_function({ "grass_float_to_str", "disp", str_t, { f64_t }, { "val" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_to_upper", "disp", str_t, { str_t }, { "s" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_to_lower", "disp", str_t, { str_t }, { "s" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_trim", "disp", str_t, { str_t }, { "s" }, false, false, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_game2d(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "g2d_window", "game2d", i64_t, { ptr_t, i64_t, i64_t }, { "title", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_is_open", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_clear", "game2d", v_t, { i64_t }, { "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_clear_rgb", "game2d", v_t, { f64_t, f64_t, f64_t }, { "r", "g", "b" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_present", "game2d", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_dt", "game2d", f64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_key_down", "game2d", i64_t, { i64_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_key_down_str", "game2d", i64_t, { ptr_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_mouse_x", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_mouse_y", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_mouse_btn", "game2d", i64_t, { i64_t }, { "btn" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_cam_shake", "game2d", v_t, { f64_t, f64_t }, { "intensity", "duration" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_emit_particles", "game2d", v_t, { f64_t, f64_t, i64_t, i64_t }, { "x", "y", "count", "color" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_emit_particles_ex", "game2d", v_t, { f64_t, f64_t, i64_t, i64_t, f64_t, f64_t }, { "x", "y", "count", "color", "speed", "lifetime" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_update_particles", "game2d", v_t, { f64_t }, { "dt" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_cam_set", "game2d", v_t, { f64_t, f64_t, f64_t, f64_t }, { "x", "y", "zoom", "angle" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_load_tex", "game2d", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_draw_sprite", "game2d", v_t, { i64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "tex", "x", "y", "w", "h", "angle", "alpha" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_draw_frame", "game2d", v_t, { i64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "tex", "fx", "fy", "fw", "fh", "x", "y", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_bind_framebuffer", "game2d", v_t, { ptr_t, i64_t, i64_t, i64_t }, { "addr", "w", "h", "pitch" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_rect", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t, i64_t }, { "x", "y", "w", "h", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_circle", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t }, { "cx", "cy", "r", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_line", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t, i64_t, i64_t }, { "x1", "y1", "x2", "y2", "hex", "width" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_text", "game2d", v_t, { i64_t, i64_t, ptr_t, i64_t, i64_t }, { "x", "y", "str", "size", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_close", "game2d", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "gfx_init", "game2d", i64_t, { ptr_t, i64_t, i64_t }, { "title", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_running", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_clear", "game2d", v_t, { ptr_t }, { "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_rect", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t, ptr_t }, { "x", "y", "w", "h", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_circle", "game2d", v_t, { i64_t, i64_t, i64_t, ptr_t }, { "x", "y", "r", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_line", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t, ptr_t, i64_t }, { "x1", "y1", "x2", "y2", "hex", "w" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_text", "game2d", v_t, { i64_t, i64_t, ptr_t, i64_t, ptr_t }, { "x", "y", "str", "sz", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_key", "game2d", i64_t, { ptr_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_mouse_x", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_mouse_y", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_mouse_down", "game2d", i64_t, { ptr_t }, { "btn" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_collide", "game2d", i64_t, { f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "x1", "y1", "w1", "h1", "x2", "y2", "w2", "h2" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_frame", "game2d", v_t, { i64_t }, { "fps" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_close", "game2d", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "create_canvas", "game2d", i64_t, { i64_t, i64_t }, { "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fill_color", "game2d", v_t, { ptr_t }, { "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "draw_rectangle", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t }, { "x", "y", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "draw_circle", "game2d", v_t, { i64_t, i64_t, i64_t }, { "cx", "cy", "r" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "draw_line", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t }, { "x1", "y1", "x2", "y2" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "save_image", "game2d", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_physics3d(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "g3d_window", "physics3d", i64_t, { ptr_t, i64_t, i64_t }, { "title", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_is_open", "physics3d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_clear", "physics3d", v_t, { i64_t }, { "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_clear_rgb", "physics3d", v_t, { f64_t, f64_t, f64_t }, { "r", "g", "b" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_present", "physics3d", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_dt", "physics3d", f64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_key_down", "physics3d", i64_t, { i64_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_key_down_str", "physics3d", i64_t, { ptr_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_mouse_x", "physics3d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_mouse_y", "physics3d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_mouse_btn", "physics3d", i64_t, { i64_t }, { "btn" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_dir_light", "physics3d", v_t, { ptr_t, i64_t }, { "dir", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_dir_light_rgb", "physics3d", v_t, { ptr_t, f64_t, f64_t, f64_t, f64_t }, { "dir", "r", "g", "b", "intensity" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_point_light", "physics3d", v_t, { ptr_t, f64_t, f64_t, f64_t, f64_t }, { "pos", "r", "g", "b", "range" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_fog", "physics3d", v_t, { f64_t, f64_t, f64_t, f64_t }, { "r", "g", "b", "density" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_skybox", "physics3d", v_t, { i64_t, i64_t }, { "top_rgb", "bottom_rgb" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_bind_framebuffer", "physics3d", v_t, { ptr_t, i64_t, i64_t, i64_t }, { "addr", "w", "h", "pitch" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_camera", "physics3d", v_t, { ptr_t, ptr_t, f64_t }, { "pos", "target", "fov" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_camera_xyz", "physics3d", v_t, { f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "px", "py", "pz", "tx", "ty", "tz", "fov" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_cube", "physics3d", i64_t, { f64_t }, { "size" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_box", "physics3d", i64_t, { f64_t, f64_t, f64_t }, { "w", "h", "d" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_sphere", "physics3d", i64_t, { f64_t, i64_t }, { "r", "segs" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_plane", "physics3d", i64_t, { f64_t, f64_t }, { "w", "d" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_plane_segs", "physics3d", i64_t, { f64_t, f64_t, i64_t }, { "w", "d", "segs" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_load_obj", "physics3d", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_set_texture", "physics3d", v_t, { i64_t, ptr_t }, { "mesh_id", "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_set_color", "physics3d", v_t, { i64_t, i64_t }, { "mesh_id", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_set_color_rgb", "physics3d", v_t, { i64_t, f64_t, f64_t, f64_t }, { "mesh_id", "r", "g", "b" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_transform", "physics3d", v_t, { i64_t, ptr_t, ptr_t, ptr_t }, { "mesh_id", "pos", "rot", "scale" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_transform_mat", "physics3d", v_t, { i64_t, ptr_t }, { "mesh_id", "mat" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_draw", "physics3d", v_t, { i64_t }, { "mesh_id" }, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "phys_world", "physics3d", i64_t, { f64_t, f64_t, f64_t }, { "gx", "gy", "gz" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_step", "physics3d", v_t, { i64_t, f64_t, i64_t }, { "world", "dt", "substeps" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_box", "physics3d", i64_t, { i64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "world", "px", "py", "pz", "sx", "sy", "sz", "mass" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_sphere", "physics3d", i64_t, { i64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "world", "px", "py", "pz", "r", "mass" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_cloth", "physics3d", i64_t, { i64_t, ptr_t, f64_t, f64_t, i64_t }, { "world", "pos", "w", "h", "segs" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_link", "physics3d", v_t, { i64_t, i64_t, i64_t, f64_t, f64_t }, { "world", "a", "b", "stiff", "brk" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_get_pos", "physics3d", ptr_t, { i64_t, i64_t }, { "world", "body" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_get_rot", "physics3d", ptr_t, { i64_t, i64_t }, { "world", "body" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_set_pos", "physics3d", v_t, { i64_t, i64_t, f64_t, f64_t, f64_t }, { "world", "body", "x", "y", "z" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_set_vel", "physics3d", v_t, { i64_t, i64_t, f64_t, f64_t, f64_t }, { "world", "body", "vx", "vy", "vz" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_impulse", "physics3d", v_t, { i64_t, i64_t, f64_t, f64_t, f64_t }, { "world", "body", "fx", "fy", "fz" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_raycast", "physics3d", i64_t, { i64_t, ptr_t, ptr_t, f64_t }, { "world", "o", "d", "max_dist" }, false, false, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_baremetal(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();
        ir::IRType* str_t = ctx.get_ptr_type(ctx.get_i8_type());

        register_function({ "color", "baremetal", v_t, { ptr_t, ptr_t }, { "fg", "bg" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_bm_color_int", "baremetal", v_t, { i64_t, i64_t }, { "fg", "bg" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_bm_color_str", "baremetal", v_t, { ptr_t, ptr_t }, { "fg", "bg" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "clear", "baremetal", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "cls", "baremetal", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "clear_all", "baremetal", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "cls_all", "baremetal", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "not_clear", "baremetal", v_t, { i64_t }, { "enable" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "not_clear_on", "baremetal", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "not_clear_off", "baremetal", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "read_key", "baremetal", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "draw_box", "baremetal", v_t, { i64_t, i64_t, i64_t, i64_t, ptr_t, i64_t, i64_t }, { "x", "y", "w", "h", "title", "fg", "bg" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "menu", "baremetal", i64_t, { ptr_t, ptr_t }, { "title", "options" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "progress_bar", "baremetal", v_t, { i64_t, i64_t, i64_t, i64_t, i64_t, i64_t }, { "x", "y", "w", "pct", "fg", "bg" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mouse_init", "baremetal", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mouse_x", "baremetal", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mouse_y", "baremetal", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mouse_btn", "baremetal", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mouse_down", "baremetal", i64_t, { i64_t }, { "btn" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mouse_down_str", "baremetal", i64_t, { ptr_t }, { "btn" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mouse_poll", "baremetal", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mouse_cursor", "baremetal", v_t, { i64_t }, { "visible" }, false, false, false, "", backend::TargetABI::Win64 });
    }

} // namespace grass::native

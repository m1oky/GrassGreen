#include "frontend/GrassSema.h"
#include <algorithm>

namespace grass::sema {

void GrassSema::registerBuiltinsMedia() {
    auto add_builtin = [this](const std::string& name, const SemaType& ret, const std::vector<SemaType>& params, bool is_variadic = false) {
        FunctionSymbol sym;
        sym.name = name;
        sym.return_type = ret;
        sym.loc = SourceLocation("<builtin>", 0, 0);
        sym.is_builtin = true;
        sym.has_body = true;
        sym.is_variadic = is_variadic;
        for (size_t i = 0; i < params.size(); ++i) {
            sym.params.emplace_back("arg" + std::to_string(i), params[i]);
        }
        functions_[name] = sym;
    };

    // 3D Math, Graphics (!gfx3d! / !3d!) and Physics (!physics!)
    StructSymbol vec3_sym;
    vec3_sym.name = "Vec3";
    vec3_sym.loc = SourceLocation("<builtin>", 0, 0);
    vec3_sym.fields["x"] = SemaType::Float();
    vec3_sym.fields["y"] = SemaType::Float();
    vec3_sym.fields["z"] = SemaType::Float();
    vec3_sym.field_order = {"x", "y", "z"};
    structs_["Vec3"] = vec3_sym;

    SemaType vec3_t = SemaType::CustomStruct("Vec3");

    add_builtin("v3_create", vec3_t, {SemaType::Float(), SemaType::Float(), SemaType::Float()});
    add_builtin("v3_add", vec3_t, {SemaType::Any(), SemaType::Any()});
    add_builtin("v3_sub", vec3_t, {SemaType::Any(), SemaType::Any()});
    add_builtin("v3_mul", vec3_t, {SemaType::Any(), SemaType::Float()});
    add_builtin("v3_dot", SemaType::Float(), {SemaType::Any(), SemaType::Any()});
    add_builtin("v3_cross", vec3_t, {SemaType::Any(), SemaType::Any()});
    add_builtin("v3_length", SemaType::Float(), {SemaType::Any()});
    add_builtin("v3_normalize", vec3_t, {SemaType::Any()});
    add_builtin("v3_lerp", vec3_t, {SemaType::Any(), SemaType::Any(), SemaType::Float()});

    add_builtin("m4_identity", SemaType::Any(), {});
    add_builtin("m4_translate", SemaType::Any(), {SemaType::Any()}, true);
    add_builtin("m4_rotate", SemaType::Any(), {SemaType::Float(), SemaType::Any()}, true);
    add_builtin("m4_scale", SemaType::Any(), {SemaType::Any()}, true);
    add_builtin("m4_look_at", SemaType::Any(), {SemaType::Any(), SemaType::Any(), SemaType::Any()});
    add_builtin("m4_perspective", SemaType::Any(), {SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float()});
    add_builtin("m4_mul", SemaType::Any(), {SemaType::Any(), SemaType::Any()});

    add_builtin("g3d_window", SemaType::Int(), {SemaType::Any()}, true);
    add_builtin("g3d_is_open", SemaType::Bool(), {});
    add_builtin("g3d_clear", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("g3d_present", SemaType::Void(), {});
    add_builtin("g3d_dt", SemaType::Float(), {});
    add_builtin("g3d_key_down", SemaType::Bool(), {SemaType::Any()});
    add_builtin("g3d_mouse_x", SemaType::Int(), {});
    add_builtin("g3d_mouse_y", SemaType::Int(), {});
    add_builtin("g3d_mouse_btn", SemaType::Bool(), {}, true);
    add_builtin("g3d_camera", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("g3d_cube", SemaType::Int(), {SemaType::Float()});
    add_builtin("g3d_box", SemaType::Int(), {SemaType::Float(), SemaType::Float(), SemaType::Float()});
    add_builtin("g3d_sphere", SemaType::Int(), {SemaType::Float()}, true);
    add_builtin("g3d_plane", SemaType::Int(), {SemaType::Float(), SemaType::Float()}, true);
    add_builtin("g3d_load_obj", SemaType::Int(), {SemaType::String()});
    add_builtin("g3d_set_texture", SemaType::Void(), {SemaType::Int(), SemaType::String()});
    add_builtin("g3d_set_color", SemaType::Void(), {SemaType::Int(), SemaType::Any()}, true);
    add_builtin("g3d_transform", SemaType::Void(), {SemaType::Int(), SemaType::Any()}, true);
    add_builtin("g3d_draw", SemaType::Void(), {SemaType::Int()});
    add_builtin("g3d_dir_light", SemaType::Void(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("g3d_point_light", SemaType::Void(), {SemaType::Any(), SemaType::Float(), SemaType::Float(), SemaType::Float()}, true);
    add_builtin("g3d_fog", SemaType::Void(), {SemaType::Float(), SemaType::Float(), SemaType::Float()}, true);
    add_builtin("g3d_skybox", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("g3d_bind_framebuffer", SemaType::Void(), {SemaType::Any(), SemaType::Int(), SemaType::Int()}, true);

    add_builtin("phys_world", SemaType::Int(), {}, true);
    add_builtin("phys_step", SemaType::Void(), {SemaType::Int(), SemaType::Float()}, true);
    add_builtin("phys_box", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Any()}, true);
    add_builtin("phys_sphere", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Any()}, true);
    add_builtin("phys_cloth", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Float(), SemaType::Float()}, true);
    add_builtin("phys_link", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Float()}, true);
    add_builtin("phys_impulse", SemaType::Void(), {SemaType::Int(), SemaType::Any()}, true);
    add_builtin("phys_raycast", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Any(), SemaType::Float()});
    add_builtin("phys_get_pos", vec3_t, {SemaType::Int()}, true);
    add_builtin("phys_get_rot", vec3_t, {SemaType::Int()}, true);
    add_builtin("phys_set_pos", SemaType::Void(), {SemaType::Int(), SemaType::Any()}, true);
    add_builtin("phys_set_vel", SemaType::Void(), {SemaType::Int(), SemaType::Any()}, true);

    // 2D Graphics (!2d! / !gfx2d! / !2DGraphics!)
    add_builtin("g2d_window", SemaType::Int(), {SemaType::Any()}, true);
    add_builtin("g2d_is_open", SemaType::Bool(), {});
    add_builtin("g2d_clear", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("g2d_present", SemaType::Void(), {});
    add_builtin("g2d_dt", SemaType::Float(), {});
    add_builtin("g2d_key_down", SemaType::Bool(), {SemaType::Any()});
    add_builtin("g2d_mouse_x", SemaType::Int(), {});
    add_builtin("g2d_mouse_y", SemaType::Int(), {});
    add_builtin("g2d_mouse_btn", SemaType::Bool(), {}, true);
    add_builtin("g2d_load_tex", SemaType::Int(), {SemaType::String()});
    add_builtin("g2d_draw_sprite", SemaType::Void(), {SemaType::Int(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float()}, true);
    add_builtin("g2d_draw_frame", SemaType::Void(), {SemaType::Int(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float()});
    add_builtin("g2d_cam_set", SemaType::Void(), {SemaType::Float(), SemaType::Float()}, true);
    add_builtin("g2d_cam_shake", SemaType::Void(), {SemaType::Float(), SemaType::Float()});
    add_builtin("g2d_emit_particles", SemaType::Void(), {SemaType::Float(), SemaType::Float(), SemaType::Int()}, true);
    add_builtin("g2d_update_particles", SemaType::Void(), {}, true);
    add_builtin("g2d_bind_framebuffer", SemaType::Void(), {SemaType::Any(), SemaType::Int(), SemaType::Int()}, true);
    add_builtin("g2d_rect", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("g2d_circle", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("g2d_line", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int()}, true);
    add_builtin("g2d_text", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::String(), SemaType::Int(), SemaType::Int()});
    add_builtin("g2d_close", SemaType::Void(), {});

    add_builtin("gfx_init", SemaType::Bool(), {SemaType::String(), SemaType::Int(), SemaType::Int()});
    add_builtin("gfx_running", SemaType::Bool(), {});
    add_builtin("gfx_clear", SemaType::Void(), {SemaType::String()});
    add_builtin("gfx_rect", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::String()});
    add_builtin("gfx_circle", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::String()});
    add_builtin("gfx_line", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::String()}, true);
    add_builtin("gfx_text", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::String(), SemaType::Int(), SemaType::String()});
    add_builtin("gfx_key", SemaType::Bool(), {SemaType::String()});
    add_builtin("gfx_mouse_x", SemaType::Int(), {});
    add_builtin("gfx_mouse_y", SemaType::Int(), {});
    add_builtin("gfx_mouse_down", SemaType::Bool(), {SemaType::String()});
    add_builtin("gfx_collide", SemaType::Int(), {SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float(), SemaType::Float()});
    add_builtin("gfx_frame", SemaType::Void(), {}, true);
    add_builtin("gfx_close", SemaType::Void(), {});

    add_builtin("create_canvas", SemaType::Int(), {SemaType::Int(), SemaType::Int()});
    add_builtin("fill_color", SemaType::Void(), {SemaType::String()});
    add_builtin("draw_rectangle", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("draw_circle", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("draw_line", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("save_image", SemaType::Int(), {SemaType::String()});

    // Audio (!audio!)
    add_builtin("audio_init", SemaType::Void(), {});
    add_builtin("synth_play", SemaType::Void(), {SemaType::String(), SemaType::Float(), SemaType::Int()}, true);
    add_builtin("synth", SemaType::Void(), {SemaType::String(), SemaType::Float(), SemaType::Int()}, true);
    add_builtin("tone", SemaType::Void(), {SemaType::Float(), SemaType::Int()}, true);
    add_builtin("beep", SemaType::Void(), {}, true);
    add_builtin("sound_play", SemaType::Int(), {SemaType::String()}, true);
    add_builtin("sound_stop", SemaType::Void(), {});
    add_builtin("sound_pause", SemaType::Void(), {});
    add_builtin("sound_resume", SemaType::Void(), {});
    add_builtin("sound_volume", SemaType::Void(), {SemaType::Int()});
    add_builtin("sound_is_playing", SemaType::Bool(), {});
    add_builtin("bind_synth", SemaType::Void(), {SemaType::String(), SemaType::String(), SemaType::Float(), SemaType::Int()}, true);
    add_builtin("bind_sound", SemaType::Void(), {SemaType::String(), SemaType::String()});
    add_builtin("attach_sound", SemaType::Void(), {SemaType::Any(), SemaType::String(), SemaType::Float(), SemaType::Int()}, true);


    add_builtin("surface", SemaType::Void(), {SemaType::String()});
    add_builtin("auto_size_display", SemaType::Void(), {});
    add_builtin("gui_cmd", SemaType::Void(), {SemaType::String()});
    add_builtin("gui_show", SemaType::Void(), {});
    add_builtin("gui_get", SemaType::String(), {SemaType::String()});
    add_builtin("gui_set", SemaType::Void(), {SemaType::String(), SemaType::String()});
    add_builtin("set_text", SemaType::Void(), {SemaType::String(), SemaType::String()});
    add_builtin("get_text", SemaType::String(), {SemaType::String()});
    add_builtin("set_val", SemaType::Void(), {SemaType::String(), SemaType::Any()});
    add_builtin("get_val", SemaType::Any(), {SemaType::String()});
    add_builtin("set_color", SemaType::Void(), {SemaType::String(), SemaType::String()});
    add_builtin("set_bg", SemaType::Void(), {SemaType::String(), SemaType::String()});
    add_builtin("set_visible", SemaType::Void(), {SemaType::String(), SemaType::Any()});
    add_builtin("is_checked", SemaType::Int(), {SemaType::String()});
    add_builtin("add_item", SemaType::Void(), {SemaType::String(), SemaType::String()});
    add_builtin("clear_list", SemaType::Void(), {SemaType::String()});
    add_builtin("get_selected", SemaType::String(), {SemaType::String()});
    add_builtin("switch_tab", SemaType::Void(), {SemaType::String()});
    add_builtin("fullscreen", SemaType::Void(), {});
    add_builtin("web_start", SemaType::Void(), {SemaType::String()});
    add_builtin("web_compile", SemaType::String(), {SemaType::String()}, true);


}

} // namespace grass::sema

#include "vm/GrassVM.h"
#include "GrassVersion.h"
#include "media/FastMath.h"
#include "baremetal/GrassASM.h"
#include "baremetal/GrassKeyboard.h"
#include "runtime/GrassHardware.h"
#include "baremetal/GrassBoot.h"
#include "baremetal/GrassBaremetal.h"
#include "media/CoreMath.h"
#include "runtime/GrassWeb.h"
#include "vm/GrassMemory.h"
#include "runtime/GrassRandom.h"
#include "baremetal/GSE.h"
#include "media/gui_engine.h"
#include "runtime/GrassTime.h"
#include "runtime/GrassNet.h"
#include "runtime/GrassJson.h"
#include "runtime/GrassSys.h"
#include "runtime/GrassProc.h"
#include "runtime/GrassFsys.h"
#include "runtime/GrassTg.h"
#include "media/Grass2DGraphics.h"
#include "media/GrassMath3D.h"
#include "media/GrassGfx3D.h"
#include "media/GrassPhysics.h"
#include "runtime/GrassIO.h"
#include "baremetal/GrassHAL.h"
#include "vm/GrassJIT.h"
#include "runtime/GrassThread.h"
#include "baremetal/GrassMMIO.h"
#include "media/GrassAudio.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <thread>
#endif

using namespace std;

bool GrassVM::callBuiltinMedia(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args) {
                    // ===== БИБЛИОТЕКА !2DGraphics! =====
                    if (fn == "gfx_init" && argc >= 3) {
                        std::string title = args[0].toString();
                        int w = (int)std::stod(args[1].toString());
                        int h = (int)std::stod(args[2].toString());
                        push(GrassValue::Num(Grass2DGraphics::init(title, w, h) ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_running") {
                        push(GrassValue::Num(Grass2DGraphics::running() ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_clear" && argc >= 1) {
                        Grass2DGraphics::clear(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_rect" && argc >= 5) {
                        int x = (int)std::stod(args[0].toString());
                        int y = (int)std::stod(args[1].toString());
                        int w = (int)std::stod(args[2].toString());
                        int h = (int)std::stod(args[3].toString());
                        Grass2DGraphics::rect(x, y, w, h, args[4].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_circle" && argc >= 4) {
                        int x = (int)std::stod(args[0].toString());
                        int y = (int)std::stod(args[1].toString());
                        int r = (int)std::stod(args[2].toString());
                        Grass2DGraphics::circle(x, y, r, args[3].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_line" && argc >= 6) {
                        int x1 = (int)std::stod(args[0].toString());
                        int y1 = (int)std::stod(args[1].toString());
                        int x2 = (int)std::stod(args[2].toString());
                        int y2 = (int)std::stod(args[3].toString());
                        int w  = (int)std::stod(args[5].toString());
                        Grass2DGraphics::line(x1, y1, x2, y2, args[4].toString(), w);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_text" && argc >= 5) {
                        int x = (int)std::stod(args[0].toString());
                        int y = (int)std::stod(args[1].toString());
                        std::string t = args[2].toString();
                        int sz = (int)std::stod(args[3].toString());
                        Grass2DGraphics::text(x, y, t, sz, args[4].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_key" && argc >= 1) {
                        push(GrassValue::Num(Grass2DGraphics::key(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_mouse_x") {
                        push(GrassValue::Num((double)Grass2DGraphics::mouse_x()));
                    }
                    else if (fn == "gfx_mouse_y") {
                        push(GrassValue::Num((double)Grass2DGraphics::mouse_y()));
                    }
                    else if (fn == "gfx_mouse_down" && argc >= 1) {
                        push(GrassValue::Num(Grass2DGraphics::mouse_down(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_collide" && argc >= 8) {
                        double x1 = std::stod(args[0].toString());
                        double y1 = std::stod(args[1].toString());
                        double w1 = std::stod(args[2].toString());
                        double h1 = std::stod(args[3].toString());
                        double x2 = std::stod(args[4].toString());
                        double y2 = std::stod(args[5].toString());
                        double w2 = std::stod(args[6].toString());
                        double h2 = std::stod(args[7].toString());
                        push(GrassValue::Num(Grass2DGraphics::collide(x1, y1, w1, h1, x2, y2, w2, h2) ? 1.0 : 0.0));
                    }
                    else if (fn == "gfx_frame") {
                        int fps = (argc >= 1) ? (int)std::stod(args[0].toString()) : 60;
                        Grass2DGraphics::frame(fps);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gfx_close") {
                        Grass2DGraphics::close();
                        push(GrassValue::Num(1.0));
                    }


                    // ===== !math3d! =====
                    else if (fn == "v3_create" && argc >= 3) {
                        GrassVec3 v((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber());
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(v.x), GrassValue::Num(v.y), GrassValue::Num(v.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "v3_add" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 r = GrassMath3D::add(getV(args[0]), getV(args[1]));
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(r.x), GrassValue::Num(r.y), GrassValue::Num(r.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "v3_sub" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 r = GrassMath3D::sub(getV(args[0]), getV(args[1]));
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(r.x), GrassValue::Num(r.y), GrassValue::Num(r.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "v3_mul" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 r = GrassMath3D::mul_scalar(getV(args[0]), (float)args[1].toNumber());
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(r.x), GrassValue::Num(r.y), GrassValue::Num(r.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "v3_dot" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        push(GrassValue::Num((double)GrassMath3D::dot(getV(args[0]), getV(args[1]))));
                    }
                    else if (fn == "v3_cross" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 r = GrassMath3D::cross(getV(args[0]), getV(args[1]));
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(r.x), GrassValue::Num(r.y), GrassValue::Num(r.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "v3_length" && argc >= 1) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        push(GrassValue::Num((double)GrassMath3D::length(getV(args[0]))));
                    }
                    else if (fn == "v3_normalize" && argc >= 1) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 r = GrassMath3D::normalize(getV(args[0]));
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(r.x), GrassValue::Num(r.y), GrassValue::Num(r.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "v3_lerp" && argc >= 3) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 r = GrassMath3D::lerp(getV(args[0]), getV(args[1]), (float)args[2].toNumber());
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(r.x), GrassValue::Num(r.y), GrassValue::Num(r.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "m4_identity") {
                        GrassMat4 m = GrassMath3D::identity();
                        GrassArray* arr = new GrassArray(); arr->elements.resize(16);
                        for (int i = 0; i < 16; i++) arr->elements[i] = GrassValue::Num(m.m[i]);
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "m4_translate" && argc >= 1) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 v;
                        if (argc >= 3) {
                            v = GrassVec3((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber());
                        } else {
                            v = getV(args[0]);
                        }
                        GrassMat4 m = GrassMath3D::translate(v);
                        GrassArray* arr = new GrassArray(); arr->elements.resize(16);
                        for (int i = 0; i < 16; i++) arr->elements[i] = GrassValue::Num(m.m[i]);
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "m4_rotate" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 axis;
                        if (argc >= 4) {
                            axis = GrassVec3((float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber());
                        } else {
                            axis = getV(args[1]);
                        }
                        GrassMat4 m = GrassMath3D::rotate((float)args[0].toNumber(), axis);
                        GrassArray* arr = new GrassArray(); arr->elements.resize(16);
                        for (int i = 0; i < 16; i++) arr->elements[i] = GrassValue::Num(m.m[i]);
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "m4_scale" && argc >= 1) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 v;
                        if (argc >= 3) {
                            v = GrassVec3((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber());
                        } else {
                            v = getV(args[0]);
                        }
                        GrassMat4 m = GrassMath3D::scale(v);
                        GrassArray* arr = new GrassArray(); arr->elements.resize(16);
                        for (int i = 0; i < 16; i++) arr->elements[i] = GrassValue::Num(m.m[i]);
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "m4_look_at" && argc >= 3) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassMat4 m = GrassMath3D::look_at(getV(args[0]), getV(args[1]), getV(args[2]));
                        GrassArray* arr = new GrassArray(); arr->elements.resize(16);
                        for (int i = 0; i < 16; i++) arr->elements[i] = GrassValue::Num(m.m[i]);
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "m4_perspective" && argc >= 4) {
                        GrassMat4 m = GrassMath3D::perspective((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber());
                        GrassArray* arr = new GrassArray(); arr->elements.resize(16);
                        for (int i = 0; i < 16; i++) arr->elements[i] = GrassValue::Num(m.m[i]);
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "m4_mul" && argc >= 2) {
                        GrassMat4 m1 = GrassMath3D::identity(), m2 = GrassMath3D::identity();
                        if (args[0].is_array() && args[0].asArray()->elements.size() >= 16) {
                            for (int i = 0; i < 16; i++) m1.m[i] = (float)args[0].asArray()->elements[i].toNumber();
                        }
                        if (args[1].is_array() && args[1].asArray()->elements.size() >= 16) {
                            for (int i = 0; i < 16; i++) m2.m[i] = (float)args[1].asArray()->elements[i].toNumber();
                        }
                        GrassMat4 m = GrassMath3D::multiply(m1, m2);
                        GrassArray* arr = new GrassArray(); arr->elements.resize(16);
                        for (int i = 0; i < 16; i++) arr->elements[i] = GrassValue::Num(m.m[i]);
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }

                    // ===== !2DGraphics! (g2d_) =====
                    else if (fn == "g2d_window" && argc >= 1) {
                        string title = "GrassGreen 2D Window";
                        int w = 800, h = 600;
                        if (argc >= 3) {
                            if (args[0].is_str()) {
                                title = args[0].toString();
                                w = (int)args[1].toNumber();
                                h = (int)args[2].toNumber();
                            } else {
                                w = (int)args[0].toNumber();
                                h = (int)args[1].toNumber();
                                title = args[2].toString();
                            }
                        } else if (argc == 2) {
                            w = (int)args[0].toNumber();
                            h = (int)args[1].toNumber();
                        } else if (argc == 1) {
                            title = args[0].toString();
                        }
                        push(GrassValue::Num(Grass2D::init_window(w, h, title) ? 1.0 : 0.0));
                    }
                    else if (fn == "g2d_is_open") {
                        push(GrassValue::Num(Grass2D::is_open() ? 1.0 : 0.0));
                    }
                    else if (fn == "g2d_clear" && argc >= 1) {
                        if (argc >= 3) {
                            Grass2D::clear((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber());
                        } else {
                            uint32_t hex = (uint32_t)args[0].toNumber();
                            float r = ((hex >> 16) & 0xFF) / 255.0f;
                            float g = ((hex >> 8) & 0xFF) / 255.0f;
                            float b = (hex & 0xFF) / 255.0f;
                            Grass2D::clear(r, g, b);
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_present") {
                        Grass2D::present();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_dt") {
                        push(GrassValue::Num((double)Grass2D::dt()));
                    }
                    else if (fn == "g2d_key_down" && argc >= 1) {
                        push(GrassValue::Num(Grass2D::key_down(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "g2d_mouse_x") {
                        push(GrassValue::Num((double)Grass2D::mouse_x()));
                    }
                    else if (fn == "g2d_mouse_y") {
                        push(GrassValue::Num((double)Grass2D::mouse_y()));
                    }
                    else if (fn == "g2d_mouse_btn" && argc >= 1) {
                        push(GrassValue::Num(Grass2D::mouse_btn((int)args[0].toNumber()) ? 1.0 : 0.0));
                    }
                    else if (fn == "g2d_load_tex" && argc >= 1) {
                        push(GrassValue::Num((double)Grass2D::load_tex(args[0].toString())));
                    }
                    else if (fn == "g2d_draw_sprite" && argc >= 5) {
                        float angle = (argc >= 6) ? (float)args[5].toNumber() : 0.0f;
                        float alpha = (argc >= 7) ? (float)args[6].toNumber() : 1.0f;
                        Grass2D::draw_sprite((uint32_t)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber(), (float)args[4].toNumber(), angle, alpha);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_draw_frame" && argc >= 9) {
                        Grass2D::draw_frame((uint32_t)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber(), (float)args[4].toNumber(), (float)args[5].toNumber(), (float)args[6].toNumber(), (float)args[7].toNumber(), (float)args[8].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_cam_set" && argc >= 2) {
                        float zoom = (argc >= 3) ? (float)args[2].toNumber() : 1.0f;
                        float angle = (argc >= 4) ? (float)args[3].toNumber() : 0.0f;
                        Grass2D::cam_set((float)args[0].toNumber(), (float)args[1].toNumber(), zoom, angle);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_cam_shake" && argc >= 2) {
                        Grass2D::cam_shake((float)args[0].toNumber(), (float)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_emit_particles" && argc >= 3) {
                        uint32_t color = (argc >= 4) ? (uint32_t)args[3].toNumber() : 0xFFFF00;
                        float speed = (argc >= 5) ? (float)args[4].toNumber() : 100.0f;
                        float life = (argc >= 6) ? (float)args[5].toNumber() : 1.0f;
                        Grass2D::emit_particles((float)args[0].toNumber(), (float)args[1].toNumber(), (int)args[2].toNumber(), color, speed, life);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_update_particles") {
                        float d = (argc >= 1) ? (float)args[0].toNumber() : Grass2D::dt();
                        Grass2D::update_particles(d);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_bind_framebuffer" && argc >= 3) {
                        int pitch = (argc >= 4) ? (int)args[3].toNumber() : 0;
                        Grass2D::bind_framebuffer((void*)(uintptr_t)args[0].toNumber(), (int)args[1].toNumber(), (int)args[2].toNumber(), pitch);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_rect" && argc >= 5) {
                        Grass2D::draw_rect((int)args[0].toNumber(), (int)args[1].toNumber(), (int)args[2].toNumber(), (int)args[3].toNumber(), (uint32_t)args[4].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_circle" && argc >= 4) {
                        Grass2D::draw_circle((int)args[0].toNumber(), (int)args[1].toNumber(), (int)args[2].toNumber(), (uint32_t)args[3].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_line" && argc >= 5) {
                        int w = (argc >= 6) ? (int)args[5].toNumber() : 1;
                        Grass2D::draw_line((int)args[0].toNumber(), (int)args[1].toNumber(), (int)args[2].toNumber(), (int)args[3].toNumber(), (uint32_t)args[4].toNumber(), w);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_text" && argc >= 5) {
                        Grass2D::draw_text((int)args[0].toNumber(), (int)args[1].toNumber(), args[2].toString(), (int)args[3].toNumber(), (uint32_t)args[4].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g2d_close") {
                        Grass2D::close_window();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "create_canvas" && argc >= 2) {
                        push(GrassValue::Num(Grass2D::create_canvas((int)args[0].toNumber(), (int)args[1].toNumber()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fill_color" && argc >= 1) {
                        Grass2D::fill_color(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "draw_rectangle" && argc >= 4) {
                        Grass2D::canvas_rect((int)args[0].toNumber(), (int)args[1].toNumber(), (int)args[2].toNumber(), (int)args[3].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "draw_circle" && argc >= 3) {
                        Grass2D::canvas_circle((int)args[0].toNumber(), (int)args[1].toNumber(), (int)args[2].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "draw_line" && argc >= 4) {
                        Grass2D::canvas_line((int)args[0].toNumber(), (int)args[1].toNumber(), (int)args[2].toNumber(), (int)args[3].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "save_image" && argc >= 1) {
                        push(GrassValue::Num(Grass2D::save_image(args[0].toString()) ? 1.0 : 0.0));
                    }

                    // ===== !gfx3d! (g3d_) =====
                    else if (fn == "g3d_window" && argc >= 1) {
                        string title = "GrassGreen 3D Window";
                        int w = 800, h = 600;
                        if (argc >= 3) {
                            if (args[0].is_str()) {
                                title = args[0].toString();
                                w = (int)args[1].toNumber();
                                h = (int)args[2].toNumber();
                            } else {
                                w = (int)args[0].toNumber();
                                h = (int)args[1].toNumber();
                                title = args[2].toString();
                            }
                        } else if (argc == 2) {
                            w = (int)args[0].toNumber();
                            h = (int)args[1].toNumber();
                        } else if (argc == 1) {
                            title = args[0].toString();
                        }
                        push(GrassValue::Num(GrassGfx3D::init_window(w, h, title) ? 1.0 : 0.0));
                    }
                    else if (fn == "g3d_is_open") {
                        push(GrassValue::Num(GrassGfx3D::is_open() ? 1.0 : 0.0));
                    }
                    else if (fn == "g3d_clear" && argc >= 1) {
                        if (argc >= 3) {
                            GrassGfx3D::clear((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber());
                        } else {
                            uint32_t hex = (uint32_t)args[0].toNumber();
                            float r = ((hex >> 16) & 0xFF) / 255.0f;
                            float g = ((hex >> 8) & 0xFF) / 255.0f;
                            float b = (hex & 0xFF) / 255.0f;
                            GrassGfx3D::clear(r, g, b);
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_present") {
                        GrassGfx3D::present();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_dt") {
                        push(GrassValue::Num((double)GrassGfx3D::dt()));
                    }
                    else if (fn == "g3d_key_down" && argc >= 1) {
                        if (args[0].is_str()) {
                            push(GrassValue::Num(GrassGfx3D::key_down_str(args[0].toString()) ? 1.0 : 0.0));
                        } else {
                            push(GrassValue::Num(GrassGfx3D::key_down((int)args[0].toNumber()) ? 1.0 : 0.0));
                        }
                    }
                    else if (fn == "g3d_mouse_x") {
                        push(GrassValue::Num((double)GrassGfx3D::mouse_x()));
                    }
                    else if (fn == "g3d_mouse_y") {
                        push(GrassValue::Num((double)GrassGfx3D::mouse_y()));
                    }
                    else if (fn == "g3d_mouse_btn") {
                        int btn = (argc >= 1) ? (int)args[0].toNumber() : 0;
                        push(GrassValue::Num(GrassGfx3D::mouse_btn(btn) ? 1.0 : 0.0));
                    }
                    else if (fn == "g3d_camera") {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        if (argc >= 7) {
                            GrassGfx3D::set_camera(GrassVec3((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber()), GrassVec3((float)args[3].toNumber(), (float)args[4].toNumber(), (float)args[5].toNumber()), (float)args[6].toNumber());
                        } else if (argc >= 3) {
                            GrassGfx3D::set_camera(getV(args[0]), getV(args[1]), (float)args[2].toNumber());
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_cube" && argc >= 1) {
                        push(GrassValue::Num((double)GrassGfx3D::create_cube((float)args[0].toNumber())));
                    }
                    else if (fn == "g3d_box" && argc >= 3) {
                        push(GrassValue::Num((double)GrassGfx3D::create_box((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber())));
                    }
                    else if (fn == "g3d_sphere" && argc >= 1) {
                        int segs = (argc >= 2) ? (int)args[1].toNumber() : 16;
                        push(GrassValue::Num((double)GrassGfx3D::create_sphere((float)args[0].toNumber(), segs)));
                    }
                    else if (fn == "g3d_plane" && argc >= 2) {
                        int segs = (argc >= 3) ? (int)args[2].toNumber() : 10;
                        push(GrassValue::Num((double)GrassGfx3D::create_plane((float)args[0].toNumber(), (float)args[1].toNumber(), segs)));
                    }
                    else if (fn == "g3d_load_obj" && argc >= 1) {
                        push(GrassValue::Num((double)GrassGfx3D::load_obj(args[0].toString())));
                    }
                    else if (fn == "g3d_set_color" && argc >= 2) {
                        if (argc >= 4) {
                            GrassGfx3D::set_color((uint32_t)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber());
                        } else {
                            uint32_t hex = (uint32_t)args[1].toNumber();
                            float r = ((hex >> 16) & 0xFF) / 255.0f;
                            float g = ((hex >> 8) & 0xFF) / 255.0f;
                            float b = (hex & 0xFF) / 255.0f;
                            GrassGfx3D::set_color((uint32_t)args[0].toNumber(), r, g, b);
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_transform" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        uint32_t mid = (uint32_t)args[0].toNumber();
                        if (argc == 2 && args[1].is_array() && args[1].asArray() && args[1].asArray()->elements.size() >= 16) {
                            GrassMat4 mat;
                            for (int i = 0; i < 16; i++) {
                                mat.m[i] = (float)args[1].asArray()->elements[i].toNumber();
                            }
                            GrassGfx3D::transform_mat(mid, mat);
                        } else if (argc >= 4) {
                            if (argc >= 10) {
                                GrassGfx3D::transform(mid, GrassVec3((float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber()), GrassVec3((float)args[4].toNumber(), (float)args[5].toNumber(), (float)args[6].toNumber()), GrassVec3((float)args[7].toNumber(), (float)args[8].toNumber(), (float)args[9].toNumber()));
                            } else {
                                GrassGfx3D::transform(mid, getV(args[1]), getV(args[2]), getV(args[3]));
                            }
                        } else if (argc == 2) {
                            GrassGfx3D::transform(mid, getV(args[1]), GrassVec3(0, 0, 0), GrassVec3(1, 1, 1));
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_draw" && argc >= 1) {
                        GrassGfx3D::draw((uint32_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_dir_light" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        if (argc >= 4) {
                            float intensity = (argc >= 5) ? (float)args[4].toNumber() : 1.0f;
                            GrassGfx3D::set_dir_light(getV(args[0]), (float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber(), intensity);
                        } else {
                            uint32_t hex = (uint32_t)args[1].toNumber();
                            float r = ((hex >> 16) & 0xFF) / 255.0f;
                            float g = ((hex >> 8) & 0xFF) / 255.0f;
                            float b = (hex & 0xFF) / 255.0f;
                            GrassGfx3D::set_dir_light(getV(args[0]), r, g, b, 1.0f);
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_point_light" && argc >= 4) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        float range = (argc >= 5) ? (float)args[4].toNumber() : 10.0f;
                        GrassGfx3D::set_point_light(getV(args[0]), (float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber(), range);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_fog" && argc >= 3) {
                        float density = (argc >= 4) ? (float)args[3].toNumber() : 0.05f;
                        GrassGfx3D::set_fog((float)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber(), density);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_skybox" && argc >= 2) {
                        GrassGfx3D::set_skybox((uint32_t)args[0].toNumber(), (uint32_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "g3d_bind_framebuffer" && argc >= 3) {
                        int pitch = (argc >= 4) ? (int)args[3].toNumber() : 0;
                        GrassGfx3D::bind_framebuffer((void*)(uintptr_t)args[0].toNumber(), (int)args[1].toNumber(), (int)args[2].toNumber(), pitch);
                        push(GrassValue::Num(1.0));
                    }

                    // ===== !physics! (phys_) =====
                    else if (fn == "phys_world") {
                        float g_y = -9.81f;
                        if (argc >= 3) g_y = (float)args[1].toNumber();
                        else if (argc >= 1) g_y = (float)args[0].toNumber();
                        push(GrassValue::Num((double)GrassPhysics::create_world(g_y)));
                    }
                    else if (fn == "phys_step" && argc >= 2) {
                        int substeps = (argc >= 3) ? (int)args[2].toNumber() : 8;
                        if (substeps < 8) substeps = 8;
                        GrassPhysics::step_world((uint32_t)args[0].toNumber(), (float)args[1].toNumber(), substeps);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "phys_box" && argc >= 3) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        if (argc >= 7) {
                            float px = (float)args[1].toNumber();
                            float py = (float)args[2].toNumber();
                            float pz = (float)args[3].toNumber();
                            float sx = (float)args[4].toNumber();
                            float sy = (float)args[5].toNumber();
                            float sz = (float)args[6].toNumber();
                            float mass = 1.0f;
                            string mat = "wood";
                            float brk = 0.0f;
                            if (argc >= 8) {
                                if (args[7].is_str()) {
                                    mat = args[7].toString();
                                    if (argc >= 9) brk = (float)args[8].toNumber();
                                } else {
                                    mass = (float)args[7].toNumber();
                                    if (argc >= 9) {
                                        if (args[8].is_str()) mat = args[8].toString();
                                        else brk = (float)args[8].toNumber();
                                    }
                                    if (argc >= 10) brk = (float)args[9].toNumber();
                                }
                            }
                            push(GrassValue::Num((double)GrassPhysics::add_box((uint32_t)args[0].toNumber(), px, py, pz, sx, sy, sz, mass, mat, brk)));
                        } else {
                            GrassVec3 p = getV(args[1]);
                            GrassVec3 s = getV(args[2]);
                            float mass = 1.0f;
                            string mat = "wood";
                            float brk = 0.0f;
                            if (argc >= 4) {
                                if (args[3].is_str()) {
                                    mat = args[3].toString();
                                    if (argc >= 5) brk = (float)args[4].toNumber();
                                } else {
                                    mass = (float)args[3].toNumber();
                                    if (argc >= 5) {
                                        if (args[4].is_str()) mat = args[4].toString();
                                        else brk = (float)args[5].toNumber();
                                    }
                                    if (argc >= 6) brk = (float)args[5].toNumber();
                                }
                            }
                            push(GrassValue::Num((double)GrassPhysics::add_box((uint32_t)args[0].toNumber(), p.x, p.y, p.z, s.x, s.y, s.z, mass, mat, brk)));
                        }
                    }
                    else if (fn == "phys_sphere" && argc >= 3) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        if (argc >= 5) {
                            float px = (float)args[1].toNumber();
                            float py = (float)args[2].toNumber();
                            float pz = (float)args[3].toNumber();
                            float r = (float)args[4].toNumber();
                            float mass = 1.0f;
                            string mat = "steel";
                            if (argc >= 6) {
                                if (args[5].is_str()) mat = args[5].toString();
                                else mass = (float)args[5].toNumber();
                            }
                            if (argc >= 7 && args[6].is_str()) mat = args[6].toString();
                            push(GrassValue::Num((double)GrassPhysics::add_sphere((uint32_t)args[0].toNumber(), px, py, pz, r, mass, mat)));
                        } else {
                            GrassVec3 p = getV(args[1]);
                            float r = (float)args[2].toNumber();
                            float mass = 1.0f;
                            string mat = "steel";
                            if (argc >= 4) {
                                if (args[3].is_str()) mat = args[3].toString();
                                else mass = (float)args[3].toNumber();
                            }
                            if (argc >= 5 && args[4].is_str()) mat = args[4].toString();
                            push(GrassValue::Num((double)GrassPhysics::add_sphere((uint32_t)args[0].toNumber(), p.x, p.y, p.z, r, mass, mat)));
                        }
                    }
                    else if (fn == "phys_cloth" && argc >= 4) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 p = getV(args[1]);
                        float w = (float)args[2].toNumber();
                        float h = (float)args[3].toNumber();
                        int segs = (argc >= 5) ? (int)args[4].toNumber() : 8;
                        push(GrassValue::Num((double)GrassPhysics::add_cloth((uint32_t)args[0].toNumber(), p.x, p.y, p.z, w, h, segs)));
                    }
                    else if (fn == "phys_link" && argc >= 4) {
                        float stiff = (float)args[3].toNumber();
                        float brk = (argc >= 5) ? (float)args[4].toNumber() : 0.0f;
                        GrassPhysics::add_link((uint32_t)args[0].toNumber(), (uint32_t)args[1].toNumber(), (uint32_t)args[2].toNumber(), stiff, brk);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "phys_impulse" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        if (argc >= 5) {
                            GrassPhysics::apply_impulse((uint32_t)args[0].toNumber(), (uint32_t)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber(), (float)args[4].toNumber());
                        } else if (argc >= 3) {
                            GrassVec3 f = getV(args[2]);
                            GrassPhysics::apply_impulse((uint32_t)args[0].toNumber(), (uint32_t)args[1].toNumber(), f.x, f.y, f.z);
                        } else {
                            GrassVec3 f = getV(args[1]);
                            GrassPhysics::apply_impulse(1, (uint32_t)args[0].toNumber(), f.x, f.y, f.z);
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "phys_raycast" && argc >= 4) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        GrassVec3 o = getV(args[1]);
                        GrassVec3 d = getV(args[2]);
                        float max_dist = (float)args[3].toNumber();
                        PhysRayHit hit = GrassPhysics::raycast((uint32_t)args[0].toNumber(), o.x, o.y, o.z, d.x, d.y, d.z, max_dist);
                        push(GrassValue::Num(hit.hit ? (double)hit.body_id : 0.0));
                    }
                    else if (fn == "phys_get_pos" && argc >= 1) {
                        uint32_t wid = (argc >= 2) ? (uint32_t)args[0].toNumber() : 1;
                        uint32_t bid = (argc >= 2) ? (uint32_t)args[1].toNumber() : (uint32_t)args[0].toNumber();
                        GrassVec3 p = GrassPhysics::get_pos(wid, bid);
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(p.x), GrassValue::Num(p.y), GrassValue::Num(p.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "phys_get_rot" && argc >= 1) {
                        uint32_t wid = (argc >= 2) ? (uint32_t)args[0].toNumber() : 1;
                        uint32_t bid = (argc >= 2) ? (uint32_t)args[1].toNumber() : (uint32_t)args[0].toNumber();
                        GrassVec3 r = GrassPhysics::get_rot(wid, bid);
                        GrassArray* arr = new GrassArray(); arr->elements = {GrassValue::Num(r.x), GrassValue::Num(r.y), GrassValue::Num(r.z)};
                        heap_arrays.push_back(arr); push(GrassValue::Array(arr));
                    }
                    else if (fn == "phys_set_pos" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        if (argc >= 5) {
                            GrassPhysics::set_pos((uint32_t)args[0].toNumber(), (uint32_t)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber(), (float)args[4].toNumber());
                        } else if (argc >= 4) {
                            GrassPhysics::set_pos(1, (uint32_t)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber());
                        } else {
                            GrassVec3 p = getV(args[1]);
                            GrassPhysics::set_pos(1, (uint32_t)args[0].toNumber(), p.x, p.y, p.z);
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "phys_set_vel" && argc >= 2) {
                        auto getV = [](const GrassValue& g) -> GrassVec3 {
                            if (g.is_array() && g.asArray() && g.asArray()->elements.size() >= 3)
                                return GrassVec3((float)g.asArray()->elements[0].toNumber(), (float)g.asArray()->elements[1].toNumber(), (float)g.asArray()->elements[2].toNumber());
                            return GrassVec3((float)g.toNumber(), 0, 0);
                        };
                        if (argc >= 5) {
                            GrassPhysics::set_vel((uint32_t)args[0].toNumber(), (uint32_t)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber(), (float)args[4].toNumber());
                        } else if (argc >= 4) {
                            GrassPhysics::set_vel(1, (uint32_t)args[0].toNumber(), (float)args[1].toNumber(), (float)args[2].toNumber(), (float)args[3].toNumber());
                        } else {
                            GrassVec3 v = getV(args[1]);
                            GrassPhysics::set_vel(1, (uint32_t)args[0].toNumber(), v.x, v.y, v.z);
                        }
                        push(GrassValue::Num(1.0));
                    }

                    // Файлы и ОС

                    else if (fn == "surface" && argc >= 1) {
                        GrassGUI::set_surface_mode(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "auto_size_display") {
                        GrassGUI::auto_size_display(true);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "fullscreen" || fn == "fullscrean") {
                        GrassGUI::set_fullscreen(true);
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "switch_tab" || fn == "switch") && argc >= 1) {
                        GrassGUI::switch_tab(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gui_cmd" && argc >= 1) {
                        syncVarsToGUI();
                        GrassGUI::exec_command(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gui_get" && argc >= 1) { push(GrassValue::Str(GrassGUI::get_input_value(args[0].toString()))); }
                    else if (fn == "gui_set" && argc >= 2) { GrassGUI::set_variable(args[0].toString(), args[1].toString()); push(GrassValue::Num(1.0)); }
                    else if (fn == "gui_show") {
                        syncVarsToGUI();
                        GrassGUI::show_window();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "gui_file" && argc >= 1) {
                        syncVarsToGUI();
                        GrassGUI::run_from_file(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "set_text" && argc >= 2) {
                        GrassGUI::set_text(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "get_text" && argc >= 1) {
                        push(GrassValue::Str(GrassGUI::get_text(args[0].toString())));
                    }
                    else if (fn == "set_val" && argc >= 2) {
                        GrassGUI::set_val(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "get_val" && argc >= 1) {
                        push(GrassValue::Num(GrassGUI::get_val(args[0].toString())));
                    }
                    else if (fn == "set_color" && argc >= 2) {
                        GrassGUI::set_color(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "set_bg" && argc >= 2) {
                        GrassGUI::set_bg(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "set_visible" && argc >= 2) {
                        GrassGUI::set_visible(args[0].toString(), args[1].toNumber() != 0.0 || args[1].toString() == "true");
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "hide" && argc >= 1) {
                        GrassGUI::set_visible(args[0].toString(), false);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "show" && argc >= 1) {
                        GrassGUI::set_visible(args[0].toString(), true);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "is_checked" && argc >= 1) {
                        push(GrassValue::Num(GrassGUI::is_checked(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "add_item" && argc >= 2) {
                        GrassGUI::add_list_item(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "clear_list" && argc >= 1) {
                        GrassGUI::clear_list(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "get_selected" && argc >= 1) {
                        push(GrassValue::Str(GrassGUI::get_list_selected(args[0].toString())));
                    }
                    else if (fn == "on_timer" && argc >= 2) {
                        GrassGUI::add_timer((int)args[0].toNumber(), (uint32_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "web_start" && argc >= 1) { GrassWeb_LoadPage(args[0].toString()); push(GrassValue::Num(1.0)); }
                    else if (fn == "web_compile" && argc >= 1) {
                        string html = CompileGrassToHTML(args[0].toString());
                        if (argc >= 2) {
                            ofstream out(args[1].toString(), ios::trunc);
                            out << html;
                            out.close();
                        }
                        push(GrassValue::Str(html));
                    }
                    // ===== БИБЛИОТЕКА !audio! =====
                    else if (fn == "tone" && argc >= 2) {
                        double vol = (argc >= 3) ? args[2].toNumber() : 80.0;
                        GrassAudio::tone(args[0].toNumber(), (int)args[1].toNumber(), vol);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "beep") {
                        double f = (argc >= 1) ? args[0].toNumber() : 750.0;
                        int dur = (argc >= 2) ? (int)args[1].toNumber() : 300;
                        GrassAudio::beep(f, dur);
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "synth_play" || fn == "synth") && argc >= 3) {
                        double vol = (argc >= 4) ? args[3].toNumber() : 80.0;
                        GrassAudio::synth_play(args[0].toString(), args[1].toNumber(), (int)args[2].toNumber(), vol);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_play" && argc >= 1) {
                        bool loop = (argc >= 2) ? (args[1].toNumber() != 0.0) : false;
                        bool ok = GrassAudio::sound_play(args[0].toString(), loop);
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "sound_stop") {
                        GrassAudio::sound_stop();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_pause") {
                        GrassAudio::sound_pause();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_resume") {
                        GrassAudio::sound_resume();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_volume" && argc >= 1) {
                        GrassAudio::sound_volume((int)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sound_is_playing") {
                        push(GrassValue::Num(GrassAudio::sound_is_playing() ? 1.0 : 0.0));
                    }
                    else if (fn == "audio_init") {
                        GrassAudio::init();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "__reg_func" && argc >= 2) {
                        func_name_to_ip[args[0].toString()] = (uint32_t)args[1].toNumber();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "attach_sound" && argc >= 5) {
                        string target_name = args[0].toString();
                        uint32_t target_ip = 0;
                        if (func_name_to_ip.count(target_name)) {
                            target_ip = func_name_to_ip[target_name];
                        } else {
                            target_ip = (uint32_t)args[0].toNumber();
                        }
                        if (target_ip != 0) {
                            attached_ip_sounds[target_ip] = { args[1].toString(), args[2].toNumber(), (int)args[3].toNumber(), args[4].toNumber() };
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "bind_synth" && argc >= 5) {
                        GrassGUI::bind_synth(args[0].toString(), args[1].toString(), args[2].toNumber(), (int)args[3].toNumber(), args[4].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "bind_sound" && argc >= 2) {
                        GrassGUI::bind_sound(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    // ── Многопоточность (!thread!) ──

    else {
        return false;
    }
    return true;
}

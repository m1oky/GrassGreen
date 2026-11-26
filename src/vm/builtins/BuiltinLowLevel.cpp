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

bool GrassVM::callBuiltinLowLevel(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args) {
                                        if (fn == "hw_ram_total" || fn == "ram_total") {
                        push(GrassValue::Num((double)GrassHardware::ram_total_mb()));
                    }
                    else if (fn == "hw_ram_used" || fn == "ram_used" || fn == "proc_mem") {
                        push(GrassValue::Num((double)GrassHardware::ram_used_mb()));
                    }
                    else if (fn == "hw_ram_free" || fn == "ram_free") {
                        push(GrassValue::Num((double)GrassHardware::ram_free_mb()));
                    }
                    else if (fn == "hw_ram_cached") {
                        push(GrassValue::Num((double)GrassHardware::ram_cached_mb()));
                    }
                    else if (fn == "hw_ram_speed") {
                        push(GrassValue::Num((double)GrassHardware::ram_speed_mhz()));
                    }
                    else if (fn == "hw_ram_temp") {
                        push(GrassValue::Num((double)GrassHardware::ram_temp()));
                    }
                    else if (fn == "hw_ram_model") {
                        push(GrassValue::Str(GrassHardware::ram_model()));
                    }
                    else if (fn == "hw_ram_form") {
                        push(GrassValue::Str(GrassHardware::ram_form()));
                    }
                    // ── CPU ──
                    else if (fn == "hw_cpu_vendor" || fn == "cpu_vendor") {
                        push(GrassValue::Str(GrassHardware::cpu_vendor()));
                    }
                    else if (fn == "hw_cpu_brand" || fn == "cpu_brand" || fn == "cpu_model") {
                        push(GrassValue::Str(GrassHardware::cpu_brand()));
                    }
                    else if (fn == "hw_cpu_cores" || fn == "cpu_cores") {
                        push(GrassValue::Num((double)GrassHardware::cpu_cores()));
                    }
                    else if (fn == "hw_cpu_threads" || fn == "hw_cpu_logical") {
                        push(GrassValue::Num((double)GrassHardware::cpu_threads()));
                    }
                    else if (fn == "hw_cpu_sockets") {
                        push(GrassValue::Num((double)GrassHardware::cpu_sockets()));
                    }
                    else if (fn == "hw_cpu_family" || fn == "cpu_family") {
                        push(GrassValue::Num((double)GrassHardware::cpu_family()));
                    }
                    else if (fn == "hw_cpu_freq" || fn == "cpu_freq") {
                        push(GrassValue::Num((double)GrassHardware::cpu_freq_mhz()));
                    }
                    else if (fn == "hw_cpu_base_freq") {
                        push(GrassValue::Num((double)GrassHardware::cpu_base_freq_mhz()));
                    }
                    else if (fn == "hw_cpu_usage") {
                        push(GrassValue::Num((double)GrassHardware::cpu_usage_percent()));
                    }
                    else if (fn == "hw_cpu_temp") {
                        push(GrassValue::Num((double)GrassHardware::cpu_temp()));
                    }
                    else if (fn == "hw_cpu_cache_l1") {
                        push(GrassValue::Num((double)GrassHardware::cpu_cache_l1_kb()));
                    }
                    else if (fn == "hw_cpu_cache_l2") {
                        push(GrassValue::Num((double)GrassHardware::cpu_cache_l2_kb()));
                    }
                    else if (fn == "hw_cpu_cache_l3") {
                        push(GrassValue::Num((double)GrassHardware::cpu_cache_l3_kb()));
                    }
                    else if (fn == "hw_cpu_virt") {
                        push(GrassValue::Str(GrassHardware::cpu_virt()));
                    }
                    else if (fn == "hw_uptime_sec") {
                        push(GrassValue::Num((double)GrassHardware::uptime_sec()));
                    }
                    else if (fn == "hw_uptime_str") {
                        push(GrassValue::Str(GrassHardware::uptime_str()));
                    }
                    // ── DISK ──
                    else if (fn == "hw_disk_size") {
                        push(GrassValue::Num((double)GrassHardware::disk_size_gb()));
                    }
                    else if (fn == "hw_disk_free") {
                        push(GrassValue::Num((double)GrassHardware::disk_free_gb()));
                    }
                    else if (fn == "hw_disk_format") {
                        push(GrassValue::Str(GrassHardware::disk_format()));
                    }
                    else if (fn == "hw_disk_model") {
                        push(GrassValue::Str(GrassHardware::disk_model()));
                    }
                    else if (fn == "hw_disk_type") {
                        push(GrassValue::Str(GrassHardware::disk_type()));
                    }
                    else if (fn == "hw_disk_temp") {
                        push(GrassValue::Num((double)GrassHardware::disk_temp()));
                    }
                    else if (fn == "hw_disk_read_speed") {
                        push(GrassValue::Num((double)GrassHardware::disk_read_speed()));
                    }
                    else if (fn == "hw_disk_write_speed") {
                        push(GrassValue::Num((double)GrassHardware::disk_write_speed()));
                    }
                    else if (fn == "hw_disk_resp_time") {
                        push(GrassValue::Num((double)GrassHardware::disk_resp_time()));
                    }
                    else if (fn == "hw_disk_usage" || fn == "hw_disk_active") {
                        push(GrassValue::Num((double)GrassHardware::disk_usage_percent()));
                    }
                    else if (fn == "hw_disk_is_system") {
                        push(GrassValue::Num((double)GrassHardware::disk_is_system()));
                    }
                    else if (fn == "hw_pagefile_total") {
                        push(GrassValue::Num((double)GrassHardware::pagefile_total_mb()));
                    }
                    else if (fn == "hw_pagefile_used") {
                        push(GrassValue::Num((double)GrassHardware::pagefile_used_mb()));
                    }
                    // ── NETWORK ──
                    else if (fn == "hw_net_adapter") {
                        push(GrassValue::Str(GrassHardware::net_adapter()));
                    }
                    else if (fn == "hw_net_type") {
                        push(GrassValue::Str(GrassHardware::net_type()));
                    }
                    else if (fn == "hw_net_ipv4") {
                        push(GrassValue::Str(GrassHardware::net_ipv4()));
                    }
                    else if (fn == "hw_net_ipv6") {
                        push(GrassValue::Str(GrassHardware::net_ipv6()));
                    }
                    else if (fn == "hw_net_recv_speed") {
                        push(GrassValue::Num((double)GrassHardware::net_recv_speed_kb()));
                    }
                    else if (fn == "hw_net_send_speed") {
                        push(GrassValue::Num((double)GrassHardware::net_send_speed_kb()));
                    }
                    else if (fn == "hw_net_recv_total") {
                        push(GrassValue::Num((double)GrassHardware::net_recv_total_mb()));
                    }
                    else if (fn == "hw_net_send_total") {
                        push(GrassValue::Num((double)GrassHardware::net_send_total_mb()));
                    }
                    // ── GPU ──
                    else if (fn == "hw_gpu_name") {
                        push(GrassValue::Str(GrassHardware::gpu_name()));
                    }
                    else if (fn == "hw_gpu_usage") {
                        push(GrassValue::Num((double)GrassHardware::gpu_usage_percent()));
                    }
                    else if (fn == "hw_gpu_temp") {
                        push(GrassValue::Num((double)GrassHardware::gpu_temp()));
                    }
                    else if (fn == "hw_gpu_vram_total") {
                        push(GrassValue::Num((double)GrassHardware::gpu_vram_total_mb()));
                    }
                    else if (fn == "hw_gpu_vram_used") {
                        push(GrassValue::Num((double)GrassHardware::gpu_vram_used_mb()));
                    }
                    else if (fn == "hw_gpu_vram_shared") {
                        push(GrassValue::Num((double)GrassHardware::gpu_vram_shared_mb()));
                    }
                    // ── MOTHERBOARD & BIOS ──
                    else if (fn == "hw_board_vendor" || fn == "board_vendor") {
                        push(GrassValue::Str(GrassHardware::board_vendor()));
                    }
                    else if (fn == "hw_board_model" || fn == "board_model") {
                        push(GrassValue::Str(GrassHardware::board_model()));
                    }
                    else if (fn == "hw_bios_vendor" || fn == "bios_vendor") {
                        push(GrassValue::Str(GrassHardware::bios_vendor()));
                    }
                    else if (fn == "hw_bios_date" || fn == "bios_date") {
                        push(GrassValue::Str(GrassHardware::bios_date()));
                    }
                    else if (fn == "hw_bios_version") {
                        push(GrassValue::Str(GrassHardware::bios_version()));
                    }
                    else if (fn == "hw_pci_count" || fn == "pci_count") {
                        push(GrassValue::Num((double)GrassHardware::pci_count()));
                    }
                    else if (fn == "hw_rdtsc" || fn == "rdtsc") {
                        push(GrassValue::Num((double)GrassHardware::rdtsc()));
                    }
                    else if (fn == "hw_reboot") {
                        GrassHardware::reboot();
                    }
                    else if (fn == "use_utf" || fn == "use_locale" || fn == "set_layout" || fn == "kbd_set_layout") {
                        if (argc >= 1) {
                            GrassValue arg = args[0];
                            if (arg.is_num()) {
                                int id = (int)arg.toNumber();
                                GrassKeyboard::set_layout(id);
                            } else {
                                string name = arg.toString();
                                GrassKeyboard::set_layout_by_name(name);
                            }
                        } else {
                            GrassKeyboard::set_layout(8);
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "get_layout" || fn == "kbd_get_layout") {
                        push(GrassValue::Str(GrassKeyboard::get_layout_name()));
                    }
                    else if ((fn == "len" || fn == "length" || fn == "size") && argc >= 1) {
                        if (args[0].is_array()) {
                            GrassArray* a = args[0].asArray();
                            push(GrassValue::Num(a ? (double)a->elements.size() : 0.0));
                        } else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            GrassArray* a = args[0].asPtr()->cell->asArray();
                            push(GrassValue::Num(a ? (double)a->elements.size() : 0.0));
                        } else {
                            push(GrassValue::Num((double)args[0].toString().size()));
                        }
                    }

                    // ===== БИБЛИОТЕКА !baremetal! =====
                    else if (fn == "clear" || fn == "cls") {
                        GrassBaremetal::clear();
                        for (const auto& line : g_protected_console_lines) {
                            if (!line.empty()) {
                                GrassIO::write_stdout(line.data(), line.size());
                                GrassIO::write_stdout("\n", 1);
                            }
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "clear_all" || fn == "cls_all") {
                        g_protected_console_lines.clear();
                        GrassBaremetal::clear();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "not_clear_on") {
                        g_not_clear_active = true;
                        GrassBaremetal::not_clear_on();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "not_clear_off") {
                        g_not_clear_active = false;
                        GrassBaremetal::not_clear_off();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "not_clear") {
                        if (argc >= 1) {
                            bool on = (args[0].toNumber() != 0.0);
                            g_not_clear_active = on;
                            if (on) GrassBaremetal::not_clear_on(); else GrassBaremetal::not_clear_off();
                        } else {
                            g_not_clear_active = true;
                            GrassBaremetal::not_clear_on();
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "color") {
                        if (argc >= 2) {
                            uint8_t fg = GrassBaremetal::parse_color(args[0].toString());
                            uint8_t bg = GrassBaremetal::parse_color(args[1].toString());
                            GrassBaremetal::set_color(fg, bg);
                        } else if (argc >= 1) {
                            string c = args[0].toString();
                            if (!c.empty() && c[0] == '#') {
                                GrassBaremetal::set_color_hex(c);
                            } else {
                                GrassBaremetal::set_color(GrassBaremetal::parse_color(c));
                            }
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_key") {
                        push(GrassValue::Str(GrassBaremetal::read_key()));
                    }
                    else if (fn == "draw_box" && argc >= 4) {
                        int bx = (int)args[0].toNumber();
                        int by = (int)args[1].toNumber();
                        int bw = (int)args[2].toNumber();
                        int bh = (int)args[3].toNumber();
                        string btitle = (argc >= 5) ? args[4].toString() : "";
                        int bfg = (argc >= 6) ? (int)GrassBaremetal::parse_color(args[5].toString()) : GrassBaremetal::COLOR_LIGHT_GREEN;
                        int bbg = (argc >= 7) ? (int)GrassBaremetal::parse_color(args[6].toString()) : GrassBaremetal::COLOR_BLACK;
                        GrassBaremetal::draw_box(bx, by, bw, bh, btitle, bfg, bbg);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "menu" && argc >= 2) {
                        string mtitle = args[0].toString();
                        vector<string> mopts;
                        if (args[1].is_array() && args[1].asArray()) {
                            for (const auto& it : args[1].asArray()->elements) mopts.push_back(it.toString());
                        } else {
                            mopts.push_back(args[1].toString());
                        }
                        int sel = GrassBaremetal::menu(mtitle, mopts);
                        push(GrassValue::Num((double)sel));
                    }
                    else if (fn == "progress_bar" && argc >= 4) {
                        int px = (int)args[0].toNumber();
                        int py = (int)args[1].toNumber();
                        int pw = (int)args[2].toNumber();
                        int ppct = (int)args[3].toNumber();
                        int pfg = (argc >= 5) ? (int)GrassBaremetal::parse_color(args[4].toString()) : GrassBaremetal::COLOR_LIGHT_GREEN;
                        int pbg = (argc >= 6) ? (int)GrassBaremetal::parse_color(args[5].toString()) : GrassBaremetal::COLOR_BLACK;
                        GrassBaremetal::progress_bar(px, py, pw, ppct, pfg, pbg);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "reboot") {
                        GrassBaremetal::reboot();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "poweroff") {
                        GrassBaremetal::poweroff();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "virtual_struct") {
                        GrassBaremetal::vfs_set_has_struct(true);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "physical_struct" && argc >= 1) {
                        bool ok = GrassBaremetal::vfs_physical_struct(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "ls") {
                        string flags = (argc >= 1) ? args[0].toString() : "";
                        string listing = GrassBaremetal::vfs_ls(flags);
                        cout << listing << "\n";
                        push(GrassValue::Str(listing));
                    }
                    else if (fn == "pwd") {
                        string p = GrassBaremetal::vfs_pwd();
                        cout << p << "\n";
                        push(GrassValue::Str(p));
                    }
                    else if (fn == "cd") {
                        string p = (argc >= 1) ? args[0].toString() : "/";
                        bool ok = GrassBaremetal::vfs_cd(p);
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "copy" && argc >= 2) {
                        bool ok = GrassBaremetal::vfs_copy(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "move" && argc >= 2) {
                        bool ok = GrassBaremetal::vfs_move(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "delete" && argc >= 1) {
                        bool ok = GrassBaremetal::vfs_delete(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "cr_folder" && argc >= 1) {
                        bool ok = GrassBaremetal::vfs_cr_folder(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "cr_file" && argc >= 1) {
                        string cnt = (argc >= 2) ? args[1].toString() : "";
                        bool ok = GrassBaremetal::vfs_cr_file(args[0].toString(), cnt);
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "not_show" && argc >= 1) {
                        bool ok = GrassBaremetal::vfs_not_show(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "specSign_show" && argc >= 1) {
                        GrassBaremetal::vfs_specSign_show(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "bm_exec" && argc >= 1) {
                        bool ok = GrassBaremetal::bm_exec(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "cat" || fn == "read_file") {
                        string p = (argc >= 1) ? args[0].toString() : "";
                        string res = GrassBaremetal::cat(p);
                        cout << res << "\n";
                        push(GrassValue::Str(res));
                    }
                    else if (fn == "gui" || fn == "vd" || fn == "vd_desktop") {
                        GrassBaremetal::vd_desktop();
                        push(GrassValue::Num(1.0));
                    }
                    // ===== МЫШЬ (!baremetal!) =====
                    else if (fn == "mouse_init") {
                        GrassBaremetal::mouse_init();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mouse_x") {
                        push(GrassValue::Num(GrassBaremetal::mouse_x()));
                    }
                    else if (fn == "mouse_y") {
                        push(GrassValue::Num(GrassBaremetal::mouse_y()));
                    }
                    else if (fn == "mouse_btn") {
                        push(GrassValue::Num(GrassBaremetal::mouse_btn()));
                    }
                    else if (fn == "mouse_down") {
                        bool down = false;
                        if (argc >= 1) {
                            if (args[0].is_str()) {
                                down = GrassBaremetal::mouse_down(args[0].toString());
                            } else {
                                down = GrassBaremetal::mouse_down((int)args[0].toNumber());
                            }
                        } else {
                            down = GrassBaremetal::mouse_down(1);
                        }
                        push(GrassValue::Num(down ? 1.0 : 0.0));
                    }
                    else if (fn == "mouse_poll") {
                        GrassBaremetal::mouse_poll();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mouse_cursor") {
                        bool vis = (argc >= 1) ? (args[0].toNumber() != 0.0) : true;
                        GrassBaremetal::mouse_cursor(vis);
                        push(GrassValue::Num(1.0));
                    }

                    else if ((fn == "discor" || fn == "sqrt") && argc >= 1) push(GrassValue::Num(FastMath::discor(args[0].toNumber())));
                    else if (fn == "sin" && argc >= 1) push(GrassValue::Num(FastMath::sin(args[0].toNumber())));
                    else if (fn == "cos" && argc >= 1) push(GrassValue::Num(FastMath::cos(args[0].toNumber())));
                    else if (fn == "abs" && argc >= 1) push(GrassValue::Num(FastMath::abs(args[0].toNumber())));
                    else if (fn == "round" && argc >= 1) push(GrassValue::Num(FastMath::round(args[0].toNumber())));
                    else if (fn == "floor" && argc >= 1) push(GrassValue::Num(std::floor(args[0].toNumber())));
                    else if (fn == "ceil" && argc >= 1) push(GrassValue::Num(std::ceil(args[0].toNumber())));
                    else if (fn == "pow" && argc >= 2) push(GrassValue::Num(std::pow(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "clamp" && argc >= 3) push(GrassValue::Num(std::clamp(args[0].toNumber(), args[1].toNumber(), args[2].toNumber())));
                    else if (fn == "min" && argc >= 2) push(GrassValue::Num(FastMath::min(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "max" && argc >= 2) push(GrassValue::Num(FastMath::max(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "rand" && argc >= 2) push(GrassValue::Num(FastMath::rand((int)args[0].toNumber(), (int)args[1].toNumber())));
                    else if (fn == "lerp" && argc >= 3) push(GrassValue::Num(FastMath::lerp(args[0].toNumber(), args[1].toNumber(), args[2].toNumber())));

                    // !CoreMath!
                    else if (fn == "qb_init") push(GrassValue::Str(CoreMath::qb_init(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "h")       push(GrassValue::Str(CoreMath::h(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "x")       push(GrassValue::Str(CoreMath::x(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "z")       push(GrassValue::Str(CoreMath::z(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "cnot" && argc >= 2) push(GrassValue::Str(CoreMath::cnot(args[0].toString(), args[1].toString())));
                    else if (fn == "q_rand")  push(GrassValue::Num((double)CoreMath::q_rand(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "q_measure") push(GrassValue::Num((double)CoreMath::q_measure(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "pi")      push(GrassValue::Str(CoreMath::pi()));
                    else if (fn == "gcd" && argc >= 2)   push(GrassValue::Num((double)CoreMath::gcd((long long)args[0].toNumber(), (long long)args[1].toNumber())));
                    else if (fn == "pow_e" && argc >= 2) push(GrassValue::Str(CoreMath::pow_e(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "sha256" && argc >= 1) push(GrassValue::Str(CoreMath::sha256(args[0].toString())));
                    else if (fn == "pow_mod" && argc >= 3) push(GrassValue::Num((double)CoreMath::pow_mod((uint64_t)args[0].toNumber(), (uint64_t)args[1].toNumber(), (uint64_t)args[2].toNumber())));
                    else if (fn == "crypto_rand") {
                        if (argc >= 2) {
                            int64_t min_v = (int64_t)args[0].toNumber();
                            int64_t max_v = (int64_t)args[1].toNumber();
                            if (max_v < min_v) std::swap(min_v, max_v);
                            uint64_t range = (uint64_t)(max_v - min_v + 1);
                            std::vector<uint8_t> b = CoreMath::crypto_rand(8);
                            uint64_t val = 0;
                            for (int i = 0; i < 8; ++i) val = (val << 8) | b[i];
                            int64_t res = range > 0 ? (min_v + (int64_t)(val % range)) : min_v;
                            push(GrassValue::Num((double)res));
                        } else {
                            size_t count = argc >= 1 ? (size_t)args[0].toNumber() : 32;
                            std::vector<uint8_t> bytes = CoreMath::crypto_rand(count);
                            GrassArray* arr = new GrassArray();
                            arr->elements.resize(bytes.size());
                            for (size_t i = 0; i < bytes.size(); ++i) {
                                arr->elements[i] = GrassValue::Num((double)bytes[i]);
                            }
                            heap_arrays.push_back(arr);
                            push(GrassValue::Array(arr));
                        }
                    }


                    else if (fn == "mmio_read8" && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::read8((uint64_t)args[0].toNumber())));
                    }
                    else if (fn == "mmio_read16" && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::read16((uint64_t)args[0].toNumber())));
                    }
                    else if (fn == "mmio_read32" && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::read32((uint64_t)args[0].toNumber())));
                    }
                    else if (fn == "mmio_read64" && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::read64((uint64_t)args[0].toNumber())));
                    }
                    else if (fn == "mmio_write8" && argc >= 2) {
                        GrassMMIO::write8((uint64_t)args[0].toNumber(), (uint8_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mmio_write16" && argc >= 2) {
                        GrassMMIO::write16((uint64_t)args[0].toNumber(), (uint16_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mmio_write32" && argc >= 2) {
                        GrassMMIO::write32((uint64_t)args[0].toNumber(), (uint32_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mmio_write64" && argc >= 2) {
                        GrassMMIO::write64((uint64_t)args[0].toNumber(), (uint64_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "io_in8" || fn == "inb") && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::in8((uint16_t)args[0].toNumber())));
                    }
                    else if ((fn == "io_in16" || fn == "inw") && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::in16((uint16_t)args[0].toNumber())));
                    }
                    else if ((fn == "io_in32" || fn == "inl") && argc >= 1) {
                        push(GrassValue::Num((double)GrassMMIO::in32((uint16_t)args[0].toNumber())));
                    }
                    else if ((fn == "io_out8" || fn == "outb") && argc >= 2) {
                        GrassMMIO::out8((uint16_t)args[0].toNumber(), (uint8_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "io_out16" || fn == "outw") && argc >= 2) {
                        GrassMMIO::out16((uint16_t)args[0].toNumber(), (uint16_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "io_out32" || fn == "outl") && argc >= 2) {
                        GrassMMIO::out32((uint16_t)args[0].toNumber(), (uint32_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "memory_barrier") {
                        GrassMMIO::memory_barrier();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_barrier") {
                        GrassMMIO::read_barrier();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "write_barrier") {
                        GrassMMIO::write_barrier();
                        push(GrassValue::Num(1.0));
                    }
                    // ── БИБЛИОТЕКА !asm! ──
                    else if (fn == "cli") {
                        GrassASM::cli();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "sti") {
                        GrassASM::sti();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "hlt") {
                        GrassASM::hlt();
                        push(GrassValue::Num(1.0));
                    }
                    else if ((fn == "in" || fn == "in8") && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::in8((uint16_t)args[0].toNumber())));
                    }
                    else if (fn == "in16" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::in16((uint16_t)args[0].toNumber())));
                    }
                    else if (fn == "in32" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::in32((uint16_t)args[0].toNumber())));
                    }
                    else if ((fn == "out" || fn == "out8") && argc >= 2) {
                        GrassASM::out8((uint16_t)args[0].toNumber(), (uint8_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "out16" && argc >= 2) {
                        GrassASM::out16((uint16_t)args[0].toNumber(), (uint16_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "out32" && argc >= 2) {
                        GrassASM::out32((uint16_t)args[0].toNumber(), (uint32_t)args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_cr0") {
                        push(GrassValue::Num((double)GrassASM::read_cr0()));
                    }
                    else if (fn == "write_cr0" && argc >= 1) {
                        GrassASM::write_cr0((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_cr2") {
                        push(GrassValue::Num((double)GrassASM::read_cr2()));
                    }
                    else if (fn == "write_cr2" && argc >= 1) {
                        GrassASM::write_cr2((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_cr3") {
                        push(GrassValue::Num((double)GrassASM::read_cr3()));
                    }
                    else if (fn == "write_cr3" && argc >= 1) {
                        GrassASM::write_cr3((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_cr4") {
                        push(GrassValue::Num((double)GrassASM::read_cr4()));
                    }
                    else if (fn == "write_cr4" && argc >= 1) {
                        GrassASM::write_cr4((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read_rflags") {
                        push(GrassValue::Num((double)GrassASM::read_rflags()));
                    }
                    else if (fn == "write_rflags" && argc >= 1) {
                        GrassASM::write_rflags((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "invlpg" && argc >= 1) {
                        GrassASM::invlpg((uint64_t)args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "rdtsc") {
                        push(GrassValue::Num((double)GrassASM::rdtsc()));
                    }
                    else if (fn == "pause") {
                        GrassASM::pause();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "nop") {
                        GrassASM::nop();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "cpuid_eax" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::cpuid_eax((uint32_t)args[0].toNumber())));
                    }
                    else if (fn == "cpuid_ebx" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::cpuid_ebx((uint32_t)args[0].toNumber())));
                    }
                    else if (fn == "cpuid_ecx" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::cpuid_ecx((uint32_t)args[0].toNumber())));
                    }
                    else if (fn == "cpuid_edx" && argc >= 1) {
                        push(GrassValue::Num((double)GrassASM::cpuid_edx((uint32_t)args[0].toNumber())));
                    }
                    else if (fn == "asm_run" && argc >= 1) {
                        push(GrassASM::run(args[0].toString()));
                    }
                    else if (fn == "asm_bytes" && argc >= 1) {
                        push(GrassASM::run_hex(args[0].toString()));
                    }
                    else if (fn == "asm_hex" && argc >= 1) {
                        push(GrassASM::run_hex(args[0].toString()));
                    }
                    else if (fn == "asm_machine_hex") {
                        push(GrassValue::Str(GrassASM::get_last_machine_hex()));
                    }
                    // ── Библиотека контроллера клавиатуры (!keyboard!) ──
                    else if (fn == "kbd_has_key" || fn == "kbd_available") {
                        push(GrassValue::Num(GrassKeyboard::has_key() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_read_scancode") {
                        push(GrassValue::Num((double)GrassKeyboard::read_scancode()));
                    }
                    else if (fn == "kbd_get_scancode") {
                        push(GrassValue::Num((double)GrassKeyboard::get_scancode()));
                    }
                    else if ((fn == "kbd_scancode_to_char" || fn == "kbd_scancode_to_ascii") && argc >= 1) {
                        push(GrassValue::Str(GrassKeyboard::scancode_to_char((uint8_t)args[0].toNumber())));
                    }
                    else if (fn == "kbd_scancode_name" && argc >= 1) {
                        push(GrassValue::Str(GrassKeyboard::scancode_name((uint8_t)args[0].toNumber())));
                    }
                    else if (fn == "kbd_getc") {
                        push(GrassValue::Str(GrassKeyboard::getc()));
                    }
                    else if (fn == "kbd_poll") {
                        push(GrassValue::Str(GrassKeyboard::poll()));
                    }
                    else if (fn == "kbd_read_line") {
                        push(GrassValue::Str(GrassKeyboard::read_line()));
                    }
                    else if (fn == "kbd_is_shift") {
                        push(GrassValue::Num(GrassKeyboard::is_shift() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_is_caps") {
                        push(GrassValue::Num(GrassKeyboard::is_caps() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_is_ctrl") {
                        push(GrassValue::Num(GrassKeyboard::is_ctrl() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_is_alt") {
                        push(GrassValue::Num(GrassKeyboard::is_alt() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_is_key_pressed" && argc >= 1) {
                        push(GrassValue::Num(GrassKeyboard::is_key_pressed((uint8_t)args[0].toNumber()) ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_leds" && argc >= 3) {
                        GrassKeyboard::set_leds(args[0].toNumber() != 0, args[1].toNumber() != 0, args[2].toNumber() != 0);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "kbd_reset") {
                        GrassKeyboard::reset();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "kbd_self_test") {
                        push(GrassValue::Num(GrassKeyboard::self_test() ? 1.0 : 0.0));
                    }
                    else if (fn == "kbd_wait_ack") {
                        push(GrassValue::Num(GrassKeyboard::wait_ack() ? 1.0 : 0.0));
                    }

    else {
        return false;
    }
    return true;
}

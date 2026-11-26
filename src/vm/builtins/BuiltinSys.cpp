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
namespace fs = std::filesystem;

bool GrassVM::callBuiltinSys(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args) {
                    // ===== БИБЛИОТЕКА !time! =====
                    if (fn == "start_time") { GrassTime::start_time(); push(GrassValue::Num(1.0)); }
                    else if (fn == "stop_time") { push(GrassValue::Num(GrassTime::stop_time())); }
                    else if (fn == "conclus_time") { push(GrassValue::Str(GrassTime::conclus_time())); }
                    else if (fn == "clock_ms" || fn == "now_ms") { push(GrassValue::Num(GrassTime::now_ms())); }
                    else if (fn == "time") { push(GrassValue::Num(GrassTime::timestamp())); }
                    else if (fn == "get_date") { push(GrassValue::Str(GrassTime::get_date())); }
                    else if (fn == "get_time_str") { push(GrassValue::Str(GrassTime::get_time_str())); }


                    // ===== РАСШИРЕННАЯ БИБЛИОТЕКА !tg! =====
                    else if (fn == "tg_init" && argc >= 1) {
                        GrassTg::init(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "tg_poll") {
                        int timeout_val = (argc >= 1) ? (int)std::stod(args[0].toString()) : 2;
                        push(GrassValue::Str(GrassTg::poll(timeout_val)));
                    }
                    else if (fn == "tg_get_user") {
                        push(GrassValue::Str(GrassTg::get_user()));
                    }
                    else if (fn == "tg_get_chat_id") {
                        push(GrassValue::Str(GrassTg::get_chat_id()));
                    }
                    else if (fn == "tg_get_msg_id") {
                        push(GrassValue::Str(GrassTg::get_msg_id()));
                    }
                    else if (fn == "tg_get_callback") {
                        push(GrassValue::Str(GrassTg::get_callback()));
                    }
                    else if (fn == "tg_send" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::send(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_reply" && argc >= 1) {
                        push(GrassValue::Num(GrassTg::reply(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_btn" && argc >= 3) {
                        push(GrassValue::Num(GrassTg::send_btn(args[0].toString(), args[1].toString(), args[2].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_inline" && argc >= 3) {
                        push(GrassValue::Num(GrassTg::send_inline(args[0].toString(), args[1].toString(), args[2].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_webapp" && argc >= 4) {
                        push(GrassValue::Num(GrassTg::send_webapp(args[0].toString(), args[1].toString(), args[2].toString(), args[3].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_photo" && argc >= 3) {
                        push(GrassValue::Num(GrassTg::send_photo(args[0].toString(), args[1].toString(), args[2].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_send_dice" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::send_dice(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_edit" && argc >= 3) {
                        push(GrassValue::Num(GrassTg::edit(args[0].toString(), args[1].toString(), args[2].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "tg_del" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::del(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "telegram_init" && argc >= 1) {
                        GrassTg::init(args[0].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "send_message" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::send(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "send_photo" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::send_photo(args[0].toString(), args[1].toString(), argc >= 3 ? args[2].toString() : "") ? 1.0 : 0.0));
                    }
                    else if (fn == "send_document" && argc >= 2) {
                        push(GrassValue::Num(GrassTg::send(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }

                    // ===== БИБЛИОТЕКА !fsys! =====
                    else if (fn == "fs_mkdir" && argc >= 1) {
                        push(GrassValue::Num(GrassFsys::mkdir(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_is_dir" && argc >= 1) {
                        push(GrassValue::Num(GrassFsys::is_dir(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_size" && argc >= 1) {
                        push(GrassValue::Num((double)GrassFsys::size(args[0].toString())));
                    }
                    else if (fn == "fs_copy" && argc >= 2) {
                        push(GrassValue::Num(GrassFsys::copy(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_list" && argc >= 1) {
                        push(GrassValue::Str(GrassFsys::list(args[0].toString())));
                    }
                    else if (fn == "fs_read" && argc >= 1) {
                        push(GrassValue::Str(GrassFsys::read(args[0].toString())));
                    }
                    else if (fn == "fs_write" && argc >= 2) {
                        push(GrassValue::Num(GrassFsys::write(args[0].toString(), args[1].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_exists" && argc >= 1) {
                        push(GrassValue::Num(GrassFsys::exists(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "fs_remove" && argc >= 1) {
                        push(GrassValue::Num(GrassFsys::remove(args[0].toString()) ? 1.0 : 0.0));
                    }

                    // ===== БИБЛИОТЕКА !proc! =====
                    else if ((fn == "proc_find" || fn == "find") && argc >= 1) {
                        push(GrassValue::Num((double)GrassProc::find(args[0].toString())));
                    }
                    else if ((fn == "proc_kill" || fn == "kill") && argc >= 1) {
                        push(GrassValue::Num(GrassProc::kill((int64_t)args[0].toNumber()) ? 1.0 : 0.0));
                    }
                    else if ((fn == "proc_mem" || fn == "mem") && argc >= 1) {
                        push(GrassValue::Num((double)GrassProc::mem((int64_t)args[0].toNumber())));
                    }
                    else if (fn == "proc_exists" || (fn == "exists" && argc >= 1 && !args[0].is_str())) {
                        push(GrassValue::Num(GrassProc::exists((int64_t)args[0].toNumber()) ? 1.0 : 0.0));
                    }
                    else if (fn == "proc_list" || fn == "list") {
                        push(GrassValue::Str(GrassProc::list()));
                    }

                    // ===== БИБЛИОТЕКА !sys! =====
                    else if (fn == "clip_set" && argc >= 1) {
                        bool ok = GrassSys::clip_set(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "clip_get") {
                        push(GrassValue::Str(GrassSys::clip_get()));
                    }
                    else if (fn == "screen_w") {
                        push(GrassValue::Num((double)GrassSys::screen_w()));
                    }
                    else if (fn == "screen_h") {
                        push(GrassValue::Num((double)GrassSys::screen_h()));
                    }
                    else if (fn == "pc_name") {
                        push(GrassValue::Str(GrassSys::pc_name()));
                    }
                    else if (fn == "user_name") {
                        push(GrassValue::Str(GrassSys::user_name()));
                    }
                    else if (fn == "cpu_cores") {
                        push(GrassValue::Num((double)GrassSys::cpu_cores()));
                    }
                    else if (fn == "ram_total_mb") {
                        push(GrassValue::Num(GrassSys::ram_total_mb()));
                    }
                    else if (fn == "ram_free_mb") {
                        push(GrassValue::Num(GrassSys::ram_free_mb()));
                    }
                    else if (fn == "get_env" && argc >= 1) {
                        push(GrassValue::Str(GrassSys::get_env(args[0].toString())));
                    }
                    else if (fn == "uptime_sec") {
                        push(GrassValue::Num(GrassSys::uptime_sec()));
                    }

                    // ===== БИБЛИОТЕКА !json! =====
                    else if (fn == "json_get" && argc >= 2) {
                        push(GrassValue::Str(GrassJson::get(args[0].toString(), args[1].toString())));
                    }
                    else if (fn == "json_set" && argc >= 3) {
                        push(GrassValue::Str(GrassJson::set(args[0].toString(), args[1].toString(), args[2].toString())));
                    }
                    else if (fn == "json_valid" && argc >= 1) {
                        push(GrassValue::Num(GrassJson::is_valid(args[0].toString()) ? 1.0 : 0.0));
                    }
                    else if (fn == "json_minify" && argc >= 1) {
                        push(GrassValue::Str(GrassJson::minify(args[0].toString())));
                    }
                    else if (fn == "json_clear") {
                        GrassJson::builder_clear();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "json_add" && argc >= 2) {
                        GrassJson::builder_add_str(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "json_add_num" && argc >= 2) {
                        GrassJson::builder_add_num(args[0].toString(), args[1].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "json_build") {
                        push(GrassValue::Str(GrassJson::builder_stringify()));
                    }
                    else if (fn == "msgbox" && argc >= 1) {
                        string title = (argc >= 2) ? args[0].toString() : "GrassGreen";
                        string msg   = (argc >= 2) ? args[1].toString() : args[0].toString();
                        MessageBoxA(NULL, msg.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "system" && argc >= 1) {
                        int r = GrassSys::system_exec(args[0].toString());
                        push(GrassValue::Num((double)r));
                    }
                    // !random!

                    else if (fn == "creat" && argc >= 1) { ofstream f(args[0].toString()); f.close(); push(GrassValue::Num(1.0)); }
                    else if (fn == "redact" && argc >= 2) { ofstream f(args[0].toString()); f << args[1].toString(); f.close(); push(GrassValue::Num(1.0)); }
                    else if (fn == "read" && argc >= 1) {
                        ifstream f(args[0].toString()); stringstream ss;
                        if (f.is_open()) ss << f.rdbuf();
                        push(GrassValue::Str(ss.str()));
                    }
                    else if (fn == "exists" && argc >= 1) {
                        ifstream f(args[0].toString());
                        push(GrassValue::Num(f.is_open() ? 1.0 : 0.0));
                    }
                    else if (fn == "remove" && argc >= 1) {
                        bool ok = fs::remove(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if ((fn == "open" || fn == "run") && argc >= 1) {
                        string target = args[0].toString();
                        string lower = target;
                        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        bool is_dangerous = (lower.find("powershell") != string::npos ||
                                             lower.find("cmd.exe") != string::npos ||
                                             lower.find(".bat") != string::npos ||
                                             lower.find(".cmd") != string::npos ||
                                             lower.find(".vbs") != string::npos ||
                                             lower.find(".ps1") != string::npos ||
                                             lower.find(".scr") != string::npos);
                        if (!is_dangerous) {
                            ShellExecuteA(NULL, "open", target.c_str(), NULL, NULL, SW_SHOWNORMAL);
                            push(GrassValue::Num(1.0));
                        } else {
                            cerr << "[GrassSandbox Guard]: Запуск системных командных процессоров и скриптов заблокирован!\n";
                            push(GrassValue::Num(0.0));
                        }
                    }

                    // ===== БИБЛИОТЕКА !GrassNet! =====
                    else if (fn == "connect" && argc >= 1) {
                        int port = (argc >= 2) ? (int)args[1].toNumber() : 80;
                        bool ok = GrassNet::connect_host(args[0].toString(), port);
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "send" && argc >= 1) {
                        string method = (argc >= 2) ? args[1].toString() : "GET";
                        string body = (argc >= 3) ? args[2].toString() : "";
                        push(GrassValue::Str(GrassNet::send_request(args[0].toString(), method, body)));
                    }
                    else if (fn == "net_set_agent" && argc >= 2) {
                        GrassNet::set_agent(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "API" && argc >= 1) {
                        GrassNet::set_api(args[0].toString());
                        push(GrassValue::Str(args[0].toString()));
                    }
                    else if (fn == "create_serv" && argc >= 4) {
                        bool ok = GrassNet::create_server(
                            args[0].toString(),
                            (int)args[1].toNumber(),
                            args[2].toString(),
                            args[3].toString()
                        );
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "accept") {
                        push(GrassValue::Str(GrassNet::accept_request()));
                    }
                    else if (fn == "ping" && argc >= 1) {
                        int port = (argc >= 2) ? (int)args[1].toNumber() : 80;
                        push(GrassValue::Num(GrassNet::ping(args[0].toString(), port)));
                    }
                    else if (fn == "resolve_ip" && argc >= 1) {
                        push(GrassValue::Str(GrassNet::resolve_ip(args[0].toString())));
                    }
                    else if (fn == "local_ip") {
                        push(GrassValue::Str(GrassNet::get_local_ip()));
                    }
                    else if (fn == "download" && argc >= 2) {
                        bool ok = GrassNet::download_file(args[0].toString(), args[1].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "net_status") {
                        push(GrassValue::Num((double)GrassNet::get_last_status()));
                    }
                    else if (fn == "net_close") {
                        GrassNet::close_all();
                        push(GrassValue::Num(1.0));
                    }

                    // !database! (GSE)
                    else if (fn == "db_open" && argc >= 1) {
                        bool ok = GrassDB::open(args[0].toString());
                        syncDBToVars();
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "db_save") {
                        if (argc >= 2) {
                            string k = args[0].toString(), v = args[1].toString();
                            GrassDB::set(k, v); GrassDB::save();
                            vars[k] = GrassValue::Str(v);
                            push(GrassValue::Num(1.0));
                        } else if (argc == 1) {
                            push(GrassValue::Num(GrassDB::save(args[0].toString()) ? 1.0 : 0.0));
                        } else {
                            push(GrassValue::Num(GrassDB::save() ? 1.0 : 0.0));
                        }
                    }
                    else if (fn == "db_set" && argc >= 2) {
                        string k = args[0].toString(), v = args[1].toString();
                        GrassDB::set(k, v);
                        vars[k] = GrassValue::Str(GrassDB::get(k));
                        push(GrassValue::Str(GrassDB::get(k)));
                    }
                    else if (fn == "db_get" && argc >= 1) {
                        string k = args[0].toString();
                        string def_v = (argc >= 2) ? args[1].toString() : "";
                        if (GrassDB::has(k)) push(GrassValue::Str(GrassDB::get(k, def_v)));
                        else if (vars.count(k)) push(vars[k]);
                        else push(GrassValue::Str(def_v));
                    }
                    else if ((fn == "db_del" || fn == "db_remove") && argc >= 1) {
                        string k = args[0].toString();
                        vars.erase(k);
                        push(GrassValue::Num(GrassDB::remove(k) ? 1.0 : 0.0));
                    }
                    else if ((fn == "db_has" || fn == "db_exists") && argc >= 1) {
                        string k = args[0].toString();
                        push(GrassValue::Num((GrassDB::has(k) || vars.count(k) > 0) ? 1.0 : 0.0));
                    }
                    else if (fn == "db_inc" && argc >= 1) {
                        long long delta = (argc >= 2) ? (long long)args[1].toNumber() : 1;
                        long long res = GrassDB::add_int(args[0].toString(), delta);
                        vars[args[0].toString()] = GrassValue::Num((double)res);
                        push(GrassValue::Num((double)res));
                    }
                    else if (fn == "db_dec" && argc >= 1) {
                        long long delta = (argc >= 2) ? (long long)args[1].toNumber() : 1;
                        long long res = GrassDB::add_int(args[0].toString(), -delta);
                        vars[args[0].toString()] = GrassValue::Num((double)res);
                        push(GrassValue::Num((double)res));
                    }
                    else if (fn == "db_clear") { GrassDB::clear(); push(GrassValue::Num(1.0)); }
                    else if (fn == "db_autosave" && argc >= 1) {
                        GrassDB::exec_command("db_autosave(" + args[0].toString() + ")");
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "db_backup") {
                        string bpath = (argc >= 1) ? args[0].toString() : "storage.gdb.bak";
                        push(GrassValue::Num(GrassDB::backup(bpath) ? 1.0 : 0.0));
                    }
                    else if (fn == "db_export_json") {
                        string jpath = (argc >= 1) ? args[0].toString() : "storage.json";
                        push(GrassValue::Num(GrassDB::export_json(jpath) ? 1.0 : 0.0));
                    }
                    else if (fn == "db_hash" && argc >= 1) {
                        push(GrassValue::Str(GrassDB::hash_string(args[0].toString())));
                    }
                    else if (fn == "db_encrypt" && argc >= 1) {
                        string salt = (argc >= 2) ? args[1].toString() : "GrassGreenKey";
                        GrassDB::encrypt_key(args[0].toString(), salt);
                        vars[args[0].toString()] = GrassValue::Str(GrassDB::get(args[0].toString()));
                        push(GrassValue::Str(GrassDB::get(args[0].toString())));
                    }
                    else if (fn == "db_decrypt" && argc >= 1) {
                        string salt = (argc >= 2) ? args[1].toString() : "GrassGreenKey";
                        push(GrassValue::Str(GrassDB::decrypt_key(args[0].toString(), salt)));
                    }
                    else if (fn == "table") {
                        string tname = (argc >= 1) ? args[0].toString() : "default";
                        GrassDB::use_table(tname);
                        push(GrassValue::Str(tname));
                    }
                    else if (isGdbSchemaKey(fn)) {
                        GrassDB::init_default_schema();
                        if (argc >= 1) {
                            GrassDB::set(fn, args[0].toString());
                            string updated = GrassDB::get(fn);
                            vars[fn] = GrassValue::Str(updated);
                            push(GrassValue::Str(updated));
                        } else {
                            push(GrassValue::Str(GrassDB::get(fn)));
                        }
                    }

                    // !interface! & !GrassWeb!

                    else if (fn == "thread_join" && argc >= 1) {
                        int64_t tid = (int64_t)args[0].toNumber();
                        push(GrassThread::join_thread(tid));
                    }
                    else if (fn == "thread_detach" && argc >= 1) {
                        int64_t tid = (int64_t)args[0].toNumber();
                        push(GrassValue::Num(GrassThread::detach_thread(tid) ? 1.0 : 0.0));
                    }
                    else if (fn == "thread_id") {
                        push(GrassValue::Num((double)GrassThread::get_current_thread_id()));
                    }
                    else if (fn == "thread_sleep" && argc >= 1) {
                        uint32_t ms = (uint32_t)args[0].toNumber();
                        GrassThread::sleep_ms(ms);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "mutex_create") {
                        int64_t mid = GrassThread::mutex_create();
                        push(GrassValue::Num((double)mid));
                    }
                    else if (fn == "mutex_lock" && argc >= 1) {
                        int64_t mid = (int64_t)args[0].toNumber();
                        push(GrassValue::Num(GrassThread::mutex_lock(mid) ? 1.0 : 0.0));
                    }
                    else if (fn == "mutex_unlock" && argc >= 1) {
                        int64_t mid = (int64_t)args[0].toNumber();
                        push(GrassValue::Num(GrassThread::mutex_unlock(mid) ? 1.0 : 0.0));
                    }
                    else if (fn == "mutex_destroy" && argc >= 1) {
                        int64_t mid = (int64_t)args[0].toNumber();
                        push(GrassValue::Num(GrassThread::mutex_destroy(mid) ? 1.0 : 0.0));
                    }
                    // ── Аппаратный доступ к памяти: MMIO и Port I/O (!mmio!) ──

                    else if (fn == "fat_status") {
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "ticks") {
                        push(GrassValue::Num((double)GrassTime::now_ms()));
                    }
                    else if (fn == "threads") {
                        push(GrassValue::Num(1.0));
                    }

    else {
        return false;
    }
    return true;
}

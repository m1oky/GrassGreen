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

bool GrassVM::callBuiltinCore(const std::string& fn, uint8_t argc, std::vector<GrassValue>& args) {
                    if (fn == "gin" || fn == "input") {
                        if (argc >= 1) {
                            string prompt = args[0].toString();
                            GrassIO::write_stdout(prompt.data(), prompt.size());
                        }
                        char buf[1024];
                        size_t len = GrassIO::read_line(buf, sizeof(buf));
                        char* p = buf;
                        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
                        while (*p == ' ' || *p == '\t') p++;
                        char* space = strpbrk(p, " \t\r\n");
                        if (space) *space = '\0';
                        string val_str(p);
                        char* end_ptr = nullptr;
                        double num_val = strtod(val_str.c_str(), &end_ptr);
                        if (!val_str.empty() && end_ptr != val_str.c_str() && *end_ptr == '\0') {
                            push(GrassValue::Num(num_val));
                        } else {
                            push(GrassValue::Str(val_str));
                        }
                    }
                    else if (fn == "gin.getline" || fn == "input.getline" || fn == "getline" || fn == "ask") {
                        if (argc >= 1) {
                            string prompt = args[0].toString();
                            GrassIO::write_stdout(prompt.data(), prompt.size());
                        }
                        char buf[1024];
                        size_t len = GrassIO::read_line(buf, sizeof(buf));
                        char* p = buf;
                        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
                        string val_str(p);
                        while (!val_str.empty() && (val_str.back() == '\r' || val_str.back() == '\n')) val_str.pop_back();
                        char* end_ptr = nullptr;
                        double num_val = strtod(val_str.c_str(), &end_ptr);
                        if (!val_str.empty() && end_ptr != val_str.c_str() && *end_ptr == '\0') {
                            push(GrassValue::Num(num_val));
                        } else {
                            push(GrassValue::Str(val_str));
                        }
                    }

                    else if (fn == "__vec_create") {
                        GrassArray* arr = new GrassArray();
                        arr->is_vec = true;
                        arr->vec_capacity = 0;
                        heap_arrays.push_back(arr);
                        push(GrassValue::Array(arr));
                    }
                    else if (fn == "__vec_push" && argc >= 2) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        if (arr) {
                            if (arr->vec_capacity == 0) {
                                arr->vec_capacity = 8;
                                arr->elements.reserve(8);
                            } else if (arr->elements.size() >= arr->vec_capacity) {
                                arr->vec_capacity *= 2;
                                arr->elements.reserve(arr->vec_capacity);
                            }
                            GrassValue pushed_val = args[1];
                            if (pushed_val.is_struct() && pushed_val.asStruct() && pushed_val.asStruct()->def) {
                                GrassStructInstance* src = pushed_val.asStruct();
                                GrassStructInstance* clone = (GrassStructInstance*)g_vm_arena.alloc(sizeof(GrassStructInstance));
                                uint8_t* cbuf = (uint8_t*)g_vm_arena.alloc(src->def->total_size);
                                if (cbuf && src->data) memcpy(cbuf, src->data, src->def->total_size);
                                clone->def = src->def;
                                clone->data = cbuf;
                                pushed_val = GrassValue::Struct(clone);
                            }
                            arr->elements.push_back(pushed_val);
                        }
                        push(GrassValue::Nil());
                    }
                    else if (fn == "__vec_pop" && argc >= 1) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        if (arr && !arr->elements.empty()) {
                            arr->elements.pop_back();
                        }
                        push(GrassValue::Nil());
                    }
                    else if (fn == "__vec_size" && argc >= 1) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        push(GrassValue::Num(arr ? (double)arr->elements.size() : 0.0));
                    }
                    else if (fn == "__vec_capacity" && argc >= 1) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        size_t cap = 0;
                        if (arr) cap = arr->is_vec ? arr->vec_capacity : arr->elements.capacity();
                        push(GrassValue::Num((double)cap));
                    }
                    else if (fn == "__vec_empty" && argc >= 1) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        push(GrassValue::Bool(!arr || arr->elements.empty()));
                    }
                    else if (fn == "__vec_clear" && argc >= 1) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        if (arr) arr->elements.clear();
                        push(GrassValue::Nil());
                    }
                    else if (fn == "__vec_reserve" && argc >= 2) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        long long nc = (long long)args[1].toNumber();
                        if (arr && nc > 0 && (size_t)nc > arr->vec_capacity) {
                            arr->vec_capacity = (size_t)nc;
                            arr->elements.reserve(arr->vec_capacity);
                        }
                        push(GrassValue::Nil());
                    }
                    else if (fn == "__vec_data" && argc >= 1) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        GrassVMPtr* p = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
                        new (p) GrassVMPtr();
                        if (arr) {
                            p->vec = &arr->elements;
                            p->vec_idx = 0;
                            p->elem_size = 8;
                        }
                        push(GrassValue::Ptr(p));
                    }
                    else if (fn == "__vec_destroy" && argc >= 1) {
                        GrassArray* arr = nullptr;
                        if (args[0].is_array()) arr = args[0].asArray();
                        else if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->cell && args[0].asPtr()->cell->is_array()) {
                            arr = args[0].asPtr()->cell->asArray();
                        }
                        if (arr) {
                            arr->elements.clear();
                            arr->vec_capacity = 0;
                        }
                        push(GrassValue::Nil());
                    }
                    else if (fn == "upper" && argc >= 1) {
                        string s = args[0].toString();
                        transform(s.begin(), s.end(), s.begin(), ::toupper);
                        push(GrassValue::Str(s));
                    }
                    else if (fn == "lower" && argc >= 1) {
                        string s = args[0].toString();
                        transform(s.begin(), s.end(), s.begin(), ::tolower);
                        push(GrassValue::Str(s));
                    }
                    else if (fn == "substr" && argc >= 2) {
                        string s = args[0].toString();
                        size_t start = (size_t)max(0.0, args[1].toNumber());
                        if (start >= s.size()) push(GrassValue::Str(""));
                        else if (argc >= 3) push(GrassValue::Str(s.substr(start, (size_t)args[2].toNumber())));
                        else push(GrassValue::Str(s.substr(start)));
                    }
                    else if (fn == "replace" && argc >= 3) {
                        string s = args[0].toString();
                        string from = args[1].toString();
                        string to = args[2].toString();
                        if (!from.empty()) {
                            size_t start_pos = 0;
                            while ((start_pos = s.find(from, start_pos)) != string::npos) {
                                s.replace(start_pos, from.length(), to);
                                start_pos += to.length();
                            }
                        }
                        push(GrassValue::Str(s));
                    }
                    else if ((fn == "contains" || fn == "find") && argc >= 2) {
                        string s = args[0].toString();
                        string sub = args[1].toString();
                        push(GrassValue::Num(s.find(sub) != string::npos ? 1.0 : 0.0));
                    }
                    else if (fn == "trim" && argc >= 1) {
                        string s = args[0].toString();
                        size_t first = s.find_first_not_of(" \t\r\n");
                        if (first == string::npos) push(GrassValue::Str(""));
                        else push(GrassValue::Str(s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1)));
                    }
                    else if ((fn == "to_str" || fn == "to_string") && argc >= 1) {
                        push(GrassValue::Str(args[0].toString()));
                    }
                    else if ((fn == "to_num" || fn == "stoi" || fn == "stod") && argc >= 1) {
                        push(GrassValue::Num(args[0].toNumber()));
                    }
                    else if (fn == "typeof" && argc >= 1) {
                        push(GrassValue::Str(args[0].is_str() ? "string" : "number"));
                    }

                    else if (fn == "random") {
                        double min_v = 0.0, max_v = 100.0;
                        if (argc == 1) max_v = args[0].toNumber();
                        else if (argc >= 2) { min_v = args[0].toNumber(); max_v = args[1].toNumber(); }
                        push(GrassValue::Num(GrassRandom::get_random(min_v, max_v)));
                    }
                    else if (fn == "random_txt") {
                        string lang = (argc >= 1) ? args[0].toString() : "en";
                        int len = (argc >= 2) ? (int)args[1].toNumber() : 1;
                        push(GrassValue::Str(GrassRandom::get_random_txt(lang, len)));
                    }
                    else if (fn == "stop_random") { GrassRandom::stop_random(); push(GrassValue::Num(1.0)); }
                    else if (fn == "play_random") { GrassRandom::play_random(); push(GrassValue::Num(1.0)); }

                    // !memory!
                    else if (fn == "memory_usit") { push(GrassValue::Num(GrassMemory::usit())); }
                    else if (fn == "memory_separ") { GrassMemory::separ(); push(GrassValue::Num(1.0)); }
                    else if (fn == "memory_auto") { GrassMemory::set_auto(true); push(GrassValue::Num(1.0)); }
                    else if (fn == "memory_clean") { GrassMemory::clean(); push(GrassValue::Num(1.0)); }
                    else if (fn == "memory_free") { push(GrassValue::Str(GrassMemory::get_system_free())); }
                    else if (fn == "memory_alloc" && argc >= 1) {
                        double req = args[0].toNumber();
                        if (req <= 0.0 || req > 1073741824.0) {
                            push(GrassValue::Num(0.0));
                        } else {
                            int block_id = GrassMemory::alloc_block((size_t)req);
                            push(GrassValue::Num((double)block_id));
                        }
                    }
                    else if (fn == "alloc" && argc >= 1) {
                        double req = args[0].toNumber();
                        if (req <= 0.0 || req > 2147483647.0) {
                            push(GrassValue::Num(0.0));
                        } else {
                            size_t sz = (size_t)req;
                            uint8_t* raw = (uint8_t*)g_vm_arena.alloc(sz);
                            if (raw) memset(raw, 0, sz);
                            GrassVMPtr* p = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
                            new (p) GrassVMPtr();
                            p->raw = raw;
                            p->raw_size = raw ? sz : 0;
                            p->elem_size = 1;
                            p->is_scalar_u8 = true;
                            p->scalar_kind = 1;
                            push(raw ? GrassValue::Ptr(p) : GrassValue::Num(0.0));
                        }
                    }
                    else if (fn == "free") {
                        push(GrassValue::Num(0.0));
                    }
                    else if (fn == "realloc" && argc >= 2) {
                        double req = args[1].toNumber();
                        if (req <= 0.0 || req > 2147483647.0) {
                            push(GrassValue::Num(0.0));
                        } else {
                            size_t nsz = (size_t)req;
                            uint8_t* raw = (uint8_t*)g_vm_arena.alloc(nsz);
                            if (raw) {
                                memset(raw, 0, nsz);
                                if (args[0].is_ptr() && args[0].asPtr() && args[0].asPtr()->raw) {
                                    GrassVMPtr* op = args[0].asPtr();
                                    size_t copy_sz = std::min(nsz, op->raw_size);
                                    if (copy_sz > 0) memcpy(raw, op->raw, copy_sz);
                                }
                            }
                            GrassVMPtr* p = (GrassVMPtr*)g_vm_arena.alloc(sizeof(GrassVMPtr));
                            new (p) GrassVMPtr();
                            p->raw = raw;
                            p->raw_size = raw ? nsz : 0;
                            p->elem_size = 1;
                            p->is_scalar_u8 = true;
                            p->scalar_kind = 1;
                            push(raw ? GrassValue::Ptr(p) : GrassValue::Num(0.0));
                        }
                    }
                    else if (fn == "memory_free_block" && argc >= 1) {
                        bool ok = GrassMemory::free_block((int)args[0].toNumber());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "memory_limit" && argc >= 1) {
                        GrassMemory::set_limit(args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "memory_dump") {
                        vector<string> st_preview;
                        for (int i = (int)stack.size() - 1; i >= max(0, (int)stack.size() - 5); --i) {
                            st_preview.push_back(stack[i].toString());
                        }
                        vector<pair<string, string>> v_preview;
                        for (const auto& kv : vars) v_preview.push_back({kv.first, kv.second.toString()});
                        GrassMemory::dump(stack.size(), st_preview, v_preview);
                        push(GrassValue::Num(1.0));
                    }

                    // !FastMath!

    else {
        return false;
    }
    return true;
}

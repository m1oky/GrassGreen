#include "native/GrassObjRuntime.h"

namespace grass::backend {

    void inject_win64_rt_io(Win64RuntimeCtx& ctx) {
        // 1. _start (Entry Point Wrapper): calls main and exits via ExitProcess(eax) with zero CRT dependency
        if (ctx.defined_symbols.count("main") > 0 && ctx.defined_symbols.count("_start") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 4: call main
                0x89, 0xC1,                         // 9: mov ecx, eax
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 11: call ExitProcess
                0x48, 0x83, 0xC4, 0x28,             // 16: add rsp, 40
                0xC3                                // 20: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 5, "main", RelocType::Rel32, -4 },
                { SectionKind::Text, 12, "ExitProcess", RelocType::Rel32, -4 }
            };
            ctx.append_function("_start", code, rels);
        }

        // 2 & 3b. grass_alloc, alloc, memory_alloc: Win32 HeapAlloc with size <= 0 and integer-overflow bounds check
        if ((ctx.needed_symbols.count("grass_alloc") > 0 && ctx.defined_symbols.count("grass_alloc") == 0) ||
            (ctx.needed_symbols.count("alloc") > 0 && ctx.defined_symbols.count("alloc") == 0) ||
            (ctx.needed_symbols.count("memory_alloc") > 0 && ctx.defined_symbols.count("memory_alloc") == 0) ||
            ((ctx.needed_symbols.count("grass_realloc") > 0 || ctx.needed_symbols.count("realloc") > 0) &&
             ctx.defined_symbols.count("grass_alloc") == 0)) {
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); };
            emit({0x48, 0x85, 0xC9});                                     // test rcx, rcx
            emit({0x7E, 0x00}); size_t jle_p = code.size() - 1;           // jle .ret_zero
            emit({0x48, 0xB8, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00}); // mov rax, 0x00007FFFFFFF0000
            emit({0x48, 0x39, 0xC1});                                     // cmp rcx, rax
            emit({0x77, 0x00}); size_t ja_p = code.size() - 1;            // ja .ret_zero
            emit({0x48, 0x83, 0xEC, 0x28});                               // sub rsp, 40
            emit({0x49, 0x89, 0xC8});                                     // mov r8, rcx (size)
            size_t rel_heap = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetProcessHeap
            emit({0x48, 0x89, 0xC1});                                     // mov rcx, rax (hHeap)
            emit({0xBA, 0x08, 0x00, 0x00, 0x00});                         // mov edx, 8 (HEAP_ZERO_MEMORY)
            size_t rel_alloc = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call HeapAlloc
            emit({0x48, 0x83, 0xC4, 0x28});                               // add rsp, 40
            emit({0xC3});                                                 // ret
            size_t ret_zero = code.size();
            code[jle_p] = static_cast<uint8_t>(ret_zero - (jle_p + 1));
            code[ja_p]  = static_cast<uint8_t>(ret_zero - (ja_p + 1));
            emit({0x31, 0xC0, 0xC3});                                     // xor eax, eax; ret
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_heap), "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_alloc), "HeapAlloc", RelocType::Rel32, -4 }
            };
            if (ctx.needed_symbols.count("grass_alloc") > 0 && ctx.defined_symbols.count("grass_alloc") == 0) ctx.append_function("grass_alloc", code, rels);
            if (ctx.needed_symbols.count("alloc") > 0 && ctx.defined_symbols.count("alloc") == 0) ctx.append_function("alloc", code, rels);
            if (ctx.needed_symbols.count("memory_alloc") > 0 && ctx.defined_symbols.count("memory_alloc") == 0) ctx.append_function("memory_alloc", code, rels);
        }

        // 3. grass_free and free: Win32 HeapFree(GetProcessHeap(), 0, ptr)
        if ((ctx.needed_symbols.count("grass_free") > 0 && ctx.defined_symbols.count("grass_free") == 0) ||
            (ctx.needed_symbols.count("free") > 0 && ctx.defined_symbols.count("free") == 0)) {
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x1A,                         // 3: jz +26 (.ret at 31)
                0x48, 0x83, 0xEC, 0x28,             // 5: sub rsp, 40
                0x49, 0x89, 0xC8,                   // 9: mov r8, rcx (ptr)
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 12: call GetProcessHeap
                0x48, 0x89, 0xC1,                   // 17: mov rcx, rax (hHeap)
                0x31, 0xD2,                         // 20: xor edx, edx
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 22: call HeapFree
                0x48, 0x83, 0xC4, 0x28,             // 27: add rsp, 40
                0xC3                                // 31: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 13, "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, 23, "HeapFree", RelocType::Rel32, -4 }
            };
            if (ctx.needed_symbols.count("grass_free") > 0) ctx.append_function("grass_free", code, rels);
            if (ctx.needed_symbols.count("free") > 0) ctx.append_function("free", code, rels);
        }

        // 3c. memory_free_block: Win32 HeapFree(GetProcessHeap(), 0, ptr) returning 1/0
        if (ctx.needed_symbols.count("memory_free_block") > 0 && ctx.defined_symbols.count("memory_free_block") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x75, 0x03,                         // 3: jnz +3 (.do_free at 8)
                0x31, 0xC0,                         // 5: xor eax, eax
                0xC3,                               // 7: ret
                0x48, 0x83, 0xEC, 0x28,             // 8: sub rsp, 40
                0x49, 0x89, 0xC8,                   // 12: mov r8, rcx (ptr)
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 15: call GetProcessHeap
                0x48, 0x89, 0xC1,                   // 20: mov rcx, rax (hHeap)
                0x31, 0xD2,                         // 23: xor edx, edx
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 25: call HeapFree
                0x85, 0xC0,                         // 30: test eax, eax
                0x0F, 0x95, 0xC0,                   // 32: setnz al
                0x0F, 0xB6, 0xC0,                   // 35: movzx eax, al
                0x48, 0x83, 0xC4, 0x28,             // 38: add rsp, 40
                0xC3                                // 42: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 16, "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, 26, "HeapFree", RelocType::Rel32, -4 }
            };
            ctx.append_function("memory_free_block", code, rels);
        }

        // 4. grass_realloc and realloc: Win32 HeapReAlloc with size <= 0 / overflow validation
        if ((ctx.needed_symbols.count("grass_realloc") > 0 && ctx.defined_symbols.count("grass_realloc") == 0) ||
            (ctx.needed_symbols.count("realloc") > 0 && ctx.defined_symbols.count("realloc") == 0)) {
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); };
            emit({0x48, 0x85, 0xD2});                                     // test rdx, rdx (new_size)
            emit({0x7F, 0x03});                                           // jg +3
            emit({0x31, 0xC0, 0xC3});                                     // xor eax, eax; ret
            emit({0x48, 0xB8, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00}); // mov rax, 0x00007FFFFFFF0000
            emit({0x48, 0x39, 0xC2});                                     // cmp rdx, rax
            emit({0x76, 0x03});                                           // jbe +3
            emit({0x31, 0xC0, 0xC3});                                     // xor eax, eax; ret
            emit({0x48, 0x85, 0xC9});                                     // test rcx, rcx
            emit({0x75, 0x08});                                           // jnz +8 (.do_realloc)
            emit({0x48, 0x89, 0xD1});                                     // mov rcx, rdx
            size_t rel_alloc = code.size() + 1;
            emit({0xE9, 0x00, 0x00, 0x00, 0x00});                         // jmp grass_alloc
            emit({0x48, 0x83, 0xEC, 0x28});                               // sub rsp, 40
            emit({0x48, 0x89, 0x4C, 0x24, 0x30});                         // mov [rsp + 48], rcx (save lpMem)
            emit({0x48, 0x89, 0x54, 0x24, 0x38});                         // mov [rsp + 56], rdx (save dwBytes)
            size_t rel_heap = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetProcessHeap
            emit({0x48, 0x89, 0xC1});                                     // mov rcx, rax (hHeap)
            emit({0xBA, 0x08, 0x00, 0x00, 0x00});                         // mov edx, 8 (HEAP_ZERO_MEMORY)
            emit({0x4C, 0x8B, 0x44, 0x24, 0x30});                         // mov r8, [rsp + 48] (lpMem)
            emit({0x4C, 0x8B, 0x4C, 0x24, 0x38});                         // mov r9, [rsp + 56] (dwBytes)
            size_t rel_realloc = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call HeapReAlloc
            emit({0x48, 0x83, 0xC4, 0x28});                               // add rsp, 40
            emit({0xC3});                                                 // ret
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_alloc), "grass_alloc", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_heap), "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_realloc), "HeapReAlloc", RelocType::Rel32, -4 }
            };
            if (ctx.needed_symbols.count("grass_realloc") > 0) ctx.append_function("grass_realloc", code, rels);
            if (ctx.needed_symbols.count("realloc") > 0) ctx.append_function("realloc", code, rels);
        }

        // 5. memcpy: pure hardware x86_64 copy loop
        if (ctx.needed_symbols.count("memcpy") > 0 && ctx.defined_symbols.count("memcpy") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x89, 0xC8,                   // 0: mov rax, rcx
                0x4D, 0x85, 0xC0,                   // 3: test r8, r8
                0x74, 0x11,                         // 6: jz +17 (.done at 25)
                0x44, 0x8A, 0x0A,                   // 8: mov r9b, [rdx]
                0x44, 0x88, 0x09,                   // 11: mov [rcx], r9b
                0x48, 0xFF, 0xC2,                   // 14: inc rdx
                0x48, 0xFF, 0xC1,                   // 17: inc rcx
                0x49, 0xFF, 0xC8,                   // 20: dec r8
                0x75, 0xEF,                         // 23: jnz -17 (to 8)
                0xC3                                // 25: ret
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("memcpy", code, rels);
        }

        // 6. memset: pure hardware x86_64 set loop
        if (ctx.needed_symbols.count("memset") > 0 && ctx.defined_symbols.count("memset") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x89, 0xC8,                   // 0: mov rax, rcx
                0x4D, 0x85, 0xC0,                   // 3: test r8, r8
                0x74, 0x0A,                         // 6: jz +10 (.done at 18)
                0x88, 0x11,                         // 8: mov [rcx], dl
                0x48, 0xFF, 0xC1,                   // 10: inc rcx
                0x49, 0xFF, 0xC8,                   // 13: dec r8
                0x75, 0xF6,                         // 16: jnz -10 (to 8)
                0xC3                                // 18: ret
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("memset", code, rels);
        }

        // 6b. gout_flush: flush 64KB stdout buffer in .bss via Win32 WriteFile
        if (ctx.needed_symbols.count("gout_flush") > 0 && ctx.defined_symbols.count("gout_flush") == 0) {
            ctx.add_bss_buffer("gout_buf", 65536);
            ctx.add_bss_buffer("gout_pos", 8);
            ctx.add_bss_buffer("gout_handle", 8);
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> b) { code.insert(code.end(), b); };

            size_t rel_pos1 = code.size() + 3;
            emit({0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov rax, [rip + gout_pos]
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x74, 0x00}); size_t jz_done = code.size() - 1;         // jz .done

            emit({0x53, 0x48, 0x83, 0xEC, 0x30});                         // push rbx; sub rsp, 48
            emit({0x48, 0x89, 0xC3});                                     // mov rbx, rax

            size_t rel_h1 = code.size() + 3;
            emit({0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov rax, [rip + gout_handle]
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x75, 0x00}); size_t jnz_h = code.size() - 1;           // jnz .have_handle

            emit({0xB9, 0xF5, 0xFF, 0xFF, 0xFF});                         // mov ecx, -11
            size_t rel_std = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetStdHandle
            size_t rel_h2 = code.size() + 3;
            emit({0x48, 0x89, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov [rip + gout_handle], rax

            size_t have_handle = code.size();
            code[jnz_h] = static_cast<uint8_t>(have_handle - (jnz_h + 1));

            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x74, 0x00}); size_t jz_h = code.size() - 1;            // jz .reset
            emit({0x48, 0x83, 0xF8, 0xFF});                               // cmp rax, -1
            emit({0x74, 0x00}); size_t je_h = code.size() - 1;            // je .reset

            emit({0x48, 0x89, 0xC1});                                     // mov rcx, rax
            size_t rel_buf = code.size() + 3;
            emit({0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00});             // lea rdx, [rip + gout_buf]
            emit({0x41, 0x89, 0xD8});                                     // mov r8d, ebx
            emit({0x4C, 0x8D, 0x4C, 0x24, 0x28});                         // lea r9, [rsp + 40]
            emit({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 32], 0
            size_t rel_wf = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call WriteFile

            size_t reset_pos = code.size();
            code[jz_h] = static_cast<uint8_t>(reset_pos - (jz_h + 1));
            code[je_h] = static_cast<uint8_t>(reset_pos - (je_h + 1));

            emit({0x31, 0xC0});                                           // xor eax, eax
            size_t rel_pos2 = code.size() + 3;
            emit({0x48, 0x89, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov [rip + gout_pos], rax
            emit({0x48, 0x83, 0xC4, 0x30, 0x5B});                         // add rsp, 48; pop rbx

            size_t done = code.size();
            code[jz_done] = static_cast<uint8_t>(done - (jz_done + 1));
            emit({0xC3});                                                 // ret

            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_pos1), "gout_pos", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_h1), "gout_handle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_std), "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_h2), "gout_handle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_buf), "gout_buf", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_wf), "WriteFile", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_pos2), "gout_pos", RelocType::Rel32, -4 }
            };
            ctx.append_function("gout_flush", code, rels);
        }

        // 6c. grass_print_char: write single byte into 64KB gout_buf, flushing only if full
        if (ctx.needed_symbols.count("grass_print_char") > 0 && ctx.defined_symbols.count("grass_print_char") == 0) {
            ctx.add_bss_buffer("gout_buf", 65536);
            ctx.add_bss_buffer("gout_pos", 8);
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> b) { code.insert(code.end(), b); };

            size_t rel_pos1 = code.size() + 3;
            emit({0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov rax, [rip + gout_pos]
            emit({0x48, 0x3D, 0x00, 0x00, 0x01, 0x00});                   // cmp rax, 65536
            emit({0x72, 0x00}); size_t jb_ok = code.size() - 1;           // jb .ok

            emit({0x51, 0x48, 0x83, 0xEC, 0x20});                         // push rcx; sub rsp, 32
            size_t rel_flush = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call gout_flush
            emit({0x48, 0x83, 0xC4, 0x20, 0x59, 0x31, 0xC0});             // add rsp, 32; pop rcx; xor eax, eax

            size_t ok = code.size();
            code[jb_ok] = static_cast<uint8_t>(ok - (jb_ok + 1));

            size_t rel_buf = code.size() + 3;
            emit({0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00});             // lea rdx, [rip + gout_buf]
            emit({0x88, 0x0C, 0x02});                                     // mov byte ptr [rdx + rax], cl
            emit({0x48, 0xFF, 0xC0});                                     // inc rax
            size_t rel_pos2 = code.size() + 3;
            emit({0x48, 0x89, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov [rip + gout_pos], rax
            emit({0xC3});                                                 // ret

            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_pos1), "gout_pos", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_flush), "gout_flush", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_buf), "gout_buf", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_pos2), "gout_pos", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_print_char", code, rels);
        }

        // 7. grass_print_newline: write '\n' (0x0A) and flush 64KB stdout buffer
        if (ctx.needed_symbols.count("grass_print_newline") > 0 && ctx.defined_symbols.count("grass_print_newline") == 0) {
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); };
            emit({0x48, 0x83, 0xEC, 0x28});                               // sub rsp, 40
            emit({0xB9, 0x0A, 0x00, 0x00, 0x00});                         // mov ecx, 10
            size_t rel_ch = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call grass_print_char
            size_t rel_fl = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call gout_flush
            emit({0x48, 0x83, 0xC4, 0x28, 0xC3});                         // add rsp, 40; ret
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_ch), "grass_print_char", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_fl), "gout_flush", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_print_newline", code, rels);
        }

        // 8. grass_print_str: copy string into 64KB gout_buf, flushing only when full
        if (ctx.needed_symbols.count("grass_print_str") > 0 && ctx.defined_symbols.count("grass_print_str") == 0) {
            ctx.add_bss_buffer("gout_buf", 65536);
            ctx.add_bss_buffer("gout_pos", 8);
            ctx.add_bss_buffer("gout_handle", 8);
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); };

            emit({0x48, 0x85, 0xC9});                                     // test rcx, rcx
            emit({0x74, 0x00}); size_t jz_null = code.size() - 1;         // jz .ret_fast

            emit({0x31, 0xC0});                                           // xor eax, eax
            size_t len_loop = code.size();
            emit({0x80, 0x3C, 0x01, 0x00});                               // cmp byte ptr [rcx + rax], 0
            emit({0x74, 0x00}); size_t je_len = code.size() - 1;          // je .len_done
            emit({0x48, 0xFF, 0xC0});                                     // inc rax
            int8_t back_len = static_cast<int8_t>(static_cast<int>(len_loop) - static_cast<int>(code.size() + 2));
            emit({0xEB, static_cast<uint8_t>(back_len)});                 // jmp .len_loop

            size_t len_done = code.size();
            code[je_len] = static_cast<uint8_t>(len_done - (je_len + 1));
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x74, 0x00}); size_t jz_empty = code.size() - 1;        // jz .ret_fast

            size_t rel_pos1 = code.size() + 3;
            emit({0x4C, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov r8, [rip + gout_pos]
            emit({0x4D, 0x8D, 0x0C, 0x00});                               // lea r9, [r8 + rax]
            emit({0x49, 0x81, 0xF9, 0x00, 0x00, 0x01, 0x00});             // cmp r9, 65536
            emit({0x77, 0x00}); size_t ja_slow = code.size() - 1;         // ja .slow_path

            size_t rel_buf1 = code.size() + 3;
            emit({0x4C, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00});             // lea r10, [rip + gout_buf]
            emit({0x4D, 0x01, 0xC2});                                     // add r10, r8
            size_t rel_pos2 = code.size() + 3;
            emit({0x4C, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00});             // mov [rip + gout_pos], r9
            emit({0x45, 0x31, 0xDB});                                     // xor r11d, r11d
            size_t copy_fast = code.size();
            emit({0x42, 0x8A, 0x14, 0x19});                               // mov dl, byte ptr [rcx + r11]
            emit({0x43, 0x88, 0x14, 0x1A});                               // mov byte ptr [r10 + r11], dl
            emit({0x49, 0xFF, 0xC3});                                     // inc r11
            emit({0x49, 0x39, 0xC3});                                     // cmp r11, rax
            int8_t back_cf = static_cast<int8_t>(static_cast<int>(copy_fast) - static_cast<int>(code.size() + 2));
            emit({0x72, static_cast<uint8_t>(back_cf)});                  // jb .copy_fast

            size_t ret_fast = code.size();
            code[jz_null] = static_cast<uint8_t>(ret_fast - (jz_null + 1));
            code[jz_empty] = static_cast<uint8_t>(ret_fast - (jz_empty + 1));
            emit({0xC3});                                                 // ret

            size_t slow_path = code.size();
            code[ja_slow] = static_cast<uint8_t>(slow_path - (ja_slow + 1));
            emit({0x53, 0x56, 0x48, 0x83, 0xEC, 0x38});                   // push rbx; push rsi; sub rsp, 56
            emit({0x48, 0x89, 0xCB});                                     // mov rbx, rcx
            emit({0x48, 0x89, 0xC6});                                     // mov rsi, rax
            size_t rel_flush = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call gout_flush

            emit({0x48, 0x81, 0xFE, 0x00, 0x00, 0x01, 0x00});             // cmp rsi, 65536
            emit({0x77, 0x00}); size_t ja_direct = code.size() - 1;       // ja .direct_write

            size_t rel_buf2 = code.size() + 3;
            emit({0x4C, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00});             // lea r10, [rip + gout_buf]
            size_t rel_pos3 = code.size() + 3;
            emit({0x48, 0x89, 0x35, 0x00, 0x00, 0x00, 0x00});             // mov [rip + gout_pos], rsi
            emit({0x45, 0x31, 0xDB});                                     // xor r11d, r11d
            size_t copy_slow = code.size();
            emit({0x42, 0x8A, 0x14, 0x1B});                               // mov dl, byte ptr [rbx + r11]
            emit({0x43, 0x88, 0x14, 0x1A});                               // mov byte ptr [r10 + r11], dl
            emit({0x49, 0xFF, 0xC3});                                     // inc r11
            emit({0x49, 0x39, 0xF3});                                     // cmp r11, rsi
            int8_t back_cs = static_cast<int8_t>(static_cast<int>(copy_slow) - static_cast<int>(code.size() + 2));
            emit({0x72, static_cast<uint8_t>(back_cs)});                  // jb .copy_slow
            emit({0xEB, 0x00}); size_t jmp_epilog = code.size() - 1;      // jmp .epilog

            size_t direct_write = code.size();
            code[ja_direct] = static_cast<uint8_t>(direct_write - (ja_direct + 1));
            size_t rel_h1 = code.size() + 3;
            emit({0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov rax, [rip + gout_handle]
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x75, 0x00}); size_t jnz_h = code.size() - 1;           // jnz .have_h
            emit({0xB9, 0xF5, 0xFF, 0xFF, 0xFF});                         // mov ecx, -11
            size_t rel_std = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetStdHandle
            size_t rel_h2 = code.size() + 3;
            emit({0x48, 0x89, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov [rip + gout_handle], rax
            size_t have_h = code.size();
            code[jnz_h] = static_cast<uint8_t>(have_h - (jnz_h + 1));
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x74, 0x00}); size_t jz_h = code.size() - 1;            // jz .epilog
            emit({0x48, 0x83, 0xF8, 0xFF});                               // cmp rax, -1
            emit({0x74, 0x00}); size_t je_h = code.size() - 1;            // je .epilog
            emit({0x48, 0x89, 0xC1});                                     // mov rcx, rax
            emit({0x48, 0x89, 0xDA});                                     // mov rdx, rbx
            emit({0x41, 0x89, 0xF0});                                     // mov r8d, esi
            emit({0x4C, 0x8D, 0x4C, 0x24, 0x28});                         // lea r9, [rsp + 40]
            emit({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 32], 0
            size_t rel_wf = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call WriteFile

            size_t epilog = code.size();
            code[jmp_epilog] = static_cast<uint8_t>(epilog - (jmp_epilog + 1));
            code[jz_h] = static_cast<uint8_t>(epilog - (jz_h + 1));
            code[je_h] = static_cast<uint8_t>(epilog - (je_h + 1));
            emit({0x48, 0x83, 0xC4, 0x38, 0x5E, 0x5B, 0xC3});             // add rsp, 56; pop rsi; pop rbx; ret

            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_pos1), "gout_pos", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_buf1), "gout_buf", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_pos2), "gout_pos", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_flush), "gout_flush", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_buf2), "gout_buf", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_pos3), "gout_pos", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_h1), "gout_handle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_std), "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_h2), "gout_handle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_wf), "WriteFile", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_print_str", code, rels);
        }

        // 9. grass_print_int: format integer in 32-byte stack buffer + copy to 64KB gout_buf (handles 0 and INT64_MIN)
        if (ctx.needed_symbols.count("grass_print_int") > 0 && ctx.defined_symbols.count("grass_print_int") == 0) {
            ctx.add_bss_buffer("gout_buf", 65536);
            ctx.add_bss_buffer("gout_pos", 8);
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> b) { code.insert(code.end(), b); };

            emit({0x53});                                                 // push rbx
            emit({0x48, 0x83, 0xEC, 0x40});                               // sub rsp, 64 ([rsp+0..31] shadow, [rsp+32..63] 32-byte buf)
            emit({0x4C, 0x8D, 0x4C, 0x24, 0x40});                         // lea r9, [rsp + 64]
            emit({0x4D, 0x89, 0xC8});                                     // mov r8, r9
            emit({0x48, 0x89, 0xC8});                                     // mov rax, rcx
            emit({0x31, 0xDB});                                           // xor ebx, ebx
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x79, 0x08});                                           // jns +8
            emit({0xBB, 0x01, 0x00, 0x00, 0x00});                         // mov ebx, 1
            emit({0x48, 0xF7, 0xD8});                                     // neg rax
            emit({0x41, 0xBA, 0x0A, 0x00, 0x00, 0x00});                   // mov r10d, 10
            size_t div_loop = code.size();
            emit({0x31, 0xD2});                                           // xor edx, edx
            emit({0x49, 0xF7, 0xF2});                                     // div r10
            emit({0x80, 0xC2, 0x30});                                     // add dl, '0'
            emit({0x49, 0xFF, 0xC9});                                     // dec r9
            emit({0x41, 0x88, 0x11});                                     // mov [r9], dl
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            int8_t back_div = static_cast<int8_t>(static_cast<int>(div_loop) - static_cast<int>(code.size() + 2));
            emit({0x75, static_cast<uint8_t>(back_div)});                 // jnz .div_loop
            emit({0x85, 0xDB});                                           // test ebx, ebx
            emit({0x74, 0x07});                                           // jz +7
            emit({0x49, 0xFF, 0xC9});                                     // dec r9
            emit({0x41, 0xC6, 0x01, 0x2D});                               // mov byte ptr [r9], '-'

            emit({0x4C, 0x89, 0xC1});                                     // mov rcx, r8
            emit({0x4C, 0x29, 0xC9});                                     // sub rcx, r9 (rcx = len 1..20)

            size_t rel_pos1 = code.size() + 3;
            emit({0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00});             // mov rax, [rip + gout_pos]
            emit({0x48, 0x8D, 0x14, 0x08});                               // lea rdx, [rax + rcx]
            emit({0x48, 0x81, 0xFA, 0x00, 0x00, 0x01, 0x00});             // cmp rdx, 65536
            emit({0x76, 0x00}); size_t jbe_copy = code.size() - 1;        // jbe .do_copy

            emit({0x4C, 0x89, 0xCB});                                     // mov rbx, r9
            emit({0x48, 0x89, 0x4C, 0x24, 0x20});                         // mov [rsp + 32], rcx
            size_t rel_flush = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call gout_flush
            emit({0x49, 0x89, 0xD9});                                     // mov r9, rbx
            emit({0x48, 0x8B, 0x4C, 0x24, 0x20});                         // mov rcx, [rsp + 32]
            emit({0x31, 0xC0});                                           // xor eax, eax
            emit({0x48, 0x89, 0xCA});                                     // mov rdx, rcx

            size_t do_copy = code.size();
            code[jbe_copy] = static_cast<uint8_t>(do_copy - (jbe_copy + 1));

            size_t rel_buf = code.size() + 3;
            emit({0x4C, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00});             // lea r10, [rip + gout_buf]
            emit({0x49, 0x01, 0xC2});                                     // add r10, rax
            size_t rel_pos2 = code.size() + 3;
            emit({0x48, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00});             // mov [rip + gout_pos], rdx
            emit({0x31, 0xC0});                                           // xor eax, eax
            size_t copy_loop = code.size();
            emit({0x45, 0x8A, 0x1C, 0x01});                               // mov r11b, byte ptr [r9 + rax]
            emit({0x45, 0x88, 0x1C, 0x02});                               // mov byte ptr [r10 + rax], r11b
            emit({0x48, 0xFF, 0xC0});                                     // inc rax
            emit({0x48, 0x39, 0xC8});                                     // cmp rax, rcx
            int8_t back_cp = static_cast<int8_t>(static_cast<int>(copy_loop) - static_cast<int>(code.size() + 2));
            emit({0x72, static_cast<uint8_t>(back_cp)});                  // jb .copy_loop

            emit({0x48, 0x83, 0xC4, 0x40, 0x5B, 0xC3});                   // add rsp, 64; pop rbx; ret

            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_pos1), "gout_pos", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_flush), "gout_flush", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_buf), "gout_buf", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_pos2), "gout_pos", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_print_int", code, rels);
        }

        // 10. grass_print_bool: prints "true" or "false"
        if (ctx.needed_symbols.count("grass_print_bool") > 0 && ctx.defined_symbols.count("grass_print_bool") == 0) {
            ctx.add_rodata_str("__grass_str_true", "true");
            ctx.add_rodata_str("__grass_str_false", "false");
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x09,                         // 3: jz +9 (.false_br at 14)
                0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, // 5: lea rcx, [rip + __grass_str_true]
                0xEB, 0x07,                         // 12: jmp +7 (.call at 21)
                0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, // 14: lea rcx, [rip + __grass_str_false]
                0xE9, 0x00, 0x00, 0x00, 0x00        // 21: jmp grass_print_str
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 8, "__grass_str_true", RelocType::Rel32, -4 },
                { SectionKind::Text, 17, "__grass_str_false", RelocType::Rel32, -4 },
                { SectionKind::Text, 22, "grass_print_str", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_print_bool", code, rels);
        }

        // 11. grass_print_ptr: prints hex pointer "0x..."
        if (ctx.needed_symbols.count("grass_print_ptr") > 0 && ctx.defined_symbols.count("grass_print_ptr") == 0) {
            std::vector<uint8_t> code = {
                0x53,                               // 0: push rbx
                0x56,                               // 1: push rsi
                0x57,                               // 2: push rdi
                0x48, 0x83, 0xEC, 0x40,             // 3: sub rsp, 64
                0x48, 0x89, 0xC8,                   // 7: mov rax, rcx
                0x48, 0x8D, 0x7C, 0x24, 0x3F,       // 10: lea rdi, [rsp + 63]
                0xC6, 0x07, 0x00,                   // 15: mov byte ptr [rdi], 0
                0x48, 0x85, 0xC0,                   // 18: test rax, rax
                0x75, 0x06,                         // 21: jnz +6 (.loop at 29)
                0x48, 0xFF, 0xCF,                   // 23: dec rdi
                0xC6, 0x07, 0x30,                   // 26: mov byte ptr [rdi], '0'
                0xEB, 0x1A,                         // 29: jmp .prefix (at 57)
                0x89, 0xC2,                         // 31: mov edx, eax (.loop)
                0x83, 0xE2, 0x0F,                   // 33: and edx, 0xF
                0x80, 0xFA, 0x0A,                   // 36: cmp dl, 10
                0x72, 0x05,                         // 39: jb +5
                0x80, 0xC2, 0x57,                   // 41: add dl, 'a' - 10 (87)
                0xEB, 0x03,                         // 44: jmp +3
                0x80, 0xC2, 0x30,                   // 46: add dl, '0' (48)
                0x48, 0xFF, 0xCF,                   // 49: dec rdi
                0x88, 0x17,                         // 52: mov [rdi], dl
                0x48, 0xC1, 0xE8, 0x04,             // 54: shr rax, 4
                0x75, 0xE3,                         // 58: jnz .loop (at 31)
                0x48, 0xFF, 0xCF,                   // 60: dec rdi (.prefix)
                0xC6, 0x07, 0x78,                   // 63: mov byte ptr [rdi], 'x'
                0x48, 0xFF, 0xCF,                   // 66: dec rdi
                0xC6, 0x07, 0x30,                   // 69: mov byte ptr [rdi], '0'
                0x48, 0x89, 0xF9,                   // 72: mov rcx, rdi
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 75: call grass_print_str
                0x48, 0x83, 0xC4, 0x40,             // 80: add rsp, 64
                0x5F,                               // 84: pop rdi
                0x5E,                               // 85: pop rsi
                0x5B,                               // 86: pop rbx
                0xC3                                // 87: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 76, "grass_print_str", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_print_ptr", code, rels);
        }

        // 12. grass_print_double: prints double value to console (64KB buffered)
        if (ctx.needed_symbols.count("grass_print_double") > 0 && ctx.defined_symbols.count("grass_print_double") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x38,             // 0: sub rsp, 56
                0xF2, 0x0F, 0x11, 0x44, 0x24, 0x20, // 4: movsd [rsp + 32], xmm0
                0xF2, 0x48, 0x0F, 0x2C, 0xC8,       // 10: cvttsd2si rcx, xmm0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 15: call grass_print_int
                0xB9, 0x2E, 0x00, 0x00, 0x00,       // 20: mov ecx, 46 ('.')
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 25: call grass_print_char
                0xF2, 0x0F, 0x10, 0x44, 0x24, 0x20, // 30: movsd xmm0, [rsp + 32]
                0xF2, 0x48, 0x0F, 0x2C, 0xC0,       // 36: cvttsd2si rax, xmm0
                0xF2, 0x48, 0x0F, 0x2A, 0xC8,       // 41: cvtsi2sd xmm1, rax
                0xF2, 0x0F, 0x5C, 0xC1,             // 46: subsd xmm0, xmm1
                0x48, 0xC7, 0xC0, 0x10, 0x27, 0x00, 0x00, // 50: mov rax, 10000
                0xF2, 0x48, 0x0F, 0x2A, 0xC8,       // 57: cvtsi2sd xmm1, rax
                0xF2, 0x0F, 0x59, 0xC1,             // 62: mulsd xmm0, xmm1
                0xF2, 0x48, 0x0F, 0x2C, 0xC8,       // 66: cvttsd2si rcx, xmm0
                0x48, 0x85, 0xC9,                   // 71: test rcx, rcx
                0x79, 0x03,                         // 74: jns +3
                0x48, 0xF7, 0xD9,                   // 76: neg rcx
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 79: call grass_print_int
                0x48, 0x83, 0xC4, 0x38,             // 84: add rsp, 56
                0xC3                                // 88: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 16, "grass_print_int", RelocType::Rel32, -4 },
                { SectionKind::Text, 26, "grass_print_char", RelocType::Rel32, -4 },
                { SectionKind::Text, 80, "grass_print_int", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_print_double", code, rels);
        }


    }

} // namespace grass::backend

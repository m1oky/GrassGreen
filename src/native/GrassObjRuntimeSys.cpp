#include "native/GrassObjRuntime.h"

namespace grass::backend {

    void inject_win64_rt_sys(Win64RuntimeCtx& ctx) {
        // 12b. exit: flush 64KB stdout buffer and terminate via ExitProcess
        if (ctx.needed_symbols.count("exit") > 0 && ctx.defined_symbols.count("exit") == 0) {
            std::vector<uint8_t> code = {
                0x53,                               // 0: push rbx
                0x48, 0x83, 0xEC, 0x20,             // 1: sub rsp, 32
                0x89, 0xCB,                         // 5: mov ebx, ecx
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 7: call gout_flush
                0x89, 0xD9,                         // 12: mov ecx, ebx
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 14: call ExitProcess
                0x48, 0x83, 0xC4, 0x20,             // 19: add rsp, 32
                0x5B,                               // 23: pop rbx
                0xC3                                // 24: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 8, "gout_flush", RelocType::Rel32, -4 },
                { SectionKind::Text, 15, "ExitProcess", RelocType::Rel32, -4 }
            };
            ctx.append_function("exit", code, rels);
        }

        // 12c. system: Win32 CreateProcessA("cmd.exe /c " + cmd) + WaitForSingleObject + GetExitCodeProcess
        if (ctx.needed_symbols.count("system") > 0 && ctx.defined_symbols.count("system") == 0) {
            ctx.add_rodata_str("__grass_cmd_exe", "cmd.exe");
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> b) { code.insert(code.end(), b); };

            emit({0x48, 0x85, 0xC9});                                     // test rcx, rcx
            emit({0x74, 0x00}); size_t jz_null = code.size() - 1;         // jz .check_shell
            emit({0x80, 0x39, 0x00});                                     // cmp byte ptr [rcx], 0
            emit({0x75, 0x00}); size_t jnz_run = code.size() - 1;         // jnz .run_cmd

            size_t check_shell = code.size();
            code[jz_null] = static_cast<uint8_t>(check_shell - (jz_null + 1));
            emit({0x48, 0x83, 0xEC, 0x38});                               // sub rsp, 56
            emit({0x31, 0xC9});                                           // xor ecx, ecx
            size_t rel_cmd = code.size() + 3;
            emit({0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00});             // lea rdx, [rip + __grass_cmd_exe]
            emit({0x45, 0x31, 0xC0});                                     // xor r8d, r8d
            emit({0x45, 0x31, 0xC9});                                     // xor r9d, r9d
            emit({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 32], 0
            emit({0x48, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 40], 0
            size_t rel_sp = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call SearchPathA
            emit({0x85, 0xC0});                                           // test eax, eax
            emit({0x0F, 0x95, 0xC0});                                     // setnz al
            emit({0x0F, 0xB6, 0xC0});                                     // movzx eax, al
            emit({0x48, 0x83, 0xC4, 0x38, 0xC3});                         // add rsp, 56; ret

            size_t run_cmd = code.size();
            code[jnz_run] = static_cast<uint8_t>(run_cmd - (jnz_run + 1));
            emit({0x53, 0x56, 0x57});                                     // push rbx; push rsi; push rdi
            emit({0x48, 0x81, 0xEC, 0xE0, 0x00, 0x00, 0x00});             // sub rsp, 224
            emit({0x48, 0x89, 0xCB});                                     // mov rbx, rcx (cmd)
            size_t rel_len = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call lstrlenA
            emit({0x89, 0xC6});                                           // mov esi, eax
            size_t rel_heap1 = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetProcessHeap
            emit({0x48, 0x89, 0xC1});                                     // mov rcx, rax
            emit({0xBA, 0x08, 0x00, 0x00, 0x00});                         // mov edx, 8 (HEAP_ZERO_MEMORY)
            emit({0x4C, 0x8D, 0x46, 0x0C});                               // lea r8, [rsi + 12]
            size_t rel_alloc = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call HeapAlloc
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x74, 0x00}); size_t jz_oom = code.size() - 1;          // jz .fail_no_free
            emit({0x48, 0x89, 0xC6});                                     // mov rsi, rax (cmdline)

            // Write "cmd.exe /c " (11 bytes) at [rsi]
            emit({0x48, 0xB8, 0x63, 0x6D, 0x64, 0x2E, 0x65, 0x78, 0x65, 0x20}); // mov rax, "cmd.exe "
            emit({0x48, 0x89, 0x06});                                     // mov [rsi], rax
            emit({0xC7, 0x46, 0x08, 0x2F, 0x63, 0x20, 0x00});             // mov dword ptr [rsi + 8], "/c \0"

            // Copy cmd (rbx) to [rsi + 11]
            emit({0x48, 0x8D, 0x56, 0x0B});                               // lea rdx, [rsi + 11]
            emit({0x31, 0xC9});                                           // xor ecx, ecx
            size_t cpy_loop = code.size();
            emit({0x8A, 0x04, 0x0B});                                     // mov al, byte ptr [rbx + rcx]
            emit({0x88, 0x04, 0x0A});                                     // mov byte ptr [rdx + rcx], al
            emit({0x48, 0xFF, 0xC1});                                     // inc rcx
            emit({0x84, 0xC0});                                           // test al, al
            int8_t back_cpy = static_cast<int8_t>(static_cast<int>(cpy_loop) - static_cast<int>(code.size() + 2));
            emit({0x75, static_cast<uint8_t>(back_cpy)});                 // jnz .cpy_loop

            // Zero STARTUPINFOA + PROCESS_INFORMATION (128 bytes at [rsp + 80])
            emit({0x48, 0x8D, 0x7C, 0x24, 0x50});                         // lea rdi, [rsp + 80]
            emit({0xB9, 0x10, 0x00, 0x00, 0x00});                         // mov ecx, 16
            emit({0x31, 0xC0});                                           // xor eax, eax
            emit({0xF3, 0x48, 0xAB});                                     // rep stosq
            emit({0xC7, 0x44, 0x24, 0x50, 0x68, 0x00, 0x00, 0x00});       // mov dword ptr [rsp + 80], 104 (si.cb)
            emit({0xC7, 0x84, 0x24, 0xD0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF}); // mov dword ptr [rsp + 208], -1

            // CreateProcessA(NULL, rsi, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)
            emit({0x31, 0xC9});                                           // xor ecx, ecx
            emit({0x48, 0x89, 0xF2});                                     // mov rdx, rsi
            emit({0x45, 0x31, 0xC0});                                     // xor r8d, r8d
            emit({0x45, 0x31, 0xC9});                                     // xor r9d, r9d
            emit({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 32], 0
            emit({0x48, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 40], 0
            emit({0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 48], 0
            emit({0x48, 0xC7, 0x44, 0x24, 0x38, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 56], 0
            emit({0x48, 0x8D, 0x44, 0x24, 0x50});                         // lea rax, [rsp + 80]
            emit({0x48, 0x89, 0x44, 0x24, 0x40});                         // mov [rsp + 64], rax
            emit({0x48, 0x8D, 0x84, 0x24, 0xB8, 0x00, 0x00, 0x00});       // lea rax, [rsp + 184]
            emit({0x48, 0x89, 0x44, 0x24, 0x48});                         // mov [rsp + 72], rax
            size_t rel_cp = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call CreateProcessA
            emit({0x85, 0xC0});                                           // test eax, eax
            emit({0x74, 0x00}); size_t jz_cp_err = code.size() - 1;       // jz .cp_err

            // WaitForSingleObject(pi.hProcess, INFINITE)
            emit({0x48, 0x8B, 0x8C, 0x24, 0xB8, 0x00, 0x00, 0x00});       // mov rcx, [rsp + 184]
            emit({0xBA, 0xFF, 0xFF, 0xFF, 0xFF});                         // mov edx, -1
            size_t rel_wait = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call WaitForSingleObject

            // GetExitCodeProcess(pi.hProcess, &exit_code)
            emit({0x48, 0x8B, 0x8C, 0x24, 0xB8, 0x00, 0x00, 0x00});       // mov rcx, [rsp + 184]
            emit({0x48, 0x8D, 0x94, 0x24, 0xD0, 0x00, 0x00, 0x00});       // lea rdx, [rsp + 208]
            size_t rel_gec = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetExitCodeProcess

            // CloseHandle(pi.hProcess); CloseHandle(pi.hThread)
            emit({0x48, 0x8B, 0x8C, 0x24, 0xB8, 0x00, 0x00, 0x00});       // mov rcx, [rsp + 184]
            size_t rel_ch1 = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call CloseHandle
            emit({0x48, 0x8B, 0x8C, 0x24, 0xC0, 0x00, 0x00, 0x00});       // mov rcx, [rsp + 192]
            size_t rel_ch2 = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call CloseHandle

            emit({0x8B, 0x9C, 0x24, 0xD0, 0x00, 0x00, 0x00});             // mov ebx, [rsp + 208]
            emit({0xEB, 0x00}); size_t jmp_free = code.size() - 1;        // jmp .free_cmdline

            size_t cp_err = code.size();
            code[jz_cp_err] = static_cast<uint8_t>(cp_err - (jz_cp_err + 1));
            emit({0xBB, 0xFF, 0xFF, 0xFF, 0xFF});                         // mov ebx, -1

            size_t free_cmdline = code.size();
            code[jmp_free] = static_cast<uint8_t>(free_cmdline - (jmp_free + 1));
            size_t rel_heap2 = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetProcessHeap
            emit({0x48, 0x89, 0xC1});                                     // mov rcx, rax
            emit({0x31, 0xD2});                                           // xor edx, edx
            emit({0x49, 0x89, 0xF0});                                     // mov r8, rsi
            size_t rel_hfree = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call HeapFree
            emit({0x48, 0x63, 0xC3});                                     // movsxd rax, ebx
            emit({0xEB, 0x00}); size_t jmp_epilog = code.size() - 1;      // jmp .epilog

            size_t fail_no_free = code.size();
            code[jz_oom] = static_cast<uint8_t>(fail_no_free - (jz_oom + 1));
            emit({0x48, 0xC7, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF});             // mov rax, -1

            size_t epilog = code.size();
            code[jmp_epilog] = static_cast<uint8_t>(epilog - (jmp_epilog + 1));
            emit({0x48, 0x81, 0xC4, 0xE0, 0x00, 0x00, 0x00});             // add rsp, 224
            emit({0x5F, 0x5E, 0x5B, 0xC3});                               // pop rdi; pop rsi; pop rbx; ret

            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_cmd), "__grass_cmd_exe", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_sp), "SearchPathA", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_len), "lstrlenA", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_heap1), "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_alloc), "HeapAlloc", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_cp), "CreateProcessA", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_wait), "WaitForSingleObject", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_gec), "GetExitCodeProcess", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_ch1), "CloseHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_ch2), "CloseHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_heap2), "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_hfree), "HeapFree", RelocType::Rel32, -4 }
            };
            ctx.append_function("system", code, rels);
        }

        // 13. grass_read_int: Win32 ReadFile(GetStdHandle(-10)) with aligned stack, handle checks, and null-termination
        if (ctx.needed_symbols.count("grass_read_int") > 0 && ctx.defined_symbols.count("grass_read_int") == 0) {
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); };
            emit({0x56});                                                 // push rsi (entry RSP%16==8 -> now 0)
            emit({0x48, 0x83, 0xEC, 0x50});                               // sub rsp, 80 (0x50): [0..31] shadow, [32] overlapped, [40] bytesRead, [48..79] buf
            emit({0xB9, 0xF6, 0xFF, 0xFF, 0xFF});                         // mov ecx, -10
            size_t rel_std = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetStdHandle
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x74, 0x00}); size_t jz_h = code.size() - 1;
            emit({0x48, 0x83, 0xF8, 0xFF});                               // cmp rax, -1
            emit({0x74, 0x00}); size_t je_h = code.size() - 1;
            emit({0x48, 0x89, 0xC1});                                     // mov rcx, rax
            emit({0x48, 0x8D, 0x54, 0x24, 0x30});                         // lea rdx, [rsp + 48]
            emit({0x41, 0xB8, 0x1F, 0x00, 0x00, 0x00});                   // mov r8d, 31
            emit({0x4C, 0x8D, 0x4C, 0x24, 0x28});                         // lea r9, [rsp + 40]
            emit({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 32], 0
            size_t rel_rf = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call ReadFile
            emit({0x85, 0xC0});                                           // test eax, eax
            emit({0x74, 0x00}); size_t jz_rf = code.size() - 1;
            emit({0x8B, 0x4C, 0x24, 0x28});                               // mov ecx, dword ptr [rsp + 40]
            emit({0x83, 0xF9, 0x1F});                                     // cmp ecx, 31
            emit({0x76, 0x05});                                           // jbe +5
            emit({0xB9, 0x1F, 0x00, 0x00, 0x00});                         // mov ecx, 31
            emit({0xC6, 0x44, 0x0C, 0x30, 0x00});                         // mov byte ptr [rsp + rcx + 48], 0
            emit({0x31, 0xC0});                                           // xor eax, eax
            emit({0x45, 0x31, 0xC0});                                     // xor r8d, r8d
            emit({0x31, 0xC9});                                           // xor ecx, ecx
            emit({0x48, 0x8D, 0x74, 0x24, 0x30});                         // lea rsi, [rsp + 48]
            size_t loop_ws = code.size();
            emit({0x0F, 0xB6, 0x14, 0x0E});                               // movzx edx, byte ptr [rsi + rcx]
            emit({0x48, 0xFF, 0xC1});                                     // inc rcx
            emit({0x80, 0xFA, 0x20});                                     // cmp dl, ' '
            emit({0x74, static_cast<uint8_t>(static_cast<int8_t>(loop_ws - (code.size() + 2)))});
            emit({0x80, 0xFA, 0x09});                                     // cmp dl, '\t'
            emit({0x74, static_cast<uint8_t>(static_cast<int8_t>(loop_ws - (code.size() + 2)))});
            emit({0x80, 0xFA, 0x2D});                                     // cmp dl, '-'
            emit({0x75, 0x0D});                                           // jne +13
            emit({0x41, 0xB8, 0x01, 0x00, 0x00, 0x00});                   // mov r8d, 1
            emit({0x0F, 0xB6, 0x14, 0x0E});                               // movzx edx, byte ptr [rsi + rcx]
            emit({0x48, 0xFF, 0xC1});                                     // inc rcx
            size_t loop_dig = code.size();
            emit({0x80, 0xFA, 0x30});                                     // cmp dl, '0'
            emit({0x72, 0x00}); size_t jb_done = code.size() - 1;
            emit({0x80, 0xFA, 0x39});                                     // cmp dl, '9'
            emit({0x77, 0x00}); size_t ja_done = code.size() - 1;
            emit({0x80, 0xEA, 0x30});                                     // sub dl, '0'
            emit({0x48, 0x0F, 0xB6, 0xD2});                               // movzx rdx, dl
            emit({0x48, 0x6B, 0xC0, 0x0A});                               // imul rax, 10
            emit({0x48, 0x01, 0xD0});                                     // add rax, rdx
            emit({0x0F, 0xB6, 0x14, 0x0E});                               // movzx edx, byte ptr [rsi + rcx]
            emit({0x48, 0xFF, 0xC1});                                     // inc rcx
            emit({0xEB, static_cast<uint8_t>(static_cast<int8_t>(loop_dig - (code.size() + 2)))});
            size_t done_digits = code.size();
            code[jb_done] = static_cast<uint8_t>(done_digits - (jb_done + 1));
            code[ja_done] = static_cast<uint8_t>(done_digits - (ja_done + 1));
            emit({0x45, 0x85, 0xC0});                                     // test r8d, r8d
            emit({0x74, 0x03});                                           // jz +3
            emit({0x48, 0xF7, 0xD8});                                     // neg rax
            emit({0xEB, 0x02});                                           // jmp +2 (.epilog)
            size_t fail_lbl = code.size();
            code[jz_h]  = static_cast<uint8_t>(fail_lbl - (jz_h + 1));
            code[je_h]  = static_cast<uint8_t>(fail_lbl - (je_h + 1));
            code[jz_rf] = static_cast<uint8_t>(fail_lbl - (jz_rf + 1));
            emit({0x31, 0xC0});                                           // xor eax, eax
            emit({0x48, 0x83, 0xC4, 0x50, 0x5E, 0xC3});                   // add rsp, 80; pop rsi; ret
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_std), "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_rf), "ReadFile", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_read_int", code, rels);
        }

        // 14. grass_array_create, grass_array_push, grass_array_get, grass_array_set, grass_array_len
        if (ctx.needed_symbols.count("grass_array_create") > 0 && ctx.defined_symbols.count("grass_array_create") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xB9, 0x20, 0x00, 0x00, 0x00,       // 4: mov ecx, 32
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 9: call grass_alloc
                0x48, 0x85, 0xC0,                   // 14: test rax, rax
                0x74, 0x38,                         // 17: jz +56 (.ret at 75)
                0x48, 0x89, 0x44, 0x24, 0x20,       // 19: mov [rsp + 32], rax
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 24: mov r10, 0x415252594752474E
                0x4C, 0x89, 0x10,                   // 34: mov [rax], r10
                0x48, 0xC7, 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, // 37: mov qword ptr [rax + 16], 0
                0x48, 0xC7, 0x40, 0x18, 0x08, 0x00, 0x00, 0x00, // 45: mov qword ptr [rax + 24], 8
                0xB9, 0x40, 0x00, 0x00, 0x00,       // 53: mov ecx, 64
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 58: call grass_alloc
                0x48, 0x8B, 0x4C, 0x24, 0x20,       // 63: mov rcx, [rsp + 32]
                0x48, 0x89, 0x41, 0x08,             // 68: mov [rcx + 8], rax
                0x48, 0x89, 0xC8,                   // 72: mov rax, rcx
                0x48, 0x83, 0xC4, 0x28,             // 75: add rsp, 40 (.ret)
                0xC3                                // 79: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 10, "grass_alloc", RelocType::Rel32, -4 },
                { SectionKind::Text, 59, "grass_alloc", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_array_create", code, rels);
        }

        if (ctx.needed_symbols.count("grass_array_push") > 0 && ctx.defined_symbols.count("grass_array_push") == 0) {
            std::vector<uint8_t> code;
            auto emit = [&](std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); };
            emit({0x48, 0x85, 0xC9});                                     // test rcx, rcx
            emit({0x74, 0x00}); size_t jz_null = code.size() - 1;
            emit({0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41}); // mov r10, magic
            emit({0x4C, 0x39, 0x11});                                     // cmp [rcx], r10
            emit({0x75, 0x00}); size_t jne_mag = code.size() - 1;
            emit({0x53, 0x56, 0x48, 0x83, 0xEC, 0x28});                   // push rbx; push rsi; sub rsp, 40
            emit({0x48, 0x89, 0xCB});                                     // mov rbx, rcx
            emit({0x48, 0x89, 0xD6});                                     // mov rsi, rdx
            emit({0x48, 0x8B, 0x43, 0x10});                               // mov rax, [rbx + 16] (len)
            emit({0x48, 0x3B, 0x43, 0x18});                               // cmp rax, [rbx + 24] (cap)
            emit({0x72, 0x00}); size_t jb_store = code.size() - 1;
            emit({0x48, 0x8B, 0x43, 0x18});                               // mov rax, [rbx + 24]
            emit({0x48, 0xD1, 0xE0});                                     // shl rax, 1
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x7F, 0x05});                                           // jg +5
            emit({0xB8, 0x08, 0x00, 0x00, 0x00});                         // mov eax, 8
            emit({0x48, 0xC1, 0xE0, 0x03});                               // shl rax, 3 (bytes = new_cap * 8)
            emit({0x48, 0x89, 0xC2});                                     // mov rdx, rax
            emit({0x48, 0x8B, 0x4B, 0x08});                               // mov rcx, [rbx + 8]
            size_t rel_realloc = code.size() + 1;
            emit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call grass_realloc
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x74, 0x00}); size_t jz_oom = code.size() - 1;
            emit({0x48, 0x89, 0x43, 0x08});                               // mov [rbx + 8], rax
            emit({0x48, 0x8B, 0x43, 0x18});                               // reload old cap
            emit({0x48, 0xD1, 0xE0});                                     // shl rax, 1
            emit({0x48, 0x85, 0xC0});                                     // test rax, rax
            emit({0x7F, 0x05});                                           // jg +5
            emit({0xB8, 0x08, 0x00, 0x00, 0x00});                         // mov eax, 8
            emit({0x48, 0x89, 0x43, 0x18});                               // mov [rbx + 24], rax
            size_t push_store = code.size();
            code[jb_store] = static_cast<uint8_t>(push_store - (jb_store + 1));
            emit({0x48, 0x8B, 0x4B, 0x08});                               // mov rcx, [rbx + 8]
            emit({0x48, 0x8B, 0x53, 0x10});                               // mov rdx, [rbx + 16]
            emit({0x48, 0x89, 0x34, 0xD1});                               // mov [rcx + rdx * 8], rsi
            emit({0x48, 0xFF, 0x43, 0x10});                               // inc qword ptr [rbx + 16]
            size_t epilog = code.size();
            code[jz_oom] = static_cast<uint8_t>(epilog - (jz_oom + 1));
            emit({0x48, 0x83, 0xC4, 0x28, 0x5E, 0x5B});                   // add rsp, 40; pop rsi; pop rbx
            size_t ret_done = code.size();
            code[jz_null] = static_cast<uint8_t>(ret_done - (jz_null + 1));
            code[jne_mag] = static_cast<uint8_t>(ret_done - (jne_mag + 1));
            emit({0xC3});
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_realloc), "grass_realloc", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_array_push", code, rels);
        }

        if (ctx.needed_symbols.count("grass_array_get") > 0 && ctx.defined_symbols.count("grass_array_get") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x31, 0xC0,                   // 0: xor rax, rax
                0x48, 0x85, 0xC9,                   // 3: test rcx, rcx
                0x74, 0x27,                         // 6: jz +39 (.ret at 47)
                0x48, 0x85, 0xD2,                   // 8: test rdx, rdx
                0x78, 0x22,                         // 11: js +34 (.ret at 47)
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 13: mov r10, 0x415252594752474E
                0x4C, 0x39, 0x11,                   // 23: cmp [rcx], r10
                0x74, 0x05,                         // 26: je +5 (.is_dyn_array at 33)
                0x0F, 0xB6, 0x04, 0x11,             // 28: movzx rax, byte ptr [rcx + rdx]
                0xC3,                               // 32: ret
                0x48, 0x3B, 0x51, 0x10,             // 33: cmp rdx, [rcx + 16] (.is_dyn_array)
                0x73, 0x08,                         // 37: jae +8 (.ret at 47)
                0x48, 0x8B, 0x41, 0x08,             // 39: mov rax, [rcx + 8]
                0x48, 0x8B, 0x04, 0xD0,             // 43: mov rax, [rax + rdx * 8]
                0xC3                                // 47: ret (.ret)
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("grass_array_get", code, rels);
        }

        if (ctx.needed_symbols.count("grass_array_set") > 0 && ctx.defined_symbols.count("grass_array_set") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x35,                         // 3: jz +53 (.ret at 58)
                0x48, 0x85, 0xD2,                   // 5: test rdx, rdx
                0x78, 0x30,                         // 8: js +48 (.ret at 58)
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 10: mov r10, 0x415252594752474E
                0x4C, 0x39, 0x11,                   // 20: cmp [rcx], r10
                0x74, 0x05,                         // 23: je +5 (.is_dyn_array at 30)
                0x44, 0x88, 0x04, 0x11,             // 25: mov [rcx + rdx], r8b
                0xC3,                               // 29: ret
                0x48, 0x3B, 0x51, 0x18,             // 30: cmp rdx, [rcx + 24] (.is_dyn_array)
                0x73, 0x16,                         // 34: jae +22 (.ret at 58)
                0x48, 0x3B, 0x51, 0x10,             // 36: cmp rdx, [rcx + 16]
                0x72, 0x08,                         // 40: jb +8 (.store at 50)
                0x48, 0x8D, 0x42, 0x01,             // 42: lea rax, [rdx + 1]
                0x48, 0x89, 0x41, 0x10,             // 46: mov [rcx + 16], rax
                0x48, 0x8B, 0x41, 0x08,             // 50: mov rax, [rcx + 8] (.store)
                0x4C, 0x89, 0x04, 0xD0,             // 54: mov [rax + rdx * 8], r8
                0xC3                                // 58: ret (.ret)
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("grass_array_set", code, rels);
        }

        if (ctx.needed_symbols.count("grass_array_len") > 0 && ctx.defined_symbols.count("grass_array_len") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x31, 0xC0,                   // 0: xor rax, rax
                0x48, 0x85, 0xC9,                   // 3: test rcx, rcx
                0x74, 0x1E,                         // 6: jz +30 (.ret at 38)
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 8: mov r10, 0x415252594752474E
                0x4C, 0x39, 0x11,                   // 18: cmp [rcx], r10
                0x74, 0x0B,                         // 21: je +11 (.is_dyn_array at 34)
                0x80, 0x3C, 0x01, 0x00,             // 23: cmp byte ptr [rcx + rax], 0 (.strlen_loop)
                0x74, 0x09,                         // 27: je +9 (.ret at 38)
                0x48, 0xFF, 0xC0,                   // 29: inc rax
                0xEB, 0xF5,                         // 32: jmp -11 (.strlen_loop at 23)
                0x48, 0x8B, 0x41, 0x10,             // 34: mov rax, [rcx + 16] (.is_dyn_array)
                0xC3                                // 38: ret (.ret)
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("grass_array_len", code, rels);
        }

        if (ctx.needed_symbols.count("str_to_int") > 0 && ctx.defined_symbols.count("str_to_int") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x31, 0xC0, 0x48, 0x85, 0xC9, 0x74, 0x55, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F, 0xB6,
                0x11, 0x80, 0xFA, 0x20, 0x74, 0x0F, 0x80, 0xFA, 0x09, 0x74, 0x0A, 0x80, 0xFA, 0x0A, 0x74, 0x05,
                0x80, 0xFA, 0x0D, 0x75, 0x05, 0x48, 0xFF, 0xC1, 0xEB, 0xE4, 0x80, 0xFA, 0x2D, 0x75, 0x0B, 0x41,
                0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0xFF, 0xC1, 0xEB, 0x08, 0x80, 0xFA, 0x2B, 0x75, 0x03, 0x48,
                0xFF, 0xC1, 0x0F, 0xB6, 0x11, 0x83, 0xEA, 0x30, 0x83, 0xFA, 0x09, 0x77, 0x0C, 0x48, 0x6B, 0xC0,
                0x0A, 0x48, 0x01, 0xD0, 0x48, 0xFF, 0xC1, 0xEB, 0xE9, 0x49, 0x0F, 0xAF, 0xC0, 0xC3
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("str_to_int", code, rels);
        }

        if (ctx.needed_symbols.count("int_to_str") > 0 && ctx.defined_symbols.count("int_to_str") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x58, 0x48, 0x89, 0x4C, 0x24, 0x20, 0xB9, 0x20, 0x00, 0x00, 0x00, 0xE8, 0x00,
                0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x60, 0x49, 0x89, 0xC1, 0x4C, 0x8B, 0x44, 0x24, 0x20,
                0x48, 0x89, 0x44, 0x24, 0x20, 0x4D, 0x85, 0xC0, 0x75, 0x08, 0x66, 0x41, 0xC7, 0x01, 0x30, 0x00,
                0xEB, 0x46, 0x79, 0x0A, 0x41, 0xC6, 0x01, 0x2D, 0x49, 0xFF, 0xC1, 0x49, 0xF7, 0xD8, 0x4C, 0x8D,
                0x54, 0x24, 0x46, 0x41, 0xC6, 0x02, 0x00, 0x49, 0xC7, 0xC3, 0x0A, 0x00, 0x00, 0x00, 0x4C, 0x89,
                0xC0, 0x48, 0x31, 0xD2, 0x49, 0xF7, 0xF3, 0x80, 0xC2, 0x30, 0x49, 0xFF, 0xCA, 0x41, 0x88, 0x12,
                0x49, 0x89, 0xC0, 0x4D, 0x85, 0xC0, 0x75, 0xE6, 0x41, 0x8A, 0x12, 0x41, 0x88, 0x11, 0x49, 0xFF,
                0xC2, 0x49, 0xFF, 0xC1, 0x84, 0xD2, 0x75, 0xF0, 0x48, 0x8B, 0x44, 0x24, 0x20, 0x48, 0x83, 0xC4,
                0x58, 0xC3
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 15, "grass_alloc", RelocType::Rel32, -4 }
            };
            ctx.append_function("int_to_str", code, rels);
        }

        // 15. Standard timer intrinsics: start_time, stop_time, conclus_time, now_ms, sleep_ms
        bool need_time = (ctx.needed_symbols.count("start_time") > 0 ||
                          ctx.needed_symbols.count("stop_time") > 0 ||
                          ctx.needed_symbols.count("conclus_time") > 0);
        if (need_time) {
            ctx.add_data_buffer("__grass_time_buf", 64);
        }

        if (ctx.needed_symbols.count("start_time") > 0 && ctx.defined_symbols.count("start_time") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, // 4: lea rcx, [rip + __grass_time_buf]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 11: call QueryPerformanceFrequency
                0x48, 0x8D, 0x0D, 0x08, 0x00, 0x00, 0x00, // 16: lea rcx, [rip + __grass_time_buf + 8]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 23: call QueryPerformanceCounter
                0x48, 0x83, 0xC4, 0x28,             // 28: add rsp, 40
                0xC3                                // 32: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 7, "__grass_time_buf", RelocType::Rel32, -4 },
                { SectionKind::Text, 12, "QueryPerformanceFrequency", RelocType::Rel32, -4 },
                { SectionKind::Text, 19, "__grass_time_buf", RelocType::Rel32, 4 }, // +8
                { SectionKind::Text, 24, "QueryPerformanceCounter", RelocType::Rel32, -4 }
            };
            ctx.append_function("start_time", code, rels);
        }

        if (ctx.needed_symbols.count("stop_time") > 0 && ctx.defined_symbols.count("stop_time") == 0) {
            std::vector<uint8_t> code = {
                0x53,                               // 0: push rbx
                0x57,                               // 1: push rdi
                0x56,                               // 2: push rsi
                0x48, 0x83, 0xEC, 0x30,             // 3: sub rsp, 48
                0x48, 0x8D, 0x0D, 0x10, 0x00, 0x00, 0x00, // 7: lea rcx, [rip + __grass_time_buf + 16]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 14: call QueryPerformanceCounter
                0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00, // 19: mov rax, [rip + __grass_time_buf + 16]
                0x48, 0x2B, 0x05, 0x08, 0x00, 0x00, 0x00, // 26: sub rax, [rip + __grass_time_buf + 8]
                0x48, 0x8B, 0x1D, 0x00, 0x00, 0x00, 0x00, // 33: mov rbx, [rip + __grass_time_buf]
                0x48, 0x85, 0xDB,                   // 40: test rbx, rbx
                0x75, 0x07,                         // 43: jnz +7
                0x48, 0xC7, 0xC3, 0x01, 0x00, 0x00, 0x00, // 45: mov rbx, 1
                0xF2, 0x48, 0x0F, 0x2A, 0xC0,       // 52: cvtsi2sd xmm0, rax
                0x48, 0xC7, 0xC1, 0xE8, 0x03, 0x00, 0x00, // 57: mov rcx, 1000
                0xF2, 0x48, 0x0F, 0x2A, 0xC9,       // 64: cvtsi2sd xmm1, rcx
                0xF2, 0x0F, 0x59, 0xC1,             // 69: mulsd xmm0, xmm1
                0xF2, 0x48, 0x0F, 0x2A, 0xD3,       // 73: cvtsi2sd xmm2, rbx
                0xF2, 0x0F, 0x5E, 0xC2,             // 78: divsd xmm0, xmm2
                0xF2, 0x0F, 0x11, 0x05, 0x18, 0x00, 0x00, 0x00, // 82: movsd [rip + __grass_time_buf + 24], xmm0
                0xF2, 0x48, 0x0F, 0x2C, 0xF0,       // 90: cvttsd2si rsi, xmm0
                0x48, 0x8D, 0x3D, 0x20, 0x00, 0x00, 0x00, // 95: lea rdi, [rip + __grass_time_buf + 32]
                0x48, 0x89, 0xF0,                   // 102: mov rax, rsi
                0x48, 0x8D, 0x5C, 0x24, 0x1E,       // 105: lea rbx, [rsp + 30]
                0xC6, 0x03, 0x00,                   // 110: mov byte ptr [rbx], 0
                0x31, 0xD2,                         // 113: xor edx, edx
                0x48, 0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00, // 115: mov rcx, 10
                0x48, 0xF7, 0xF1,                   // 122: div rcx
                0x80, 0xC2, 0x30,                   // 125: add dl, 48
                0x48, 0xFF, 0xCB,                   // 128: dec rbx
                0x88, 0x13,                         // 131: mov [rbx], dl
                0x48, 0x85, 0xC0,                   // 133: test rax, rax
                0x75, 0xE7,                         // 136: jnz -25 (to 113)
                0x8A, 0x03,                         // 138: mov al, [rbx]
                0x48, 0xFF, 0xC3,                   // 140: inc rbx
                0x88, 0x07,                         // 143: mov [rdi], al
                0x48, 0xFF, 0xC7,                   // 145: inc rdi
                0x84, 0xC0,                         // 148: test al, al
                0x75, 0xF2,                         // 150: jnz -14 (to 138)
                0x48, 0xFF, 0xCF,                   // 152: dec rdi
                0xC6, 0x07, 0x2E,                   // 155: mov byte ptr [rdi], '.' (0x2E)
                0x48, 0xFF, 0xC7,                   // 158: inc rdi
                0x48, 0xC7, 0xC0, 0x64, 0x00, 0x00, 0x00, // 161: mov rax, 100
                0xF2, 0x48, 0x0F, 0x2A, 0xC8,       // 168: cvtsi2sd xmm1, rax
                0xF2, 0x48, 0x0F, 0x2A, 0xD6,       // 173: cvtsi2sd xmm2, rsi
                0xF2, 0x0F, 0x10, 0xD8,             // 178: movsd xmm3, xmm0
                0xF2, 0x0F, 0x5C, 0xDA,             // 182: subsd xmm3, xmm2
                0xF2, 0x0F, 0x59, 0xD9,             // 186: mulsd xmm3, xmm1
                0xF2, 0x48, 0x0F, 0x2C, 0xC3,       // 190: cvttsd2si rax, xmm3
                0x48, 0x85, 0xC0,                   // 195: test rax, rax
                0x79, 0x03,                         // 198: jns +3
                0x48, 0xF7, 0xD8,                   // 200: neg rax
                0x31, 0xD2,                         // 203: xor edx, edx
                0x48, 0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00, // 205: mov rcx, 10
                0x48, 0xF7, 0xF1,                   // 212: div rcx
                0x04, 0x30,                         // 215: add al, 48
                0x80, 0xC2, 0x30,                   // 217: add dl, 48
                0x88, 0x07,                         // 220: mov [rdi], al
                0x88, 0x57, 0x01,                   // 222: mov [rdi + 1], dl
                0xC6, 0x47, 0x02, 0x20,             // 225: mov byte ptr [rdi + 2], ' '
                0xC6, 0x47, 0x03, 0x6D,             // 229: mov byte ptr [rdi + 3], 'm'
                0xC6, 0x47, 0x04, 0x73,             // 233: mov byte ptr [rdi + 4], 's'
                0xC6, 0x47, 0x05, 0x00,             // 237: mov byte ptr [rdi + 5], 0
                0x48, 0x89, 0xF0,                   // 241: mov rax, rsi
                0x48, 0x83, 0xC4, 0x30,             // 244: add rsp, 48
                0x5E,                               // 248: pop rsi
                0x5F,                               // 249: pop rdi
                0x5B,                               // 250: pop rbx
                0xC3                                // 251: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 10, "__grass_time_buf", RelocType::Rel32, 12 }, // +16
                { SectionKind::Text, 15, "QueryPerformanceCounter", RelocType::Rel32, -4 },
                { SectionKind::Text, 22, "__grass_time_buf", RelocType::Rel32, 12 }, // +16
                { SectionKind::Text, 29, "__grass_time_buf", RelocType::Rel32, 4 },  // +8
                { SectionKind::Text, 36, "__grass_time_buf", RelocType::Rel32, -4 }, // +0
                { SectionKind::Text, 86, "__grass_time_buf", RelocType::Rel32, 20 }, // +24
                { SectionKind::Text, 98, "__grass_time_buf", RelocType::Rel32, 28 }  // +32
            };
            ctx.append_function("stop_time", code, rels);
        }

        if (ctx.needed_symbols.count("conclus_time") > 0 && ctx.defined_symbols.count("conclus_time") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x8D, 0x05, 0x20, 0x00, 0x00, 0x00, // 0: lea rax, [rip + __grass_time_buf + 32]
                0xC3                                      // 7: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 3, "__grass_time_buf", RelocType::Rel32, 28 } // +32
            };
            ctx.append_function("conclus_time", code, rels);
        }

        if (ctx.needed_symbols.count("now_ms") > 0 && ctx.defined_symbols.count("now_ms") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 4: call GetTickCount64
                0x48, 0x83, 0xC4, 0x28,             // 9: add rsp, 40
                0xC3                                // 13: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 5, "GetTickCount64", RelocType::Rel32, -4 }
            };
            ctx.append_function("now_ms", code, rels);
        }

        if (ctx.needed_symbols.count("sleep_ms") > 0 && ctx.defined_symbols.count("sleep_ms") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 4: call Sleep
                0x48, 0x83, 0xC4, 0x28,             // 9: add rsp, 40
                0xC3                                // 13: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 5, "Sleep", RelocType::Rel32, -4 }
            };
            ctx.append_function("sleep_ms", code, rels);
        }

        // 16. Math intrinsics: sqrt, discor, abs/fabs, floor, ceil, sin, cos, pow
        if ((ctx.needed_symbols.count("sqrt") > 0 && ctx.defined_symbols.count("sqrt") == 0) ||
            (ctx.needed_symbols.count("discor") > 0 && ctx.defined_symbols.count("discor") == 0)) {
            std::vector<uint8_t> code = {
                0xF2, 0x0F, 0x51, 0xC0,             // sqrtsd xmm0, xmm0
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            if (ctx.needed_symbols.count("sqrt") > 0 && ctx.defined_symbols.count("sqrt") == 0) ctx.append_function("sqrt", code, rels);
            if (ctx.needed_symbols.count("discor") > 0 && ctx.defined_symbols.count("discor") == 0) ctx.append_function("discor", code, rels);
        }

        if ((ctx.needed_symbols.count("abs") > 0 && ctx.defined_symbols.count("abs") == 0) ||
            (ctx.needed_symbols.count("fabs") > 0 && ctx.defined_symbols.count("fabs") == 0)) {
            std::vector<uint8_t> code = {
                0x66, 0x0F, 0x76, 0xC9,             // pcmpeqd xmm1, xmm1
                0x66, 0x0F, 0x73, 0xD1, 0x01,       // psrlq xmm1, 1
                0x66, 0x0F, 0xDB, 0xC1,             // pand xmm0, xmm1
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            if (ctx.needed_symbols.count("abs") > 0) ctx.append_function("abs", code, rels);
            if (ctx.needed_symbols.count("fabs") > 0) ctx.append_function("fabs", code, rels);
        }

        if (ctx.needed_symbols.count("floor") > 0 && ctx.defined_symbols.count("floor") == 0) {
            std::vector<uint8_t> code = {
                0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x01, // roundsd xmm0, xmm0, 1 (round down)
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("floor", code, rels);
        }

        if (ctx.needed_symbols.count("ceil") > 0 && ctx.defined_symbols.count("ceil") == 0) {
            std::vector<uint8_t> code = {
                0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x02, // roundsd xmm0, xmm0, 2 (round up)
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("ceil", code, rels);
        }

        if (ctx.needed_symbols.count("sin") > 0 && ctx.defined_symbols.count("sin") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x18,             // sub rsp, 24
                0xF2, 0x0F, 0x11, 0x04, 0x24,       // movsd [rsp], xmm0
                0xDD, 0x04, 0x24,                   // fld qword ptr [rsp]
                0xD9, 0xFE,                         // fsin
                0xDD, 0x1C, 0x24,                   // fstp qword ptr [rsp]
                0xF2, 0x0F, 0x10, 0x04, 0x24,       // movsd xmm0, [rsp]
                0x48, 0x83, 0xC4, 0x18,             // add rsp, 24
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("sin", code, rels);
        }

        if (ctx.needed_symbols.count("cos") > 0 && ctx.defined_symbols.count("cos") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x18,             // sub rsp, 24
                0xF2, 0x0F, 0x11, 0x04, 0x24,       // movsd [rsp], xmm0
                0xDD, 0x04, 0x24,                   // fld qword ptr [rsp]
                0xD9, 0xFF,                         // fcos
                0xDD, 0x1C, 0x24,                   // fstp qword ptr [rsp]
                0xF2, 0x0F, 0x10, 0x04, 0x24,       // movsd xmm0, [rsp]
                0x48, 0x83, 0xC4, 0x18,             // add rsp, 24
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("cos", code, rels);
        }

        if (ctx.needed_symbols.count("pow") > 0 && ctx.defined_symbols.count("pow") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x18,             // sub rsp, 24
                0xF2, 0x0F, 0x11, 0x04, 0x24,       // movsd [rsp], xmm0 (base)
                0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, // movsd [rsp+8], xmm1 (exp)
                0xDD, 0x44, 0x24, 0x08,             // fld qword ptr [rsp+8]
                0xDD, 0x04, 0x24,                   // fld qword ptr [rsp]
                0xD9, 0xF1,                         // fyl2x
                0xD9, 0xC0,                         // fld st(0)
                0xD9, 0xFC,                         // frndint
                0xDC, 0xE9,                         // fsubr st(1), st(0)
                0xD9, 0xC9,                         // fxch st(1)
                0xD9, 0xF0,                         // f2xm1
                0xD9, 0xE8,                         // fld1
                0xDE, 0xC1,                         // faddp st(1), st
                0xD9, 0xFD,                         // fscale
                0xDD, 0xD9,                         // fstp st(1)
                0xDD, 0x1C, 0x24,                   // fstp qword ptr [rsp]
                0xF2, 0x0F, 0x10, 0x04, 0x24,       // movsd xmm0, [rsp]
                0x48, 0x83, 0xC4, 0x18,             // add rsp, 24
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            ctx.append_function("pow", code, rels);
        }

        // 17. grass_install_runtime_crash_handler & __grass_veh_handler
        if (ctx.needed_symbols.count("grass_install_runtime_crash_handler") > 0 &&
            ctx.defined_symbols.count("grass_install_runtime_crash_handler") == 0) {
            ctx.add_rodata_str("__grass_crash_msg",
                "\r\n================================================================================\r\n"
                "GRASS NATIVE RUNTIME CRASH DETECTED\r\n"
                "Exception Code: 0xC0000005\r\n"
                "================================================================================\r\n"
                "Grass Native Stack Trace:\r\n"
                "  #0  in <native code>\r\n"
                "================================================================================\r\n\r\n");

            std::vector<uint8_t> veh_code;
            auto vemit = [&](std::initializer_list<uint8_t> b) { veh_code.insert(veh_code.end(), b); };
            vemit({0x48, 0x85, 0xC9});                                     // test rcx, rcx
            vemit({0x74, 0x00}); size_t jz_p1 = veh_code.size() - 1;
            vemit({0x48, 0x8B, 0x01});                                     // mov rax, [rcx]
            vemit({0x48, 0x85, 0xC0});                                     // test rax, rax
            vemit({0x74, 0x00}); size_t jz_p2 = veh_code.size() - 1;
            vemit({0x48, 0x83, 0xEC, 0x38});                               // sub rsp, 56
            vemit({0xB9, 0xF4, 0xFF, 0xFF, 0xFF});                         // mov ecx, -12 (STD_ERROR_HANDLE)
            size_t rel_std = veh_code.size() + 1;
            vemit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call GetStdHandle
            vemit({0x48, 0x85, 0xC0});                                     // test rax, rax
            vemit({0x74, 0x00}); size_t jz_h = veh_code.size() - 1;
            vemit({0x48, 0x83, 0xF8, 0xFF});                               // cmp rax, -1
            vemit({0x74, 0x00}); size_t je_h = veh_code.size() - 1;
            vemit({0x48, 0x89, 0xC1});                                     // mov rcx, rax
            size_t rel_msg = veh_code.size() + 3;
            vemit({0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00});             // lea rdx, [rip + __grass_crash_msg]
            vemit({0x41, 0xB8, 0xDA, 0x00, 0x00, 0x00});                   // mov r8d, 218
            vemit({0x4C, 0x8D, 0x4C, 0x24, 0x28});                         // lea r9, [rsp + 40]
            vemit({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 32], 0
            size_t rel_wf = veh_code.size() + 1;
            vemit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call WriteFile
            size_t do_exit = veh_code.size();
            veh_code[jz_h] = static_cast<uint8_t>(do_exit - (jz_h + 1));
            veh_code[je_h] = static_cast<uint8_t>(do_exit - (je_h + 1));
            vemit({0xB9, 0x01, 0x00, 0x00, 0x00});                         // mov ecx, 1
            size_t rel_exit = veh_code.size() + 1;
            vemit({0xE8, 0x00, 0x00, 0x00, 0x00});                         // call ExitProcess
            size_t pass_lbl = veh_code.size();
            veh_code[jz_p1] = static_cast<uint8_t>(pass_lbl - (jz_p1 + 1));
            veh_code[jz_p2] = static_cast<uint8_t>(pass_lbl - (jz_p2 + 1));
            vemit({0x31, 0xC0, 0xC3});                                     // xor eax, eax; ret
            std::vector<RelocEntry> veh_rels = {
                { SectionKind::Text, static_cast<uint32_t>(rel_std), "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_msg), "__grass_crash_msg", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_wf), "WriteFile", RelocType::Rel32, -4 },
                { SectionKind::Text, static_cast<uint32_t>(rel_exit), "ExitProcess", RelocType::Rel32, -4 }
            };
            ctx.append_function("__grass_veh_handler", veh_code, veh_rels);

            std::vector<uint8_t> inst_code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xB9, 0x01, 0x00, 0x00, 0x00,       // 4: mov ecx, 1
                0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00, // 9: lea rdx, [rip + __grass_veh_handler]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 16: call AddVectoredExceptionHandler
                0x48, 0x83, 0xC4, 0x28,             // 21: add rsp, 40
                0xC3                                // 25: ret
            };
            std::vector<RelocEntry> inst_rels = {
                { SectionKind::Text, 12, "__grass_veh_handler", RelocType::Rel32, -4 },
                { SectionKind::Text, 17, "AddVectoredExceptionHandler", RelocType::Rel32, -4 }
            };
            ctx.append_function("grass_install_runtime_crash_handler", inst_code, inst_rels);
        }


    }

} // namespace grass::backend

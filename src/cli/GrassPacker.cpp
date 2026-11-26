#include "cli/GrassPacker.h"
#include "vm/GrassVM.h"
#include "runtime/GrassThread.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;
namespace fs = std::filesystem;

static const char MAGIC_FOOTER[8] = {'G','R','A','S','S','E','X','E'};

#if defined(_WIN32)
bool CheckAndRunEmbeddedBytecode() {
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    HANDLE hFile = CreateFileA(self_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER total_size;
    if (!GetFileSizeEx(hFile, &total_size) || total_size.QuadPart < 12) {
        CloseHandle(hFile);
        return false;
    }

    LARGE_INTEGER seek_pos;
    seek_pos.QuadPart = total_size.QuadPart - 12;
    SetFilePointerEx(hFile, seek_pos, NULL, FILE_BEGIN);

    uint8_t footer[12];
    DWORD bytes_read = 0;
    if (!ReadFile(hFile, footer, 12, &bytes_read, NULL) || bytes_read != 12) {
        CloseHandle(hFile);
        return false;
    }

    if (memcmp(footer + 4, MAGIC_FOOTER, 8) != 0) {
        CloseHandle(hFile);
        return false;
    }

    uint32_t bc_size = 0;
    memcpy(&bc_size, footer, 4);
    if (bc_size == 0 || (LONGLONG)(bc_size + 12) > total_size.QuadPart) {
        CloseHandle(hFile);
        return false;
    }

    seek_pos.QuadPart = total_size.QuadPart - 12 - bc_size;
    SetFilePointerEx(hFile, seek_pos, NULL, FILE_BEGIN);

    vector<uint8_t> bytecode(bc_size);
    if (!ReadFile(hFile, bytecode.data(), bc_size, &bytes_read, NULL) || bytes_read != bc_size) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    g_vm_arena.reset();
    GrassVM vm(bytecode);
    vm.execute();
    GrassThread::shutdown();
    return true;
}

// Вычисление чистого размера PE-ядра без чтения всего файла
static LONGLONG GetCleanStubSize(const char* path, LONGLONG& out_total_size) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return -1;

    LARGE_INTEGER fsize;
    if (!GetFileSizeEx(hFile, &fsize)) {
        CloseHandle(hFile);
        return -1;
    }
    out_total_size = fsize.QuadPart;
    LONGLONG stub_size = fsize.QuadPart;

    if (stub_size >= 12) {
        LARGE_INTEGER pos;
        pos.QuadPart = stub_size - 12;
        SetFilePointerEx(hFile, pos, NULL, FILE_BEGIN);
        uint8_t footer[12];
        DWORD read_b = 0;
        if (ReadFile(hFile, footer, 12, &read_b, NULL) && read_b == 12) {
            uint32_t old_bc = 0;
            memcpy(&old_bc, footer, 4);
            if (memcmp(footer + 4, MAGIC_FOOTER, 8) == 0 && (LONGLONG)(old_bc + 12) < stub_size) {
                stub_size -= (old_bc + 12);
            }
        }
    }
    CloseHandle(hFile);
    return stub_size;
}

bool BuildStandaloneExe(const string& output_exe, const vector<uint8_t>& bytecode) {
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    LONGLONG self_total = 0;
    LONGLONG stub_size = GetCleanStubSize(self_path, self_total);
    if (stub_size <= 0) return false;

    uint32_t bc_size = (uint32_t)bytecode.size();
    uint8_t footer[12];
    memcpy(footer, &bc_size, 4);
    memcpy(footer + 4, MAGIC_FOOTER, 8);

    HANDLE hSrc = CreateFileA(self_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSrc == INVALID_HANDLE_VALUE) return false;
    vector<char> pe_stub((size_t)stub_size);
    DWORD r = 0;
    ReadFile(hSrc, pe_stub.data(), (DWORD)stub_size, &r, NULL);
    CloseHandle(hSrc);

    HANDLE hDst = CreateFileA(output_exe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDst == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    WriteFile(hDst, pe_stub.data(), (DWORD)pe_stub.size(), &w, NULL);
    WriteFile(hDst, bytecode.data(), bc_size, &w, NULL);
    WriteFile(hDst, footer, 12, &w, NULL);
    CloseHandle(hDst);
    return true;
}
#else
// POSIX ELF-PACKER ДЛЯ LINUX
static int64_t GetCleanStubSize(const char* path, int64_t& out_total_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    int64_t fsize = (int64_t)ftell(f);
    out_total_size = fsize;
    int64_t stub_size = fsize;
    if (stub_size >= 12) {
        fseek(f, stub_size - 12, SEEK_SET);
        uint8_t footer[12];
        if (fread(footer, 1, 12, f) == 12) {
            uint32_t old_bc = 0;
            memcpy(&old_bc, footer, 4);
            if (memcmp(footer + 4, MAGIC_FOOTER, 8) == 0 && (int64_t)(old_bc + 12) < stub_size) {
                stub_size -= (old_bc + 12);
            }
        }
    }
    fclose(f);
    return stub_size;
}

bool CheckAndRunEmbeddedBytecode() {
    char self_path[1024];
    ssize_t len = ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len <= 0) return false;
    self_path[len] = '\0';

    FILE* f = fopen(self_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    int64_t total_size = (int64_t)ftell(f);
    if (total_size < 12) { fclose(f); return false; }

    fseek(f, total_size - 12, SEEK_SET);
    uint8_t footer[12];
    if (fread(footer, 1, 12, f) != 12 || memcmp(footer + 4, MAGIC_FOOTER, 8) != 0) {
        fclose(f);
        return false;
    }

    uint32_t bc_size = 0;
    memcpy(&bc_size, footer, 4);
    if (bc_size == 0 || (int64_t)(bc_size + 12) > total_size) {
        fclose(f);
        return false;
    }

    fseek(f, total_size - 12 - bc_size, SEEK_SET);
    vector<uint8_t> bytecode(bc_size);
    if (fread(bytecode.data(), 1, bc_size, f) != bc_size) {
        fclose(f);
        return false;
    }
    fclose(f);

    g_vm_arena.reset();
    GrassVM vm(bytecode);
    vm.execute();
    GrassThread::shutdown();
    return true;
}

bool BuildStandaloneExe(const string& output_exe, const vector<uint8_t>& bytecode) {
    char self_path[1024];
    ssize_t len = ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len <= 0) return false;
    self_path[len] = '\0';

    int64_t self_total = 0;
    int64_t stub_size = GetCleanStubSize(self_path, self_total);
    if (stub_size <= 0) return false;

    uint32_t bc_size = (uint32_t)bytecode.size();
    uint8_t footer[12];
    memcpy(footer, &bc_size, 4);
    memcpy(footer + 4, MAGIC_FOOTER, 8);

    FILE* fSrc = fopen(self_path, "rb");
    if (!fSrc) return false;
    vector<char> stub((size_t)stub_size);
    if (fread(stub.data(), 1, (size_t)stub_size, fSrc) != (size_t)stub_size) {
        fclose(fSrc);
        return false;
    }
    fclose(fSrc);

    FILE* fDst = fopen(output_exe.c_str(), "wb");
    if (!fDst) return false;
    fwrite(stub.data(), 1, stub.size(), fDst);
    fwrite(bytecode.data(), 1, bc_size, fDst);
    fwrite(footer, 1, 12, fDst);
    fclose(fDst);

    try {
        fs::permissions(output_exe, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, fs::perm_options::replace);
    } catch (...) {}
    return true;
}
#endif

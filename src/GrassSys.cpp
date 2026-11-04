#include "GrassSys.h"
#include "GrassHAL.h"
#include <windows.h>
#include <thread>
#include <vector>

namespace GrassSys {

    bool clip_set(const std::string& text) {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::clipboard_set(text);
        }
        if (!OpenClipboard(NULL)) return false;
        EmptyClipboard();

        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (!hGlob) {
            CloseClipboard();
            return false;
        }

        memcpy(GlobalLock(hGlob), text.c_str(), text.size() + 1);
        GlobalUnlock(hGlob);

        SetClipboardData(CF_TEXT, hGlob);
        CloseClipboard();
        return true;
    }

    std::string clip_get() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::clipboard_get();
        }
        if (!OpenClipboard(NULL)) return "";
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (!hData) {
            CloseClipboard();
            return "";
        }

        char* pszText = static_cast<char*>(GlobalLock(hData));
        std::string result = pszText ? std::string(pszText) : "";
        GlobalUnlock(hData);
        CloseClipboard();
        return result;
    }

    int screen_w() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::g_software_fb.width;
        }
        return GetSystemMetrics(SM_CXSCREEN);
    }

    int screen_h() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::g_software_fb.height;
        }
        return GetSystemMetrics(SM_CYSCREEN);
    }

    std::string pc_name() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_system_name();
        }
        char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {0};
        DWORD size = sizeof(buffer);
        if (GetComputerNameA(buffer, &size)) {
            return std::string(buffer);
        }
        return "Unknown-PC";
    }

    std::string user_name() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_user_name();
        }
        char buffer[256] = {0};
        DWORD size = sizeof(buffer);
        if (GetUserNameA(buffer, &size)) {
            return std::string(buffer);
        }
        return "Unknown-User";
    }

    std::string get_env(const std::string& key) {
        if (GrassHAL::is_freestanding()) {
            return "";
        }
        char buffer[4096] = {0};
        DWORD res = GetEnvironmentVariableA(key.c_str(), buffer, sizeof(buffer));
        if (res > 0 && res < sizeof(buffer)) {
            return std::string(buffer);
        }
        return "";
    }

    int cpu_cores() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_cpu_cores();
        }
        unsigned int cores = std::thread::hardware_concurrency();
        return (cores > 0) ? (int)cores : 1;
    }

    double ram_total_mb() {
        if (GrassHAL::is_freestanding()) {
            return (double)(GrassHAL::get_total_ram_bytes() / (1024 * 1024));
        }
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            return (double)(memInfo.ullTotalPhys / (1024 * 1024));
        }
        return 0.0;
    }

    double ram_free_mb() {
        if (GrassHAL::is_freestanding()) {
            return (double)(GrassHAL::get_free_ram_bytes() / (1024 * 1024));
        }
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            return (double)(memInfo.ullAvailPhys / (1024 * 1024));
        }
        return 0.0;
    }

    double uptime_sec() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_elapsed_ms(0) / 1000.0;
        }
        return (double)(GetTickCount64() / 1000);
    }

} // namespace GrassSys

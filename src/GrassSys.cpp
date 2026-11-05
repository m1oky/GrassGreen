#include "GrassSys.h"
#include "GrassHAL.h"
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

namespace GrassSys {

    bool clip_set(const std::string& text) {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::clipboard_set(text);
        }
#if defined(_WIN32)
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
#else
        FILE* pipe = popen("xclip -selection clipboard 2>/dev/null || wl-copy 2>/dev/null", "w");
        if (pipe) {
            fwrite(text.data(), 1, text.size(), pipe);
            pclose(pipe);
            return true;
        }
        return false;
#endif
    }

    std::string clip_get() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::clipboard_get();
        }
#if defined(_WIN32)
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
#else
        FILE* pipe = popen("xclip -selection clipboard -o 2>/dev/null || wl-paste 2>/dev/null", "r");
        if (pipe) {
            char buffer[1024];
            std::string result;
            while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
            pclose(pipe);
            return result;
        }
        return "";
#endif
    }

    int screen_w() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::g_software_fb.width;
        }
#if defined(_WIN32)
        return GetSystemMetrics(SM_CXSCREEN);
#else
        return 1920;
#endif
    }

    int screen_h() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::g_software_fb.height;
        }
#if defined(_WIN32)
        return GetSystemMetrics(SM_CYSCREEN);
#else
        return 1080;
#endif
    }

    std::string pc_name() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_system_name();
        }
#if defined(_WIN32)
        char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {0};
        DWORD size = sizeof(buffer);
        if (GetComputerNameA(buffer, &size)) {
            return std::string(buffer);
        }
        return "Unknown-PC";
#else
        char host[256] = {0};
        if (gethostname(host, sizeof(host)) == 0) return std::string(host);
        return "Linux-PC";
#endif
    }

    std::string user_name() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_user_name();
        }
#if defined(_WIN32)
        char buffer[256] = {0};
        DWORD size = sizeof(buffer);
        if (GetUserNameA(buffer, &size)) {
            return std::string(buffer);
        }
        return "Unknown-User";
#else
        const char* u = getenv("USER");
        if (u) return std::string(u);
        return "linux-user";
#endif
    }

    std::string get_env(const std::string& key) {
        if (GrassHAL::is_freestanding()) {
            return "";
        }
#if defined(_WIN32)
        char buffer[4096] = {0};
        DWORD res = GetEnvironmentVariableA(key.c_str(), buffer, sizeof(buffer));
        if (res > 0 && res < sizeof(buffer)) {
            return std::string(buffer);
        }
        return "";
#else
        const char* val = getenv(key.c_str());
        return val ? std::string(val) : "";
#endif
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
#if defined(_WIN32)
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            return (double)(memInfo.ullTotalPhys / (1024 * 1024));
        }
        return 0.0;
#else
        struct sysinfo si;
        if (sysinfo(&si) == 0) return (double)((si.totalram * si.mem_unit) / (1024 * 1024));
        return 16384.0;
#endif
    }

    double ram_free_mb() {
        if (GrassHAL::is_freestanding()) {
            return (double)(GrassHAL::get_free_ram_bytes() / (1024 * 1024));
        }
#if defined(_WIN32)
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            return (double)(memInfo.ullAvailPhys / (1024 * 1024));
        }
        return 0.0;
#else
        struct sysinfo si;
        if (sysinfo(&si) == 0) return (double)((si.freeram * si.mem_unit) / (1024 * 1024));
        return 8192.0;
#endif
    }

    double uptime_sec() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_elapsed_ms(0) / 1000.0;
        }
#if defined(_WIN32)
        return (double)(GetTickCount64() / 1000);
#else
        struct sysinfo si;
        if (sysinfo(&si) == 0) return (double)si.uptime;
        return 0.0;
#endif
    }

} // namespace GrassSys

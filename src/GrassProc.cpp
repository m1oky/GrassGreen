#include "GrassProc.h"
#include "GrassHAL.h"
#include <algorithm>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

static std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

int64_t GrassProc::find(const std::string& name) {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::proc_find(name);
    }
#if defined(_WIN32)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);
    std::string target = to_lower_str(name);
    int64_t found_pid = -1;

    if (Process32First(snapshot, &entry)) {
        do {
            if (to_lower_str(entry.szExeFile) == target) {
                found_pid = static_cast<int64_t>(entry.th32ProcessID);
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found_pid;
#else
    return GrassHAL::proc_find(name);
#endif
}

bool GrassProc::kill(int64_t pid) {
    if (pid <= 0) return false;
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::proc_kill(pid);
    }
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    BOOL res = TerminateProcess(h, 1);
    CloseHandle(h);
    return res != FALSE;
#else
    return GrassHAL::proc_kill(pid);
#endif
}

int64_t GrassProc::mem(int64_t pid) {
    if (pid <= 0) return 0;
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::proc_mem(pid);
    }
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if (!h) return 0;

    PROCESS_MEMORY_COUNTERS_EX pmc;
    int64_t mb = 0;
    if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        mb = static_cast<int64_t>(pmc.WorkingSetSize / (1024 * 1024));
    }
    CloseHandle(h);
    return mb;
#else
    return GrassHAL::proc_mem(pid);
#endif
}

bool GrassProc::exists(int64_t pid) {
    if (pid <= 0) return false;
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::proc_exists(pid);
    }
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    bool ok = (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE);
    CloseHandle(h);
    return ok;
#else
    return GrassHAL::proc_exists(pid);
#endif
}

std::string GrassProc::list() {
    if (GrassHAL::is_freestanding()) {
        return GrassHAL::proc_list();
    }
#if defined(_WIN32)
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return "";

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);
    std::ostringstream ss;

    if (Process32First(snapshot, &entry)) {
        do {
            ss << entry.th32ProcessID << "\t" << entry.szExeFile << "\n";
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return ss.str();
#else
    return GrassHAL::proc_list();
#endif
}

#include "GrassTime.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace GrassTime {
    static auto timer_start = std::chrono::high_resolution_clock::now();
    static double last_elapsed_ms = 0.0;
    static bool is_running = false;

    void start_time() {
        timer_start = std::chrono::high_resolution_clock::now();
        is_running = true;
        last_elapsed_ms = 0.0;
    }

    double stop_time() {
        if (is_running) {
            auto now = std::chrono::high_resolution_clock::now();
            last_elapsed_ms = std::chrono::duration<double, std::milli>(now - timer_start).count();
            is_running = false;
        }
        return last_elapsed_ms;
    }

    std::string conclus_time() {
        if (is_running) {
            stop_time();
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << last_elapsed_ms << " ms";
        return oss.str();
    }

    double now_ms() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return (double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    double timestamp() {
        return (double)std::time(nullptr);
    }

    std::string get_date() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << st.wYear << "-"
            << std::setw(2) << st.wMonth << "-"
            << std::setw(2) << st.wDay;
        return oss.str();
    }

    std::string get_time_str() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << st.wHour << ":"
            << std::setw(2) << st.wMinute << ":"
            << std::setw(2) << st.wSecond;
        return oss.str();
    }

    void sleep_ms(int ms) {
        if (ms > 0) Sleep(ms);
    }
}
#include "GrassTime.h"
#include "GrassHAL.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace GrassTime {
    static auto timer_start = std::chrono::high_resolution_clock::now();
    static uint64_t freestanding_timer_start = 0;
    static double last_elapsed_ms = 0.0;
    static bool is_running = false;

    void start_time() {
        if (GrassHAL::is_freestanding()) {
            freestanding_timer_start = GrassHAL::read_tsc();
        } else {
            timer_start = std::chrono::high_resolution_clock::now();
        }
        is_running = true;
        last_elapsed_ms = 0.0;
    }

    double stop_time() {
        if (is_running) {
            if (GrassHAL::is_freestanding()) {
                last_elapsed_ms = GrassHAL::get_elapsed_ms(freestanding_timer_start);
            } else {
                auto now = std::chrono::high_resolution_clock::now();
                last_elapsed_ms = std::chrono::duration<double, std::milli>(now - timer_start).count();
            }
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
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_elapsed_ms(0);
        }
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return (double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

    double timestamp() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_elapsed_ms(0) / 1000.0;
        }
        return (double)std::time(nullptr);
    }

    std::string get_date() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_rtc_date();
        }
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << st.wYear << "-"
            << std::setw(2) << st.wMonth << "-"
            << std::setw(2) << st.wDay;
        return oss.str();
    }

    std::string get_time_str() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_rtc_time();
        }
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << st.wHour << ":"
            << std::setw(2) << st.wMinute << ":"
            << std::setw(2) << st.wSecond;
        return oss.str();
    }

    void sleep_ms(int ms) {
        if (ms <= 0) return;
        if (GrassHAL::is_freestanding()) {
            GrassHAL::delay_ms((uint32_t)ms);
        } else {
            Sleep(ms);
        }
    }
}

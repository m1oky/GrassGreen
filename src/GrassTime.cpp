#include "GrassTime.h"
#include "GrassHAL.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

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
#if defined(_WIN32)
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << st.wYear << "-"
            << std::setw(2) << st.wMonth << "-"
            << std::setw(2) << st.wDay;
        return oss.str();
#else
        std::time_t t = std::time(nullptr);
        std::tm tm_buf;
        localtime_r(&t, &tm_buf);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << (tm_buf.tm_year + 1900) << "-"
            << std::setw(2) << (tm_buf.tm_mon + 1) << "-"
            << std::setw(2) << tm_buf.tm_mday;
        return oss.str();
#endif
    }

    std::string get_time_str() {
        if (GrassHAL::is_freestanding()) {
            return GrassHAL::get_rtc_time();
        }
#if defined(_WIN32)
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << st.wHour << ":"
            << std::setw(2) << st.wMinute << ":"
            << std::setw(2) << st.wSecond;
        return oss.str();
#else
        std::time_t t = std::time(nullptr);
        std::tm tm_buf;
        localtime_r(&t, &tm_buf);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
            << std::setw(2) << tm_buf.tm_min << ":"
            << std::setw(2) << tm_buf.tm_sec;
        return oss.str();
#endif
    }

    void sleep_ms(int ms) {
        if (ms <= 0) return;
        if (GrassHAL::is_freestanding()) {
            GrassHAL::delay_ms((uint32_t)ms);
        } else {
#if defined(_WIN32)
            Sleep(ms);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
        }
    }
}

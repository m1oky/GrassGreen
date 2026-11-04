// =============================================================================
//  GrassGreen 4.0 — Multithreading & Async Task System Implementation
//  Файл создан Архитектором. Реализация — Кодер Ядра («Занятой-Core»).
// =============================================================================
#include "GrassThread.h"
#include "GrassHAL.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <chrono>

namespace GrassThread {

    struct ThreadItem {
        int64_t id;
        std::thread th;
        std::atomic<bool> completed{false};
        GrassValue result{GrassValue::Num(0.0)};
        bool detached{false};
    };

    static std::mutex g_mgr_mutex;
    static std::unordered_map<int64_t, std::shared_ptr<ThreadItem>> g_threads;
    static std::atomic<int64_t> g_next_thread_id{1};

    static std::mutex g_mutex_mgr_lock;
    static std::unordered_map<int64_t, std::shared_ptr<std::mutex>> g_mutexes;
    static std::atomic<int64_t> g_next_mutex_id{1};

    void initialize() {
        std::lock_guard<std::mutex> lock1(g_mgr_mutex);
        std::lock_guard<std::mutex> lock2(g_mutex_mgr_lock);
        g_threads.clear();
        g_mutexes.clear();
    }

    void shutdown() {
        std::vector<std::shared_ptr<ThreadItem>> to_join;
        std::vector<std::shared_ptr<ThreadItem>> to_wait_detached;
        {
            std::lock_guard<std::mutex> lock(g_mgr_mutex);
            for (auto& kv : g_threads) {
                if (!kv.second->detached && kv.second->th.joinable()) {
                    to_join.push_back(kv.second);
                } else if (kv.second->detached && !kv.second->completed) {
                    to_wait_detached.push_back(kv.second);
                }
            }
        }
        for (auto& item : to_join) {
            if (item->th.joinable()) {
                item->th.join();
            }
        }
        for (auto& item : to_wait_detached) {
            int limit = 100;
            while (!item->completed && limit-- > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_mgr_mutex);
            g_threads.clear();
        }
    }

    int64_t create_thread(std::function<GrassValue()> worker, bool detached) {
        if (GrassHAL::is_freestanding()) {
            // Freestanding режим: кооперативное выполнение без поддержки аппаратных потоков
            int64_t tid = g_next_thread_id++;
            auto item = std::make_shared<ThreadItem>();
            item->id = tid;
            item->detached = detached;
            item->result = worker();
            item->completed = true;

            std::lock_guard<std::mutex> lock(g_mgr_mutex);
            g_threads[tid] = item;
            return tid;
        }

        int64_t tid = g_next_thread_id++;
        auto item = std::make_shared<ThreadItem>();
        item->id = tid;
        item->detached = detached;

        {
            std::lock_guard<std::mutex> lock(g_mgr_mutex);
            g_threads[tid] = item;
        }

        item->th = std::thread([item, worker]() {
            try {
                item->result = worker();
            } catch (...) {
                item->result = GrassValue::Num(-1.0);
            }
            item->completed = true;
        });

        if (detached) {
            item->th.detach();
        }

        return tid;
    }

    GrassValue join_thread(int64_t thread_id) {
        std::shared_ptr<ThreadItem> item;
        {
            std::lock_guard<std::mutex> lock(g_mgr_mutex);
            auto it = g_threads.find(thread_id);
            if (it == g_threads.end()) {
                return GrassValue::Num(0.0);
            }
            item = it->second;
        }

        if (item->th.joinable()) {
            item->th.join();
        } else if (item->detached && !item->completed) {
            while (!item->completed) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        return item->result;
    }

    bool detach_thread(int64_t thread_id) {
        std::lock_guard<std::mutex> lock(g_mgr_mutex);
        auto it = g_threads.find(thread_id);
        if (it != g_threads.end() && it->second->th.joinable()) {
            it->second->th.detach();
            it->second->detached = true;
            return true;
        }
        return false;
    }

    int64_t get_current_thread_id() {
        auto id = std::this_thread::get_id();
        return (int64_t)std::hash<std::thread::id>{}(id);
    }

    void sleep_ms(uint32_t ms) {
        if (GrassHAL::is_freestanding()) {
            GrassHAL::delay_ms(ms);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }

    // ── Мьютексы ──
    int64_t mutex_create() {
        int64_t mid = g_next_mutex_id++;
        auto m = std::make_shared<std::mutex>();
        std::lock_guard<std::mutex> lock(g_mutex_mgr_lock);
        g_mutexes[mid] = m;
        return mid;
    }

    bool mutex_lock(int64_t mutex_id) {
        std::shared_ptr<std::mutex> m;
        {
            std::lock_guard<std::mutex> lock(g_mutex_mgr_lock);
            auto it = g_mutexes.find(mutex_id);
            if (it == g_mutexes.end()) return false;
            m = it->second;
        }
        m->lock();
        return true;
    }

    bool mutex_unlock(int64_t mutex_id) {
        std::shared_ptr<std::mutex> m;
        {
            std::lock_guard<std::mutex> lock(g_mutex_mgr_lock);
            auto it = g_mutexes.find(mutex_id);
            if (it == g_mutexes.end()) return false;
            m = it->second;
        }
        m->unlock();
        return true;
    }

    bool mutex_destroy(int64_t mutex_id) {
        std::lock_guard<std::mutex> lock(g_mutex_mgr_lock);
        return g_mutexes.erase(mutex_id) > 0;
    }

} // namespace GrassThread

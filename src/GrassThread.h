// =============================================================================
//  GrassGreen 4.0 — Multithreading & Async Task System (GrassThread)
//  Файл создан Архитектором. Реализация — Кодер Ядра («Занятой-Core»).
// =============================================================================
#pragma once

#include "GrassSyntax.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace GrassThread {

    // Инициализация подсистемы многопоточности
    void initialize();

    // Завершение работы и ожидание незавершенных фоновых потоков
    void shutdown();

    // Создание и запуск потока с функцией обратного вызова
    int64_t create_thread(std::function<GrassValue()> worker, bool detached = false);

    // Ожидание завершения потока и получение результата
    GrassValue join_thread(int64_t thread_id);

    // Отсоединение потока
    bool detach_thread(int64_t thread_id);

    // Получение идентификатора текущего системного потока
    int64_t get_current_thread_id();

    // Приостановка текущего потока на N миллисекунд
    void sleep_ms(uint32_t ms);

    // ── Мьютексы (Синхронизация) ──
    int64_t mutex_create();
    bool mutex_lock(int64_t mutex_id);
    bool mutex_unlock(int64_t mutex_id);
    bool mutex_destroy(int64_t mutex_id);

} // namespace GrassThread

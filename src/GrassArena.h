// =============================================================================
//  GrassGreen 4.0 — Arena Allocator (bump-pointer)
//  Файл создан Архитектором. Реализация — Кодер Ядра.
// =============================================================================
#ifndef GRASS_ARENA_H
#define GRASS_ARENA_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <windows.h>

class GrassArena {
private:
    uint8_t* base;
    size_t   capacity;
    size_t   offset;

public:
    // -------------------------------------------------------------------------
    //  Конструктор: резервирует initial байт через VirtualAlloc (или malloc).
    // -------------------------------------------------------------------------
    explicit GrassArena(size_t initial = 1024 * 1024)
        : base(nullptr), capacity(initial), offset(0)
    {
        base = (uint8_t*)VirtualAlloc(
            nullptr, initial, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!base) {
            // Fallback для совместимости (non-Windows, WSL, и т.д.)
            base = (uint8_t*)malloc(initial);
        }
    }

    // -------------------------------------------------------------------------
    //  Деструктор: освобождает память.
    // -------------------------------------------------------------------------
    ~GrassArena()
    {
        if (base) {
            VirtualFree(base, 0, MEM_RELEASE);
        }
        base     = nullptr;
        capacity = 0;
        offset   = 0;
    }

    // -------------------------------------------------------------------------
    //  Выделение выровненного блока bytes байт из арены.
    //  Возвращает nullptr, если арена исчерпана.
    // -------------------------------------------------------------------------
    void* alloc(size_t bytes, size_t align = 8)
    {
        size_t aligned = (offset + align - 1) & ~(align - 1);

        if (aligned + bytes > capacity) {
            // Расширяем арену удвоением, пока не хватит места
            size_t new_cap = capacity * 2;
            while (new_cap < aligned + bytes) {
                new_cap *= 2;
            }
            uint8_t* new_base = (uint8_t*)VirtualAlloc(
                nullptr, new_cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!new_base) {
                new_base = (uint8_t*)malloc(new_cap);
            }
            if (!new_base) return nullptr;

            memcpy(new_base, base, offset);
            VirtualFree(base, 0, MEM_RELEASE);

            base     = new_base;
            capacity = new_cap;
        }

        offset = aligned + bytes;
        return base + aligned;
    }

    // -------------------------------------------------------------------------
    //  Копирует строку src длиной len в арену и добавляет '\0'.
    //  Возвращает указатель на копию внутри арены.
    // -------------------------------------------------------------------------
    char* alloc_string(const char* src, size_t len)
    {
        char* dst = (char*)alloc(len + 1, 1);
        if (!dst) return nullptr;
        memcpy(dst, src, len);
        dst[len] = '\0';
        return dst;
    }

    // -------------------------------------------------------------------------
    //  Сбрасывает арену: offset = 0, память не освобождается.
    // -------------------------------------------------------------------------
    void reset()
    {
        offset = 0;
    }

    // -------------------------------------------------------------------------
    //  Возвращает количество использованных байт.
    // -------------------------------------------------------------------------
    size_t used() const
    {
        return offset;
    }

    // Запрет копирования
    GrassArena(const GrassArena&)            = delete;
    GrassArena& operator=(const GrassArena&) = delete;
};

#endif // GRASS_ARENA_H

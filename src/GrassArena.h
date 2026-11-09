// =============================================================================
//  GrassGreen 4.0 — Chained Chunk Arena Allocator (thread-safe, no UAF)
// =============================================================================
#ifndef GRASS_ARENA_H
#define GRASS_ARENA_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

class GrassArena {
private:
    struct Chunk {
        uint8_t* mem = nullptr;
        size_t cap = 0;
        size_t off = 0;
        Chunk* next = nullptr;
        bool is_virt = false;
    };

    Chunk* head = nullptr;
    Chunk* current = nullptr;
    size_t chunk_size;
    mutable std::mutex arena_mtx;

    Chunk* create_chunk(size_t cap) {
        Chunk* c = new Chunk();
        c->cap = cap;
        c->off = 0;
        c->next = nullptr;
#if defined(_WIN32)
        c->mem = (uint8_t*)VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (c->mem) {
            c->is_virt = true;
        } else {
            c->mem = (uint8_t*)malloc(cap);
            c->is_virt = false;
        }
#else
        c->mem = (uint8_t*)mmap(nullptr, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (c->mem != MAP_FAILED && c->mem != nullptr) {
            c->is_virt = true;
        } else {
            c->mem = (uint8_t*)malloc(cap);
            c->is_virt = false;
        }
#endif
        return c;
    }

    void free_chunk(Chunk* c) {
        if (!c) return;
        if (c->mem) {
#if defined(_WIN32)
            if (c->is_virt) {
                VirtualFree(c->mem, 0, MEM_RELEASE);
            } else {
                free(c->mem);
            }
#else
            if (c->is_virt) {
                munmap(c->mem, c->cap);
            } else {
                free(c->mem);
            }
#endif
        }
        delete c;
    }

public:
    explicit GrassArena(size_t initial = 4 * 1024 * 1024)
        : chunk_size(initial)
    {
        head = create_chunk(chunk_size);
        current = head;
    }

    ~GrassArena()
    {
        std::lock_guard<std::mutex> lock(arena_mtx);
        Chunk* c = head;
        while (c) {
            Chunk* nxt = c->next;
            free_chunk(c);
            c = nxt;
        }
        head = nullptr;
        current = nullptr;
    }

    void* alloc(size_t bytes, size_t align = 8)
    {
        std::lock_guard<std::mutex> lock(arena_mtx);
        if (!current) {
            head = create_chunk(std::max(chunk_size, bytes + align));
            current = head;
        }

        size_t aligned = (current->off + align - 1) & ~(align - 1);
        if (aligned + bytes > current->cap) {
            size_t next_sz = std::max(chunk_size, bytes + align);
            Chunk* nxt = create_chunk(next_sz);
            current->next = nxt;
            current = nxt;
            aligned = (current->off + align - 1) & ~(align - 1);
        }

        current->off = aligned + bytes;
        return current->mem + aligned;
    }

    char* alloc_string(const char* src, size_t len)
    {
        char* dst = (char*)alloc(len + 1, 1);
        if (!dst) return nullptr;
        memcpy(dst, src, len);
        dst[len] = '\0';
        return dst;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(arena_mtx);
        if (head) {
            Chunk* c = head->next;
            while (c) {
                Chunk* nxt = c->next;
                free_chunk(c);
                c = nxt;
            }
            head->next = nullptr;
            head->off = 0;
            current = head;
        }
    }

    size_t used() const
    {
        std::lock_guard<std::mutex> lock(arena_mtx);
        size_t total = 0;
        Chunk* c = head;
        while (c) {
            total += c->off;
            c = c->next;
        }
        return total;
    }

    GrassArena(const GrassArena&)            = delete;
    GrassArena& operator=(const GrassArena&) = delete;
};

#endif // GRASS_ARENA_H

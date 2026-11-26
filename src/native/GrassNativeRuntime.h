#pragma once

namespace grass::codegen {

    inline const char* kEmbeddedRuntimeC = R"(/* GrassGreen Standalone Native Runtime (Zero Dependencies) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
static LARGE_INTEGER g_perf_freq;
static LARGE_INTEGER g_perf_start;
static LARGE_INTEGER g_perf_stop;
static double g_elapsed_ms = 0.0;
static char g_conclus_buf[64];
static char g_date_buf[32];
static char g_time_buf[32];

void start_time(void) {
    QueryPerformanceFrequency(&g_perf_freq);
    QueryPerformanceCounter(&g_perf_start);
}

double stop_time(void) {
    QueryPerformanceCounter(&g_perf_stop);
    if (g_perf_freq.QuadPart != 0) {
        g_elapsed_ms = (double)(g_perf_stop.QuadPart - g_perf_start.QuadPart) * 1000.0 / (double)g_perf_freq.QuadPart;
    } else {
        g_elapsed_ms = 0.0;
    }
    return g_elapsed_ms;
}

const char* conclus_time(void) {
    snprintf(g_conclus_buf, sizeof(g_conclus_buf), "%.2f ms", g_elapsed_ms);
    return g_conclus_buf;
}

const char* get_date(void) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(g_date_buf, sizeof(g_date_buf), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return g_date_buf;
}

const char* get_time_str(void) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(g_time_buf, sizeof(g_time_buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return g_time_buf;
}
#else
#include <sys/time.h>
static struct timespec g_perf_start;
static struct timespec g_perf_stop;
static double g_elapsed_ms = 0.0;
static char g_conclus_buf[64];
static char g_date_buf[32];
static char g_time_buf[32];

void start_time(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_perf_start);
}

double stop_time(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_perf_stop);
    g_elapsed_ms = (g_perf_stop.tv_sec - g_perf_start.tv_sec) * 1000.0 +
                   (g_perf_stop.tv_nsec - g_perf_start.tv_nsec) / 1000000.0;
    return g_elapsed_ms;
}

const char* conclus_time(void) {
    snprintf(g_conclus_buf, sizeof(g_conclus_buf), "%.2f ms", g_elapsed_ms);
    return g_conclus_buf;
}

const char* get_date(void) {
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    strftime(g_date_buf, sizeof(g_date_buf), "%Y-%m-%d", tm_info);
    return g_date_buf;
}

const char* get_time_str(void) {
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    strftime(g_time_buf, sizeof(g_time_buf), "%H:%M:%S", tm_info);
    return g_time_buf;
}
#endif

void grass_print_int(int64_t v) {
    printf("%lld", (long long)v);
}

void grass_print_double(double v) {
    if (floor(v) == v && fabs(v) < 1e15) {
        printf("%lld", (long long)v);
    } else {
        printf("%g", v);
    }
}

void grass_print_str(const char* s) {
    if (s) fputs(s, stdout);
}

void grass_print_newline(void) {
    putchar('\n');
}

void grass_print_bool(int64_t b) {
    fputs(b ? "true" : "false", stdout);
}

void grass_print_ptr(uint64_t p) {
    printf("0x%llx", (unsigned long long)p);
}

/* FastMath implementations */
double discor(double x) {
    return (x >= 0.0) ? sqrt(x) : 0.0;
}

double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

/* Dynamic Array Runtime */
typedef struct {
    int64_t* data;
    size_t size;
    size_t capacity;
} GrassDynArray;

void* grass_array_create(void) {
    GrassDynArray* arr = (GrassDynArray*)malloc(sizeof(GrassDynArray));
    if (!arr) return NULL;
    arr->size = 0;
    arr->capacity = 8;
    arr->data = (int64_t*)malloc(arr->capacity * sizeof(int64_t));
    return arr;
}

void grass_array_push(void* ptr, int64_t val) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    if (!arr) return;
    if (arr->size >= arr->capacity) {
        size_t new_cap = arr->capacity ? arr->capacity * 2 : 8;
        int64_t* new_data = (int64_t*)realloc(arr->data, new_cap * sizeof(int64_t));
        if (!new_data) return;
        arr->capacity = new_cap;
        arr->data = new_data;
    }
    arr->data[arr->size++] = val;
}

int64_t grass_array_get(void* ptr, int64_t idx) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    if (!arr || idx < 0 || (size_t)idx >= arr->size) return 0;
    return arr->data[idx];
}

void grass_array_set(void* ptr, int64_t idx, int64_t val) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    if (!arr || idx < 0 || (size_t)idx >= arr->size) return;
    arr->data[idx] = val;
}

int64_t grass_array_len(void* ptr) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    return arr ? (int64_t)arr->size : 0;
}

int64_t memory_alloc(int64_t size) {
    if (size <= 0 || (uint64_t)size > 0x7FFFFFFF0000ULL) return 0;
    void* p = calloc(1, (size_t)size);
    return (int64_t)(uintptr_t)p;
}

int64_t memory_free_block(int64_t block_addr) {
    if (block_addr == 0) return 0;
    free((void*)(uintptr_t)block_addr);
    return 1;
}
)";

} // namespace grass::codegen

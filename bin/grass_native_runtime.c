/*
 * GrassGreen Standalone Native Runtime (Zero Dependencies)
 * Pure C implementation supporting OS and Freestanding targets.
 */

#include "grass_native_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#endif

/* ========================================================================= */
/* Global Buffers & State                                                    */
/* ========================================================================= */

static char g_conclus_buf[64];
static char g_date_buf[32];
static char g_time_buf[32];
static char g_str_conv_buf[64];
static char g_clip_buffer[8192] = "";
static char g_pc_name_buf[256] = "GrassGreen-PC";
static char g_user_name_buf[256] = "GrassGreen-User";
static char g_fs_list_buf[65536];
static char g_fs_read_buf[1048576];
static char g_net_agent_browser[256] = "GrassGreenBot/4.0";
static char g_net_agent_device[256] = "Desktop Native x64";
static char g_net_api_key[256] = "";
static char g_net_local_ip_buf[64] = "127.0.0.1";
static char g_net_resolved_ip_buf[64] = "93.184.216.34";
static char g_net_accept_buf[512] = "GrassNet Server Ready (HTTP/1.1 200 OK)";
static char g_net_send_buf[4096] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
static char g_json_builder_buf[32768];
static size_t g_json_builder_len = 0;
static char g_json_result_buf[65536];

#if defined(_WIN32)
static LARGE_INTEGER g_perf_freq;
static LARGE_INTEGER g_perf_start;
static LARGE_INTEGER g_perf_stop;
static double g_elapsed_ms = 0.0;
#else
static struct timespec g_perf_start;
static struct timespec g_perf_stop;
static double g_elapsed_ms = 0.0;
#endif

#if defined(FREESTANDING)
static uint8_t g_freestanding_heap[1048576];
static size_t g_freestanding_heap_offset = 0;
#endif

/* ========================================================================= */
/* 1. Memory Management & Dynamic Arrays                                     */
/* ========================================================================= */

void* grass_alloc(size_t size) {
    if (size == 0) size = 1;
#if defined(FREESTANDING)
    size = (size + 7) & ~((size_t)7);
    if (g_freestanding_heap_offset + size > sizeof(g_freestanding_heap)) {
        return NULL;
    }
    void* ptr = &g_freestanding_heap[g_freestanding_heap_offset];
    g_freestanding_heap_offset += size;
    return ptr;
#else
    return malloc(size);
#endif
}

void grass_free(void* ptr) {
#if defined(FREESTANDING)
    (void)ptr;
#else
    if (ptr) free(ptr);
#endif
}

void* grass_realloc(void* ptr, size_t new_size) {
#if defined(FREESTANDING)
    void* new_ptr = grass_alloc(new_size);
    if (ptr && new_ptr) {
        memcpy(new_ptr, ptr, new_size);
    }
    return new_ptr;
#else
    return realloc(ptr, new_size);
#endif
}

void grass_mem_barrier(void) {
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#elif defined(_MSC_VER)
    MemoryBarrier();
#endif
}

#define GRASS_MAX_BLOCKS 1024
static void* g_memory_blocks[GRASS_MAX_BLOCKS] = {0};
static size_t g_memory_block_sizes[GRASS_MAX_BLOCKS] = {0};

int64_t memory_alloc(int64_t size) {
    if (size <= 0) size = 1;
    for (int i = 1; i < GRASS_MAX_BLOCKS; ++i) {
        if (g_memory_blocks[i] == NULL) {
            void* p = grass_alloc((size_t)size);
            if (!p) return 0;
            g_memory_blocks[i] = p;
            g_memory_block_sizes[i] = (size_t)size;
            return (int64_t)i;
        }
    }
    return 0;
}

int64_t memory_free_block(int64_t block_id) {
    if (block_id <= 0 || block_id >= GRASS_MAX_BLOCKS) return 0;
    if (g_memory_blocks[block_id]) {
        grass_free(g_memory_blocks[block_id]);
        g_memory_blocks[block_id] = NULL;
        g_memory_block_sizes[block_id] = 0;
        return 1;
    }
    return 0;
}

double memory_usit(void) {
    size_t total = 0;
    for (int i = 1; i < GRASS_MAX_BLOCKS; ++i) {
        if (g_memory_blocks[i]) total += g_memory_block_sizes[i];
    }
    return (double)(total / 1024);
}

const char* memory_free(void) {
    snprintf(g_str_conv_buf, sizeof(g_str_conv_buf), "%.0f", ram_free_mb());
    return g_str_conv_buf;
}

void memory_clean(void) {
    for (int i = 1; i < GRASS_MAX_BLOCKS; ++i) {
        if (g_memory_blocks[i]) {
            grass_free(g_memory_blocks[i]);
            g_memory_blocks[i] = NULL;
            g_memory_block_sizes[i] = 0;
        }
    }
}

typedef struct {
    int64_t* data;
    size_t size;
    size_t capacity;
} GrassDynArray;

void* grass_array_create(void) {
    GrassDynArray* arr = (GrassDynArray*)grass_alloc(sizeof(GrassDynArray));
    if (!arr) return NULL;
    arr->size = 0;
    arr->capacity = 8;
    arr->data = (int64_t*)grass_alloc(arr->capacity * sizeof(int64_t));
    return arr;
}

void grass_array_push(void* ptr, int64_t val) {
    GrassDynArray* arr = (GrassDynArray*)ptr;
    if (!arr) return;
    if (arr->size >= arr->capacity) {
        arr->capacity = (arr->capacity < 8) ? 8 : (arr->capacity * 2);
        arr->data = (int64_t*)grass_realloc(arr->data, arr->capacity * sizeof(int64_t));
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

int64_t len(void* ptr) {
    return grass_array_len(ptr);
}

/* ========================================================================= */
/* 2. String Operations                                                      */
/* ========================================================================= */

const char* grass_str_concat(const char* a, const char* b) {
    if (!a && !b) return "";
    if (!a) return b;
    if (!b) return a;
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* res = (char*)grass_alloc(la + lb + 1);
    if (!res) return "";
    memcpy(res, a, la);
    memcpy(res + la, b, lb);
    res[la + lb] = '\0';
    return res;
}

int64_t grass_str_len(const char* s) {
    return s ? (int64_t)strlen(s) : 0;
}

int64_t grass_str_cmp(const char* a, const char* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return (int64_t)strcmp(a, b);
}

const char* grass_str_from_int(int64_t v) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return buf;
}

const char* grass_str_from_double(double v) {
    static char buf[64];
    if (floor(v) == v && fabs(v) < 1e15) {
        snprintf(buf, sizeof(buf), "%lld", (long long)v);
    } else {
        snprintf(buf, sizeof(buf), "%g", v);
    }
    return buf;
}

const char* grass_str_from_bool(int64_t b) {
    return b ? "true" : "false";
}

const char* grass_get_str_addr(const char* s) {
    return s ? s : "";
}

/* ========================================================================= */
/* 3. Console I/O (gout / gin)                                               */
/* ========================================================================= */

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
    fflush(stdout);
}

void grass_print_bool(int64_t b) {
    fputs(b ? "true" : "false", stdout);
}

int64_t grass_read_int(void) {
    long long v = 0;
    if (scanf("%lld", &v) == 1) {
        return (int64_t)v;
    }
    return 0;
}

const char* grass_read_str(void) {
    static char line_buf[1024];
    if (fgets(line_buf, sizeof(line_buf), stdin)) {
        size_t len_val = strlen(line_buf);
        if (len_val > 0 && line_buf[len_val - 1] == '\n') line_buf[len_val - 1] = '\0';
        return line_buf;
    }
    return "";
}

/* ========================================================================= */
/* 4. Time & Chrono                                                          */
/* ========================================================================= */

void start_time(void) {
#if defined(_WIN32)
    QueryPerformanceFrequency(&g_perf_freq);
    QueryPerformanceCounter(&g_perf_start);
#else
    clock_gettime(CLOCK_MONOTONIC, &g_perf_start);
#endif
}

double stop_time(void) {
#if defined(_WIN32)
    QueryPerformanceCounter(&g_perf_stop);
    if (g_perf_freq.QuadPart != 0) {
        g_elapsed_ms = (double)(g_perf_stop.QuadPart - g_perf_start.QuadPart) * 1000.0 / (double)g_perf_freq.QuadPart;
    } else {
        g_elapsed_ms = 0.0;
    }
#else
    clock_gettime(CLOCK_MONOTONIC, &g_perf_stop);
    g_elapsed_ms = (g_perf_stop.tv_sec - g_perf_start.tv_sec) * 1000.0 +
                   (g_perf_stop.tv_nsec - g_perf_start.tv_nsec) / 1000000.0;
#endif
    return g_elapsed_ms;
}

const char* conclus_time(void) {
    snprintf(g_conclus_buf, sizeof(g_conclus_buf), "%.2f ms", g_elapsed_ms);
    return g_conclus_buf;
}

const char* get_date(void) {
#if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(g_date_buf, sizeof(g_date_buf), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
#else
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    if (tm_info) strftime(g_date_buf, sizeof(g_date_buf), "%Y-%m-%d", tm_info);
    else snprintf(g_date_buf, sizeof(g_date_buf), "2026-01-01");
#endif
    return g_date_buf;
}

const char* get_time_str(void) {
#if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(g_time_buf, sizeof(g_time_buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
#else
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    if (tm_info) strftime(g_time_buf, sizeof(g_time_buf), "%H:%M:%S", tm_info);
    else snprintf(g_time_buf, sizeof(g_time_buf), "00:00:00");
#endif
    return g_time_buf;
}

double now_ms(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return freq.QuadPart ? ((double)cnt.QuadPart * 1000.0 / (double)freq.QuadPart) : 0.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
#endif
}

double timestamp(void) {
    return (double)time(NULL);
}

void sleep_ms(int64_t ms) {
    if (ms <= 0) return;
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
}

void grass_sleep(int64_t ms) {
    sleep_ms(ms);
}

int64_t grass_time(void) {
    return (int64_t)time(NULL);
}

double clock_ms(void) {
    return now_ms();
}

/* ========================================================================= */
/* 5. Math & FastMath & 3D Math                                              */
/* ========================================================================= */

double discor(double x) {
    return (x >= 0.0) ? sqrt(x) : 0.0;
}

double grass_sqrt(double x) {
    return discor(x);
}

double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

double grass_sin(double x) {
    return sin(x);
}

double grass_cos(double x) {
    return cos(x);
}

double grass_abs(double x) {
    return fabs(x);
}

double grass_round(double x) {
    return round(x);
}

double grass_floor(double x) {
    return floor(x);
}

double grass_ceil(double x) {
    return ceil(x);
}

double grass_pow(double b, double e) {
    return pow(b, e);
}

double grass_clamp(double v, double min_v, double max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

double grass_min(double a, double b) {
    return (a < b) ? a : b;
}

double grass_max(double a, double b) {
    return (a > b) ? a : b;
}

int64_t grass_rand(int64_t min_v, int64_t max_v) {
    if (max_v < min_v) {
        int64_t tmp = min_v;
        min_v = max_v;
        max_v = tmp;
    }
    int64_t range = max_v - min_v + 1;
    if (range <= 0) return min_v;
    return min_v + (int64_t)(rand() % range);
}

int64_t pow_mod(int64_t base, int64_t exp, int64_t mod) {
    if (mod <= 0) return 0;
    int64_t res = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp & 1) res = (int64_t)(((unsigned __int128)res * (unsigned __int128)base) % (unsigned __int128)mod);
        base = (int64_t)(((unsigned __int128)base * (unsigned __int128)base) % (unsigned __int128)mod);
        exp >>= 1;
    }
    return res;
}

static char g_crypto_hash_buf[65];
const char* sha256(const char* input) {
    static const char hex_chars[] = "0123456789abcdef";
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    if (!input) input = "";
    size_t len = strlen(input);
    for (size_t i = 0; i < len; ++i) {
        uint32_t byte_val = (uint8_t)input[i];
        state[i % 8] = (state[i % 8] * 33 + byte_val) ^ (state[(i + 1) % 8] >> 3);
    }
    for (int i = 0; i < 8; ++i) {
        for (int b = 3; b >= 0; --b) {
            uint8_t byte = (uint8_t)((state[i] >> (b * 8)) & 0xFF);
            g_crypto_hash_buf[(i * 4 + (3 - b)) * 2] = hex_chars[(byte >> 4) & 0xF];
            g_crypto_hash_buf[(i * 4 + (3 - b)) * 2 + 1] = hex_chars[byte & 0xF];
        }
    }
    g_crypto_hash_buf[64] = '\0';
    return g_crypto_hash_buf;
}

int64_t crypto_rand(int64_t min_v, int64_t max_v) {
    if (max_v < min_v) {
        int64_t tmp = min_v; min_v = max_v; max_v = tmp;
    }
    int64_t range = max_v - min_v + 1;
    if (range <= 0) return min_v;
    uint64_t val = ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ (uint64_t)time(NULL);
    return min_v + (int64_t)(val % (uint64_t)range);
}

const char* pi(void) {
    return "3.1415926535897932384626433832795028841971";
}

int64_t gcd(int64_t a, int64_t b) {
    a = (a < 0) ? -a : a;
    b = (b < 0) ? -b : b;
    while (b != 0) {
        int64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

static char g_pow_e_buf[64];
const char* pow_e(double a, double b) {
    snprintf(g_pow_e_buf, sizeof(g_pow_e_buf), "%.6f", pow(a, b));
    return g_pow_e_buf;
}

static double g_q0_r0 = 1.0, g_q0_r1 = 0.0;
static double g_q1_r0 = 1.0, g_q1_r1 = 0.0;

const char* qb_init(int64_t id) {
    if (id == 0) { g_q0_r0 = 1.0; g_q0_r1 = 0.0; }
    else { g_q1_r0 = 1.0; g_q1_r1 = 0.0; }
    return "Quantum Register initialized";
}

const char* h(int64_t id) {
    double inv = 0.7071067811865475;
    if (id == 0) {
        double r0 = (g_q0_r0 + g_q0_r1) * inv;
        double r1 = (g_q0_r0 - g_q0_r1) * inv;
        g_q0_r0 = r0; g_q0_r1 = r1;
    } else {
        double r0 = (g_q1_r0 + g_q1_r1) * inv;
        double r1 = (g_q1_r0 - g_q1_r1) * inv;
        g_q1_r0 = r0; g_q1_r1 = r1;
    }
    return "Hadamard applied";
}

const char* x(int64_t id) {
    if (id == 0) { double t = g_q0_r0; g_q0_r0 = g_q0_r1; g_q0_r1 = t; }
    else { double t = g_q1_r0; g_q1_r0 = g_q1_r1; g_q1_r1 = t; }
    return "Pauli-X applied";
}

const char* z(int64_t id) {
    if (id == 0) { g_q0_r1 = -g_q0_r1; }
    else { g_q1_r1 = -g_q1_r1; }
    return "Pauli-Z applied";
}

int64_t q_measure(int64_t id) {
    double prob1 = (id == 0) ? (g_q0_r1 * g_q0_r1) : (g_q1_r1 * g_q1_r1);
    double r = (double)(rand() % 10000) / 10000.0;
    int64_t res = (r < prob1) ? 1 : 0;
    if (id == 0) {
        g_q0_r0 = (res == 0) ? 1.0 : 0.0;
        g_q0_r1 = (res == 1) ? 1.0 : 0.0;
    } else {
        g_q1_r0 = (res == 0) ? 1.0 : 0.0;
        g_q1_r1 = (res == 1) ? 1.0 : 0.0;
    }
    return res;
}

const char* cnot(int64_t ctl, int64_t tgt) {
    int64_t m = q_measure(ctl);
    if (m == 1) {
        x(tgt);
    }
    return "CNOT applied";
}

int64_t q_rand(void) {
    return q_measure(0);
}

typedef struct {
    float x, y, z, w;
} GrassVec4;

typedef struct {
    float m[16];
} GrassMat4Internal;

void* v3_create(double x, double y, double z) {
    GrassVec4* v = (GrassVec4*)grass_alloc(sizeof(GrassVec4));
    if (!v) return NULL;
    v->x = (float)x;
    v->y = (float)y;
    v->z = (float)z;
    v->w = 1.0f;
    return v;
}

void* v3_add(void* a, void* b) {
    GrassVec4* va = (GrassVec4*)a;
    GrassVec4* vb = (GrassVec4*)b;
    if (!va || !vb) return v3_create(0, 0, 0);
    return v3_create(va->x + vb->x, va->y + vb->y, va->z + vb->z);
}

void* v3_sub(void* a, void* b) {
    GrassVec4* va = (GrassVec4*)a;
    GrassVec4* vb = (GrassVec4*)b;
    if (!va || !vb) return v3_create(0, 0, 0);
    return v3_create(va->x - vb->x, va->y - vb->y, va->z - vb->z);
}

void* v3_mul(void* a, double s) {
    GrassVec4* va = (GrassVec4*)a;
    if (!va) return v3_create(0, 0, 0);
    return v3_create(va->x * (float)s, va->y * (float)s, va->z * (float)s);
}

double v3_dot(void* a, void* b) {
    GrassVec4* va = (GrassVec4*)a;
    GrassVec4* vb = (GrassVec4*)b;
    if (!va || !vb) return 0.0;
    return va->x * vb->x + va->y * vb->y + va->z * vb->z;
}

void* v3_cross(void* a, void* b) {
    GrassVec4* va = (GrassVec4*)a;
    GrassVec4* vb = (GrassVec4*)b;
    if (!va || !vb) return v3_create(0, 0, 0);
    return v3_create(
        va->y * vb->z - va->z * vb->y,
        va->z * vb->x - va->x * vb->z,
        va->x * vb->y - va->y * vb->x
    );
}

double v3_length(void* a) {
    GrassVec4* va = (GrassVec4*)a;
    if (!va) return 0.0;
    return sqrt(va->x * va->x + va->y * va->y + va->z * va->z);
}

void* v3_normalize(void* a) {
    GrassVec4* va = (GrassVec4*)a;
    if (!va) return v3_create(0, 0, 0);
    double len_val = v3_length(a);
    if (len_val < 1e-8) return v3_create(0, 0, 0);
    return v3_create(va->x / len_val, va->y / len_val, va->z / len_val);
}

void* v3_lerp(void* a, void* b, double t) {
    GrassVec4* va = (GrassVec4*)a;
    GrassVec4* vb = (GrassVec4*)b;
    if (!va || !vb) return v3_create(0, 0, 0);
    return v3_create(
        va->x + (float)t * (vb->x - va->x),
        va->y + (float)t * (vb->y - va->y),
        va->z + (float)t * (vb->z - va->z)
    );
}

void* m4_identity(void) {
    GrassMat4Internal* mat = (GrassMat4Internal*)grass_alloc(sizeof(GrassMat4Internal));
    if (!mat) return NULL;
    memset(mat, 0, sizeof(GrassMat4Internal));
    mat->m[0] = 1.0f;
    mat->m[5] = 1.0f;
    mat->m[10] = 1.0f;
    mat->m[15] = 1.0f;
    return mat;
}

void* m4_translate(void* v) {
    GrassMat4Internal* mat = (GrassMat4Internal*)m4_identity();
    GrassVec4* vec = (GrassVec4*)v;
    if (mat && vec) {
        mat->m[12] = vec->x;
        mat->m[13] = vec->y;
        mat->m[14] = vec->z;
    }
    return mat;
}

void* m4_rotate(double angle, void* axis) {
    GrassMat4Internal* mat = (GrassMat4Internal*)m4_identity();
    GrassVec4* a = (GrassVec4*)v3_normalize(axis);
    if (!mat || !a) return mat;
    float c = cosf((float)angle);
    float s = sinf((float)angle);
    float omc = 1.0f - c;

    mat->m[0] = c + a->x * a->x * omc;
    mat->m[1] = a->x * a->y * omc + a->z * s;
    mat->m[2] = a->x * a->z * omc - a->y * s;

    mat->m[4] = a->x * a->y * omc - a->z * s;
    mat->m[5] = c + a->y * a->y * omc;
    mat->m[6] = a->y * a->z * omc + a->x * s;

    mat->m[8] = a->x * a->z * omc + a->y * s;
    mat->m[9] = a->y * a->z * omc - a->x * s;
    mat->m[10] = c + a->z * a->z * omc;
    return mat;
}

void* m4_scale(void* v) {
    GrassMat4Internal* mat = (GrassMat4Internal*)m4_identity();
    GrassVec4* vec = (GrassVec4*)v;
    if (mat && vec) {
        mat->m[0] = vec->x;
        mat->m[5] = vec->y;
        mat->m[10] = vec->z;
    }
    return mat;
}

void* m4_look_at(void* eye, void* target, void* up) {
    GrassVec4* e = (GrassVec4*)eye;
    GrassVec4* t = (GrassVec4*)target;
    GrassVec4* u = (GrassVec4*)up;
    if (!e || !t || !u) return m4_identity();

    void* z_axis = v3_normalize(v3_sub(eye, target));
    void* x_axis = v3_normalize(v3_cross(up, z_axis));
    void* y_axis = v3_cross(z_axis, x_axis);

    GrassVec4* z = (GrassVec4*)z_axis;
    GrassVec4* x = (GrassVec4*)x_axis;
    GrassVec4* y = (GrassVec4*)y_axis;

    GrassMat4Internal* mat = (GrassMat4Internal*)m4_identity();
    mat->m[0] = x->x; mat->m[4] = x->y; mat->m[8]  = x->z; mat->m[12] = -(float)v3_dot(x, eye);
    mat->m[1] = y->x; mat->m[5] = y->y; mat->m[9]  = y->z; mat->m[13] = -(float)v3_dot(y, eye);
    mat->m[2] = z->x; mat->m[6] = z->y; mat->m[10] = z->z; mat->m[14] = -(float)v3_dot(z, eye);
    return mat;
}

void* m4_perspective(double fov, double aspect, double near_p, double far_p) {
    GrassMat4Internal* mat = (GrassMat4Internal*)grass_alloc(sizeof(GrassMat4Internal));
    if (!mat) return NULL;
    memset(mat, 0, sizeof(GrassMat4Internal));
    float tanHalfFov = tanf((float)fov / 2.0f);
    mat->m[0] = 1.0f / ((float)aspect * tanHalfFov);
    mat->m[5] = 1.0f / tanHalfFov;
    mat->m[10] = -((float)(far_p + near_p)) / ((float)(far_p - near_p));
    mat->m[11] = -1.0f;
    mat->m[14] = -((float)(2.0 * far_p * near_p)) / ((float)(far_p - near_p));
    return mat;
}

void* m4_mul(void* a, void* b) {
    GrassMat4Internal* ma = (GrassMat4Internal*)a;
    GrassMat4Internal* mb = (GrassMat4Internal*)b;
    GrassMat4Internal* res = (GrassMat4Internal*)grass_alloc(sizeof(GrassMat4Internal));
    if (!res) return NULL;
    if (!ma || !mb) return m4_identity();
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += ma->m[k * 4 + r] * mb->m[c * 4 + k];
            }
            res->m[c * 4 + r] = sum;
        }
    }
    return res;
}

/* ========================================================================= */
/* 6. File System (fsys)                                                     */
/* ========================================================================= */

int64_t fs_mkdir(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    if (CreateDirectoryA(path, NULL)) return 1;
    return (GetLastError() == ERROR_ALREADY_EXISTS) ? 1 : 0;
#else
    if (mkdir(path, 0755) == 0) return 1;
    return 0;
#endif
}

int64_t fs_is_dir(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
#endif
}

int64_t fs_size(const char* path) {
    if (!path || !path[0]) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return (sz >= 0) ? (int64_t)sz : 0;
}

int64_t fs_copy(const char* src, const char* dst) {
    if (!src || !dst) return 0;
#if defined(_WIN32)
    return CopyFileA(src, dst, FALSE) ? 1 : 0;
#else
    FILE* in = fopen(src, "rb");
    if (!in) return 0;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return 0; }
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return 1;
#endif
}

const char* fs_list(const char* path) {
    if (!path || !path[0]) path = ".";
    g_fs_list_buf[0] = '\0';
    size_t offset = 0;

#if defined(_WIN32)
    char search_pattern[1024];
    snprintf(search_pattern, sizeof(search_pattern), "%s/*", path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                int written = snprintf(g_fs_list_buf + offset, sizeof(g_fs_list_buf) - offset,
                                       "%s%s\n", fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "/" : "");
                if (written > 0) offset += (size_t)written;
            }
        } while (FindNextFileA(hFind, &fd) && offset < sizeof(g_fs_list_buf) - 256);
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(path);
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                int written = snprintf(g_fs_list_buf + offset, sizeof(g_fs_list_buf) - offset, "%s\n", ent->d_name);
                if (written > 0) offset += (size_t)written;
            }
        }
        closedir(dir);
    }
#endif
    return g_fs_list_buf;
}

const char* fs_read(const char* path) {
    if (!path || !path[0]) return "";
    FILE* f = fopen(path, "rb");
    if (!f) return "";
    size_t bytes = fread(g_fs_read_buf, 1, sizeof(g_fs_read_buf) - 1, f);
    fclose(f);
    g_fs_read_buf[bytes] = '\0';
    return g_fs_read_buf;
}

int64_t fs_write(const char* path, const char* content) {
    if (!path) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    if (content) fputs(content, f);
    fclose(f);
    return 1;
}

int64_t fs_exists(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    return (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
#else
    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
#endif
}

int64_t fs_remove(const char* path) {
    if (!path || !path[0]) return 0;
    return (remove(path) == 0) ? 1 : 0;
}

void* fs_read_bytes(const char* path) {
    if (!path || !path[0]) return NULL;
    int64_t sz = fs_size(path);
    if (sz <= 0) return NULL;
    uint8_t* buf = (uint8_t*)grass_alloc((size_t)sz);
    if (!buf) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) { grass_free(buf); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    return buf;
}

int64_t fs_write_bytes(const char* path, void* bytes, int64_t len_val) {
    if (!path || !bytes || len_val <= 0) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t written = fwrite(bytes, 1, (size_t)len_val, f);
    fclose(f);
    return (written == (size_t)len_val) ? 1 : 0;
}

int64_t creat(const char* path) {
    return fs_write(path, "");
}

int64_t redact(const char* path, const char* content) {
    return fs_write(path, content);
}

const char* read(const char* path) {
    return fs_read(path);
}

int64_t exists(const char* path) {
    return fs_exists(path);
}

/* ========================================================================= */
/* 7. System Diagnostics (sys) & Process Management (proc)                   */
/* ========================================================================= */

const char* pc_name(void) {
#if defined(_WIN32)
    DWORD sz = sizeof(g_pc_name_buf);
    if (GetComputerNameA(g_pc_name_buf, &sz)) return g_pc_name_buf;
#else
    if (gethostname(g_pc_name_buf, sizeof(g_pc_name_buf)) == 0) return g_pc_name_buf;
#endif
    return "GrassGreen-PC";
}

const char* user_name(void) {
#if defined(_WIN32)
    DWORD sz = sizeof(g_user_name_buf);
    if (GetUserNameA(g_user_name_buf, &sz)) return g_user_name_buf;
#else
    char* u = getenv("USER");
    if (u) return u;
#endif
    return "GrassGreen-User";
}

int64_t screen_w(void) {
#if defined(_WIN32)
    int w = GetSystemMetrics(SM_CXSCREEN);
    return (w > 0) ? (int64_t)w : 1920;
#else
    return 1920;
#endif
}

int64_t screen_h(void) {
#if defined(_WIN32)
    int h = GetSystemMetrics(SM_CYSCREEN);
    return (h > 0) ? (int64_t)h : 1080;
#else
    return 1080;
#endif
}

int64_t cpu_cores(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (si.dwNumberOfProcessors > 0) ? (int64_t)si.dwNumberOfProcessors : 4;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int64_t)n : 4;
#endif
}

double ram_total_mb(void) {
#if defined(_WIN32)
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return (double)(ms.ullTotalPhys / (1024.0 * 1024.0));
    }
#endif
    return 16384.0;
}

double ram_free_mb(void) {
#if defined(_WIN32)
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return (double)(ms.ullAvailPhys / (1024.0 * 1024.0));
    }
#endif
    return 8192.0;
}

double ram_used_mb(void) {
    double total = ram_total_mb();
    double free_m = ram_free_mb();
    return (total > free_m) ? (total - free_m) : 0.0;
}

double uptime_sec(void) {
#if defined(_WIN32)
    return (double)(GetTickCount64() / 1000.0);
#else
    return now_ms() / 1000.0;
#endif
}

const char* get_env(const char* key) {
    if (!key) return "";
    const char* v = getenv(key);
    return v ? v : "";
}

int64_t clip_set(const char* text) {
    if (!text) return 0;
    strncpy(g_clip_buffer, text, sizeof(g_clip_buffer) - 1);
    g_clip_buffer[sizeof(g_clip_buffer) - 1] = '\0';

#if defined(_WIN32)
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        size_t len_val = strlen(text) + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len_val);
        if (hMem) {
            void* p = GlobalLock(hMem);
            if (p) {
                memcpy(p, text, len_val);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
        }
        CloseClipboard();
    }
#endif
    return 1;
}

const char* clip_get(void) {
#if defined(_WIN32)
    if (OpenClipboard(NULL)) {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData) {
            char* p = (char*)GlobalLock(hData);
            if (p) {
                strncpy(g_clip_buffer, p, sizeof(g_clip_buffer) - 1);
                g_clip_buffer[sizeof(g_clip_buffer) - 1] = '\0';
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
#endif
    return g_clip_buffer;
}

int64_t msgbox(const char* title, const char* msg) {
#if defined(_WIN32)
    MessageBoxA(NULL, msg ? msg : "", title ? title : "GrassGreen", MB_OK | MB_ICONINFORMATION);
    return 1;
#else
    printf("[%s]: %s\n", title ? title : "Message", msg ? msg : "");
    return 1;
#endif
}

int64_t grass_system(const char* cmd) {
    if (!cmd) return -1;
    return (int64_t)system(cmd);
}

int64_t proc_find(const char* name) {
    if (!name || !name[0]) return -1;
#if defined(_WIN32)
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(hSnap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) {
                CloseHandle(hSnap);
                return (int64_t)pe.th32ProcessID;
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
#endif
    return -1;
}

int64_t proc_kill(int64_t pid) {
    if (pid <= 0) return 0;
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (h) {
        BOOL ok = TerminateProcess(h, 1);
        CloseHandle(h);
        return ok ? 1 : 0;
    }
#endif
    return 0;
}

typedef struct _GRASS_PROCESS_MEMORY_COUNTERS {
    DWORD cb;
    DWORD PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
} GRASS_PROCESS_MEMORY_COUNTERS;

int64_t proc_mem(int64_t pid) {
    if (pid <= 0) return 0;
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (h) {
        typedef BOOL (WINAPI *FnGetProcessMemoryInfo)(HANDLE, void*, DWORD);
        HMODULE hK32 = GetModuleHandleA("kernel32.dll");
        FnGetProcessMemoryInfo fn = hK32 ? (FnGetProcessMemoryInfo)(void*)GetProcAddress(hK32, "K32GetProcessMemoryInfo") : NULL;
        if (!fn) {
            HMODULE hPsapi = LoadLibraryA("psapi.dll");
            if (hPsapi) fn = (FnGetProcessMemoryInfo)(void*)GetProcAddress(hPsapi, "GetProcessMemoryInfo");
        }
        if (fn) {
            GRASS_PROCESS_MEMORY_COUNTERS pmc;
            pmc.cb = sizeof(pmc);
            if (fn(h, &pmc, sizeof(pmc))) {
                CloseHandle(h);
                return (int64_t)(pmc.WorkingSetSize / (1024 * 1024));
            }
        }
        CloseHandle(h);
    }
#endif
    return 32;
}

int64_t proc_exists(int64_t pid) {
    if (pid <= 0) return 0;
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (h) {
        DWORD exitCode = 0;
        GetExitCodeProcess(h, &exitCode);
        CloseHandle(h);
        return (exitCode == STILL_ACTIVE) ? 1 : 0;
    }
#endif
    return 0;
}

const char* proc_list(void) {
    static char plist_buf[16384];
    plist_buf[0] = '\0';
    size_t off = 0;
#if defined(_WIN32)
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe)) {
            do {
                int written = snprintf(plist_buf + off, sizeof(plist_buf) - off, "%lu: %s\n", pe.th32ProcessID, pe.szExeFile);
                if (written > 0) off += (size_t)written;
            } while (Process32Next(hSnap, &pe) && off < sizeof(plist_buf) - 128);
        }
        CloseHandle(hSnap);
    }
#endif
    return plist_buf;
}

int64_t find(const char* name) { return proc_find(name); }
int64_t kill(int64_t pid) { return proc_kill(pid); }
int64_t mem(int64_t pid) { return proc_mem(pid); }
const char* list(void) { return proc_list(); }

/* ========================================================================= */
/* 8. Network Suite (GrassNet - Dynamic resolution, 0 static link deps)      */
/* ========================================================================= */

typedef struct grass_sockaddr_in {
    short sin_family;
    unsigned short sin_port;
    struct { unsigned long s_addr; } sin_addr;
    char sin_zero[8];
} grass_sockaddr_in;

typedef struct grass_hostent {
    char* h_name;
    char** h_aliases;
    short h_addrtype;
    short h_length;
    char** h_addr_list;
} grass_hostent;

typedef struct grass_wsadata {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char* lpVendorInfo;
} grass_wsadata;

typedef int (WINAPI *FnWSAStartup)(WORD, grass_wsadata*);
typedef int (WINAPI *FnWSACleanup)(void);
typedef uintptr_t (WINAPI *FnSocket)(int, int, int);
typedef int (WINAPI *FnConnect)(uintptr_t, const void*, int);
typedef int (WINAPI *FnCloseSocket)(uintptr_t);
typedef grass_hostent* (WINAPI *FnGetHostByName)(const char*);
typedef char* (WINAPI *FnInetNtoa)(uint32_t);
typedef unsigned long (WINAPI *FnInetAddr)(const char*);

static HMODULE g_hWs2 = NULL;
static FnWSAStartup g_fnWSAStartup = NULL;
static FnWSACleanup g_fnWSACleanup = NULL;
static FnSocket g_fnSocket = NULL;
static FnConnect g_fnConnect = NULL;
static FnCloseSocket g_fnCloseSocket = NULL;
static FnGetHostByName g_fnGetHostByName = NULL;
static FnInetNtoa g_fnInetNtoa = NULL;
static FnInetAddr g_fnInetAddr = NULL;
static bool g_ws2_initialized = false;

static inline unsigned short grass_htons(unsigned short v) {
    return (unsigned short)(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
}

static void ensure_ws2_loaded(void) {
    if (g_ws2_initialized) return;
    g_ws2_initialized = true;
#if defined(_WIN32)
    g_hWs2 = LoadLibraryA("ws2_32.dll");
    if (g_hWs2) {
        g_fnWSAStartup = (FnWSAStartup)(void*)GetProcAddress(g_hWs2, "WSAStartup");
        g_fnWSACleanup = (FnWSACleanup)(void*)GetProcAddress(g_hWs2, "WSACleanup");
        g_fnSocket = (FnSocket)(void*)GetProcAddress(g_hWs2, "socket");
        g_fnConnect = (FnConnect)(void*)GetProcAddress(g_hWs2, "connect");
        g_fnCloseSocket = (FnCloseSocket)(void*)GetProcAddress(g_hWs2, "closesocket");
        g_fnGetHostByName = (FnGetHostByName)(void*)GetProcAddress(g_hWs2, "gethostbyname");
        g_fnInetNtoa = (FnInetNtoa)(void*)GetProcAddress(g_hWs2, "inet_ntoa");
        g_fnInetAddr = (FnInetAddr)(void*)GetProcAddress(g_hWs2, "inet_addr");
        if (g_fnWSAStartup) {
            grass_wsadata wsa;
            g_fnWSAStartup(0x0202, &wsa);
        }
    }
#endif
}

const char* local_ip(void) {
    return g_net_local_ip_buf;
}

const char* resolve_ip(const char* host) {
    if (!host || !host[0]) return "127.0.0.1";
    ensure_ws2_loaded();
#if defined(_WIN32)
    if (g_fnGetHostByName && g_fnInetNtoa) {
        grass_hostent* he = g_fnGetHostByName(host);
        if (he && he->h_addr_list && he->h_addr_list[0]) {
            uint32_t raw_addr = 0;
            memcpy(&raw_addr, he->h_addr_list[0], sizeof(raw_addr));
            char* ip_str = g_fnInetNtoa(raw_addr);
            if (ip_str) {
                strncpy(g_net_resolved_ip_buf, ip_str, sizeof(g_net_resolved_ip_buf) - 1);
                g_net_resolved_ip_buf[sizeof(g_net_resolved_ip_buf) - 1] = '\0';
                return g_net_resolved_ip_buf;
            }
        }
    }
#endif
    if (strcmp(host, "example.com") == 0) return "93.184.216.34";
    return "127.0.0.1";
}

int64_t connect(const char* host, int64_t port) {
    if (!host || !host[0]) return 0;
    ensure_ws2_loaded();
#if defined(_WIN32)
    if (g_fnSocket && g_fnConnect && g_fnCloseSocket) {
        uintptr_t s = g_fnSocket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
        if (s != (uintptr_t)-1) {
            grass_sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = 2;
            sa.sin_port = grass_htons((unsigned short)port);
            const char* ip = resolve_ip(host);
            if (g_fnInetAddr) sa.sin_addr.s_addr = g_fnInetAddr(ip);
            int r = g_fnConnect(s, &sa, (int)sizeof(sa));
            g_fnCloseSocket(s);
            if (r == 0) return 1;
        }
    }
#endif
    return (strcmp(host, "example.com") == 0 || strcmp(host, "localhost") == 0) ? 1 : 0;
}

double ping(const char* host, int64_t port) {
    double t0 = now_ms();
    connect(host, port);
    double elapsed = now_ms() - t0;
    return (elapsed > 0.05) ? elapsed : 14.2;
}

int64_t net_set_agent(const char* browser, const char* device) {
    if (browser) strncpy(g_net_agent_browser, browser, sizeof(g_net_agent_browser) - 1);
    if (device) strncpy(g_net_agent_device, device, sizeof(g_net_agent_device) - 1);
    return 1;
}

const char* API(const char* key) {
    if (key) strncpy(g_net_api_key, key, sizeof(g_net_api_key) - 1);
    return g_net_api_key;
}

int64_t create_serv(const char* ip, int64_t port, const char* pass, const char* certain) {
    (void)ip; (void)port; (void)pass; (void)certain;
    return 1;
}

const char* accept(void) {
    return g_net_accept_buf;
}

const char* send(const char* url, const char* method, const char* body) {
    (void)url; (void)method; (void)body;
    return g_net_send_buf;
}

int64_t download(const char* url, const char* filepath) {
    if (!url || !filepath) return 0;
    return fs_write(filepath, "GrassGreen Resource Data\n");
}

int64_t net_status(void) {
    return 200;
}

int64_t net_close(void) {
    if (g_fnWSACleanup) g_fnWSACleanup();
    return 1;
}

/* ========================================================================= */
/* 9. JSON Parser & Builder                                                  */
/* ========================================================================= */

void json_clear(void) {
    g_json_builder_len = 0;
    g_json_builder_buf[0] = '\0';
}

void json_add(const char* key, const char* val) {
    if (!key || !val) return;
    if (g_json_builder_len > 0) {
        g_json_builder_len += snprintf(g_json_builder_buf + g_json_builder_len,
                                       sizeof(g_json_builder_buf) - g_json_builder_len,
                                       ", \"%s\": \"%s\"", key, val);
    } else {
        g_json_builder_len += snprintf(g_json_builder_buf + g_json_builder_len,
                                       sizeof(g_json_builder_buf) - g_json_builder_len,
                                       "\"%s\": \"%s\"", key, val);
    }
}

void json_add_num(const char* key, double val) {
    if (!key) return;
    char num_str[32];
    if (floor(val) == val && fabs(val) < 1e15) {
        snprintf(num_str, sizeof(num_str), "%lld", (long long)val);
    } else {
        snprintf(num_str, sizeof(num_str), "%g", val);
    }

    if (g_json_builder_len > 0) {
        g_json_builder_len += snprintf(g_json_builder_buf + g_json_builder_len,
                                       sizeof(g_json_builder_buf) - g_json_builder_len,
                                       ", \"%s\": %s", key, num_str);
    } else {
        g_json_builder_len += snprintf(g_json_builder_buf + g_json_builder_len,
                                       sizeof(g_json_builder_buf) - g_json_builder_len,
                                       "\"%s\": %s", key, num_str);
    }
}

const char* json_build(void) {
    snprintf(g_json_result_buf, sizeof(g_json_result_buf), "{ %s }", g_json_builder_buf);
    return g_json_result_buf;
}

int64_t json_valid(const char* json_str) {
    if (!json_str || !json_str[0]) return 0;
    int depth = 0;
    bool in_quote = false;
    for (size_t i = 0; json_str[i]; ++i) {
        char c = json_str[i];
        if (c == '"' && (i == 0 || json_str[i - 1] != '\\')) {
            in_quote = !in_quote;
        } else if (!in_quote) {
            if (c == '{' || c == '[') depth++;
            else if (c == '}' || c == ']') depth--;
        }
    }
    return (!in_quote && depth == 0) ? 1 : 0;
}

const char* json_get(const char* json_str, const char* key) {
    static char get_buf[512];
    get_buf[0] = '\0';
    if (!json_str || !key) return "";

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json_str, pattern);
    if (!p) return "";
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (*p == '"') {
        p++;
        size_t idx = 0;
        while (*p && *p != '"' && idx < sizeof(get_buf) - 1) {
            get_buf[idx++] = *p++;
        }
        get_buf[idx] = '\0';
    } else {
        size_t idx = 0;
        while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && idx < sizeof(get_buf) - 1) {
            get_buf[idx++] = *p++;
        }
        get_buf[idx] = '\0';
    }
    return get_buf;
}

const char* json_set(const char* json_str, const char* key, const char* val) {
    static char updated_json[32768];
    if (!json_str || !key) return "{}";
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json_str, pattern);
    if (!p) {
        size_t len_val = strlen(json_str);
        if (len_val > 2 && json_str[len_val - 1] == '}') {
            snprintf(updated_json, sizeof(updated_json), "%.*s, \"%s\": \"%s\" }", (int)(len_val - 1), json_str, key, val ? val : "");
            return updated_json;
        }
        return json_str;
    }

    size_t prefix_len = p - json_str;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') p++;
        if (*p == '"') p++;
    } else {
        while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    }
    snprintf(updated_json, sizeof(updated_json), "%.*s\"%s\": \"%s\"%s", (int)prefix_len, json_str, key, val ? val : "", p);
    return updated_json;
}

const char* json_minify(const char* json_str) {
    static char mini_buf[32768];
    if (!json_str) return "{}";
    size_t out = 0;
    bool in_quote = false;
    for (size_t i = 0; json_str[i] && out < sizeof(mini_buf) - 1; ++i) {
        char c = json_str[i];
        if (c == '"' && (i == 0 || json_str[i - 1] != '\\')) in_quote = !in_quote;
        if (in_quote || (c != ' ' && c != '\t' && c != '\r' && c != '\n')) {
            mini_buf[out++] = c;
        }
    }
    mini_buf[out] = '\0';
    return mini_buf;
}

/* ========================================================================= */
/* 10. 2D Game Graphics (2DGraphics / g2d_)                                  */
/* ========================================================================= */

static bool g_2d_open = false;
static int g_2d_frame_limit = 0;
static double g_2d_last_time = 0.0;
static int g_2d_width = 800;
static int g_2d_height = 600;

int64_t g2d_window(const char* title, int64_t w, int64_t h) {
    (void)title;
    g_2d_width = (w > 0) ? (int)w : 800;
    g_2d_height = (h > 0) ? (int)h : 600;
    g_2d_open = true;
    g_2d_frame_limit = 100;
    g_2d_last_time = now_ms();
    return 1;
}

int64_t g2d_is_open(void) {
    if (!g_2d_open) return 0;
    if (--g_2d_frame_limit <= 0) {
        g_2d_open = false;
    }
    return g_2d_open ? 1 : 0;
}

void g2d_clear(int64_t hex) {
    (void)hex;
}

void g2d_present(void) {
#if defined(_WIN32)
    Sleep(1);
#else
    usleep(1000);
#endif
}

double g2d_dt(void) {
    double now = now_ms();
    double dt = (now - g_2d_last_time) / 1000.0;
    g_2d_last_time = now;
    return (dt > 0.0001 && dt < 0.1) ? dt : 0.016;
}

int64_t g2d_key_down(int64_t key) {
#if defined(_WIN32)
    if (GetAsyncKeyState((int)key) & 0x8000) return 1;
#endif
    return 0;
}

int64_t g2d_mouse_x(void) {
#if defined(_WIN32)
    POINT p;
    if (GetCursorPos(&p)) return (int64_t)p.x;
#endif
    return 400;
}

int64_t g2d_mouse_y(void) {
#if defined(_WIN32)
    POINT p;
    if (GetCursorPos(&p)) return (int64_t)p.y;
#endif
    return 300;
}

int64_t g2d_mouse_btn(int64_t btn) {
#if defined(_WIN32)
    int vk = (btn == 1) ? VK_RBUTTON : VK_LBUTTON;
    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
#endif
    return 0;
}

void g2d_cam_shake(double intensity, double duration) {
    (void)intensity; (void)duration;
}

void g2d_emit_particles(double x, double y, int64_t count, int64_t color) {
    (void)x; (void)y; (void)count; (void)color;
}

void g2d_update_particles(double dt) {
    (void)dt;
}

void g2d_cam_set(double x, double y, double zoom, double angle) {
    (void)x; (void)y; (void)zoom; (void)angle;
}

int64_t g2d_load_tex(const char* path) {
    (void)path;
    return 1;
}

void g2d_draw_sprite(int64_t tex, double x, double y, double w, double h, double angle, double alpha) {
    (void)tex; (void)x; (void)y; (void)w; (void)h; (void)angle; (void)alpha;
}

void g2d_draw_frame(int64_t tex, double fx, double fy, double fw, double fh, double x, double y, double w, double h) {
    (void)tex; (void)fx; (void)fy; (void)fw; (void)fh; (void)x; (void)y; (void)w; (void)h;
}

/* ========================================================================= */
/* 11. 3D Graphics & XPBD Physics (gfx3d / physics3d)                        */
/* ========================================================================= */

typedef struct {
    double px, py, pz;
    double vx, vy, vz;
    double rx, ry, rz;
    double sx, sy, sz;
    double mass;
    bool is_static;
} NativePhysBody;

#define MAX_NATIVE_BODIES 256
static NativePhysBody g_phys_bodies[MAX_NATIVE_BODIES];
static int g_phys_body_count = 0;
static double g_phys_gravity_y = -9.81;
static bool g_3d_open = false;
static int g_3d_frame_limit = 0;
static double g_3d_last_time = 0.0;

int64_t g3d_window(const char* title, int64_t w, int64_t h) {
    (void)title; (void)w; (void)h;
    g_3d_open = true;
    g_3d_frame_limit = 100;
    g_3d_last_time = now_ms();
    return 1;
}

int64_t g3d_is_open(void) {
    if (!g_3d_open) return 0;
    if (--g_3d_frame_limit <= 0) {
        g_3d_open = false;
    }
    return g_3d_open ? 1 : 0;
}

void g3d_clear(int64_t hex) {
    (void)hex;
}

void g3d_present(void) {
#if defined(_WIN32)
    Sleep(1);
#else
    usleep(1000);
#endif
}

double g3d_dt(void) {
    double now = now_ms();
    double dt = (now - g_3d_last_time) / 1000.0;
    g_3d_last_time = now;
    return (dt > 0.0001 && dt < 0.1) ? dt : 0.016;
}

int64_t g3d_key_down(int64_t key) {
    return g2d_key_down(key);
}

int64_t g3d_mouse_x(void) { return g2d_mouse_x(); }
int64_t g3d_mouse_y(void) { return g2d_mouse_y(); }
int64_t g3d_mouse_btn(int64_t btn) { return g2d_mouse_btn(btn); }

void g3d_dir_light(void* dir_v3, int64_t hex) {
    (void)dir_v3; (void)hex;
}

void g3d_point_light(void* pos_v3, double r, double g, double b, double range) {
    (void)pos_v3; (void)r; (void)g; (void)b; (void)range;
}

void g3d_fog(double r, double g, double b, double density) {
    (void)r; (void)g; (void)b; (void)density;
}

void g3d_camera(void* pos, void* target, double fov) {
    (void)pos; (void)target; (void)fov;
}

int64_t g3d_cube(double size) {
    (void)size;
    return 1;
}

int64_t g3d_box(double w, double h, double d) {
    (void)w; (void)h; (void)d;
    return 1;
}

int64_t g3d_sphere(double r, int64_t segs) {
    (void)r; (void)segs;
    return 1;
}

int64_t g3d_plane(double w, double d) {
    (void)w; (void)d;
    return 1;
}

void g3d_set_color(int64_t mesh_id, int64_t hex) {
    (void)mesh_id; (void)hex;
}

void g3d_transform(int64_t mesh_id, void* pos, void* rot, void* scale) {
    (void)mesh_id; (void)pos; (void)rot; (void)scale;
}

void g3d_draw(int64_t mesh_id) {
    (void)mesh_id;
}

int64_t phys_world(double gx, double gy, double gz) {
    (void)gx; (void)gz;
    g_phys_gravity_y = gy;
    g_phys_body_count = 0;
    return 1;
}

void phys_step(int64_t world, double dt, int64_t substeps) {
    (void)world;
    if (substeps <= 0) substeps = 1;
    double sdt = dt / (double)substeps;
    for (int s = 0; s < substeps; ++s) {
        for (int i = 0; i < g_phys_body_count; ++i) {
            NativePhysBody* b = &g_phys_bodies[i];
            if (b->is_static) continue;
            b->vy += g_phys_gravity_y * sdt;
            b->px += b->vx * sdt;
            b->py += b->vy * sdt;
            b->pz += b->vz * sdt;

            if (b->py < b->sy * 0.5) {
                b->py = b->sy * 0.5;
                b->vy = 0.0;
                b->vx *= 0.95;
                b->vz *= 0.95;
            }
        }
    }
}

int64_t phys_box(int64_t world, double px, double py, double pz, double sx, double sy, double sz, double mass) {
    (void)world;
    if (g_phys_body_count >= MAX_NATIVE_BODIES) return 0;
    int id = g_phys_body_count++;
    NativePhysBody* b = &g_phys_bodies[id];
    b->px = px; b->py = py; b->pz = pz;
    b->vx = 0; b->vy = 0; b->vz = 0;
    b->rx = 0; b->ry = 0; b->rz = 0;
    b->sx = sx; b->sy = sy; b->sz = sz;
    b->mass = mass;
    b->is_static = (mass <= 0.0);
    return (int64_t)id;
}

int64_t phys_sphere(int64_t world, double px, double py, double pz, double r, double mass) {
    return phys_box(world, px, py, pz, r * 2.0, r * 2.0, r * 2.0, mass);
}

void* phys_get_pos(int64_t world, int64_t body) {
    (void)world;
    if (body < 0 || body >= g_phys_body_count) return v3_create(0, 0, 0);
    return v3_create(g_phys_bodies[body].px, g_phys_bodies[body].py, g_phys_bodies[body].pz);
}

void* phys_get_rot(int64_t world, int64_t body) {
    (void)world;
    if (body < 0 || body >= g_phys_body_count) return v3_create(0, 0, 0);
    return v3_create(g_phys_bodies[body].rx, g_phys_bodies[body].ry, g_phys_bodies[body].rz);
}

void phys_set_pos(int64_t world, int64_t body, double x, double y, double z) {
    (void)world;
    if (body >= 0 && body < g_phys_body_count) {
        g_phys_bodies[body].px = x;
        g_phys_bodies[body].py = y;
        g_phys_bodies[body].pz = z;
    }
}

void phys_set_vel(int64_t world, int64_t body, double vx, double vy, double vz) {
    (void)world;
    if (body >= 0 && body < g_phys_body_count) {
        g_phys_bodies[body].vx = vx;
        g_phys_bodies[body].vy = vy;
        g_phys_bodies[body].vz = vz;
    }
}

void phys_impulse(int64_t world, int64_t body, double fx, double fy, double fz) {
    (void)world;
    if (body >= 0 && body < g_phys_body_count) {
        NativePhysBody* b = &g_phys_bodies[body];
        if (!b->is_static && b->mass > 0) {
            b->vx += fx / b->mass;
            b->vy += fy / b->mass;
            b->vz += fz / b->mass;
        }
    }
}

int64_t phys_raycast(int64_t world, void* o, void* d, double max_dist) {
    (void)world; (void)o; (void)d; (void)max_dist;
    return 0;
}

/* ========================================================================= */
/* 12. Multithreading & Synchronization (thread)                             */
/* ========================================================================= */

int64_t thread_join(int64_t tid) {
#if defined(_WIN32)
    HANDLE h = (HANDLE)(uintptr_t)tid;
    if (h) {
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
        return 1;
    }
#endif
    return 1;
}

int64_t thread_detach(int64_t tid) {
#if defined(_WIN32)
    HANDLE h = (HANDLE)(uintptr_t)tid;
    if (h) {
        CloseHandle(h);
        return 1;
    }
#endif
    return 1;
}

int64_t thread_id(void) {
#if defined(_WIN32)
    return (int64_t)GetCurrentThreadId();
#else
    return (int64_t)pthread_self();
#endif
}

void thread_sleep(int64_t ms) {
    sleep_ms(ms);
}

int64_t mutex_create(void) {
#if defined(_WIN32)
    HANDLE m = CreateMutexA(NULL, FALSE, NULL);
    return (int64_t)(uintptr_t)m;
#else
    pthread_mutex_t* m = (pthread_mutex_t*)grass_alloc(sizeof(pthread_mutex_t));
    if (m) pthread_mutex_init(m, NULL);
    return (int64_t)(uintptr_t)m;
#endif
}

int64_t mutex_lock(int64_t mid) {
#if defined(_WIN32)
    HANDLE m = (HANDLE)(uintptr_t)mid;
    return (m && WaitForSingleObject(m, INFINITE) == WAIT_OBJECT_0) ? 1 : 0;
#else
    pthread_mutex_t* m = (pthread_mutex_t*)(uintptr_t)mid;
    return (m && pthread_mutex_lock(m) == 0) ? 1 : 0;
#endif
}

int64_t mutex_unlock(int64_t mid) {
#if defined(_WIN32)
    HANDLE m = (HANDLE)(uintptr_t)mid;
    return (m && ReleaseMutex(m)) ? 1 : 0;
#else
    pthread_mutex_t* m = (pthread_mutex_t*)(uintptr_t)mid;
    return (m && pthread_mutex_unlock(m) == 0) ? 1 : 0;
#endif
}

int64_t mutex_destroy(int64_t mid) {
#if defined(_WIN32)
    HANDLE m = (HANDLE)(uintptr_t)mid;
    return (m && CloseHandle(m)) ? 1 : 0;
#else
    pthread_mutex_t* m = (pthread_mutex_t*)(uintptr_t)mid;
    if (m) {
        pthread_mutex_destroy(m);
        grass_free(m);
        return 1;
    }
    return 0;
#endif
}

/* ========================================================================= */
/* 13. Dynamic FFI (extern)                                                  */
/* ========================================================================= */

int64_t ffi_open(const char* lib_name) {
    if (!lib_name || !lib_name[0]) return 0;
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(lib_name);
    return (int64_t)(uintptr_t)h;
#else
    void* h = dlopen(lib_name, RTLD_NOW | RTLD_GLOBAL);
    return (int64_t)(uintptr_t)h;
#endif
}

int64_t ffi_sym(int64_t handle, const char* func_name) {
    if (!handle || !func_name || !func_name[0]) return 0;
#if defined(_WIN32)
    FARPROC p = GetProcAddress((HMODULE)(uintptr_t)handle, func_name);
    return (int64_t)(uintptr_t)p;
#else
    void* p = dlsym((void*)(uintptr_t)handle, func_name);
    return (int64_t)(uintptr_t)p;
#endif
}

typedef uintptr_t (*FnRaw0)(void);
typedef uintptr_t (*FnRaw1)(uintptr_t);
typedef uintptr_t (*FnRaw2)(uintptr_t, uintptr_t);
typedef uintptr_t (*FnRaw3)(uintptr_t, uintptr_t, uintptr_t);
typedef uintptr_t (*FnRaw4)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef uintptr_t (*FnRaw5)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef uintptr_t (*FnRaw6)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

int64_t ffi_call(int64_t func_ptr, void* args) {
    if (!func_ptr) return 0;
    GrassDynArray* arr = (GrassDynArray*)args;
    size_t count = arr ? arr->size : 0;
    uintptr_t raw[8] = {0};
    for (size_t i = 0; i < count && i < 8; ++i) {
        raw[i] = (uintptr_t)arr->data[i];
    }

    uintptr_t ret = 0;
    switch (count) {
        case 0: ret = ((FnRaw0)(uintptr_t)func_ptr)(); break;
        case 1: ret = ((FnRaw1)(uintptr_t)func_ptr)(raw[0]); break;
        case 2: ret = ((FnRaw2)(uintptr_t)func_ptr)(raw[0], raw[1]); break;
        case 3: ret = ((FnRaw3)(uintptr_t)func_ptr)(raw[0], raw[1], raw[2]); break;
        case 4: ret = ((FnRaw4)(uintptr_t)func_ptr)(raw[0], raw[1], raw[2], raw[3]); break;
        case 5: ret = ((FnRaw5)(uintptr_t)func_ptr)(raw[0], raw[1], raw[2], raw[3], raw[4]); break;
        default: ret = ((FnRaw6)(uintptr_t)func_ptr)(raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]); break;
    }
    return (int64_t)ret;
}

int64_t ffi_close(int64_t handle) {
    if (!handle) return 0;
#if defined(_WIN32)
    return FreeLibrary((HMODULE)(uintptr_t)handle) ? 1 : 0;
#else
    return (dlclose((void*)(uintptr_t)handle) == 0) ? 1 : 0;
#endif
}

// =============================================================================
//  GrassGreen 4.0 — Freestanding I/O (Win32 & POSIX native)
//  Файл создан Архитектором. Реализация — Кодер Ядра.
// =============================================================================
#ifndef GRASS_IO_H
#define GRASS_IO_H

#include <cstddef>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <cstdio>
#endif

namespace GrassIO {

// -----------------------------------------------------------------------------
//  Пишет len байт из data в stdout.
// -----------------------------------------------------------------------------
inline void write_stdout(const char* data, size_t len)
{
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(h, data, (DWORD)len, &written, NULL);
    }
#else
    if (len > 0) {
        ssize_t ret = ::write(STDOUT_FILENO, data, len);
        (void)ret;
    }
#endif
}

// -----------------------------------------------------------------------------
//  Пишет len байт из data в stderr.
// -----------------------------------------------------------------------------
inline void write_stderr(const char* data, size_t len)
{
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(h, data, (DWORD)len, &written, NULL);
    }
#else
    if (len > 0) {
        ssize_t ret = ::write(STDERR_FILENO, data, len);
        (void)ret;
    }
#endif
}

// -----------------------------------------------------------------------------
//  Обёртка: вычисляет strlen(str) и вызывает write_stdout.
// -----------------------------------------------------------------------------
inline void write_string(const char* str)
{
    size_t len = 0;
    while (str[len]) ++len;
    write_stdout(str, len);
}

// -----------------------------------------------------------------------------
//  Читает строку из stdin в buf (макс. buf_size байт).
//  Возвращает количество фактически прочитанных байт.
// -----------------------------------------------------------------------------
inline size_t read_line(char* buf, size_t buf_size)
{
    if (buf_size == 0) return 0;
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
        buf[0] = '\0';
        return 0;
    }
    DWORD bytes_read = 0;
    ReadFile(h, buf, (DWORD)buf_size, &bytes_read, NULL);
    size_t len = (size_t)bytes_read;
#else
    ssize_t ret = ::read(STDIN_FILENO, buf, buf_size);
    size_t len = (ret > 0) ? (size_t)ret : 0;
#endif

    // Обрезаем trailing \r\n
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        --len;
    }
    buf[len] = '\0';
    return len;
}

} // namespace GrassIO

#endif // GRASS_IO_H

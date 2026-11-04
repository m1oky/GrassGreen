// =============================================================================
//  GrassGreen 4.0 — Freestanding I/O (Win32 native)
//  Файл создан Архитектором. Реализация — Кодер Ядра.
// =============================================================================
#ifndef GRASS_IO_H
#define GRASS_IO_H

#include <windows.h>
#include <cstddef>

namespace GrassIO {

// -----------------------------------------------------------------------------
//  Пишет len байт из data в stdout через Win32 WriteFile.
// -----------------------------------------------------------------------------
inline void write_stdout(const char* data, size_t len)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(h, data, (DWORD)len, &written, NULL);
    }
}

// -----------------------------------------------------------------------------
//  Пишет len байт из data в stderr через Win32 WriteFile.
// -----------------------------------------------------------------------------
inline void write_stderr(const char* data, size_t len)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(h, data, (DWORD)len, &written, NULL);
    }
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
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
        buf[0] = '\0';
        return 0;
    }
    DWORD bytes_read = 0;
    ReadFile(h, buf, (DWORD)buf_size, &bytes_read, NULL);

    // Обрезаем trailing \r\n
    size_t len = (size_t)bytes_read;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        --len;
    }
    buf[len] = '\0';
    return len;
}

} // namespace GrassIO

#endif // GRASS_IO_H

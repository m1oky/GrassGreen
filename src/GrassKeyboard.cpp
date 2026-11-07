// =============================================================================
//  GrassGreen 4.2.0 — Библиотека контроллера клавиатуры (!keyboard!)
//  Реализация взаимодействия с контроллером PS/2 8042, порты 0x60 / 0x64
//  Поддержка раскладок (UTF-8, RU ЙЦУКЕН, EN, CP1251, CP866) и функции use utf -
// =============================================================================
#include "GrassKeyboard.h"
#include "GrassHAL.h"
#include "GrassMMIO.h"
#include "GrassASM.h"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif

namespace GrassKeyboard {

    static bool s_shift_state = false;
    static bool s_caps_state = false;
    static bool s_ctrl_state = false;
    static bool s_alt_state = false;
    static uint8_t s_last_scancode = 0;
    static bool s_key_map[256] = { false };
    static int s_active_layout = 8; // 8 = UTF-8 по умолчанию

    // Таблица PS/2 Set 1 Scancode -> ASCII (Lower & Upper)
    static const char scancode_set1_lower[128] = {
        0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0
    };

    static const char scancode_set1_upper[128] = {
        0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
        '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0
    };

    // Русская раскладка ЙЦУКЕН (UTF-8) для PS/2 Set 1 скан-кодов
    static const char* get_ru_char(uint8_t sc, bool upper) {
        switch (sc) {
            case 0x10: return upper ? "Й" : "й";
            case 0x11: return upper ? "Ц" : "ц";
            case 0x12: return upper ? "У" : "у";
            case 0x13: return upper ? "К" : "к";
            case 0x14: return upper ? "Е" : "е";
            case 0x15: return upper ? "Н" : "н";
            case 0x16: return upper ? "Г" : "г";
            case 0x17: return upper ? "Ш" : "ш";
            case 0x18: return upper ? "Щ" : "щ";
            case 0x19: return upper ? "З" : "з";
            case 0x1A: return upper ? "Х" : "х";
            case 0x1B: return upper ? "Ъ" : "ъ";
            case 0x1E: return upper ? "Ф" : "ф";
            case 0x1F: return upper ? "Ы" : "ы";
            case 0x20: return upper ? "В" : "в";
            case 0x21: return upper ? "А" : "а";
            case 0x22: return upper ? "П" : "п";
            case 0x23: return upper ? "Р" : "р";
            case 0x24: return upper ? "О" : "о";
            case 0x25: return upper ? "Л" : "л";
            case 0x26: return upper ? "Д" : "д";
            case 0x27: return upper ? "Ж" : "ж";
            case 0x28: return upper ? "Э" : "э";
            case 0x29: return upper ? "Ё" : "ё";
            case 0x2C: return upper ? "Я" : "я";
            case 0x2D: return upper ? "Ч" : "ч";
            case 0x2E: return upper ? "С" : "с";
            case 0x2F: return upper ? "М" : "м";
            case 0x30: return upper ? "И" : "и";
            case 0x31: return upper ? "Т" : "т";
            case 0x32: return upper ? "Ь" : "ь";
            case 0x33: return upper ? "Б" : "б";
            case 0x34: return upper ? "Ю" : "ю";
            case 0x35: return upper ? "," : ".";
            default: return nullptr;
        }
    }

    // ASCII -> PS/2 Set 1 Scancode для режима эмуляции в VM
    static uint8_t ascii_to_set1_scancode(int ch) {
        if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
        switch (ch) {
            case 27:  return 0x01; // Esc
            case '1': case '!': return 0x02;
            case '2': case '@': return 0x03;
            case '3': case '#': return 0x04;
            case '4': case '$': return 0x05;
            case '5': case '%': return 0x06;
            case '6': case '^': return 0x07;
            case '7': case '&': return 0x08;
            case '8': case '*': return 0x09;
            case '9': case '(': return 0x0A;
            case '0': case ')': return 0x0B;
            case '-': case '_': return 0x0C;
            case '=': case '+': return 0x0D;
            case '\b': return 0x0E;
            case '\t': return 0x0F;
            case 'Q': return 0x10; case 'W': return 0x11; case 'E': return 0x12;
            case 'R': return 0x13; case 'T': return 0x14; case 'Y': return 0x15;
            case 'U': return 0x16; case 'I': return 0x17; case 'O': return 0x18;
            case 'P': return 0x19; case '[': case '{': return 0x1A;
            case ']': case '}': return 0x1B; case '\n': case '\r': return 0x1C;
            case 'A': return 0x1E; case 'S': return 0x1F; case 'D': return 0x20;
            case 'F': return 0x21; case 'G': return 0x22; case 'H': return 0x23;
            case 'J': return 0x24; case 'K': return 0x25; case 'L': return 0x26;
            case ';': case ':': return 0x27; case '\'': case '"': return 0x28;
            case '`': case '~': return 0x29;
            case 'Z': return 0x2C; case 'X': return 0x2D; case 'C': return 0x2E;
            case 'V': return 0x2F; case 'B': return 0x30; case 'N': return 0x31;
            case 'M': return 0x32; case ',': case '<': return 0x33;
            case '.': case '>': return 0x34; case '/': case '?': return 0x35;
            case ' ': return 0x39;
            default: return 0;
        }
    }

    void initialize() {
        s_shift_state = false;
        s_caps_state = false;
        s_ctrl_state = false;
        s_alt_state = false;
        s_last_scancode = 0;
        for (int i = 0; i < 256; ++i) s_key_map[i] = false;
    }

    // ── 1. Проверка наличия клавиши в буфере (has_key) ──
    bool has_key() {
        if (GrassHAL::is_freestanding()) {
            // В режиме bare-metal опрашиваем бит 0 регистра статуса контроллера 8042 (порт 0x64)
            return (GrassMMIO::in8(0x64) & 0x01) != 0;
        }

#if defined(_WIN32)
        return _kbhit() != 0;
#else
        int bytesWaiting = 0;
        ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);
        return bytesWaiting > 0;
#endif
    }

    // ── 2. Чтение сырого скан-кода (read_scancode) ──
    uint8_t read_scancode() {
        if (GrassHAL::is_freestanding()) {
            if ((GrassMMIO::in8(0x64) & 0x01) == 0) return 0;
            uint8_t sc = GrassMMIO::in8(0x60);
            s_last_scancode = sc;

            // Обработка Break-кодов (отпускание клавиши: старший бит = 1)
            if (sc & 0x80) {
                uint8_t make = sc & 0x7F;
                s_key_map[make] = false;
                if (make == 0x2A || make == 0x36) s_shift_state = false;
                if (make == 0x1D) s_ctrl_state = false;
                if (make == 0x38) s_alt_state = false;
            } else {
                s_key_map[sc] = true;
                if (sc == 0x2A || sc == 0x36) s_shift_state = true;
                if (sc == 0x1D) s_ctrl_state = true;
                if (sc == 0x38) s_alt_state = true;
                if (sc == 0x3A) s_caps_state = !s_caps_state;
            }
            return sc;
        }

#if defined(_WIN32)
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 0 || ch == 224) {
                // Префикс расширенных клавиш (стрелки, F-клавиши)
                ch = _getch();
                uint8_t sc = (uint8_t)ch;
                s_last_scancode = sc;
                return sc;
            }
            uint8_t sc = ascii_to_set1_scancode(ch);
            if (sc == 0) sc = (uint8_t)ch;
            s_last_scancode = sc;
            s_key_map[sc] = true;
            return sc;
        }
#endif
        return 0;
    }

    // ── 3. Блокирующее чтение скан-кода (get_scancode) ──
    uint8_t get_scancode() {
        while (true) {
            if (has_key()) {
                uint8_t sc = read_scancode();
                if (sc != 0 && !(sc & 0x80)) { // Возвращаем только Make-код (нажатие)
                    return sc;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // ── 4. Преобразование скан-кода в ASCII / UTF-8 символ ──
    char scancode_to_ascii(uint8_t scancode, bool shift, bool caps) {
        if (scancode >= 128) return 0;
        bool use_upper = (shift ^ caps);
        char c = use_upper ? scancode_set1_upper[scancode] : scancode_set1_lower[scancode];
        return c;
    }

    std::string scancode_to_char(uint8_t scancode) {
        if (scancode >= 128) return "";
        bool use_upper = (is_shift() ^ is_caps());

        // Если активна русская раскладка (ID 2, 1251, 866, ru)
        if (s_active_layout == 2 || s_active_layout == 1251 || s_active_layout == 866) {
            const char* ru = get_ru_char(scancode, use_upper);
            if (ru) return std::string(ru);
        }

        char c = scancode_to_ascii(scancode, is_shift(), is_caps());
        if (c == 0) return "";
        return std::string(1, c);
    }

    std::string scancode_name(uint8_t scancode) {
        uint8_t make = scancode & 0x7F;
        bool is_break = (scancode & 0x80) != 0;
        std::string name;
        switch (make) {
            case 0x01: name = "Escape"; break;
            case 0x0E: name = "Backspace"; break;
            case 0x0F: name = "Tab"; break;
            case 0x1C: name = "Enter"; break;
            case 0x1D: name = "Ctrl"; break;
            case 0x2A: name = "LeftShift"; break;
            case 0x36: name = "RightShift"; break;
            case 0x38: name = "Alt"; break;
            case 0x39: name = "Space"; break;
            case 0x3A: name = "CapsLock"; break;
            case 0x3B: name = "F1"; break; case 0x3C: name = "F2"; break;
            case 0x3D: name = "F3"; break; case 0x3E: name = "F4"; break;
            case 0x3F: name = "F5"; break; case 0x40: name = "F6"; break;
            case 0x41: name = "F7"; break; case 0x42: name = "F8"; break;
            case 0x43: name = "F9"; break; case 0x44: name = "F10"; break;
            default: {
                char c = scancode_set1_lower[make];
                if (c >= 32 && c <= 126) name = std::string(1, c);
                else name = "Key_0x" + std::to_string((int)make);
            }
        }
        return is_break ? (name + ":Release") : name;
    }

    // ── 5. Высокоуровневый ввод одного символа (getc) ──
    std::string getc() {
        if (GrassHAL::is_freestanding()) {
            while (true) {
                uint8_t sc = get_scancode();
                std::string s = scancode_to_char(sc);
                if (!s.empty()) return s;
            }
        }

#if defined(_WIN32)
        int ch = _getch();
        if (ch == '\r') ch = '\n';
        return std::string(1, (char)ch);
#else
        char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
            if (ch == '\r') ch = '\n';
            return std::string(1, ch);
        }
        return "";
#endif
    }

    // ── 6. Неблокирующий опрос (poll) ──
    std::string poll() {
        if (!has_key()) return "";
        return getc();
    }

    // ── 7. Чтение строки с эхом и Backspace (read_line) ──
    std::string read_line() {
        std::string res;
        while (true) {
            std::string c = getc();
            if (c.empty()) continue;

            if (c == "\n" || c == "\r") {
                std::cout << std::endl;
                break;
            } else if (c == "\b" || c == "\x7F") {
                if (!res.empty()) {
                    res.pop_back();
                    std::cout << "\b \b" << std::flush;
                }
            } else {
                res += c;
                std::cout << c << std::flush;
            }
        }
        return res;
    }

    // ── 8. Клавиши-модификаторы ──
    bool is_shift() {
        if (GrassHAL::is_freestanding()) return s_shift_state;
#if defined(_WIN32)
        return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
#else
        return s_shift_state;
#endif
    }

    bool is_caps() {
        if (GrassHAL::is_freestanding()) return s_caps_state;
#if defined(_WIN32)
        return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
#else
        return s_caps_state;
#endif
    }

    bool is_ctrl() {
        if (GrassHAL::is_freestanding()) return s_ctrl_state;
#if defined(_WIN32)
        return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
#else
        return s_ctrl_state;
#endif
    }

    bool is_alt() {
        if (GrassHAL::is_freestanding()) return s_alt_state;
#if defined(_WIN32)
        return (GetKeyState(VK_MENU) & 0x8000) != 0;
#else
        return s_alt_state;
#endif
    }

    bool is_key_pressed(uint8_t scancode) {
        return s_key_map[scancode & 0x7F];
    }

    // ── 9. Команды контроллера клавиатуры 8042 ──
    bool wait_ack() {
        for (int i = 0; i < 1000; ++i) {
            if ((GrassMMIO::in8(0x64) & 0x01) != 0) {
                if (GrassMMIO::in8(0x60) == 0xFA) return true;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return false;
    }

    void set_leds(bool caps, bool num, bool scroll) {
        if (!GrassHAL::is_freestanding()) return;
        // Команда 0xED: установка индикаторов LED
        GrassMMIO::out8(0x60, 0xED);
        wait_ack();
        uint8_t led_byte = (caps ? 4 : 0) | (num ? 2 : 0) | (scroll ? 1 : 0);
        GrassMMIO::out8(0x60, led_byte);
        wait_ack();
    }

    void reset() {
        if (!GrassHAL::is_freestanding()) return;
        GrassMMIO::out8(0x60, 0xFF);
        wait_ack();
    }

    bool self_test() {
        if (!GrassHAL::is_freestanding()) return true;
        // Команда 0xAA: самодиагностика контроллера
        GrassMMIO::out8(0x64, 0xAA);
        for (int i = 0; i < 1000; ++i) {
            if ((GrassMMIO::in8(0x64) & 0x01) != 0) {
                return (GrassMMIO::in8(0x60) == 0x55);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return false;
    }

    // ── 10. Управление кодировкой и раскладкой клавиатуры (use utf - ...) ──
    void set_layout(int layout_id) {
        s_active_layout = layout_id;
#if defined(_WIN32)
        if (layout_id == 8 || layout_id == 65001 || layout_id == 0) {
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
        } else if (layout_id == 2 || layout_id == 1251) {
            SetConsoleOutputCP(1251);
            SetConsoleCP(1251);
        } else if (layout_id == 866) {
            SetConsoleOutputCP(866);
            SetConsoleCP(866);
        } else if (layout_id == 1 || layout_id == 1252 || layout_id == 437) {
            SetConsoleOutputCP(437);
            SetConsoleCP(437);
        }
#endif
    }

    void set_layout_by_name(const std::string& raw) {
        std::string s = raw;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "utf-8" || s == "utf8" || s == "utf_8" || s == "unicode" || s == "8" || s == "65001") {
            set_layout(8);
        } else if (s == "ru" || s == "rus" || s == "russian" || s == "cyrillic" || s == "2" || s == "1251") {
            set_layout(2);
        } else if (s == "866" || s == "dos" || s == "oem") {
            set_layout(866);
        } else if (s == "en" || s == "eng" || s == "english" || s == "us" || s == "1" || s == "1252" || s == "437") {
            set_layout(1);
        } else if (s == "de" || s == "ger" || s == "german" || s == "3") {
            set_layout(3);
        } else if (s == "fr" || s == "fra" || s == "french" || s == "4") {
            set_layout(4);
        } else if (s == "es" || s == "spa" || s == "spanish" || s == "5") {
            set_layout(5);
        } else {
            char* end_ptr = nullptr;
            long val = strtol(s.c_str(), &end_ptr, 10);
            if (end_ptr != s.c_str() && *end_ptr == '\0') {
                set_layout((int)val);
            } else {
                set_layout(8);
            }
        }
    }

    int get_layout() {
        return s_active_layout;
    }

    std::string get_layout_name() {
        switch (s_active_layout) {
            case 2: case 1251: case 866: return "ru";
            case 1: case 1252: case 437: return "en";
            case 3: return "de";
            case 4: return "fr";
            case 5: return "es";
            default: return "utf-8";
        }
    }

} // namespace GrassKeyboard

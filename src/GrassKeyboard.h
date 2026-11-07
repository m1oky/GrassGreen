// =============================================================================
//  GrassGreen 4.2.0 — Библиотека контроллера клавиатуры (!keyboard!)
//  Низкоуровневый опрос портов PS/2 (0x60, 0x64), скан-коды и ввод символов
//  100% C++20 без внешних зависимостей (работает в VM и на голом железе)
// =============================================================================
#pragma once

#include "GrassSyntax.h"
#include <string>
#include <cstdint>

namespace GrassKeyboard {

    // Инициализация подсистемы клавиатуры
    void initialize();

    // ── 1. Низкоуровневый опрос портов контроллера ──
    bool has_key();              // Проверка наличия данных в буфере (порт 0x64 бит 0)
    uint8_t read_scancode();     // Чтение скан-кода из порта 0x60 (неблокирующее)
    uint8_t get_scancode();      // Блокирующее ожидание и чтение скан-кода

    // ── 2. Преобразование скан-кодов PS/2 (Set 1) в символы ──
    char scancode_to_ascii(uint8_t scancode, bool shift, bool caps);
    std::string scancode_to_char(uint8_t scancode);
    std::string scancode_name(uint8_t scancode);

    // ── 3. Высокоуровневый ввод ──
    std::string getc();          // Блокирующее чтение одного символа
    std::string poll();          // Неблокирующий опрос: символ или пустая строка ""
    std::string read_line();     // Чтение строки до нажатия Enter с эхом и Backspace

    // ── 4. Состояние клавиш-модификаторов ──
    bool is_shift();
    bool is_caps();
    bool is_ctrl();
    bool is_alt();
    bool is_key_pressed(uint8_t scancode);

    // ── 5. Управление контроллером 8042 ──
    void set_leds(bool caps, bool num, bool scroll);
    void reset();
    bool self_test();
    bool wait_ack();

    // ── 6. Управление кодировкой и раскладкой клавиатуры (use utf - ...) ──
    void set_layout(int layout_id);
    void set_layout_by_name(const std::string& name);
    int get_layout();
    std::string get_layout_name();

} // namespace GrassKeyboard

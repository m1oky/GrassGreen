#ifndef GRASS_GUI_ENGINE_H
#define GRASS_GUI_ENGINE_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>

// Структура элемента интерфейса GrassGreen UI
struct UIElement {
    std::string type;          // "button", "label", "enter", "icon", "video"
    std::string id;            // ID или имя элемента (id / name)
    std::wstring text;         // Текст элемента
    int x = 0, y = 0;          // Координаты (window-ob)
    int w = 160, h = 38;       // Размеры (size, size-top)
    int align = 0;             // 0 - нет, 1 - middle, 2 - left, 3 - right
    
    COLORREF txt_color = RGB(0, 255, 204); // Цвет текста (text-color)
    COLORREF btn_color = RGB(22, 22, 30);  // Цвет кнопки (button-color)
    bool has_custom_btn_color = false;

    int f_size = 18;                       // Размер шрифта (font-size)
    std::wstring f_style = L"Segoe UI";    // Шрифт (font-style)
    int radius = 0;                        // Скругление углов в px (radius)
    int pad = 0;                           // Внутренний отступ текста (pad)
    bool has_shadow = false;               // Тень элемента (shadow)
    bool is_glass = false;                 // Полупрозрачность (glass)
    
    bool animated = false;                 // Анимация появления (animate-..)
    int alpha = 255;                       // Текущая прозрачность для анимации

    // Действия и события
    bool has_click_action = false;         // Связка (click-ob)
    std::string target_input_id;
    std::string target_label_id;
    bool trigger_clear_on_click = false;   // Очистка при клике (clear)

    bool has_switch_event = false;         // Переключение экрана (switch)
    std::string switch_target;

    bool has_run_action = false;           // Запуск внешнего .exe (run)
    std::string run_target;

    std::wstring media_path;               // Путь к фото/видео (icon, video)
    HWND hwnd = NULL;                      // Дескриптор дочернего окна Win32
    bool is_hovered = false;
    bool is_placeholder = true;
};

namespace GrassGUI {
    // Полный сброс состояния интерфейса
    void reset();

    // Обмен переменными между виртуальной машиной GrassGreen и GUI
    void set_variable(const std::string& name, const std::string& val);
    std::string get_input_value(const std::string& element_id);

    // Построчное выполнение команд разметки interface.txt
    void exec_command(const std::string& raw_line);

    // Запуск UI из массива строк (например, из блока кода в ВМ)
    int run_from_lines(const std::vector<std::string>& lines);

    // Запуск UI напрямую из .grass файла
    int run_from_file(const std::string& filepath);

    // Отрисовка и запуск цикла событий текущего настроенного окна
    int show_window();
}

#endif // GRASS_GUI_ENGINE_H
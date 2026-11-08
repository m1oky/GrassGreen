#ifndef GRASS_GUI_ENGINE_H
#define GRASS_GUI_ENGINE_H

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdint>
typedef void* HWND;
typedef uint32_t COLORREF;
#ifndef RGB
#define RGB(r,g,b) ((COLORREF)(((uint8_t)(r)|((uint16_t)((uint8_t)(g))<<8))|(((uint32_t)((uint8_t)(b))<<16))))
#endif
#ifndef GetRValue
#define GetRValue(rgb) ((uint8_t)(rgb))
#define GetGValue(rgb) ((uint8_t)(((uint16_t)(rgb)) >> 8))
#define GetBValue(rgb) ((uint8_t)((rgb)>>16))
#endif
#endif
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// Режимы поверхности отображения (Universal Surface Engine)
enum class SurfaceMode {
    Auto,           // Авто-определение (Windows/Linux -> Windowed, Freestanding -> Freestanding)
    Windowed,       // Нативная коробка ОС с заголовком и кнопками Win32, BitBlt из виртуального буфера
    Freestanding,   // Голый фреймбуфер без ОС, прямая запись в VESA/GOP через GrassMMIO
    Borderless      // Кастомный оверлей без системной рамки, изумрудные элементы и эффект стекла
};

// Стили оформления заголовка / шапки окна (VD+ Header System)
enum class HeaderStyle {
    Modern,         // Тонкая темная шапка с минималистичными кнопками и заголовком (по умолчанию при скруглении)
    Dots,           // Стиль macOS: 3 цветные круглые точки (красная, желтая, зеленая) с hover-эффектами
    Minimal,        // Синоним Dots
    Glass,          // Акриловое матовое стекло (Glassmorphism) с отражением и разделителем (окно остается сплошным)
    Emerald,        // Фирменный градиент GrassGreen с изумрудным неоновым свечением
    Native,         // Классическая системная рамка Windows 98/Win32
    None            // Без шапки (frameless). Окно свободно перетаскивается за любую пустую область!
};

// Структура частицы для динамического фонового свечения
struct Particle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float size = 3.0f;
    float alpha = 120.0f;
    float base_alpha = 120.0f;
    float pulse_speed = 0.05f;
    float pulse_t = 0.0f;
};

// Структура элемента интерфейса GrassGreen UI (VD+ Compositor)
struct UIElement {
    std::string type;          // "button", "label", "enter", "icon", "video", "svg"
    std::string id;            // ID или имя элемента (id / name)
    std::wstring text;         // Текст элемента
    int x = 0, y = 0;          // Координаты (window-ob)
    int orig_y = 0;            // Исходный Y для анимации сдвига (slide-up)
    int orig_design_x = -1;
    int orig_design_y = -1;
    int orig_design_w = -1;
    int orig_design_h = -1;
    int orig_design_f_size = 15;
    int w = 160, h = 38;       // Размеры (size, size-top)
    int align = 0;             // 0 - нет, 1 - middle, 2 - left, 3 - right
    
    std::string tab_name = "main"; // Вкладка, к которой привязан элемент (Tabs System)

    COLORREF txt_color = RGB(244, 244, 245); // Цвет текста (text-color, default Zinc 100)
    COLORREF btn_color = RGB(22, 163, 74);   // Цвет кнопки/блока (button-color, default Emerald 600)
    bool has_custom_btn_color = false;

    int f_size = 15;                       // Размер шрифта (font-size)
    std::wstring f_style = L"Segoe UI";    // Шрифт (font-style)
    bool font_bold = true;                 // Жирный шрифт (font-weight: bold)
    int radius = 0;                        // Скругление углов в px (0 по умолчанию для текстов, 8 для кнопок и инпутов)
    int pad = 12;                          // Внутренний отступ текста (pad)
    bool has_shadow = false;               // Тень элемента (shadow)
    int shadow_blur = 12;                  // Радиус размытия тени
    COLORREF shadow_color = RGB(0, 0, 0);  // Цвет тени
    bool is_glass = false;                 // Эффект матового стекла (glass)
    
    // VD+ (Video Graphics Plus) Стилизация уровня CSS
    bool has_gradient = false;             // CSS Линейный градиент (gradient)
    COLORREF grad_color1 = RGB(34, 197, 94); // Начальный цвет градиента
    COLORREF grad_color2 = RGB(21, 128, 61); // Конечный цвет градиента
    float grad_angle = 90.0f;              // Угол градиента (в градусах)

    bool has_border = false;               // CSS Рамка (border)
    int border_width = 1;                  // Толщина рамки
    COLORREF border_color = RGB(39, 39, 42); // Цвет рамки (#27272a)

    bool has_glow = false;                 // Неоновое свечение (glow)
    int glow_radius = 8;                   // Радиус свечения
    COLORREF glow_color = RGB(34, 197, 94); // Цвет свечения (#22c55e)

    bool has_hover_color = false;          // Цвет при наведении (hover-color)
    COLORREF hover_color = RGB(74, 222, 128); // #4ade80

    bool has_active_color = false;         // Цвет при клике (active-color)
    COLORREF active_color = RGB(21, 128, 61); // #15803d

    std::wstring placeholder = L"";        // Подсказка для поля ввода (placeholder)
    COLORREF placeholder_color = RGB(113, 113, 122); // Zinc 500
    bool is_focused = false;               // Активен ли фокус ввода
    size_t caret_pos = 0;                  // Позиция каретки
    bool is_pressed = false;               // Нажата ли кнопка прямо сейчас

    // CSS Анимации (fade-in, slide-up, pulse, glow)
    bool animated = false;                 // Анимация активна
    std::string anim_type = "";            // "fade-in", "slide-up", "pulse", "glow"
    int anim_duration = 300;               // Длительность анимации (мс)
    float anim_progress = 1.0f;            // 0.0 .. 1.0
    float pulse_phase = 0.0f;              // Текущая фаза пульсации
    int alpha = 255;                       // Текущая прозрачность для анимации

    // Действия и события
    bool has_click_action = false;         // Связка (click-ob)
    std::string target_input_id;
    std::string target_label_id;
    bool trigger_clear_on_click = false;   // Очистка при клике (clear)

    bool has_tab_switch = false;           // Переключение вкладки (click-switch / switch)
    std::string target_tab_name;

    bool has_switch_event = false;         // Переключение файла экрана (switch)
    std::string switch_target;

    bool has_run_action = false;           // Запуск внешнего .exe (run)
    std::string run_target;

    std::wstring media_path;               // Путь к фото/видео/svg (icon, video, svg)
    bool is_svg = false;                   // Флаг векторного SVG
    HWND hwnd = NULL;                      // Дескриптор дочернего окна (если используется)
    bool is_hovered = false;
    bool is_placeholder = true;

    // Виджет часов (Clock Widget)
    bool is_clock = false;                 // Флаг виджета часов
    std::string clock_format = "HH:mm:ss"; // Формат времени ("HH:mm:ss", "HH:mm", "date", "datetime")
};

namespace GrassGUI {
    // Управление режимом поверхности отображения (surface)
    void set_surface_mode(const std::string& mode);
    void set_surface_mode(SurfaceMode mode);
    SurfaceMode get_surface_mode();
    SurfaceMode resolve_active_mode();
    std::string get_surface_mode_name();

    // Стили и кастомизация шапки окна (header)
    void set_header_style(const std::string& style);
    void set_header_style(HeaderStyle style);
    HeaderStyle get_header_style();
    void set_header_color(COLORREF col);
    void set_header_text_color(COLORREF col);
    void set_header_height(int h);
    void set_header_buttons(const std::string& mode); // "all", "close", "none", "left", "right"
    void set_header_buttons_pos(const std::string& pos); // "left", "right", "center"
    std::string get_header_buttons_pos();
    void set_header_buttons_offset(int offset);
    int get_header_buttons_offset();
    void set_header_title_align(const std::string& align); // "left", "center", "right"
    std::string get_header_title_align();

    // Кастомная рамка всего окна
    void set_window_border(int width, COLORREF col);

    // Управление графическим движком VD+ (Video Graphics Plus)
    void set_vd_plus(bool enable);
    bool is_vd_plus_enabled();

    // High-DPI и автоматическая адаптация дисплея (auto_size_display)
    void enable_high_dpi_awareness();
    float get_dpi_scale();
    void auto_size_display(bool enable = true);
    bool is_auto_size_display_enabled();

    // Скругление углов всего окна приложения (border-radius-app)
    void set_app_border_radius(int radius);
    int get_app_border_radius();

    // Настройка фреймбуфера для режима Freestanding (VESA / GOP physical memory)
    void set_freestanding_framebuffer(uint64_t physical_addr, int width, int height, int pitch);
    uint64_t get_freestanding_framebuffer_addr();

    // Система вкладок (Tabs System: tab / switch)
    void set_current_tab(const std::string& tab_name);
    std::string get_current_tab();
    void switch_tab(const std::string& tab_name);

    // Система фоновых живых частиц (Particle Engine)
    void enable_particles(int count = 35, COLORREF col = RGB(34, 197, 94), float speed = 1.0f);
    bool are_particles_enabled();
    void update_particles_for_size(int w, int h);

    // Режим полного экрана / максимизации (fullscreen / fullscrean)
    void set_fullscreen(bool enable = true);
    bool is_fullscreen();

    // Автоматическое центрирование всех элементов (layout center / align-all)
    void set_auto_center_elements(bool enable = true);
    bool is_auto_center_enabled();

    // Z-Index и слои окон (Z-Order, активация при клике, поверх всех окон)
    void set_window_topmost(bool topmost);
    bool is_window_topmost();
    void set_window_z_index(int z);
    int get_window_z_index();
    void bring_window_to_front();

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

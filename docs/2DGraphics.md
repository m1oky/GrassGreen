# Библиотека !2DGraphics! — Управление 2D графикой

- `create_canvas(width, height)` — Создание нового холста для рисования.
- `draw_line(x1, y1, x2, y2)` — Рисование линии между двумя точками.
- `draw_circle(x, y, radius)` — Рисование круга.
- `draw_rectangle(x, y, width, height)` — Рисование прямоугольника.
- `fill_color(color)` — Установка цвета заливки.
- `save_image(path)` — Сохранение холста в файл.
- `gfx_init(title, width, height)` — Инициализация графического окна с двойной буферизацией.
- `gfx_running()` — Проверка активности окна (1 если открыто, 0 если закрыто).
- `gfx_clear(hex_color)` — Очистка кадра заданным цветом.
- `gfx_line(x1, y1, x2, y2, color, thickness)` — Отрисовка линии с заданной толщиной.
- `gfx_rect(x, y, w, h, color)` — Отрисовка прямоугольника.
- `gfx_circle(x, y, radius, color)` — Отрисовка круга.
- `gfx_text(x, y, text, size, color)` — Вывод текстовой строки на холст.
- `gfx_key(key_name)` — Проверка нажатия клавиши на клавиатуре (WASD, стрелочки, ESC).
- `gfx_collide(x1, y1, w1, h1, x2, y2, w2, h2)` — Проверка пересечения двух прямоугольных объектов (AABB-коллизия).

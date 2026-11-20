# Модуль !2DGraphics! — 2D-движок и спрайтовая анимация

Модуль `!2DGraphics!` предоставляет кроссплатформенный 2D-движок для создания игр, визуализаций и интерфейсов с аппаратным выводом на Windows и Linux, а также программным фреймбуфером для Bare-Metal.

---

## Окно, цикл и ввод

- `g2d_window(w, h, title)` — создание графического окна заданного разрешения.
- `g2d_is_open()` — возвращает 1, пока окно активно.
- `g2d_clear(r, g, b)` — очистка экрана RGB-цветом (от 0.0 до 1.0).
- `g2d_present()` — отображение сформированного кадра на экране.
- `g2d_dt()` — дельта времени между кадрами (в секундах).
- `g2d_key_down(key)` — опрос нажатия клавиши (`"SPACE"`, `"ENTER"`, `"UP"`, `"DOWN"`, `"LEFT"`, `"RIGHT"`, `"W"`, `"A"`, `"S"`, `"D"`).
- `g2d_mouse_x()` / `g2d_mouse_y()` — текущие координаты курсора мыши в окне.
- `g2d_mouse_btn(btn)` — проверка зажатия кнопок мыши (0 — левая, 1 — правая).
- `g2d_bind_framebuffer(addr, w, h, pitch)` — прямое связывание с видеобуфером для Bare-Metal.

---

## Текстуры и спрайты

- `g2d_load_tex(path)` — загрузка изображения в текстуру.
- `g2d_draw_sprite(tex, x, y, w, h, angle, alpha)` — отрисовка спрайта с центром в `(x, y)`, масштабированием, поворотом и прозрачностью `alpha` [0..1].
- `g2d_draw_frame(tex, fx, fy, fw, fh, x, y, w, h)` — покадровая анимация: вырезание и отрисовка кадра из спрайт-листа.

---

## Камера и спецэффекты

- `g2d_cam_set(x, y, zoom, angle)` — позиционирование 2D-камеры в мировых координатах.
- `g2d_cam_shake(intensity, duration)` — эффект тряски экрана заданной силы и длительности.
- `g2d_emit_particles(x, y, count, color, speed, lifetime)` — встроенная система взрывов и частиц.
- `g2d_update_particles(dt)` — обновление и анимация активных частиц.

---

## Пример использования

```grass
!GrassGreen!
!2DGraphics!

int main() {
    g2d_window(800, 600, "GrassGreen 2D Demo");
    take player_x = 400.0;
    take player_y = 300.0;

    while (g2d_is_open()) {
        take dt = g2d_dt();

        if (g2d_key_down("LEFT")) { player_x -= 200.0 * dt; }
        if (g2d_key_down("RIGHT")) { player_x += 200.0 * dt; }
        if (g2d_key_down("UP")) { player_y -= 200.0 * dt; }
        if (g2d_key_down("DOWN")) { player_y += 200.0 * dt; }

        if (g2d_key_down("SPACE")) {
            g2d_emit_particles(player_x, player_y, 20, 16776960, 150.0, 0.8);
            g2d_cam_shake(5.0, 0.2);
        }

        g2d_update_particles(dt);

        g2d_clear(0.1, 0.1, 0.15);
        g2d_draw_sprite(0, player_x, player_y, 32.0, 32.0, 0.0, 1.0);
        g2d_present();
    }
    return 0;
}
```

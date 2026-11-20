# Модуль !physics! — Позиционная физика (XPBD)

Модуль `!physics!` реализует переносимый физический движок реального времени на базе Extended Position-Based Dynamics (XPBD). Не зависит от операционной системы и одинаково работает на Windows, Linux и Bare-Metal.

---

## Создание мира и шаг симуляции

- `phys_world(gravity_y)` — создает новый физический мир с заданной гравитацией (например, `-9.81`). Возвращает дескриптор мира `world_id`.
- `phys_step(world, dt, substeps)` — выполняет шаг симуляции с внутренним делением на `substeps` для устранения нестабильности и рывков.

---

## Тела и разрушаемость

- `phys_box(world, pos_vec3, size_vec3, material, break_force)` — создает кубическое тело с материалом (`"static"`, `"steel"`, `"wood"`, `"rubber"`, `"glass"`). Если `break_force > 0`, тело разрушается при сильном ударе.
- `phys_sphere(world, pos_vec3, radius, material)` — создает физическую сферу с упругим отскоком.
- `phys_cloth(world, pos_vec3, width, height, segments)` — симуляция сетки ткани / флага на связях.
- `phys_link(world, body_a, body_b, stiffness, break_force)` — связывает два тела ограничением расстояния с заданной жесткостью.

---

## Управление и интеграция с графикой

- `phys_impulse(body, force_vec3)` — прикладывает мгновенный импульс силы к телу.
- `phys_raycast(world, origin_vec3, dir_vec3, max_dist)` — бросает луч и проверяет пересечение с объектами.
- `phys_get_pos(body)` — возвращает текущий вектор координат тела `[x, y, z]`.
- `phys_get_rot(body)` — возвращает текущие углы поворота тела `[rx, ry, rz]`.

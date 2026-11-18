# Библиотека !interface! — Универсальный реактивный GUI (VD+ Engine)

> **Статус:** Модернизация ядра интерфейса v4.3 (Графический движок VD+ — Video Graphics Plus)  
> **Назначение:** Создание высокопроизводительных, красивых и реактивных графических приложений, системных мониторов (Диспетчер задач), синтезаторов звука, IDE и визуальных оболочек операционной системы без внешних зависимостей.  
> **Стиль:** Графика уровня современного CSS (Tailwind/Figma): субпиксельное сглаживание углов, линейные градиенты, многослойные тени (Box Shadow), эффект матового стекла (Aero/Mica), неоновое свечение (Glow), векторная отрисовка SVG, поддержка любых форматов видео, плавающий Tooltip, кастомные курсоры и настраиваемая шапка окна.

---

## 1. Реактивность, двусторонняя связь с переменными `take` и API по ID

В GrassGreen v4.3 виджеты обладают полноценной двусторонней связью с переменными `take` и поддерживают динамическое управление по их `id(...)`:

### Функции манипуляции виджетами
- `set_text(widget_id, value)` — динамически меняет отображаемый текст у `label`, `button`, `enter` или `checkbox`. Числа, строки и результаты выражений автоматически конвертируются в текст, и окно мгновенно перерисовывается без мерцания.
- `get_text(widget_id)` — возвращает текущую строку из поля `enter` или текст любого другого виджета в переменную `take`.
- `set_val(widget_id, number | string)` — устанавливает числовое или строковое значение виджета:
  - `progress` — процент заполнения (0..100).
  - `slider` — положение ползунка в заданном диапазоне min..max.
  - `checkbox` — состояние флага (1/0 или true/false).
  - `enter` — значение поля ввода.
- `get_val(widget_id)` — возвращает числовое значение (`double`): процент для `progress`, позицию для `slider`, 1 или 0 для `checkbox`, либо число из `enter`.
- `set_color(widget_id, hex)` — динамически меняет цвет текста элемента (например, подсветка красным `#ef4444` при критической нагрузке RAM > 80%).
- `set_bg(widget_id, hex)` — динамически меняет цвет фона или кнопки.
- `set_visible(widget_id, bool)` — управление видимостью элемента.
- `hide(widget_id)` / `show(widget_id)` — удобные функции для скрытия и отображения элементов по ID.
- `is_checked(widget_id)` — возвращает логическое значение (`true` / `false`), отмечен ли чекбокс.

### Интерполяция переменных `{var}`
Внутри строк `label "Использовано RAM: {ram_used} МБ"` или заголовков автоматически подставляются актуальные значения локальных и глобальных переменных `take`, а также системные метрики `!hardware!`:
- `{hw_cpu_cores}` — количество физических ядер CPU.
- `{hw_cpu_threads}` — количество логических потоков CPU.
- `{hw_cpu_freq}` — текущая тактовая частота процессора в МГц.
- `{hw_ram_used}` — объем занятой оперативной памяти в МБ.
- `{hw_ram_total}` — общий объем оперативной памяти в МБ.

---

## 2. Неблокирующий таймер обновлений: `every(ms)`

Для мониторинга системных ресурсов в реальном времени (Диспетчер задач, графики, таймеры) реализован неблокирующий блок `every(milliseconds)`:

```grass
every(1000) {
    take ram = ram_used_mb();
    set_text("ram_lbl", "RAM: " + to_str(ram) + " МБ");
    set_val("ram_progress", (ram * 100) / ram_total_mb());
    
    if (ram > 4000) {
        set_color("ram_lbl", "#ef4444");
    } else {
        set_color("ram_lbl", "#22c55e");
    }
}
```
- Интегрирован в нативный 60 FPS цикл сообщений Win32 (`WM_TIMER`).
- Не подвешивает интерфейс и окно, ввод мыши и клавиатуры остается отзывчивым.
- Имеет прямой доступ на чтение и запись к переменным родительской области видимости.

---

## 3. Обработчики событий и звук кнопок

### Блоки `on_click` и `on_change`
В отличие от старых версий, код внутри `button` не вызывается при создании окна. Для выполнения логики по нажатию используется явный блок `on_click`:

```grass
button "Запустить анализ" {
    id(btn_start);
    tooltip("Нажмите для запуска фонового теста");
    cursor("pointer");
    click-sound(false); // Отключение звука клика

    on_click {
        counter = counter + 1;
        set_text("status_lbl", "Выполнено кликов: " + to_str(counter));
        set_bg("btn_start", "#15803d");
    }
}
```

Для полей ввода и слайдеров поддерживается блок `on_change`:
```grass
slider(0, 100, 50) {
    id(volume_slider);
    on_change {
        take cur_vol = get_val("volume_slider");
        set_text("vol_lbl", "Громкость: " + to_str(cur_vol) + "%");
    }
}
```

### Настройка звука кнопок и прямая привязка синтеза (Sound & Synth Binding)

В GrassGreen v4.3 звук и процедурный синтез можно привязывать напрямую к элементам UI без написания ручных обработчиков:

#### Декларативные директивы внутри виджетов:
- `click-synth(waveform, freq_hz, duration_ms, volume)` — воспроизводит процедурный синтез звуковой волны напрямую при клике по кнопке (например, `click-synth("sine", 440, 150, 80)`).
- `click-sound("path/to/sound.wav")` — проигрывание аудиофайла при клике. Также сохранена обратная совместимость с `click-sound(true)` (дефолтный щелчок) и `click-sound(false)` (выключение).
- `hover-synth(waveform, freq_hz, duration_ms, volume)` — воспроизведение синтезированного звука при наведении курсора мыши на виджет.
- `hover-sound("path/to/hover.wav")` — проигрывание аудиофайла при наведении мыши.

```grass
button "Нота До (C)" {
    id(btn_c);
    size(100, 50);
    click-synth("sine", 261.63, 180, 85);
    hover-synth("sine", 523.25, 30, 40);
    tooltip("Нажмите для воспроизведения ноты До");
    on_click {
        set_text("status_lbl", "Сыграна нота До");
    }
}
```

#### Программное связывание из кода GrassGreen:
- `bind_synth(target_id, waveform, freq_hz, duration_ms, volume)` — привязывает синтез к элементу по ID, либо ко всем кнопкам сразу при передаче `"all"` или `"buttons"`.
- `bind_sound(target_id, "path.wav")` — привязывает аудиофайл к элементу по ID, либо ко всем кнопкам (`"all"` / `"buttons"`).
- `attach_sound(func_name, waveform, freq_hz, duration_ms, volume)` — привязывает синтез волны к вызову конкретной функции. Каждый раз при вызове этой функции (из `on_click` или логики) автоматически звучит привязанный эффект.

```grass
// Установить стандартный звук клика для всех кнопок окна
bind_synth("buttons", "square", 880, 40, 30);

// Привязать звук к функции сохранения
attach_sound(save_data, "triangle", 587, 80, 70);
```

> **Примечание:** Все вызовы аудио внутри GUI-потока (включая `synth_play` и `tone` внутри `on_click` и пользовательских функций) выполняются неблокирующе в фоновом режиме, сохраняя плавный 60 FPS рендеринг окна без подтормаживаний.

---

## 4. Постоянный каркас и исправление переключения вкладок (`tab`)

Элементы, объявленные непосредственно внутри блока `window { ... }` вне директив `tab`, образуют **постоянный каркас интерфейса** (Persistent Layout Frame):
- Они остаются видимыми при переключении любых вкладок (шапка, статус-бар, боковое меню, кнопки вкладок).
- Директива `tab "name" { ... }` изолирует элементы только для соответствующей вкладки.
- Переключение вкладок производится через `click-switch("tab_name")` у кнопки или функцию `switch("tab_name")`.

```grass
window {
    // Этот заголовок будет виден ВСЕГДА:
    label "Моя программа" { window-ob(20, 20); size(200, 30); }
    button "Вкладка 1" { window-ob(20, 60); click-switch("tab1"); }
    button "Вкладка 2" { window-ob(130, 60); click-switch("tab2"); }

    tab "tab1" {
        label "Контент вкладки 1" { window-ob(20, 110); }
    }

    tab "tab2" {
        label "Контент вкладки 2" { window-ob(20, 110); }
    }
}
```

---

## 5. Базовые виджеты и UX-модификаторы

### Новые виджеты
- `progress(percent)` — современный прогресс-бар с градиентным заполнением и отображением процентов:
  ```grass
  progress(65) {
      id(download_bar);
      window-ob(30, 100);
      size(400, 22);
      radius(6);
  }
  ```
- `slider(min, max, default)` — интерактивный ползунок для настройки параметров:
  ```grass
  slider(0, 100, 50) {
      id(brightness_slider);
      window-ob(30, 140);
      size(300, 24);
      cursor("pointer");
  }
  ```
- `checkbox("Текст надписи")` — флажок с плавной анимацией отметки:
  ```grass
  checkbox("Запускать при старте системы") {
      id(chk_autostart);
      window-ob(30, 180);
      size(250, 24);
      val("1"); // По умолчанию отмечен
      cursor("pointer");
  }
  ```
- `list` / `scroll-box` — прокручиваемый список элементов с поддержкой выбора и прокрутки колесиком мыши:
  ```grass
  list {
      id(services_list);
      window-ob(30, 220);
      size(500, 160);
      radius(8);
      item("Служба ядра (Kernel)");
      item("Графический сервер (VD+)");
      item("Сетевой стек (GrassNet)");
  }
  ```
  Функции для списков:
  - `add_item(list_id, text)` — добавить новый элемент в список.
  - `clear_list(list_id)` — очистить весь список.
  - `get_selected(list_id)` — получить строку выбранного элемента.

### UX-модификаторы
- `tooltip("Текст всплывающей подсказки")` — автоматический показ всплывающей подсказки в полупрозрачном контейнере при наведении курсора мыши на виджет.
- `cursor("pointer" | "text" | "default" | "none")` — смена системного курсора мыши при наведении на элемент (рука с пальцем `pointer`, текстовая каретка `text`, обычная стрелка `default`, либо запрещающий знак `none`).
- `disabled(true | false)` — блокировка элемента: виджет становится неактивным, не реагирует на клики и ввод текста, курсор меняется на системный запрет.

---

## 6. Полный пример: Реактивный Диспетчер задач

```grass
!GrassGreen!
!interface!
!hardware!

int main() {
    take counter = 0;

    window {
        title("Диспетчер задач GrassGreen");
        size(640, 520);
        theme("dark");
        border-radius-app(12);

        // Постоянная шапка
        label "Системный монитор" {
            window-ob(30, 20);
            size(300, 26);
            text-color(#22c55e);
            font-size(18);
            font-weight("bold");
        }

        checkbox "Авто-обновление" {
            id(chk_refresh);
            window-ob(450, 20);
            size(160, 24);
            val("1");
            cursor("pointer");
        }

        button "Метрики" {
            window-ob(30, 60);
            size(120, 32);
            click-switch("metrics");
            click-sound(false);
            cursor("pointer");
        }

        button "Процессы" {
            window-ob(160, 60);
            size(120, 32);
            click-switch("procs");
            click-sound(false);
            cursor("pointer");
        }

        // Вкладка Метрик
        tab "metrics" {
            label "Память: {hw_ram_used} МБ из {hw_ram_total} МБ" {
                id(ram_lbl);
                window-ob(30, 110);
                size(580, 24);
                font-size(14);
            }

            progress(50) {
                id(ram_bar);
                window-ob(30, 140);
                size(580, 20);
                tooltip("Загрузка RAM");
            }

            button "Сбросить кэш" {
                id(btn_flush);
                window-ob(30, 180);
                size(200, 36);
                cursor("pointer");
                click-sound(false);
                on_click {
                    set_text("btn_flush", "Очищено!");
                    set_bg("btn_flush", "#2563eb");
                }
            }
        }

        // Вкладка Процессов
        tab "procs" {
            list {
                id(plist);
                window-ob(30, 110);
                size(580, 220);
                item("grassc.exe - 14 МБ");
                item("GrassVM.Core - 38 МБ");
                item("System Daemon - 2 МБ");
            }

            button "Добавить процесс" {
                window-ob(30, 345);
                size(180, 34);
                cursor("pointer");
                click-sound(false);
                on_click {
                    add_item("plist", "Worker Task #" + to_str(counter));
                }
            }
        }

        // Фоновый реактивный таймер
        every(1000) {
            counter = counter + 1;
            take used = ram_used_mb();
            take tot = ram_total_mb();
            if (tot > 0) {
                take pct = (used * 100) / tot;
                set_val("ram_bar", pct);
                if (pct > 80) {
                    set_color("ram_lbl", "#ef4444");
                } else {
                    set_color("ram_lbl", "#22c55e");
                }
            }
        }

        show();
    }
    return 0;
}
```

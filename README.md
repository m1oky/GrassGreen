<div align="center">

  <img src="logo.svg" alt="GrassGreen Logo" width="120" height="120" />

  # GrassGreen Programming Language
  ### *Высокопроизводительный стек-ориентированный язык системного уровня с компиляцией в нативный Win32 PE-байт-код*

  [![Standard](https://img.shields.io/badge/Language_Standard-C%2B%2B20-00599C?style=flat-square&logo=c%2B%2B)](https://isocpp.org/)
  [![Platform](https://img.shields.io/badge/Platform-Windows_x64-0078D6?style=flat-square&logo=windows)](https://microsoft.com)
  [![Architecture](https://img.shields.io/badge/VM-Zero--Allocation_Stack_Engine-00ffcc?style=flat-square)](#архитектура-ядра)
  [![Version](https://img.shields.io/badge/Release-v3.0.1-brightgreen?style=flat-square)](https://github.com/m1oky/GrassGreen)

  <p align="center">
    <b>GrassGreen</b> — независимый компилируемый язык программирования. Включает собственную виртуальную машину (Stack VM) с прямой диспетчеризацией опкодов, инкрементальный PE-Packer для генерации автономных <code>.exe</code>, модульный GUI-движок на базе Win32/GDI+, встроенный reactive-web сервер и модули прямого управления памятью.
  </p>

</div>

---

## Архитектура ядра

Компилятор `grassc` преобразует исходный текст в компактный двоичный поток инструкций с фиксированным шагом и связывает его с исполняемым ядром через оверлей PE-заголовка:

```text
[ Исходный код .grass ]
         │
         ▼
[ Лексер & GrassSyntax ] ──► [ Валидатор строгой изоляции библиотек ]
                                        │
                                        ▼
                             [ Генератор Байт-кода ]
                                        │
         ┌──────────────────────────────┴──────────────────────────────┐
         ▼                                                             ▼
[ Нативная Stack VM (O(1)) ]                                 [ Fast Win32 PE-Packer ]
    - Предвыделенный стек вызовов                                 - Инкрементальная запись
    - Реестр контекста переменных                                 - Генерация автономного .exe (<2 ms)
    - Нулевые аллокации в циклах
```

### Бенчмарки производительности

Тест выполнения 30 000 итераций арифметического сложения в цикле с проверкой граничных условий (CPU Intel Core i7, Windows 10 x64):

| Язык / Среда | Время выполнения | Относительная скорость |
| :--- | :---: | :---: |
| **GrassGreen v3.0.1 (Stack VM)** | **6.00 ms** | **1.0x (Baseline)** |
| CPython 3.12 (Standard) | 92.40 ms | ~15.4x медленнее |
| NodeJS v20 (V8 JIT Warmup) | 14.10 ms | ~2.3x медленнее |

*Сборка в автономный `.exe` занимает **1.45 ms** благодаря инкрементальному обновлению PE-хвоста без полной перезаписи бинарника.*

---

## Строгая модульная система

Язык отвергает концепцию неявного импорта. Каждый вызов функций ядра контролируется на этапе трансляции синтаксического дерева. Точка входа обязана объявляться через сигнатуру `int main() { ... }`.

```text
!GrassGreen!    // Обязательный заголовок базового языка
!time!          // Подключение модуля точных таймеров
!FastMath!      // Подключение модуля векторной математики
```

---

## Документация и спецификация библиотек

### 1. `!GrassGreen!` (Базовое ядро и управляющие конструкции)
Предоставляет стандартную C++ семантику: типы `take`, `int`, `string`, управление потоками ввода-вывода `cout` / `cin`, циклы `for`, `while`, ветвления `if` / `else`.

```cpp
!GrassGreen!

int main() {
    take counter = 0;
    string title = "Kernel Initialized";

    for (take i = 0; i < 5; i++) {
        counter += i;
    }

    if (counter >= 10) {
        cout << title << ": " << counter << endl;
    }

    return 0;
}
```

---

### 2. `!time!` (Аппаратное профилирование и системные часы)
Высокоточное профилирование участков кода через счетчики `std::chrono::high_resolution_clock` и нативные системные вызовы Windows API.

* **`start_time()`** — запуск высокоточного аппаратного секундомера.
* **`stop_time()`** — фиксация интервала и возврат значения в миллисекундах (`double`).
* **`conclus_time()`** — форматированный вывод затраченного времени строкой (`"7.42 ms"`).
* **`get_date()` / `get_time_str()`** — получение локальной даты и времени ОС.

```cpp
!GrassGreen!
!time!

int main() {
    start_time();

    take sum = 0;
    for (take i = 1; i <= 30000; i++) {
        sum = sum + i;
    }

    stop_time();

    cout << "Сумма: " << sum << " | Время: " << conclus_time() << endl;
    cout << "Метка времени: " << get_date() << " [" << get_time_str() << "]" << endl;
    return 0;
}
```

---

### 3. `!interface!` (Нативный Win32/GDI+ Fluent GUI Engine)
Оконный движок с субклассированием оконных процедур, поддержкой скругления хитбоксов через `SetWindowRgn`, антиалиасингом ClearType и защитой от мерцания буфера (`WS_CLIPCHILDREN`).

```cpp
!GrassGreen!
!interface!

int main() {
    window {
        title("GrassGreen Engine Console");
        size(500, 420);
        color(#12131a);
        theme("dark");
        glass();

        label "Компилятор GrassGreen v3.0" {
            id(lbl_status);
            window-ob(0, 40);
            size(500, 32);
            middle();
            text-color(#00ffcc);
            font-size(20);
            font-style("Segoe UI");
            shadow();
        }

        enter "Ввод команды..." {
            id(cmd_input);
            window-ob(50, 100);
            size(400, 40);
            middle();
            radius(8);
        }

        button "Выполнить операцию" {
            id(btn_exec);
            window-ob(50, 160);
            size(400, 44);
            middle();
            button-color(#1a2634);
            text-color(#00ffcc);
            radius(10);
            shadow();
            click-ob(cmd_input, lbl_status);
        }

        show();
    }
    return 0;
}
```

---

### 4. `!FastMath!` (Оптимизированная вычислительная математика)
Векторная и тригонометрическая математика без накладных расходов. Используется в симуляциях и графических расчетах.

* `discor(x)` / `sqrt(x)` — быстрое вычисление квадратного корня.
* `lerp(a, b, t)` — линейная интерполяция между узлами.
* `clamp(val, min, max)` — ограничение диапазона скаляра.
* `sin(rad)`, `cos(rad)`, `abs(val)`, `round(val)`, `min(a, b)`, `max(a, b)`.

```cpp
!GrassGreen!
!FastMath!

int main() {
    take velocity = 144.0;
    take root = discor(velocity);
    take smoothed = lerp(0.0, 100.0, 0.75);

    cout << "Root: " << root << " | Lerp: " << smoothed << endl;
    return 0;
}
```

---

### 5. `!CoreMath!` (Эмулятор квантовых вентилей и криптографические примитивы)
Программная эмуляция базовых унитарных квантовых преобразований кубитов и работа с криптографическими алгоритмами.

* `qb_init(q)` — инициализация регистра состояния кубита в базисе $\vert{}0\rangle$.
* `h(q)` — применение оператора Адамара (создание квантовой суперпозиции).
* `x(q)` — вентиль Паули-X (квантовое отрицание NOT).
* `cnot(ctrl, target)` — двухкубитовый контролируемый вентиль отрицания.
* `q_rand(q)` — редукция суперпозиции и измерение квантового бита (0 или 1).
* `gcd(a, b)` — алгоритм Евклида для нахождения наибольшего общего делителя.

```cpp
!GrassGreen!
!CoreMath!

int main() {
    qb_init("q0");
    h("q0");
    take measured_bit = q_rand("q0");

    cout << "Квантовое случайное состояние: " << measured_bit << endl;
    return 0;
}
```

---

### 6. `!memory!` (Прямой мониторинг и управление распределением ОЗУ)
Инструменты низкоуровневого контроля кучи, ручного выделения блоков памяти и защиты от утечек.

* `memory_usit()` — текущее потребление памяти виртуальной машиной в байтах.
* `memory_free()` — объем физически свободной оперативной памяти в системе.
* `memory_alloc(bytes)` — резервирование дескриптора блока памяти.
* `memory_free_block(id)` — явное освобождение выделенного сегмента.
* `memory_limit(bytes)` — аппаратный барьер ВМ (аварийный останов при переполнении).
* `memory_clean()` — сборка неиспользуемых строковых литералов.

```cpp
!GrassGreen!
!memory!

int main() {
    memory_limit(67108864); // Барьер в 64 МБ

    take block = memory_alloc(1048576);
    cout << "Выделен блок ID: " << block << endl;
    cout << "Использовано ядром: " << memory_usit() << " байт" << endl;

    memory_free_block(block);
    memory_clean();
    return 0;
}
```

---

### 7. `!random!` (Аппаратный генератор энтропии)
Генератор случайных величин на основе `std::mt19937` с поддержкой генерации случайных строковых токенов разного регистра.

```cpp
!GrassGreen!
!random!

int main() {
    take roll = random(1, 100);
    string token = random_txt("en", 16);

    cout << "Случайный диапазон [1-100]: " << roll << endl;
    cout << "Токен сессии: " << token << endl;
    return 0;
}
```

---

### 8. `!database!` (Встраиваемый Key-Value движок GSE)
Собственный структурированный бинарный движок хранения данных (**Grass Storage Engine**):

* Многотабличные пространства (`table("users")`).
* Атомарный инкремент и декремент числовых ключей без блокировки потока (`db_inc`).
* 64-битное хеширование ключей по алгоритму FNV-1a (`db_hash`).
* Симметричное блочное XOR-шифрование с солью (`db_encrypt` / `db_decrypt`).
* Экспорт в JSON и инкрементальный бэкап.

```cpp
!GrassGreen!
!database!

int main() {
    db_open("storage.gdb");
    table("session_tokens");

    db_set("admin_key", "sec_token_9918");
    db_encrypt("admin_key", "SaltPhrase");

    take visits = db_inc("visits_total", 1);
    db_save();

    cout << "Записей в таблице: " << visits << endl;
    return 0;
}
```

---

### 9. `!GrassWeb!` (Реактивный легковесный Web-раннер)
Встроенный HTTP-сервер на сокетах WinSock2. Транслирует компактный `.grass` DSL непосредственно в чистый HTML5/CSS3/ES6 без тяжелых зависимостей (React/Vue/Webpack), отдавая страницы клиенту за 1–3 ms.

```text
!GREEN_WWW1!
!GrassWeb!

glass(16, 0.08)
neon(#00ffcc)
box-start
    icon(bolt)
    color(#00ffcc)
    thick()
    G5 GrassGreen Edge Platform

    <name>/status_lbl
    color(#8e99a8)
    G2 Шлюз запущен на localhost:8080

    row-start
        <name>/btn_ping
        grad(#00ffcc, #0088ff)
        round-ob(8)
        button = Проверить статус ядра
    row-end
box-end

script-start
    on-click(btn_ping)
        set-text(status_lbl, Система в норме: задержка 0.4 ms)
        repaint-color(status_lbl, #00ff88)
        play-anim(btn_ping, pulse, 0.2s)
    end-click
script-end
```

---

## Сборка компилятора из исходников

Для сборки используется компилятор MinGW-w64 (GCC с поддержкой стандарта C++20):

```bash
g++ -std=c++20 -O3 -s \
  src/grassc.cpp \
  src/GrassSyntax.cpp \
  src/GrassTime.cpp \
  src/gui_engine.cpp \
  src/GrassWeb.cpp \
  src/GSE.cpp \
  src/FastMath.cpp \
  src/CoreMath.cpp \
  src/GrassMemory.cpp \
  src/GrassRandom.cpp \
  -o bin/grassc.exe \
  -lgdiplus -lgdi32 -lcomctl32 -lwinmm -lws2_32 -lurlmon
```

### Развертывание окружения в VS Code

Выполни первичную инициализацию компилятора:

```powershell
.\bin\grassc.exe install
```

Команда автоматически:
1. Зарегистрирует векторные SVG-иконки для `.grass` (`logo.svg`) и баз данных `.gdb` (`gdb_logo.svg`).
2. Установит модуль TextMate-грамматики подсветки синтаксиса в расширения редактора.
3. Добавит шаблоны сниппетов для сигнатур `int main()`, блоков `window { ... }` и циклов `for`.
4. Настроит горячую сборку текущего открытого скрипта по нажатию **`Ctrl + Shift + B`**.

---

## Структура файлов проекта

```text
GrassGreen-v-3-0-1/
├── bin/
│   └── grassc.exe           # Готовый нативный компилятор, VM и упаковщик
├── docs/                    # Техническая документация функций и сигнатур
│   ├── coremath.txt
│   ├── fastmath.txt
│   ├── gdb.txt
│   ├── interface.txt
│   ├── memory.txt
│   ├── random.txt
│   ├── time.txt
│   └── WEB.txt
├── examples/                # Комплект эталонных тестовых сценариев
│   ├── benchmark.grass      # Стресс-тест циклов (30 000 итераций)
│   ├── config.gdb           # Бинарная конфигурационная база данных
│   ├── math.grass           # Комплексный тест CoreMath и FastMath
│   ├── memory.grass         # Проверка граничных условий и лимитов кучи
│   ├── test_gui.grass       # Оконное приложение Fluent Win32
│   └── test_web.grass       # Реактивный веб-интерфейс на сокетах
├── src/                     # Исходный код C++
│   ├── CoreMath.cpp / .h    # Квантовый эмулятор кубитов
│   ├── FastMath.cpp / .h    # Оптимизированная тригонометрия и lerp
│   ├── GrassMemory.cpp / .h # Менеджер кучи и аллокатор блоков
│   ├── GrassRandom.cpp / .h # PRNG на базе mt19937
│   ├── GrassSyntax.cpp / .h # Модульный лексер и генератор байт-кода
│   ├── GrassTime.cpp / .h   # Модуль высокоточных часов
│   ├── GrassWeb.cpp / .h    # Встроенный HTTP-сервер
│   ├── GSE.cpp / .h         # Движок базы данных Grass Storage Engine
│   ├── gui_engine.cpp / .h  # Оконная подсистема Win32/GDI+
│   └── grassc.cpp           # Точка входа компилятора, Stack VM и PE-Packer
├── gdb_logo.svg             # Векторная иконка хранилищ GSE (.gdb)
├── logo.svg                 # Векторная иконка файлов GrassGreen (.grass)
├── LICENSE                  # Лицензия проекта (MIT)
└── README.md                # Спецификация и руководство разработчика
```

---

## Команды CLI

* **`grassc .// <файл.grass>`** — компиляция скрипта в автономный нативный исполняемый файл `.exe`.
* **`grassc ./! <файл.grass>`** — компиляция и мгновенное выполнение байт-кода внутри встроенной виртуальной машины.
* **`grassc install`** — переустановка ассоциаций Windows, иконок и синтаксического расширения VS Code.

---

<div align="center">
  <sub>Разработано для высокопроизводительных компактных вычислений и системных утилит под ОС Windows.</sub>
</div>

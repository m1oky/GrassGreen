const vscode = require('vscode');

/**
 * GrassGreen 4.0 Standard Library Reference & API Catalog
 */
const MODULES = [
    { name: "!GrassGreen!", doc: "Базовое ядро языка GrassGreen 4.0, потоковый ввод-вывод (gout/gin), типы данных и операторы." },
    { name: "!time!", doc: "Модуль профилирования, таймеров и работы с системным временем." },
    { name: "!interface!", doc: "Графический Fluent GUI движок (Win32 / GDI+), окна, кнопки, поля ввода." },
    { name: "!FastMath!", doc: "Быстрая математика: тригонометрия, интерполяция lerp/clamp, расстояния." },
    { name: "!CoreMath!", doc: "Криптопримитивы, хэширование и квантовые симуляторы." },
    { name: "!memory!", doc: "Мониторинг ОЗУ и низкоуровневое управление ареной памяти." },
    { name: "!random!", doc: "Генератор псевдослучайных целых и вещественных чисел." },
    { name: "!database!", doc: "Встроенный Key-Value движок GSE (файлы .gdb)." },
    { name: "!GrassWeb!", doc: "Реактивный Web-раннер, интеграция HTML/CSS интерфейсов." },
    { name: "!GrassNet!", doc: "Сетевой стек: сокеты TCP/IP, клиент HTTP/HTTPS, сетевые утилиты." },
    { name: "!json!", doc: "Парсинг, валидация и сериализация JSON структур." },
    { name: "!sys!", doc: "Мониторинг операционной системы Windows, CPU, ОЗУ, буфер обмена." },
    { name: "!proc!", doc: "Управление системными процессами Windows (PID, статус, память)." },
    { name: "!fsys!", doc: "Файловая система: чтение, запись, проверка путей и списки файлов." },
    { name: "!tg!", doc: "Интеграция с Telegram Bot API." },
    { name: "!2DGraphics!", doc: "Аппаратная 2D-графика: примитивы, коллизии, игровой цикл." },
    { name: "!audio!", doc: "Нативный аудио-движок: синтез волн (синус, меандр, пила, треугольник, шум), генерация тонов, воспроизведение WAV и звуковые эффекты." },
    { name: "!video!", doc: "Движок видеографики VD+: аппаратный фреймбуфер, интеграция медиа и аппаратные поверхности surface." },
    { name: "!mmio!", doc: "Прямой доступ к памяти (MMIO), аппаратные порты (in/out) и барьеры." },
    { name: "!thread!", doc: "Нативная многопоточность: системные потоки ОС и мьютексы." }
];

const FUNCTIONS = [
    // Audio Engine
    { name: "audio_play", signature: "bool audio_play(string path)", doc: "Воспроизводит аудиофайл WAV.", snippet: "audio_play(${1:\"path/to/file.wav\"})" },
    { name: "audio_stop", signature: "void audio_stop()", doc: "Останавливает воспроизведение всех текущих звуков.", snippet: "audio_stop()" },
    { name: "audio_tone", signature: "void audio_tone(int freq, int ms)", doc: "Генерирует чистый звуковой тон частоты freq Гц на протяжении ms миллисекунд.", snippet: "audio_tone(${1:440}, ${2:1000})" },
    { name: "audio_synth", signature: "void audio_synth(int freq, int ms, int wave_type)", doc: "Синтезирует волну (0=синус, 1=меандр, 2=пила, 3=треугольник, 4=шум) частоты freq на ms миллисекунд.", snippet: "audio_synth(${1:440}, ${2:1000}, ${3:0})" },
    { name: "audio_volume", signature: "void audio_volume(int vol)", doc: "Устанавливает уровень громкости звука от 0 до 100%.", snippet: "audio_volume(${1:80})" },
    { name: "audio_is_playing", signature: "bool audio_is_playing()", doc: "Возвращает true, если в данный момент воспроизводится аудиофайл или тон.", snippet: "audio_is_playing()" },
    { name: "audio_noise", signature: "void audio_noise(int ms)", doc: "Генерирует белый шум заданной длительности в миллисекундах.", snippet: "audio_noise(${1:500})" },
    { name: "audio_beep", signature: "void audio_beep(int freq, int ms)", doc: "Подает системный звуковой сигнал заданной частоты и длительности.", snippet: "audio_beep(${1:800}, ${2:200})" },

    // GUI VD+ Engine
    { name: "auto_size_display", signature: "void auto_size_display()", doc: "Автоматически определяет разрешение экрана (включая 2K/4K и масштаб Windows DPI 125%/150%) и оптимизирует окно.", snippet: "auto_size_display()" },
    { name: "border-radius-app", signature: "void border-radius-app(int radius)", doc: "Задает скругление внешних углов всего окна приложения (например 16px).", snippet: "border-radius-app(${1:16})" },
    { name: "header", signature: "void header(string style)", doc: "Стиль шапки окна: emerald, native (macOS 3 точки), glass (прозрачная), modern, none.", snippet: "header(\"${1:emerald}\")" },
    { name: "header-color", signature: "void header-color(string hex_color)", doc: "Цвет фона заголовка окна.", snippet: "header-color(\"${1:#0f172a}\")" },
    { name: "header-text-color", signature: "void header-text-color(string hex_color)", doc: "Цвет текста заголовка окна.", snippet: "header-text-color(\"${1:#22c55e}\")" },
    { name: "clock", signature: "void clock()", doc: "Встроенный синхронный виджет часов (ЧЧ:ММ:СС) с автообновлением каждую секунду.", snippet: "clock()" },
    { name: "tab", signature: "tab = \"Name\" { ... }", doc: "Создает страницу вкладки внутри окна приложения.", snippet: "tab = \"${1:TabName}\" {\n\t${0}\n}" },
    { name: "switch", signature: "void switch(string tab_name)", doc: "Переключает активную вкладку окна на tab_name.", snippet: "switch(\"${1:TabName}\")" },
    { name: "fullscreen", signature: "void fullscreen()", doc: "Разворачивает окно приложения на весь экран (Fullscreen mode).", snippet: "fullscreen()" },
    { name: "z-index", signature: "void z-index(int level)", doc: "Слой отрисовки виджета или окна. При клике окно автоматически всплывает на верхний z-слой.", snippet: "z-index(${1:10})" },
    { name: "particles", signature: "void particles(int count, string color)", doc: "Анимированные светящиеся частицы на фоне окна.", snippet: "particles(${1:35}, \"${2:#22c55e}\")" },
    { name: "surface", signature: "void surface(string mode)", doc: "Режим аппаратного рендеринга: freestanding (прямо в видеопамять без системных окон) или direct.", snippet: "surface(\"${1:freestanding}\")" },

    // Concurrency / Threads
    { name: "thread", signature: "take thread(take func, take arg)", doc: "Запускает функцию `func` в отдельном потоке ОС с аргументом `arg`. Возвращает хэндл потока.", snippet: "thread(${1:function_name}, ${2:argument})" },
    { name: "thread_join", signature: "take thread_join(take handle)", doc: "Блокирует текущий поток до завершения потока `handle` и возвращает результат его выполнения.", snippet: "thread_join(${1:handle})" },
    { name: "thread_detach", signature: "void thread_detach(take handle)", doc: "Отсоединяет поток `handle` от текущего контекста (фоновое выполнение).", snippet: "thread_detach(${1:handle})" },
    { name: "thread_id", signature: "int thread_id()", doc: "Возвращает системный идентификатор (TID) текущего выполняющегося потока.", snippet: "thread_id()" },
    { name: "thread_sleep", signature: "void thread_sleep(int ms)", doc: "Приостанавливает выполнение текущего потока на `ms` миллисекунд.", snippet: "thread_sleep(${1:milliseconds})" },
    { name: "mutex_create", signature: "take mutex_create()", doc: "Создает новый системный мьютекс и возвращает его хэндл.", snippet: "mutex_create()" },
    { name: "mutex_lock", signature: "void mutex_lock(take mutex)", doc: "Захватывает мьютекс `mutex`. Если он занят, блокирует поток до освобождения.", snippet: "mutex_lock(${1:mutex})" },
    { name: "mutex_unlock", signature: "void mutex_unlock(take mutex)", doc: "Освобождает ранее захваченный мьютекс `mutex`.", snippet: "mutex_unlock(${1:mutex})" },
    { name: "mutex_destroy", signature: "void mutex_destroy(take mutex)", doc: "Уничтожает системный мьютекс `mutex` и освобождает связанные дескрипторы.", snippet: "mutex_destroy(${1:mutex})" },

    // MMIO & Hardware
    { name: "mmio_read8", signature: "u8 mmio_read8(take addr)", doc: "Считывает 8-битное значение по физическому/виртуальному адресу `addr` без кэширования.", snippet: "mmio_read8(${1:addr})" },
    { name: "mmio_read16", signature: "u16 mmio_read16(take addr)", doc: "Считывает 16-битное значение по адресу `addr`.", snippet: "mmio_read16(${1:addr})" },
    { name: "mmio_read32", signature: "u32 mmio_read32(take addr)", doc: "Считывает 32-битное значение по адресу `addr`.", snippet: "mmio_read32(${1:addr})" },
    { name: "mmio_read64", signature: "u64 mmio_read64(take addr)", doc: "Считывает 64-битное значение по адресу `addr`.", snippet: "mmio_read64(${1:addr})" },
    { name: "mmio_write8", signature: "void mmio_write8(take addr, u8 val)", doc: "Записывает 8-битное значение `val` по адресу `addr` с барьером компилятора.", snippet: "mmio_write8(${1:addr}, ${2:val})" },
    { name: "mmio_write16", signature: "void mmio_write16(take addr, u16 val)", doc: "Записывает 16-битное значение `val` по адресу `addr`.", snippet: "mmio_write16(${1:addr}, ${2:val})" },
    { name: "mmio_write32", signature: "void mmio_write32(take addr, u32 val)", doc: "Записывает 32-битное значение `val` по адресу `addr`.", snippet: "mmio_write32(${1:addr}, ${2:val})" },
    { name: "mmio_write64", signature: "void mmio_write64(take addr, u64 val)", doc: "Записывает 64-битное значение `val` по адресу `addr`.", snippet: "mmio_write64(${1:addr}, ${2:val})" },
    { name: "memory_barrier", signature: "void memory_barrier()", doc: "Устанавливает полный барьер памяти (запрет спекулятивной перестановки инструкций процессором и компилятором).", snippet: "memory_barrier()" },
    { name: "inb", signature: "u8 inb(u16 port)", doc: "Чтение байта из аппаратного порта ввода-вывода (I/O port).", snippet: "inb(${1:port})" },
    { name: "outb", signature: "void outb(u16 port, u8 val)", doc: "Запись байта в аппаратный порт ввода-вывода.", snippet: "outb(${1:port}, ${2:val})" },

    // Math
    { name: "sqrt", signature: "float sqrt(float x)", doc: "Вычисляет квадратный корень числа `x`.", snippet: "sqrt(${1:x})" },
    { name: "pow", signature: "float pow(float base, float exp)", doc: "Возводит `base` в степень `exp`.", snippet: "pow(${1:base}, ${2:exp})" },
    { name: "sin", signature: "float sin(float rad)", doc: "Тригонометрический синус угла в радианах.", snippet: "sin(${1:rad})" },
    { name: "cos", signature: "float cos(float rad)", doc: "Тригонометрический косинус угла в радианах.", snippet: "cos(${1:rad})" },
    { name: "tan", signature: "float tan(float rad)", doc: "Тригонометрический тангенс угла в радианах.", snippet: "tan(${1:rad})" },
    { name: "atan2", signature: "float atan2(float y, float x)", doc: "Арктангенс от двух переменных `y / x` с учетом квадранта.", snippet: "atan2(${1:y}, ${2:x})" },
    { name: "abs", signature: "take abs(take x)", doc: "Абсолютная величина числа `x`.", snippet: "abs(${1:x})" },
    { name: "floor", signature: "float floor(float x)", doc: "Округление вниз до ближайшего целого.", snippet: "floor(${1:x})" },
    { name: "ceil", signature: "float ceil(float x)", doc: "Округление вверх до ближайшего целого.", snippet: "ceil(${1:x})" },
    { name: "lerp", signature: "float lerp(float a, float b, float t)", doc: "Линейная интерполяция между `a` и `b` с фактором `t` [0..1].", snippet: "lerp(${1:a}, ${2:b}, ${3:t})" },
    { name: "clamp", signature: "float clamp(float val, float min, float max)", doc: "Ограничивает значение `val` диапазоном [`min`, `max`].", snippet: "clamp(${1:val}, ${2:min}, ${3:max})" },
    { name: "dist2d", signature: "float dist2d(float x1, float y1, float x2, float y2)", doc: "Евклидово расстояние между двумя точками на плоскости.", snippet: "dist2d(${1:x1}, ${2:y1}, ${3:x2}, ${4:y2})" },

    // Random
    { name: "rand_int", signature: "int rand_int(int min, int max)", doc: "Генерирует случайное целое число в диапазоне от `min` до `max` включительно.", snippet: "rand_int(${1:min}, ${2:max})" },
    { name: "rand_float", signature: "float rand_float(float min, float max)", doc: "Генерирует вещественное случайное число в диапазоне [`min`, `max`].", snippet: "rand_float(${1:min}, ${2:max})" },

    // Filesystem
    { name: "fs_read", signature: "string fs_read(string path)", doc: "Считывает всё содержимое текстового файла `path`.", snippet: "fs_read(${1:\"path/to/file\"})" },
    { name: "fs_write", signature: "bool fs_write(string path, string content)", doc: "Перезаписывает файл `path` содержимым `content`.", snippet: "fs_write(${1:\"path/to/file\"}, ${2:content})" },
    { name: "fs_append", signature: "bool fs_append(string path, string content)", doc: "Дописывает `content` в конец файла `path`.", snippet: "fs_append(${1:\"path/to/file\"}, ${2:content})" },
    { name: "fs_exists", signature: "bool fs_exists(string path)", doc: "Проверяет существование файла или папки по пути `path`.", snippet: "fs_exists(${1:\"path/to/file\"})" },
    { name: "fs_delete", signature: "bool fs_delete(string path)", doc: "Удаляет файл или директорию `path`.", snippet: "fs_delete(${1:\"path/to/file\"})" },
    { name: "fs_mkdir", signature: "bool fs_mkdir(string path)", doc: "Создает новую папку по указанному пути `path`.", snippet: "fs_mkdir(${1:\"new_dir\"})" },
    { name: "fs_size", signature: "int fs_size(string path)", doc: "Возвращает размер файла в байтах.", snippet: "fs_size(${1:\"path/to/file\"})" },
    { name: "fs_list", signature: "array fs_list(string path)", doc: "Возвращает массив имен файлов и папок внутри каталога `path`.", snippet: "fs_list(${1:\"path/to/dir\"})" },

    // System & OS
    { name: "pc_name", signature: "string pc_name()", doc: "Возвращает сетевое имя компьютера в Windows.", snippet: "pc_name()" },
    { name: "user_name", signature: "string user_name()", doc: "Возвращает имя текущего пользователя Windows.", snippet: "user_name()" },
    { name: "screen_w", signature: "int screen_w()", doc: "Возвращает ширину основного монитора в пикселях.", snippet: "screen_w()" },
    { name: "screen_h", signature: "int screen_h()", doc: "Возвращает высоту основного монитора в пикселях.", snippet: "screen_h()" },
    { name: "cpu_cores", signature: "int cpu_cores()", doc: "Возвращает количество логических ядер центрального процессора.", snippet: "cpu_cores()" },
    { name: "ram_total_mb", signature: "int ram_total_mb()", doc: "Возвращает общий объем оперативной памяти в мегабайтах.", snippet: "ram_total_mb()" },
    { name: "ram_free_mb", signature: "int ram_free_mb()", doc: "Возвращает объем свободной оперативной памяти в мегабайтах.", snippet: "ram_free_mb()" },
    { name: "uptime_sec", signature: "int uptime_sec()", doc: "Время непрерывной работы операционной системы в секундах.", snippet: "uptime_sec()" },
    { name: "clip_set", signature: "void clip_set(string text)", doc: "Помещает текстовую строку `text` в буфер обмена Windows.", snippet: "clip_set(${1:\"text\"})" },
    { name: "clip_get", signature: "string clip_get()", doc: "Считывает текущее текстовое содержимое буфера обмена Windows.", snippet: "clip_get()" },

    // Process
    { name: "proc_id", signature: "int proc_id()", doc: "Возвращает Process ID (PID) текущего запущенного процесса GrassGreen.", snippet: "proc_id()" },
    { name: "proc_kill", signature: "bool proc_kill(int pid)", doc: "Принудительно завершает процесс Windows по его идентификатору `pid`.", snippet: "proc_kill(${1:pid})" },
    { name: "proc_is_alive", signature: "bool proc_is_alive(int pid)", doc: "Проверяет, работает ли процесс с указанным `pid` в данный момент.", snippet: "proc_is_alive(${1:pid})" },
    { name: "proc_ram", signature: "int proc_ram(int pid)", doc: "Возвращает объем памяти (Working Set), используемый процессом `pid` в килобайтах.", snippet: "proc_ram(${1:pid})" },

    // Time
    { name: "time_ms", signature: "int time_ms()", doc: "Текущее монотонное системное время в миллисекундах с высоким разрешением.", snippet: "time_ms()" },
    { name: "get_time_str", signature: "string get_time_str()", doc: "Текущее системное время в формате ЧЧ:ММ:СС.", snippet: "get_time_str()" },
    { name: "get_date", signature: "string get_date()", doc: "Текущая системная дата в формате ДД.ММ.ГГГГ.", snippet: "get_date()" },

    // JSON
    { name: "json_parse", signature: "take json_parse(string json_str)", doc: "Парсит строку JSON в объект или массив GrassGreen.", snippet: "json_parse(${1:json_str})" },
    { name: "json_stringify", signature: "string json_stringify(take val)", doc: "Сериализует значение, структуру или массив в строку формата JSON.", snippet: "json_stringify(${1:val})" },
    { name: "json_get", signature: "take json_get(string json_str, string key)", doc: "Быстрое извлечение строкового поля `key` из JSON без полного разбора объекта.", snippet: "json_get(${1:json_str}, ${2:\"key\"})" },
    { name: "json_valid", signature: "bool json_valid(string json_str)", doc: "Проверяет корректность синтаксиса JSON строки.", snippet: "json_valid(${1:json_str})" },

    // Memory Arena
    { name: "memory_free", signature: "void memory_free()", doc: "Сбрасывает системную арену памяти GrassGreen (быстрое освобождение всех временных аллокаций).", snippet: "memory_free()" },
    { name: "memory_usit", signature: "int memory_usit()", doc: "Возвращает объем занятой памяти внутри текущей арены в байтах.", snippet: "memory_usit()" },

    // Array primitives
    { name: "push", signature: "void push(array arr, take val)", doc: "Добавляет элемент `val` в конец динамического массива `arr`.", snippet: "push(${1:arr}, ${2:val})" },
    { name: "pop", signature: "take pop(array arr)", doc: "Удаляет и возвращает последний элемент динамического массива `arr`.", snippet: "pop(${1:arr})" },
    { name: "len", signature: "int len(array arr)", doc: "Возвращает количество элементов в массиве или длину строки.", snippet: "len(${1:arr})" }
];

const KEYWORDS = [
    { name: "take", doc: "Объявление переменной или сигнатуры функции с динамическим/статическим типом (аналог `auto` / `let`).", snippet: "take ${1:name} = ${2:value};" },
    { name: "set", doc: "Оператор изменения значения существующей переменной.", snippet: "set ${1:name} = ${2:value};" },
    { name: "volatile", doc: "Модификатор переменной: запрещает компилятору и VM кэшировать значение в регистрах (критично для MMIO).", snippet: "volatile " },
    { name: "struct", doc: "Объявление составного типа данных C-ABI со строгой раскладкой полей в памяти.", snippet: "struct ${1:Name} {\n\t${2:field}: ${3:u32}\n}" },
    { name: "thread", doc: "Ключевое слово для создания системного потока или объявления потоковой задачи.", snippet: "thread(${1:func}, ${2:arg})" },
    { name: "async", doc: "Асинхронный запуск задачи в пуле воркеров без блокировки текущего контекста.", snippet: "async ${1:task}();" },
    { name: "return", doc: "Возврат значения из функции или завершение точки входа `main`.", snippet: "return ${1:0};" },
    { name: "if", doc: "Условный оператор ветвления.", snippet: "if (${1:condition}) {\n\t${0}\n}" },
    { name: "else", doc: "Ветка условного оператора при ложном условии.", snippet: "else {\n\t${0}\n}" },
    { name: "while", doc: "Цикл с предусловием.", snippet: "while (${1:condition}) {\n\t${0}\n}" },
    { name: "for", doc: "Параметрический цикл.", snippet: "for (${1:take i = 0}; ${2:i < 10}; ${3:set i = i + 1}) {\n\t${0}\n}" }
];

const STREAMS = [
    { name: "gout", doc: "Стандартный поток вывода GrassGreen. Использует оператор направления `<<`:\n```grassgreen\ngout << \"Текст\" << endl;\n```", snippet: "gout << \"${1:Message}\" << endl;" },
    { name: "gin", doc: "Стандартный поток ввода GrassGreen. Использует оператор направления `>>`:\n```grassgreen\ngin >> my_var;\n```", snippet: "gin >> ${1:variable};" },
    { name: "endl", doc: "Терминатор строки и сброс буфера стандартного потока вывода.", snippet: "endl" }
];

const TYPES = ["int", "void", "string", "float", "double", "u8", "u16", "u32", "u64", "bool", "char"];

/**
 * Builds command string that checks PATH and falls back to default installation path if terminal hasn't refreshed PATH
 */
function getBuildCommand(filePath) {
    return `$cmd = if (Get-Command grassc -ErrorAction SilentlyContinue) { "grassc" } elseif (Test-Path "C:\\GrassGreen\\bin\\grassc.exe") { "C:\\GrassGreen\\bin\\grassc.exe" } else { "grassc" }; & $cmd -O3 -emit-exe "${filePath}"`;
}

function getRunCommand(filePath) {
    return `$cmd = if (Get-Command grassc -ErrorAction SilentlyContinue) { "grassc" } elseif (Test-Path "C:\\GrassGreen\\bin\\grassc.exe") { "C:\\GrassGreen\\bin\\grassc.exe" } else { "grassc" }; & $cmd "${filePath}"`;
}

function getOrCreateTerminal(name) {
    const existing = vscode.window.terminals.find(t => t.name === name);
    if (existing) return existing;
    return vscode.window.createTerminal(name);
}

/**
 * Activates the GrassGreen 4.0 extension
 * @param {vscode.ExtensionContext} context
 */
function activate(context) {
    // 1. Completion Provider
    const completionProvider = vscode.languages.registerCompletionItemProvider('grassgreen', {
        provideCompletionItems(document, position) {
            const linePrefix = document.lineAt(position).text.substr(0, position.character);
            const items = [];

            // Modules (!...)
            if (linePrefix.endsWith('!')) {
                for (const mod of MODULES) {
                    const item = new vscode.CompletionItem(mod.name.replace(/!/g, ''), vscode.CompletionItemKind.Module);
                    item.insertText = mod.name.endsWith('!') ? mod.name.slice(1) : mod.name;
                    item.detail = 'GrassGreen Module';
                    item.documentation = new vscode.MarkdownString(mod.doc);
                    items.push(item);
                }
                return items;
            }

            // Stream I/O
            for (const st of STREAMS) {
                const item = new vscode.CompletionItem(st.name, vscode.CompletionItemKind.Class);
                item.detail = 'GrassGreen Stream I/O';
                item.documentation = new vscode.MarkdownString(st.doc);
                item.insertText = new vscode.SnippetString(st.snippet);
                items.push(item);
            }

            // Built-in Standard Library Functions
            for (const fn of FUNCTIONS) {
                const item = new vscode.CompletionItem(fn.name, vscode.CompletionItemKind.Function);
                item.detail = fn.signature;
                item.documentation = new vscode.MarkdownString(fn.doc);
                item.insertText = new vscode.SnippetString(fn.snippet);
                items.push(item);
            }

            // Keywords
            for (const kw of KEYWORDS) {
                const item = new vscode.CompletionItem(kw.name, vscode.CompletionItemKind.Keyword);
                item.detail = 'GrassGreen Keyword';
                item.documentation = new vscode.MarkdownString(kw.doc);
                item.insertText = new vscode.SnippetString(kw.snippet);
                items.push(item);
            }

            // Types
            for (const ty of TYPES) {
                const item = new vscode.CompletionItem(ty, vscode.CompletionItemKind.TypeParameter);
                item.detail = `GrassGreen Primitive Type (${ty})`;
                items.push(item);
            }

            // Directives when typing letters
            for (const mod of MODULES) {
                const item = new vscode.CompletionItem(mod.name, vscode.CompletionItemKind.Module);
                item.detail = 'GrassGreen Directive';
                item.documentation = new vscode.MarkdownString(mod.doc);
                item.insertText = mod.name;
                items.push(item);
            }

            return items;
        }
    }, '!', '<', '>', ' ', ':');

    // 2. Hover Provider
    const hoverProvider = vscode.languages.registerHoverProvider('grassgreen', {
        provideHover(document, position) {
            const range = document.getWordRangeAtPosition(position, /[!a-zA-Z0-9_]+/);
            if (!range) return null;

            const word = document.getText(range);

            const mod = MODULES.find(m => m.name === word || m.name === `!${word}!`);
            if (mod) {
                const md = new vscode.MarkdownString();
                md.appendCodeblock(mod.name, 'grassgreen');
                md.appendMarkdown(`**Модуль GrassGreen 4.0**\n\n${mod.doc}`);
                return new vscode.Hover(md);
            }

            const fn = FUNCTIONS.find(f => f.name === word);
            if (fn) {
                const md = new vscode.MarkdownString();
                md.appendCodeblock(fn.signature, 'grassgreen');
                md.appendMarkdown(`\n${fn.doc}`);
                return new vscode.Hover(md);
            }

            const st = STREAMS.find(s => s.name === word);
            if (st) {
                const md = new vscode.MarkdownString();
                md.appendCodeblock(st.name, 'grassgreen');
                md.appendMarkdown(`\n${st.doc}`);
                return new vscode.Hover(md);
            }

            const kw = KEYWORDS.find(k => k.name === word);
            if (kw) {
                const md = new vscode.MarkdownString();
                md.appendCodeblock(kw.name, 'grassgreen');
                md.appendMarkdown(`\n${kw.doc}`);
                return new vscode.Hover(md);
            }

            return null;
        }
    });

    // 3. Document Symbol Provider (Outline & Breadcrumbs)
    const symbolProvider = vscode.languages.registerDocumentSymbolProvider('grassgreen', {
        provideDocumentSymbols(document) {
            const symbols = [];
            const text = document.getText();
            const lines = text.split('\n');

            for (let i = 0; i < lines.length; i++) {
                const line = lines[i];

                const fnMatch = line.match(/(?:take|int|void|float|string)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/);
                if (fnMatch) {
                    const fnName = fnMatch[1];
                    const range = new vscode.Range(i, 0, i, line.length);
                    const selRange = new vscode.Range(i, line.indexOf(fnName), i, line.indexOf(fnName) + fnName.length);
                    const sym = new vscode.DocumentSymbol(
                        fnName,
                        `(${fnMatch[2]})`,
                        vscode.SymbolKind.Function,
                        range,
                        selRange
                    );
                    symbols.push(sym);
                }

                const structMatch = line.match(/struct\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
                if (structMatch) {
                    const structName = structMatch[1];
                    const range = new vscode.Range(i, 0, i, line.length);
                    const selRange = new vscode.Range(i, line.indexOf(structName), i, line.indexOf(structName) + structName.length);
                    const sym = new vscode.DocumentSymbol(
                        structName,
                        'C-ABI Struct',
                        vscode.SymbolKind.Struct,
                        range,
                        selRange
                    );
                    symbols.push(sym);
                }
            }

            return symbols;
        }
    });

    // 4. Build Command (Ctrl + Shift + B)
    const buildCommand = vscode.commands.registerCommand('grassgreen.build', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== 'grassgreen') {
            vscode.window.showErrorMessage('Откройте файл GrassGreen (.grass) для сборки.');
            return;
        }
        await editor.document.save();
        const filePath = editor.document.fileName;
        const terminal = getOrCreateTerminal('GrassGreen Build');
        terminal.show();
        terminal.sendText(getBuildCommand(filePath));
        vscode.window.setStatusBarMessage('$(gear) GrassGreen: Сборка .exe (O3) запущена...', 4000);
    });

    // 5. Run Command (F5)
    const runCommand = vscode.commands.registerCommand('grassgreen.runFile', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== 'grassgreen') {
            vscode.window.showErrorMessage('Откройте файл GrassGreen (.grass) для запуска.');
            return;
        }
        await editor.document.save();
        const filePath = editor.document.fileName;
        const terminal = getOrCreateTerminal('GrassGreen Run');
        terminal.show();
        terminal.sendText(getRunCommand(filePath));
        vscode.window.setStatusBarMessage('$(play) GrassGreen: Запуск...', 3000);
    });

    // 6. Task Provider for standard VS Code Run Build Task
    const taskProvider = vscode.tasks.registerTaskProvider('grassgreen', {
        provideTasks() {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== 'grassgreen') return [];
            const filePath = editor.document.fileName;

            const buildTask = new vscode.Task(
                { type: 'grassgreen', task: 'build' },
                vscode.TaskScope.Workspace || vscode.TaskScope.Global,
                'Собрать в .exe (Native Build -O3)',
                'GrassGreen',
                new vscode.ShellExecution(getBuildCommand(filePath))
            );
            buildTask.group = vscode.TaskGroup.Build;

            const runTask = new vscode.Task(
                { type: 'grassgreen', task: 'run' },
                vscode.TaskScope.Workspace || vscode.TaskScope.Global,
                'Скомпилировать и выполнить',
                'GrassGreen',
                new vscode.ShellExecution(getRunCommand(filePath))
            );

            return [buildTask, runTask];
        },
        resolveTask(task) {
            return task;
        }
    });

    // 7. Status Bar Action Buttons (Click to Build / Run)
    const buildStatusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    buildStatusBarItem.command = 'grassgreen.build';
    buildStatusBarItem.text = '$(gear) G&G: Собрать .exe (Ctrl+Shift+B)';
    buildStatusBarItem.tooltip = 'Собрать самостоятельный исполняемый файл (.exe) с оптимизацией -O3';

    const runStatusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 99);
    runStatusBarItem.command = 'grassgreen.runFile';
    runStatusBarItem.text = '$(play) G&G: Запустить (F5)';
    runStatusBarItem.tooltip = 'Скомпилировать и мгновенно выполнить программу в терминале';

    function updateStatusBar(editor) {
        if (editor && editor.document.languageId === 'grassgreen') {
            buildStatusBarItem.show();
            runStatusBarItem.show();
        } else {
            buildStatusBarItem.hide();
            runStatusBarItem.hide();
        }
    }

    vscode.window.onDidChangeActiveTextEditor(updateStatusBar, null, context.subscriptions);
    updateStatusBar(vscode.window.activeTextEditor);

    context.subscriptions.push(
        completionProvider,
        hoverProvider,
        symbolProvider,
        buildCommand,
        runCommand,
        taskProvider,
        buildStatusBarItem,
        runStatusBarItem
    );
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};

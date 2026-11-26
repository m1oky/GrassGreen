using System;
using System.Collections.Generic;

namespace GrassGreen.VisualStudio.Constants
{
    public class BuiltinFunctionInfo
    {
        public string Name { get; set; }
        public string Signature { get; set; }
        public string Documentation { get; set; }
        public string Snippet { get; set; }
        public string Module { get; set; }

        public BuiltinFunctionInfo(string name, string signature, string doc, string snippet = null, string module = "core")
        {
            Name = name;
            Signature = signature;
            Documentation = doc;
            Snippet = snippet ?? name + "()";
            Module = module ?? "core";
        }
    }

    public class ModuleInfo
    {
        public string Name { get; set; }
        public string Documentation { get; set; }

        public ModuleInfo(string name, string doc)
        {
            Name = name;
            Documentation = doc;
        }
    }

    public static class GrassLanguageData
    {
        public static readonly HashSet<string> Directives = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "!GrassGreen!", "!disp!", "!baremetal!", "!bm!", "!kernel!", "!BareMetal!",
            "!hardware!", "!hw!", "!sysinfo!", "!device!",
            "!time!", "!interface!", "!gui!", "!GrassGUI!",
            "!FastMath!", "!CoreMath!", "!memory!", "!random!",
            "!database!", "!gdb!", "!GrassWeb!", "!GREEN_WWW1!",
            "!GrassNet!", "!net!", "!json!", "!sys!", "!proc!", "!fsys!",
            "!tg!", "!2DGraphics!", "!keyboard!", "!kbd!", "!ps2!", "!PS2Keyboard!",
            "!asm!", "!GrassASM!", "!assembler!", "!audio!", "!sound!", "!GrassAudio!",
            "!mmio!", "!io!", "!thread!", "!vd!", "!vd+!", "!VD+!",
            "!ffi!", "!langgen!", "!languages!", "!lang!",
            "!math3d!", "!Math3D!", "!gfx3d!", "!Gfx3D!", "!3d!", "!3D!", "!physics!", "!Physics!"
        };

        public static readonly HashSet<string> Keywords = new HashSet<string>(StringComparer.Ordinal)
        {
            "take", "set", "volatile", "static", "const", "struct", "enum", "array",
            "thread", "async", "return", "if", "else", "while", "for", "break",
            "continue", "match", "switch", "case", "default", "tab", "use", "utf", "not_clear", "clear", "cls",
            "clearable", "virtual_struct", "physical_struct", "reboot", "poweroff",
            "folder", "dir", "file", "import", "include", "using",
            "fn", "fun", "func", "var", "let", "check", "this"
        };

        public static readonly HashSet<string> Types = new HashSet<string>(StringComparer.Ordinal)
        {
            "int", "void", "string", "float", "double", "u8", "u16", "u32", "u64",
            "i8", "i16", "i32", "i64", "f32", "f64", "byte", "ptr", "const",
            "bool", "char", "array", "num", "str", "auto", "Vec3", "Mat4"
        };

        public static readonly HashSet<string> UiProperties = new HashSet<string>(StringComparer.Ordinal)
        {
            "window", "label", "button", "enter", "icon", "iconP", "video",
            "border-radius-app", "border_radius_app", "window-ob", "ob-window",
            "text-color", "font-size", "font-style", "font-family",
            "header-color", "header_color", "header-text-color", "header_text_color",
            "bg-color", "border-color", "hover-color", "active-color", "focus-color",
            "cursor-color", "button-color", "auto_size_display", "z-index", "z_index",
            "clock", "clock-format", "click-ob", "set-text", "set-val", "clear-val",
            "repaint-color", "repaint-bg", "play-anim", "script-start", "script-end",
            "on-click", "end-click", "on-input", "end-input", "on-hover", "end-hover",
            "on-key", "end-key", "every", "end-every", "table", "end-table",
            "box-start", "box-end", "row-start", "row-end", "col-start", "col-end",
            "shadow", "radius", "glass", "pad", "middle", "left", "right", "center",
            "align", "placeholder", "title", "size-top", "theme", "fon", "surface",
            "layer", "gradient", "glow", "border", "header", "size", "text", "switch",
            "hide", "show", "toggle", "fullscreen", "fullscrean", "animation", "animate",
            "particles", "svg", "image", "vd_plus", "engine"
        };

        public static readonly HashSet<string> Streams = new HashSet<string>(StringComparer.Ordinal)
        {
            "gout", "gin", "endl", "cout", "cin", "text"
        };

        public static readonly HashSet<string> Constants = new HashSet<string>(StringComparer.Ordinal)
        {
            "BLACK", "BLUE", "GREEN", "CYAN", "RED", "MAGENTA", "BROWN", "LIGHT_GRAY",
            "DARK_GRAY", "LIGHT_BLUE", "LIGHT_GREEN", "LIGHT_CYAN", "LIGHT_RED",
            "LIGHT_MAGENTA", "YELLOW", "WHITE",
            "true", "false", "null", "nil"
        };

        public static readonly List<ModuleInfo> Modules = new List<ModuleInfo>
        {
            new ModuleInfo("!GrassGreen!", "Базовое ядро языка GrassGreen 4.3.0, потоковый ввод-вывод (gout/gin), типы данных и операторы."),
            new ModuleInfo("!baremetal!", "Системная разработка и собственные ОС: 16 цветов VGA и HEX, плавный курсор 60+ FPS, read_key(), TUI-виджеты, перезапуск reboot() и выключение poweroff(), RamFS VFS."),
            new ModuleInfo("!bm!", "Короткий алиас библиотеки прямого программирования на голом железе (!baremetal!)."),
            new ModuleInfo("!kernel!", "Алиас библиотеки !baremetal! для ядер операционных систем."),
            new ModuleInfo("!hardware!", "Библиотека прямого низкоуровневого опроса оборудования ПК: RAM, CPU, Материнская плата, BIOS, PCI."),
            new ModuleInfo("!hw!", "Короткий алиас библиотеки низкоуровневого опроса оборудования (!hardware!)."),
            new ModuleInfo("!time!", "Модуль профилирования, таймеров и работы с системным временем."),
            new ModuleInfo("!interface!", "Графический Fluent GUI движок VD+ (Win32 / GDI+), окна, кнопки, поля ввода."),
            new ModuleInfo("!FastMath!", "Быстрая математика: тригонометрия, интерполяция lerp/clamp, расстояния."),
            new ModuleInfo("!CoreMath!", "Криптопримитивы (SHA-256), модульная арифметика pow_mod, крипто-стойкий рандом crypto_rand, число pi, gcd, pow_e и симулятор квантовых гейтов (qb_init, h, x, z, cnot, q_measure, q_rand)."),
            new ModuleInfo("!memory!", "Мониторинг ОЗУ, упаковка чисел pack_u16..64 и управление ареной памяти."),
            new ModuleInfo("!random!", "Генератор псевдослучайных целых и вещественных чисел."),
            new ModuleInfo("!database!", "Встроенный Key-Value движок GSE (файлы .gdb)."),
            new ModuleInfo("!GrassWeb!", "Реактивный Web-раннер, интеграция HTML/CSS интерфейсов."),
            new ModuleInfo("!GrassNet!", "Сетевой стек: сокеты TCP/IP, клиент HTTP/HTTPS, сетевые утилиты."),
            new ModuleInfo("!json!", "Парсинг, валидация и сериализация JSON структур."),
            new ModuleInfo("!sys!", "Мониторинг операционной системы Windows, CPU, ОЗУ, буфер обмена и аргументы cli_args()."),
            new ModuleInfo("!proc!", "Управление системными процессами Windows (PID, статус, память)."),
            new ModuleInfo("!fsys!", "Файловая система: чтение и запись текстовых и двоичных файлов (fs_read_bytes/fs_write_bytes)."),
            new ModuleInfo("!tg!", "Интеграция с Telegram Bot API."),
            new ModuleInfo("!2DGraphics!", "Аппаратная 2D-графика: примитивы, коллизии, игровой цикл."),
            new ModuleInfo("!keyboard!", "Библиотека контроллера клавиатуры PS/2: опрос портов 0x60/0x64, скан-коды."),
            new ModuleInfo("!asm!", "Встроенный ассемблер ядра: низкоуровневые вставки x86_64, регистры CR0-CR4, порты и прерывания."),
            new ModuleInfo("!audio!", "Нативный аудио-движок: синтез волн (синус, меандр, пила, треугольник, шум), тоны, WAV."),
            new ModuleInfo("!mmio!", "Прямой доступ к памяти (MMIO), аппаратные порты (in/out) и барьеры памяти."),
            new ModuleInfo("!thread!", "Нативная многопоточность: системные потоки ОС и мьютексы."),
            new ModuleInfo("!ffi!", "Универсальный FFI: динамическая загрузка библиотек (LoadLibraryA/dlopen), поиск символов ffi_sym и вызов нативных C-функций ffi_call."),
            new ModuleInfo("!langgen!", "Высокоуровневый движок создания предметно-ориентированных языков (DSL) и компиляторов: шаблоны правил, автоматический лексер, парсер выражений и интерпретатор."),
            new ModuleInfo("!math3d!", "3D линейная алгебра: векторы v3_create, v3_add, v3_sub, v3_mul, v3_dot, v3_cross, v3_length, v3_normalize, v3_lerp и матрицы 4x4 m4_identity, m4_translate, m4_rotate, m4_scale, m4_look_at, m4_perspective, m4_mul."),
            new ModuleInfo("!gfx3d!", "Программный 3D-растеризатор: z-буфер, загрузка OBJ, куб, сфера, плоскость, направленный и точечный свет, туман, скайбокс и рендеринг в буфер."),
            new ModuleInfo("!physics!", "Позиционная физика на базе XPBD: мир phys_world, твердые тела (phys_box, phys_sphere), ткань phys_cloth, связи phys_link, импульсы phys_impulse и raycast.")
        };

        public static bool IsModuleIncluded(string module, HashSet<string> included)
        {
            if (string.IsNullOrEmpty(module) || module.Equals("core", StringComparison.OrdinalIgnoreCase))
                return true;
            if (included == null || included.Count == 0)
                return false;
            if (included.Contains(module))
                return true;

            if (module.Equals("!baremetal!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!bm!") || included.Contains("!kernel!") || included.Contains("!BareMetal!");
            if (module.Equals("!hardware!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!hw!") || included.Contains("!sysinfo!") || included.Contains("!device!");
            if (module.Equals("!keyboard!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!kbd!") || included.Contains("!ps2!") || included.Contains("!PS2Keyboard!");
            if (module.Equals("!asm!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!GrassASM!") || included.Contains("!assembler!");
            if (module.Equals("!audio!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!sound!") || included.Contains("!GrassAudio!");
            if (module.Equals("!interface!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!gui!") || included.Contains("!GrassGUI!") || included.Contains("!vd!") || included.Contains("!vd+!") || included.Contains("!VD+!");
            if (module.Equals("!mmio!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!io!");
            if (module.Equals("!database!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!gdb!");
            if (module.Equals("!GrassWeb!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!GREEN_WWW1!");
            if (module.Equals("!GrassNet!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!net!");
            if (module.Equals("!langgen!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!languages!") || included.Contains("!lang!");
            if (module.Equals("!math3d!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!math3d!") || included.Contains("!Math3D!");
            if (module.Equals("!gfx3d!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!gfx3d!") || included.Contains("!Gfx3D!");
            if (module.Equals("!physics!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!physics!") || included.Contains("!Physics!");
            if (module.Equals("!CoreMath!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!CoreMath!");
            if (module.Equals("!2DGraphics!", StringComparison.OrdinalIgnoreCase))
                return included.Contains("!2DGraphics!");

            return false;
        }

        public static readonly List<BuiltinFunctionInfo> BuiltinFunctions = new List<BuiltinFunctionInfo>
        {
            // Core builtins (always available)
            new BuiltinFunctionInfo("len", "int len(take str_or_arr)", "Возвращает длину строки или количество элементов в массиве.", "len()", "core"),
            new BuiltinFunctionInfo("upper", "string upper(string str)", "Перевод всех символов строки в верхний регистр.", "upper()", "core"),
            new BuiltinFunctionInfo("lower", "string lower(string str)", "Перевод всех символов строки в нижний регистр.", "lower()", "core"),
            new BuiltinFunctionInfo("substr", "string substr(string str, int start, int length)", "Извлечение подстроки фиксированной длины начиная с позиции start.", "substr(str, 0, len)", "core"),
            new BuiltinFunctionInfo("replace", "string replace(string str, string search, string replacement)", "Замена всех вхождений подстроки search на replacement.", "replace(str, search, rep)", "core"),
            new BuiltinFunctionInfo("contains", "bool contains(string str, string substr)", "Проверка наличия подстроки (возвращает 1 или 0).", "contains(str, substr)", "core"),
            new BuiltinFunctionInfo("trim", "string trim(string str)", "Удаление начальных и завершающих пробелов и переводов строк.", "trim(str)", "core"),
            new BuiltinFunctionInfo("to_str", "string to_str(take val)", "Преобразование числа в строковый тип.", "to_str(val)", "core"),
            new BuiltinFunctionInfo("to_num", "float to_num(string str)", "Парсинг строки в числовое значение.", "to_num(str)", "core"),
            new BuiltinFunctionInfo("typeof", "string typeof(take val)", "Определение типа переменной (string, number, struct, array).", "typeof(val)", "core"),
            new BuiltinFunctionInfo("clear", "void clear()", "Аппаратная очистка экрана терминала.", "clear()", "core"),
            new BuiltinFunctionInfo("cls", "void cls()", "Аппаратная очистка экрана терминала (алиас clear).", "cls()", "core"),
            new BuiltinFunctionInfo("color", "void color(string name)", "Установка цвета текста консоли (green, cyan, red, yellow, white, blue, gray).", "color(\"green\");", "core"),
            new BuiltinFunctionInfo("msgbox", "void msgbox(string title, string text)", "Отображение стандартного модального окна Windows (MessageBox).", "msgbox(\"Title\", \"Message\");", "core"),
            new BuiltinFunctionInfo("system", "int system(string command)", "Выполнение команды в оболочке операционной системы (cmd.exe).", "system(\"cmd\");", "core"),
            new BuiltinFunctionInfo("exists", "bool exists(string filepath)", "Проверка физического наличия файла по пути (1 или 0).", "exists(path)", "core"),
            new BuiltinFunctionInfo("remove", "bool remove(string filepath)", "Удаление файла с диска.", "remove(path)", "core"),
            new BuiltinFunctionInfo("time", "int time()", "Текущий UNIX-таймстемп в секундах.", "time()", "core"),
            new BuiltinFunctionInfo("clock_ms", "int clock_ms()", "Значение монотонного аппаратного счётчика времени в миллисекундах.", "clock_ms()", "core"),
            new BuiltinFunctionInfo("beep", "void beep(int freq = 800, int ms = 200)", "Воспроизведение системного звукового сигнала указанной частоты и длительности.", "beep(800, 200);", "core"),
            new BuiltinFunctionInfo("input", "take input(string prompt, take target_var)", "Вывод подсказки и ожидание одиночного ввода до первого пробела.", "input(prompt, target);", "core"),

            // FFI (!ffi!)
            new BuiltinFunctionInfo("ffi_open", "take ffi_open(string lib_name)", "Динамически загружает системную библиотеку (LoadLibraryA / dlopen).", "ffi_open(\"kernel32.dll\");", "!ffi!"),
            new BuiltinFunctionInfo("ffi_sym", "take ffi_sym(take lib_handle, string func_name)", "Получает адрес экспортируемой C-функции по имени (GetProcAddress / dlsym).", "ffi_sym(lib, \"FuncName\");", "!ffi!"),
            new BuiltinFunctionInfo("ffi_call", "take ffi_call(take func_ptr, array args)", "Вызывает нативную C-функцию с передачей аргументов без перекомпиляции ядра.", "ffi_call(fn, [arg1, arg2]);", "!ffi!"),
            new BuiltinFunctionInfo("ffi_close", "void ffi_close(take lib_handle)", "Выгружает ранее загруженную динамическую библиотеку (FreeLibrary / dlclose).", "ffi_close(lib);", "!ffi!"),

            // LangGen (!langgen!)
            new BuiltinFunctionInfo("lang_new", "take lang_new(string name)", "Создает новый экземпляр собственного предметно-ориентированного языка (DSL).", "lang_new(\"MyLang\");", "!langgen!"),
            new BuiltinFunctionInfo("lang_comment", "void lang_comment(take lang, string prefix)", "Задает префикс комментариев языка (например, // или #).", "lang_comment(lang, \"//\");", "!langgen!"),
            new BuiltinFunctionInfo("lang_rule", "void lang_rule(take lang, string name, string pattern)", "Регистрирует синтаксическое правило по шаблону с плейсхолдерами ({id}, {num}, {str}, {expr}, {block}).", "lang_rule(lang, \"set_var\", \"set {id} = {expr}\");", "!langgen!"),
            new BuiltinFunctionInfo("lang_parse", "take lang_parse(take lang, string source)", "Выполняет лексический и синтаксический анализ кода на созданном языке.", "lang_parse(lang, code);", "!langgen!"),
            new BuiltinFunctionInfo("lang_count", "int lang_count(take ast)", "Возвращает количество распознанных инструкций верхнего уровня в AST.", "lang_count(ast);", "!langgen!"),
            new BuiltinFunctionInfo("lang_cmd", "string lang_cmd(take ast, int index)", "Возвращает имя правила сработавшей инструкции AST по индексу.", "lang_cmd(ast, i);", "!langgen!"),
            new BuiltinFunctionInfo("lang_arg", "take lang_arg(take ast, int index, string key)", "Извлекает строковое значение захваченного плейсхолдера по ключу.", "lang_arg(ast, i, \"key\");", "!langgen!"),
            new BuiltinFunctionInfo("lang_eval", "take lang_eval(take lang, string expr_str)", "Вычисляет значение математического/логического выражения с учетом переменных языка.", "lang_eval(lang, expr);", "!langgen!"),
            new BuiltinFunctionInfo("lang_set_var", "void lang_set_var(take lang, string name, take val)", "Устанавливает значение переменной в таблице символов DSL.", "lang_set_var(lang, \"x\", 10);", "!langgen!"),
            new BuiltinFunctionInfo("lang_get_var", "take lang_get_var(take lang, string name)", "Возвращает текущее значение переменной из таблицы символов DSL.", "lang_get_var(lang, \"x\");", "!langgen!"),

            // Filesystem (!fsys!)
            new BuiltinFunctionInfo("fs_read", "string fs_read(string path)", "Считывает всё содержимое текстового файла в строку.", "fs_read(path);", "!fsys!"),
            new BuiltinFunctionInfo("fs_write", "bool fs_write(string path, string content)", "Записывает текстовую строку в файл (перезаписывает существующий).", "fs_write(path, text);", "!fsys!"),
            new BuiltinFunctionInfo("fs_append", "bool fs_append(string path, string content)", "Дописывает текстовую строку в конец указанного файла.", "fs_append(path, text);", "!fsys!"),
            new BuiltinFunctionInfo("fs_exists", "bool fs_exists(string path)", "Проверяет физическое наличие файла или папки по пути.", "fs_exists(path);", "!fsys!"),
            new BuiltinFunctionInfo("fs_delete", "bool fs_delete(string path)", "Удаляет файл или пустую папку с диска.", "fs_delete(path);", "!fsys!"),
            new BuiltinFunctionInfo("fs_mkdir", "bool fs_mkdir(string path)", "Создает новую папку.", "fs_mkdir(path);", "!fsys!"),
            new BuiltinFunctionInfo("fs_size", "int fs_size(string path)", "Возвращает точный размер файла в байтах.", "fs_size(path);", "!fsys!"),
            new BuiltinFunctionInfo("fs_list", "array fs_list(string path)", "Возвращает массив имен файлов и папок внутри каталога.", "fs_list(path);", "!fsys!"),
            new BuiltinFunctionInfo("fs_copy", "bool fs_copy(string src, string dst)", "Копирует файл в указанный путь.", "fs_copy(src, dst);", "!fsys!"),
            new BuiltinFunctionInfo("fs_is_dir", "bool fs_is_dir(string path)", "Проверяет, является ли указанный путь папкой.", "fs_is_dir(path);", "!fsys!"),
            new BuiltinFunctionInfo("fs_read_bytes", "array fs_read_bytes(string path)", "Читает двоичный файл в массив байтов (чисел 0..255) без искажения нулевых байтов.", "fs_read_bytes(path);", "!fsys!"),
            new BuiltinFunctionInfo("fs_write_bytes", "bool fs_write_bytes(string path, array byte_array)", "Записывает массив байтов напрямую в бинарный файл.", "fs_write_bytes(path, bytes);", "!fsys!"),

            // System & OS (!sys!)
            new BuiltinFunctionInfo("cli_args", "array cli_args()", "Возвращает массив строк с аргументами командной строки, переданными при запуске.", "cli_args();", "!sys!"),
            new BuiltinFunctionInfo("pc_name", "string pc_name()", "Возвращает сетевое имя компьютера в Windows.", "pc_name();", "!sys!"),
            new BuiltinFunctionInfo("user_name", "string user_name()", "Возвращает имя текущего пользователя Windows.", "user_name();", "!sys!"),
            new BuiltinFunctionInfo("screen_w", "int screen_w()", "Возвращает ширину основного монитора в пикселях.", "screen_w();", "!sys!"),
            new BuiltinFunctionInfo("screen_h", "int screen_h()", "Возвращает высоту основного монитора в пикселях.", "screen_h();", "!sys!"),
            new BuiltinFunctionInfo("cpu_cores", "int cpu_cores()", "Возвращает количество логических ядер центрального процессора.", "cpu_cores();", "!sys!"),
            new BuiltinFunctionInfo("ram_total_mb", "int ram_total_mb()", "Возвращает общий объем оперативной памяти в мегабайтах.", "ram_total_mb();", "!sys!"),
            new BuiltinFunctionInfo("ram_free_mb", "int ram_free_mb()", "Возвращает объем свободной оперативной памяти в мегабайтах.", "ram_free_mb();", "!sys!"),
            new BuiltinFunctionInfo("uptime_sec", "int uptime_sec()", "Время непрерывной работы операционной системы в секундах.", "uptime_sec();", "!sys!"),
            new BuiltinFunctionInfo("clip_set", "void clip_set(string text)", "Помещает текстовую строку в буфер обмена Windows.", "clip_set(text);", "!sys!"),
            new BuiltinFunctionInfo("clip_get", "string clip_get()", "Считывает текущее текстовое содержимое буфера обмена Windows.", "clip_get();", "!sys!"),
            new BuiltinFunctionInfo("get_env", "string get_env(string name)", "Чтение значения системной переменной среды (PATH, TEMP и др.).", "get_env(\"PATH\");", "!sys!"),

            // Memory (!memory!)
            new BuiltinFunctionInfo("pack_u16", "array pack_u16(take val)", "Упаковывает 16-битное число в массив из 2 байтов (little-endian).", "pack_u16(val);", "!memory!"),
            new BuiltinFunctionInfo("pack_u32", "array pack_u32(take val)", "Упаковывает 32-битное число в массив из 4 байтов (little-endian).", "pack_u32(val);", "!memory!"),
            new BuiltinFunctionInfo("pack_u64", "array pack_u64(take val)", "Упаковывает 64-битное число в массив из 8 байтов (little-endian).", "pack_u64(val);", "!memory!"),
            new BuiltinFunctionInfo("memory_free", "void memory_free()", "Сбрасывает системную арену памяти GrassGreen.", "memory_free();", "!memory!"),
            new BuiltinFunctionInfo("memory_usit", "int memory_usit()", "Возвращает объем занятой памяти внутри текущей арены в байтах.", "memory_usit();", "!memory!"),
            new BuiltinFunctionInfo("memory_alloc", "take memory_alloc(int bytes)", "Выделение непрерывного двоичного буфера байтов заданного размера.", "memory_alloc(bytes);", "!memory!"),
            new BuiltinFunctionInfo("memory_free_block", "void memory_free_block(take id)", "Освобождение выделенного буфера памяти по дескриптору.", "memory_free_block(id);", "!memory!"),
            new BuiltinFunctionInfo("memory_dump", "void memory_dump()", "Вывод отладочного слепка стека виртуальной машины и переменных.", "memory_dump();", "!memory!"),
            new BuiltinFunctionInfo("memory_limit", "void memory_limit(int mb)", "Установка лимита потребления памяти процессом в мегабайтах.", "memory_limit(1024);", "!memory!"),

            // Baremetal & RamFS / VFS (!baremetal!)
            new BuiltinFunctionInfo("read_key", "string read_key()", "Высокоуровневый ввод: считывает нажатие клавиши.", "read_key()", "!baremetal!"),
            new BuiltinFunctionInfo("draw_box", "void draw_box(int x, int y, int w, int h, string title = \"\", int fg = LIGHT_GREEN, int bg = BLACK)", "Отрисовка TUI диалогового окна с рамкой и заголовком.", "draw_box(x, y, w, h, \"title\");", "!baremetal!"),
            new BuiltinFunctionInfo("menu", "int menu(string title, array options)", "Интерактивное меню TUI с навигацией стрелками и подтверждением Enter.", "menu(\"Title\", [\"Option 1\", \"Option 2\"])", "!baremetal!"),
            new BuiltinFunctionInfo("progress_bar", "void progress_bar(int x, int y, int width, int percent, int fg = LIGHT_GREEN, int bg = BLACK)", "Индикатор выполнения (Progress Bar) с процентами.", "progress_bar(x, y, width, percent);", "!baremetal!"),
            new BuiltinFunctionInfo("reboot", "void reboot()", "Мгновенный аппаратный перезапуск компьютера через контроллер 8042.", "reboot();", "!baremetal!"),
            new BuiltinFunctionInfo("poweroff", "void poweroff()", "Аппаратное отключение питания ПК через контроллер ACPI.", "poweroff();", "!baremetal!"),
            new BuiltinFunctionInfo("virtual_struct", "virtual_struct { ... }", "Декларативное описание дерева папок и файлов RamFS / VFS в коде ядра ОС.", null, "!baremetal!"),
            new BuiltinFunctionInfo("physical_struct", "void physical_struct(string host_path)", "Автоматический перенос реальной папки хост-системы в образ диска ОС при сборке.", "physical_struct(\"app\");", "!baremetal!"),
            new BuiltinFunctionInfo("ls", "string ls(string flags = \"\")", "Показывает структуру файлов и папок в текущем пути VFS.", "ls()", "!baremetal!"),
            new BuiltinFunctionInfo("pwd", "string pwd()", "Возвращает текущий путь в файловой системе VFS.", "pwd()", "!baremetal!"),
            new BuiltinFunctionInfo("cd", "bool cd(string path)", "Перемещение по каталогам VFS.", "cd(\"..\");", "!baremetal!"),
            new BuiltinFunctionInfo("cr_folder", "bool cr_folder(string name)", "Создаёт новую папку в текущем каталоге VFS.", "cr_folder(\"folder\");", "!baremetal!"),
            new BuiltinFunctionInfo("cr_file", "bool cr_file(string name, string content = \"\")", "Создаёт новый файл в текущем каталоге VFS.", "cr_file(\"file.txt\", \"content\");", "!baremetal!"),
            new BuiltinFunctionInfo("copy", "bool copy(string src, string dst)", "Копирует файл в файловой системе VFS.", "copy(\"src\", \"dst\");", "!baremetal!"),
            new BuiltinFunctionInfo("move", "bool move(string src, string dst)", "Перемещает файл в файловой системе VFS.", "move(\"src\", \"dst\");", "!baremetal!"),
            new BuiltinFunctionInfo("delete", "bool delete(string path)", "Удаляет файл или каталог в файловой системе VFS.", "delete(\"path\");", "!baremetal!"),
            new BuiltinFunctionInfo("not_show", "bool not_show(string name)", "Скрывает файл или папку при выводе команды ls.", "not_show(\"filename\");", "!baremetal!"),
            new BuiltinFunctionInfo("specSign_show", "void specSign_show(string flag)", "Управляет отображением скрытых файлов.", "specSign_show(\"-a\");", "!baremetal!"),
            new BuiltinFunctionInfo("not_clear", "not_clear { ... } / not_clear;", "Делает блок вывода не удаляемым при очистке экрана clear.", null, "!baremetal!"),

            // Hardware Inspection (!hardware!)
            new BuiltinFunctionInfo("hw_cpu_vendor", "string hw_cpu_vendor()", "Производитель процессора (GenuineIntel, AuthenticAMD).", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_cpu_brand", "string hw_cpu_brand()", "Полное коммерческое название модели процессора.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_cpu_cores", "int hw_cpu_cores()", "Число физических ядер процессора.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_cpu_threads", "int hw_cpu_threads()", "Число логических процессоров / потоков.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_cpu_base_freq", "int hw_cpu_base_freq()", "Базовая тактовая частота в МГц.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_cpu_freq", "int hw_cpu_freq()", "Текущая тактовая частота в МГц.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_cpu_usage", "int hw_cpu_usage()", "Загрузка процессора в процентах (0 - 100%).", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_cpu_temp", "int hw_cpu_temp()", "Температура процессора в градусах Цельсия.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_ram_total", "int hw_ram_total()", "Общий объем оперативной памяти в мегабайтах.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_ram_used", "int hw_ram_used()", "Занятый объем оперативной памяти в мегабайтах.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_ram_free", "int hw_ram_free()", "Свободный объем оперативной памяти в мегабайтах.", null, "!hardware!"),
            new BuiltinFunctionInfo("proc_mem", "int proc_mem", "Текущее использование памяти в мегабайтах.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_uptime_sec", "int hw_uptime_sec()", "Время непрерывной работы в секундах.", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_uptime_str", "string hw_uptime_str()", "Время непрерывной работы (ДД:ЧЧ:ММ:СС).", null, "!hardware!"),
            new BuiltinFunctionInfo("hw_rdtsc", "u64 hw_rdtsc()", "Аппаратный счетчик тактов процессора Time-Stamp Counter.", null, "!hardware!"),

            // Keyboard (!keyboard!)
            new BuiltinFunctionInfo("kbd_has_key", "bool kbd_has_key()", "Проверка наличия данных в буфере контроллера клавиатуры.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_available", "bool kbd_available()", "Алиас kbd_has_key(): проверка готовности нажатой клавиши.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_read_scancode", "u8 kbd_read_scancode()", "Считывание сырого скан-кода PS/2 из порта 0x60.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_get_scancode", "u8 kbd_get_scancode()", "Блокирующее ожидание и чтение скан-кода нажатия.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_scancode_to_char", "string kbd_scancode_to_char(take scancode)", "Преобразование скан-кода PS/2 в символ ASCII.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_scancode_name", "string kbd_scancode_name(take scancode)", "Получение читаемого имени клавиши (Enter, Backspace, Space...).", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_getc", "string kbd_getc()", "Блокирующее чтение одного символа с клавиатуры.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_poll", "string kbd_poll()", "Неблокирующий опрос: возвращает символ клавиши или пустую строку.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_read_line", "string kbd_read_line()", "Построчный ввод строки до Enter с поддержкой эха и Backspace.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_is_shift", "bool kbd_is_shift()", "Проверка зажатия клавиши Shift.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_is_caps", "bool kbd_is_caps()", "Проверка активности режима Caps Lock.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_is_ctrl", "bool kbd_is_ctrl()", "Проверка зажатия клавиши Control.", null, "!keyboard!"),
            new BuiltinFunctionInfo("kbd_is_alt", "bool kbd_is_alt()", "Проверка зажатия клавиши Alt.", null, "!keyboard!"),

            // ASM & Kernel (!asm!)
            new BuiltinFunctionInfo("cli", "void cli()", "Сброс флага прерываний (CLI). Запрещает аппаратные прерывания.", "cli();", "!asm!"),
            new BuiltinFunctionInfo("sti", "void sti()", "Установка флага прерываний (STI). Разрешает аппаратные прерывания.", "sti();", "!asm!"),
            new BuiltinFunctionInfo("hlt", "void hlt()", "Остановка процессора до следующего аппаратного прерывания (HLT).", "hlt();", "!asm!"),
            new BuiltinFunctionInfo("in", "u8 in(u16 port)", "Чтение байта из аппаратного порта ввода-вывода x86 (IN AL, DX).", "in(port);", "!asm!"),
            new BuiltinFunctionInfo("out", "void out(u16 port, u8 val)", "Запись байта в аппаратный порт ввода-вывода x86 (OUT DX, AL).", "out(port, val);", "!asm!"),
            new BuiltinFunctionInfo("read_cr0", "u64 read_cr0()", "Чтение системного регистра управления CR0.", "read_cr0();", "!asm!"),
            new BuiltinFunctionInfo("write_cr0", "void write_cr0(u64 val)", "Запись в системный регистр управления CR0.", "write_cr0(val);", "!asm!"),
            new BuiltinFunctionInfo("read_cr2", "u64 read_cr2()", "Чтение регистра CR2 (линейный адрес Page Fault).", "read_cr2();", "!asm!"),
            new BuiltinFunctionInfo("write_cr2", "void write_cr2(u64 val)", "Запись в регистр CR2.", "write_cr2(val);", "!asm!"),
            new BuiltinFunctionInfo("read_cr3", "u64 read_cr3()", "Чтение физического адреса таблицы страниц PML4 (CR3).", "read_cr3();", "!asm!"),
            new BuiltinFunctionInfo("write_cr3", "void write_cr3(u64 val)", "Загрузка нового адреса каталога страниц PML4 в CR3.", "write_cr3(val);", "!asm!"),
            new BuiltinFunctionInfo("read_cr4", "u64 read_cr4()", "Чтение регистра управления CR4.", "read_cr4();", "!asm!"),
            new BuiltinFunctionInfo("write_cr4", "void write_cr4(u64 val)", "Запись в системный регистр управления CR4.", "write_cr4(val);", "!asm!"),
            new BuiltinFunctionInfo("read_rflags", "u64 read_rflags()", "Чтение регистра флагов процессора RFLAGS.", "read_rflags();", "!asm!"),
            new BuiltinFunctionInfo("write_rflags", "void write_rflags(u64 val)", "Запись в регистр флагов процессора RFLAGS.", "write_rflags(val);", "!asm!"),
            new BuiltinFunctionInfo("rdtsc", "u64 rdtsc()", "Чтение аппаратного счетчика тактов процессора RDTSC.", "rdtsc();", "!asm!"),
            new BuiltinFunctionInfo("pause", "void pause()", "Оптимизационная пауза для спинлоков (PAUSE).", "pause();", "!asm!"),
            new BuiltinFunctionInfo("nop", "void nop()", "Холостая инструкция процессора (NOP).", "nop();", "!asm!"),
            new BuiltinFunctionInfo("asm_run", "take asm_run(take code)", "Трансляция и выполнение ассемблерного кода x86_64 без внешних компиляторов.", "asm_run(code);", "!asm!"),

            // Audio (!audio!)
            new BuiltinFunctionInfo("audio_play", "bool audio_play(string path)", "Воспроизводит аудиофайл WAV.", "audio_play(path);", "!audio!"),
            new BuiltinFunctionInfo("audio_stop", "void audio_stop()", "Останавливает воспроизведение всех звуков.", "audio_stop();", "!audio!"),
            new BuiltinFunctionInfo("audio_tone", "void audio_tone(int freq, int ms)", "Генерирует звуковой тон заданной частоты и длительности.", "audio_tone(440, 500);", "!audio!"),
            new BuiltinFunctionInfo("audio_synth", "void audio_synth(int freq, int ms, int wave_type)", "Синтезирует волну (0=синус, 1=меандр, 2=пила, 3=треугольник, 4=шум).", "audio_synth(440, 500, 0);", "!audio!"),
            new BuiltinFunctionInfo("audio_volume", "void audio_volume(int vol)", "Устанавливает уровень громкости звука от 0 до 100%.", "audio_volume(80);", "!audio!"),
            new BuiltinFunctionInfo("audio_is_playing", "bool audio_is_playing()", "Возвращает true, если воспроизводится звук.", "audio_is_playing();", "!audio!"),

            // MMIO (!mmio!)
            new BuiltinFunctionInfo("mmio_read8", "u8 mmio_read8(take addr)", "Считывает 8-битное значение по физическому адресу без кэширования.", "mmio_read8(addr);", "!mmio!"),
            new BuiltinFunctionInfo("mmio_read16", "u16 mmio_read16(take addr)", "Считывает 16-битное значение по адресу.", "mmio_read16(addr);", "!mmio!"),
            new BuiltinFunctionInfo("mmio_read32", "u32 mmio_read32(take addr)", "Считывает 32-битное значение по адресу.", "mmio_read32(addr);", "!mmio!"),
            new BuiltinFunctionInfo("mmio_read64", "u64 mmio_read64(take addr)", "Считывает 64-битное значение по адресу.", "mmio_read64(addr);", "!mmio!"),
            new BuiltinFunctionInfo("mmio_write8", "void mmio_write8(take addr, u8 val)", "Записывает 8-битное значение по адресу с барьером памяти.", "mmio_write8(addr, val);", "!mmio!"),
            new BuiltinFunctionInfo("mmio_write16", "void mmio_write16(take addr, u16 val)", "Записывает 16-битное значение по адресу.", "mmio_write16(addr, val);", "!mmio!"),
            new BuiltinFunctionInfo("mmio_write32", "void mmio_write32(take addr, u32 val)", "Записывает 32-битное значение по адресу.", "mmio_write32(addr, val);", "!mmio!"),
            new BuiltinFunctionInfo("mmio_write64", "void mmio_write64(take addr, u64 val)", "Записывает 64-битное значение по адресу.", "mmio_write64(addr, val);", "!mmio!"),
            new BuiltinFunctionInfo("memory_barrier", "void memory_barrier()", "Устанавливает полный барьер памяти процессора.", "memory_barrier();", "!mmio!"),
            new BuiltinFunctionInfo("inb", "u8 inb(u16 port)", "Чтение байта из аппаратного порта ввода-вывода.", "inb(port);", "!mmio!"),
            new BuiltinFunctionInfo("outb", "void outb(u16 port, u8 val)", "Запись байта в аппаратный порт ввода-вывода.", "outb(port, val);", "!mmio!"),

            // Math (!FastMath!)
            new BuiltinFunctionInfo("sqrt", "float sqrt(float x)", "Вычисляет квадратный корень числа.", "sqrt(x)", "!FastMath!"),
            new BuiltinFunctionInfo("pow", "float pow(float base, float exp)", "Возводит base в степень exp.", "pow(base, exp)", "!FastMath!"),
            new BuiltinFunctionInfo("sin", "float sin(float rad)", "Тригонометрический синус угла в радианах.", "sin(rad)", "!FastMath!"),
            new BuiltinFunctionInfo("cos", "float cos(float rad)", "Тригонометрический косинус угла в радианах.", "cos(rad)", "!FastMath!"),
            new BuiltinFunctionInfo("tan", "float tan(float rad)", "Тригонометрический тангенс угла в радианах.", "tan(rad)", "!FastMath!"),
            new BuiltinFunctionInfo("atan2", "float atan2(float y, float x)", "Арктангенс от двух переменных y / x с учетом квадранта.", "atan2(y, x)", "!FastMath!"),
            new BuiltinFunctionInfo("abs", "take abs(take x)", "Абсолютная величина числа.", "abs(x)", "!FastMath!"),
            new BuiltinFunctionInfo("floor", "float floor(float x)", "Округление вниз до ближайшего целого.", "floor(x)", "!FastMath!"),
            new BuiltinFunctionInfo("ceil", "float ceil(float x)", "Округление вверх до ближайшего целого.", "ceil(x)", "!FastMath!"),
            new BuiltinFunctionInfo("lerp", "float lerp(float a, float b, float t)", "Линейная интерполяция между a и b с фактором t [0..1].", "lerp(a, b, t)", "!FastMath!"),
            new BuiltinFunctionInfo("clamp", "float clamp(float val, float min, float max)", "Ограничивает значение диапазоном [min, max].", "clamp(val, min, max)", "!FastMath!"),
            new BuiltinFunctionInfo("dist2d", "float dist2d(float x1, float y1, float x2, float y2)", "Евклидово расстояние между двумя точками на плоскости.", "dist2d(x1, y1, x2, y2)", "!FastMath!"),

            // Random (!random!)
            new BuiltinFunctionInfo("rand_int", "int rand_int(int min, int max)", "Генерирует случайное целое число от min до max включительно.", "rand_int(min, max);", "!random!"),
            new BuiltinFunctionInfo("rand_float", "float rand_float(float min, float max)", "Генерирует вещественное случайное число в диапазоне [min, max].", "rand_float(min, max);", "!random!"),

            // Process (!proc!)
            new BuiltinFunctionInfo("proc_id", "int proc_id()", "Возвращает Process ID (PID) текущего запущенного процесса.", "proc_id();", "!proc!"),
            new BuiltinFunctionInfo("proc_kill", "bool proc_kill(int pid)", "Принудительно завершает процесс по идентификатору pid.", "proc_kill(pid);", "!proc!"),
            new BuiltinFunctionInfo("proc_is_alive", "bool proc_is_alive(int pid)", "Проверяет, работает ли процесс с указанным pid.", "proc_is_alive(pid);", "!proc!"),
            new BuiltinFunctionInfo("proc_ram", "int proc_ram(int pid)", "Возвращает объем памяти (Working Set) процесса в килобайтах.", "proc_ram(pid);", "!proc!"),

            // Time (!time!)
            new BuiltinFunctionInfo("time_ms", "int time_ms()", "Текущее монотонное системное время в миллисекундах.", "time_ms();", "!time!"),
            new BuiltinFunctionInfo("get_time_str", "string get_time_str()", "Текущее системное время в формате ЧЧ:ММ:СС.", "get_time_str();", "!time!"),
            new BuiltinFunctionInfo("get_date", "string get_date()", "Текущая системная дата в формате ДД.ММ.ГГГГ.", "get_date();", "!time!"),
            new BuiltinFunctionInfo("start_time", "void start_time()", "Запуск аппаратного таймера профилирования кода.", "start_time();", "!time!"),
            new BuiltinFunctionInfo("stop_time", "void stop_time()", "Остановка таймера профилирования.", "stop_time();", "!time!"),
            new BuiltinFunctionInfo("conclus_time", "int conclus_time()", "Получить прошедшее время в миллисекундах.", "conclus_time();", "!time!"),

            // JSON (!json!)
            new BuiltinFunctionInfo("json_parse", "take json_parse(string json_str)", "Парсит строку JSON в объект или массив GrassGreen.", "json_parse(json_str);", "!json!"),
            new BuiltinFunctionInfo("json_stringify", "string json_stringify(take val)", "Сериализует значение, структуру или массив в строку JSON.", "json_stringify(val);", "!json!"),
            new BuiltinFunctionInfo("json_get", "take json_get(string json_str, string key)", "Быстрое извлечение строкового поля key из JSON.", "json_get(json_str, key);", "!json!"),
            new BuiltinFunctionInfo("json_valid", "bool json_valid(string json_str)", "Проверяет корректность синтаксиса JSON строки.", "json_valid(json_str);", "!json!"),

            // Thread (!thread!)
            new BuiltinFunctionInfo("thread", "take thread(take func, take arg)", "Запускает функцию в отдельном системном потоке ОС.", "thread(fn, arg);", "!thread!"),
            new BuiltinFunctionInfo("thread_join", "take thread_join(take handle)", "Блокирует текущий поток до завершения потока handle.", "thread_join(h);", "!thread!"),
            new BuiltinFunctionInfo("thread_detach", "void thread_detach(take handle)", "Отсоединяет поток handle для фонового выполнения.", "thread_detach(h);", "!thread!"),
            new BuiltinFunctionInfo("thread_id", "int thread_id()", "Возвращает системный идентификатор (TID) текущего потока.", "thread_id();", "!thread!"),
            new BuiltinFunctionInfo("thread_sleep", "void thread_sleep(int ms)", "Приостанавливает выполнение текущего потока на ms миллисекунд.", "thread_sleep(ms);", "!thread!"),
            new BuiltinFunctionInfo("mutex_create", "take mutex_create()", "Создает новый системный мьютекс.", "mutex_create();", "!thread!"),
            new BuiltinFunctionInfo("mutex_lock", "void mutex_lock(take mutex)", "Захватывает мьютекс.", "mutex_lock(m);", "!thread!"),
            new BuiltinFunctionInfo("mutex_unlock", "void mutex_unlock(take mutex)", "Освобождает ранее захваченный мьютекс.", "mutex_unlock(m);", "!thread!"),
            new BuiltinFunctionInfo("mutex_destroy", "void mutex_destroy(take mutex)", "Уничтожает системный мьютекс.", "mutex_destroy(m);", "!thread!"),

            // GUI VD+ Engine (!interface!)
            new BuiltinFunctionInfo("auto_size_display", "void auto_size_display()", "Автоматически определяет разрешение экрана (High-DPI 2K/4K) и масштабирует окно.", "auto_size_display();", "!interface!"),
            new BuiltinFunctionInfo("border-radius-app", "void border-radius-app(int radius)", "Задает скругление внешних углов окна приложения в пикселях.", "border-radius-app(16);", "!interface!"),
            new BuiltinFunctionInfo("header", "void header(string style)", "Стиль шапки окна (emerald, native, glass, modern, none).", "header(\"emerald\");", "!interface!"),
            new BuiltinFunctionInfo("header-color", "void header-color(string hex_color)", "Цвет фона заголовка окна.", "header-color(\"#111827\");", "!interface!"),
            new BuiltinFunctionInfo("header-text-color", "void header-text-color(string hex_color)", "Цвет текста заголовка окна.", "header-text-color(\"#22c55e\");", "!interface!"),
            new BuiltinFunctionInfo("clock", "void clock()", "Встроенный синхронный виджет часов (ЧЧ:ММ:СС).", "clock();", "!interface!"),
            new BuiltinFunctionInfo("tab", "tab = \"Name\" { ... }", "Создает страницу вкладки внутри окна приложения.", null, "!interface!"),
            new BuiltinFunctionInfo("switch", "void switch(string tab_name)", "Переключает активную вкладку окна.", "switch(\"TabName\");", "!interface!"),
            new BuiltinFunctionInfo("fullscreen", "void fullscreen()", "Разворачивает окно приложения на весь экран.", "fullscreen();", "!interface!"),
            new BuiltinFunctionInfo("z-index", "void z-index(int level)", "Слой отрисовки виджета или окна.", "z-index(1);", "!interface!"),
            new BuiltinFunctionInfo("particles", "void particles(int count, string color)", "Анимированные светящиеся частицы на фоне окна.", "particles(50, \"#22c55e\");", "!interface!"),
            new BuiltinFunctionInfo("surface", "void surface(string mode)", "Режим рендеринга (freestanding, direct, windowed, auto).", "surface(\"freestanding\");", "!interface!"),
            new BuiltinFunctionInfo("window", "window { ... }", "Главный блок графического окна приложения.", null, "!interface!"),
            new BuiltinFunctionInfo("button", "button \"Text\" { ... }", "Виджет интерактивной кнопки.", null, "!interface!"),
            new BuiltinFunctionInfo("label", "label \"Text\" { ... }", "Виджет текстовой метки.", null, "!interface!"),
            new BuiltinFunctionInfo("window-ob", "void window-ob(int x, int y)", "Позиционирование виджета в координатах окна.", "window-ob(x, y);", "!interface!"),
            new BuiltinFunctionInfo("size", "void size(int width, int height)", "Размер окна или элемента интерфейса.", "size(w, h);", "!interface!"),

            // CoreMath (!CoreMath!)
            new BuiltinFunctionInfo("sha256", "string sha256(string data)", "Хеширование данных по криптографическому стандарту SHA-256 (64 hex-символа).", "sha256(data);", "!CoreMath!"),
            new BuiltinFunctionInfo("pow_mod", "int pow_mod(int base, int exp, int mod)", "Быстрое модульное возведение в степень (base^exp % mod) для криптографии RSA/DH.", "pow_mod(base, exp, mod);", "!CoreMath!"),
            new BuiltinFunctionInfo("crypto_rand", "int crypto_rand(int min, int max)", "Криптографически стойкое случайное число из системного энтропийного пула ОС/аппаратного генератора.", "crypto_rand(min, max);", "!CoreMath!"),
            new BuiltinFunctionInfo("gcd", "int gcd(int a, int b)", "Наибольший общий делитель двух целых чисел по алгоритму Евклида.", "gcd(a, b);", "!CoreMath!"),
            new BuiltinFunctionInfo("pow_e", "float pow_e(float x)", "Вычисление экспоненциальной функции e^x.", "pow_e(x);", "!CoreMath!"),
            new BuiltinFunctionInfo("pi", "float pi()", "Математическая константа Пи (3.141592653589793).", "pi();", "!CoreMath!"),
            new BuiltinFunctionInfo("qb_init", "take qb_init(int id)", "Инициализирует квантовый кубит в базисном состоянии |0>.", "qb_init(0);", "!CoreMath!"),
            new BuiltinFunctionInfo("h", "take h(int id)", "Квантовый гейт Адамара: перевод кубита в суперпозицию (|0> + |1>)/sqrt(2).", "h(0);", "!CoreMath!"),
            new BuiltinFunctionInfo("x", "take x(int id)", "Квантовый гейт Паули-X (квантовое НЕ): инверсия амплитуд кубита.", "x(0);", "!CoreMath!"),
            new BuiltinFunctionInfo("z", "take z(int id)", "Квантовый гейт Паули-Z: поворот фазы состояния |1> на 180 градусов.", "z(0);", "!CoreMath!"),
            new BuiltinFunctionInfo("cnot", "take cnot(int control, int target)", "Квантовый гейт CNOT (управляемое НЕ): запутывание двух кубитов.", "cnot(0, 1);", "!CoreMath!"),
            new BuiltinFunctionInfo("q_measure", "int q_measure(int id)", "Коллапс волновой функции кубита и измерение классического бита (0 или 1).", "q_measure(0);", "!CoreMath!"),
            new BuiltinFunctionInfo("q_rand", "int q_rand()", "Квантовый генератор истинно случайных бит на базе гейта Адамара и измерения.", "q_rand();", "!CoreMath!"),

            // 3D Math (!math3d!)
            new BuiltinFunctionInfo("v3_create", "take v3_create(float x, float y, float z)", "Создает трехмерный вектор (x, y, z).", "v3_create(x, y, z);", "!math3d!"),
            new BuiltinFunctionInfo("v3_add", "take v3_add(take a, take b)", "Векторное сложение (a + b).", "v3_add(a, b);", "!math3d!"),
            new BuiltinFunctionInfo("v3_sub", "take v3_sub(take a, take b)", "Векторное вычитание (a - b).", "v3_sub(a, b);", "!math3d!"),
            new BuiltinFunctionInfo("v3_mul", "take v3_mul(take v, float s)", "Умножение 3D-вектора на скалярное число.", "v3_mul(v, s);", "!math3d!"),
            new BuiltinFunctionInfo("v3_dot", "float v3_dot(take a, take b)", "Скалярное произведение двух 3D-векторов.", "v3_dot(a, b);", "!math3d!"),
            new BuiltinFunctionInfo("v3_cross", "take v3_cross(take a, take b)", "Векторное произведение двух 3D-векторов.", "v3_cross(a, b);", "!math3d!"),
            new BuiltinFunctionInfo("v3_length", "float v3_length(take v)", "Евклидова длина (модуль) 3D-вектора.", "v3_length(v);", "!math3d!"),
            new BuiltinFunctionInfo("v3_normalize", "take v3_normalize(take v)", "Нормализация вектора к единичной длине.", "v3_normalize(v);", "!math3d!"),
            new BuiltinFunctionInfo("v3_lerp", "take v3_lerp(take a, take b, float t)", "Линейная интерполяция между векторами a и b с коэффициентом t [0..1].", "v3_lerp(a, b, t);", "!math3d!"),
            new BuiltinFunctionInfo("m4_identity", "take m4_identity()", "Возвращает единичную матрицу 4x4.", "m4_identity();", "!math3d!"),
            new BuiltinFunctionInfo("m4_translate", "take m4_translate(float x, float y, float z)", "Матрица трансляции (переноса) 4x4.", "m4_translate(x, y, z);", "!math3d!"),
            new BuiltinFunctionInfo("m4_rotate", "take m4_rotate(float rad, float ax, float ay, float az)", "Матрица вращения вокруг произвольной оси.", "m4_rotate(rad, ax, ay, az);", "!math3d!"),
            new BuiltinFunctionInfo("m4_scale", "take m4_scale(float sx, float sy, float sz)", "Матрица масштабирования 4x4.", "m4_scale(sx, sy, sz);", "!math3d!"),
            new BuiltinFunctionInfo("m4_look_at", "take m4_look_at(take eye, take target, take up)", "Матрица вида камеры (View Matrix).", "m4_look_at(eye, target, up);", "!math3d!"),
            new BuiltinFunctionInfo("m4_perspective", "take m4_perspective(float fov, float aspect, float near, float far)", "Матрица перспективной проекции.", "m4_perspective(fov, aspect, near, far);", "!math3d!"),
            new BuiltinFunctionInfo("m4_mul", "take m4_mul(take a, take b)", "Умножение матриц 4x4.", "m4_mul(a, b);", "!math3d!"),

            // 2D Graphics (!2DGraphics!)
            new BuiltinFunctionInfo("g2d_window", "void g2d_window(string title, int w, int h)", "Создает окно 2D-графики с двойной буферизацией.", "g2d_window(\"Title\", 800, 600);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_is_open", "bool g2d_is_open()", "Проверяет активность окна и опрашивает события ОС.", "g2d_is_open()", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_clear", "void g2d_clear(int color_hex)", "Очищает 2D буфер экрана заданным цветом (0xRRGGBB).", "g2d_clear(0x000000);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_present", "void g2d_present()", "Отображает готовый 2D-кадр на экране (swap buffers).", "g2d_present();", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_dt", "float g2d_dt()", "Возвращает дельту времени кадра в секундах.", "g2d_dt()", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_key_down", "bool g2d_key_down(int vk_code)", "Проверяет зажатие клавиши клавиатуры по коду VK.", "g2d_key_down(key)", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_mouse_x", "int g2d_mouse_x()", "Координата X курсора мыши в окне.", "g2d_mouse_x()", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_mouse_y", "int g2d_mouse_y()", "Координата Y курсора мыши в окне.", "g2d_mouse_y()", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_mouse_btn", "bool g2d_mouse_btn(int btn)", "Проверяет нажатие кнопки мыши (0=left, 1=right).", "g2d_mouse_btn(0)", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_load_tex", "int g2d_load_tex(string path)", "Загружает растровое изображение в текстуру и возвращает ID.", "g2d_load_tex(path);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_draw_sprite", "void g2d_draw_sprite(int tex_id, float x, float y, float w, float h, float angle = 0)", "Отрисовывает текстурированный 2D-спрайт.", "g2d_draw_sprite(tex, x, y, w, h, 0);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_draw_frame", "void g2d_draw_frame(int tex_id, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh)", "Отрисовывает кадр из атласа спрайтов (spritesheet).", "g2d_draw_frame(tex, sx, sy, sw, sh, dx, dy, dw, dh);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_cam_set", "void g2d_cam_set(float x, float y, float zoom)", "Устанавливает позицию и масштаб 2D камеры.", "g2d_cam_set(x, y, 1.0);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_cam_shake", "void g2d_cam_shake(float intensity, float duration)", "Запускает эффект вибрации/тряски камеры.", "g2d_cam_shake(5.0, 0.3);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_emit_particles", "void g2d_emit_particles(float x, float y, int count, int color_hex)", "Генерирует взрыв светящихся 2D частиц.", "g2d_emit_particles(x, y, 20, 0x00FF00);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_update_particles", "void g2d_update_particles(float dt)", "Обновляет физику и отрисовывает активные частицы.", "g2d_update_particles(dt);", "!2DGraphics!"),
            new BuiltinFunctionInfo("g2d_bind_framebuffer", "void g2d_bind_framebuffer(take buffer_ptr, int w, int h, int pitch)", "Привязывает внешний фреймбуфер для прямого рендеринга на bare-metal.", "g2d_bind_framebuffer(fb, w, h, pitch);", "!2DGraphics!"),

            // 3D Graphics Rasterizer (!gfx3d!)
            new BuiltinFunctionInfo("g3d_window", "void g3d_window(string title, int w, int h)", "Создает окно для программного 3D рендеринга.", "g3d_window(\"3D Scene\", 800, 600);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_is_open", "bool g3d_is_open()", "Проверяет активность 3D окна.", "g3d_is_open()", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_clear", "void g3d_clear(int color_hex)", "Очищает цветовой буфер и Z-буфер глубины.", "g3d_clear(0x000000);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_present", "void g3d_present()", "Выводит отрисованный 3D кадр на экран.", "g3d_present();", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_dt", "float g3d_dt()", "Дельта времени кадра 3D симуляции.", "g3d_dt()", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_key_down", "bool g3d_key_down(take key)", "Проверяет зажатие клавиши клавиатуры по ASCII/VK коду или имени (\"W\", \"SPACE\", 32).", "g3d_key_down(key)", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_mouse_x", "int g3d_mouse_x()", "Координата X курсора мыши внутри окна 3D сцены.", "g3d_mouse_x()", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_mouse_y", "int g3d_mouse_y()", "Координата Y курсора мыши внутри окна 3D сцены.", "g3d_mouse_y()", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_mouse_btn", "bool g3d_mouse_btn(int btn = 0)", "Проверяет нажатие кнопки мыши (0=left, 1=right, 2=middle).", "g3d_mouse_btn(0)", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_camera", "void g3d_camera(take pos, take target, float fov)", "Задает параметры 3D камеры сцены.", "g3d_camera(pos, target, 60.0);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_cube", "int g3d_cube(float size)", "Создает процедурный меш куба.", "g3d_cube(1.0);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_box", "int g3d_box(float sx, float sy, float sz)", "Создает процедурный меш параллелепипеда (box) заданных размеров.", "g3d_box(1.0, 1.0, 1.0);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_sphere", "int g3d_sphere(float radius, int slices = 16, int stacks = 16)", "Создает процедурный меш сферы.", "g3d_sphere(1.0, 16, 16);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_plane", "int g3d_plane(float size_x, float size_z)", "Создает процедурный меш плоскости.", "g3d_plane(10.0, 10.0);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_load_obj", "int g3d_load_obj(string path)", "Загружает 3D меш из файла формата Wavefront OBJ.", "g3d_load_obj(\"model.obj\");", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_set_color", "void g3d_set_color(int mesh_id, int color_hex)", "Задает цвет граней и вершин меша.", "g3d_set_color(id, 0x00FF00);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_transform", "void g3d_transform(int mesh_id, take m4_mat)", "Устанавливает матрицу трансформации меша в пространстве.", "g3d_transform(id, mat);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_draw", "void g3d_draw(int mesh_id)", "Растеризует 3D меш с z-буферизацией и освещением.", "g3d_draw(id);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_dir_light", "void g3d_dir_light(take dir, int color_hex)", "Устанавливает направленный глобальный источник света (солнце).", "g3d_dir_light(dir, 0xFFFFFF);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_point_light", "void g3d_point_light(take pos, int color_hex, float radius)", "Добавляет локальный точечный источник света.", "g3d_point_light(pos, 0xFFFFFF, 10.0);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_fog", "void g3d_fog(int color_hex, float near, float far)", "Настраивает параметры объемного тумана сцены.", "g3d_fog(0x222222, 5.0, 30.0);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_skybox", "void g3d_skybox(int top_color, int bottom_color)", "Задает цвета градиентного фона скайбокса.", "g3d_skybox(0x101030, 0x050510);", "!gfx3d!"),
            new BuiltinFunctionInfo("g3d_bind_framebuffer", "void g3d_bind_framebuffer(take buffer_ptr, int w, int h, int pitch)", "Привязывает внешний bare-metal буфер для 3D рендеринга.", "g3d_bind_framebuffer(fb, w, h, pitch);", "!gfx3d!"),

            // XPBD Physics Engine (!physics!)
            new BuiltinFunctionInfo("phys_world", "take phys_world(float gx, float gy, float gz)", "Создает физический мир XPBD с вектором гравитации.", "phys_world(0.0, -9.81, 0.0);", "!physics!"),
            new BuiltinFunctionInfo("phys_step", "void phys_step(take world, float dt, int sub_steps = 4)", "Выполняет шаг симуляции физики с субстеппингом.", "phys_step(world, dt, 4);", "!physics!"),
            new BuiltinFunctionInfo("phys_box", "take phys_box(take world, float px, float py, float pz, float sx, float sy, float sz, float mass)", "Создает твердое тело параллелепипеда (box).", "phys_box(world, px, py, pz, sx, sy, sz, mass);", "!physics!"),
            new BuiltinFunctionInfo("phys_sphere", "take phys_sphere(take world, float px, float py, float pz, float radius, float mass)", "Создает твердое тело сферы.", "phys_sphere(world, px, py, pz, radius, mass);", "!physics!"),
            new BuiltinFunctionInfo("phys_cloth", "take phys_cloth(take world, float px, float py, float pz, int w, int h, float spacing, float mass)", "Создает симуляцию сетки ткани (cloth XPBD).", "phys_cloth(world, px, py, pz, 10, 10, 0.5, 1.0);", "!physics!"),
            new BuiltinFunctionInfo("phys_link", "void phys_link(take body_a, take body_b, float dist, float compliance = 0.0)", "Создает дистанционную связь (Distance Constraint) между телами.", "phys_link(a, b, dist, 0.0);", "!physics!"),
            new BuiltinFunctionInfo("phys_impulse", "void phys_impulse(take body, float ix, float iy, float iz)", "Прикладывает мгновенный линейный импульс к физическому телу.", "phys_impulse(body, ix, iy, iz);", "!physics!"),
            new BuiltinFunctionInfo("phys_raycast", "take phys_raycast(take world, take origin, take dir, float max_dist)", "Бросает луч в физическом мире для проверки коллизий.", "phys_raycast(world, origin, dir, 100.0);", "!physics!"),
            new BuiltinFunctionInfo("phys_get_pos", "take phys_get_pos(take body)", "Возвращает текущую трехмерную позицию тела (Vector3).", "phys_get_pos(body);", "!physics!"),
            new BuiltinFunctionInfo("phys_get_rot", "take phys_get_rot(take body)", "Возвращает ориентацию тела в пространстве.", "phys_get_rot(body);", "!physics!"),
            new BuiltinFunctionInfo("phys_set_pos", "void phys_set_pos(take body, float x, float y, float z)", "Устанавливает новую позицию физического тела и сбрасывает скорость.", "phys_set_pos(body, x, y, z);", "!physics!"),
            new BuiltinFunctionInfo("phys_set_vel", "void phys_set_vel(take body, float vx, float vy, float vz)", "Задает линейную скорость физическому телу.", "phys_set_vel(body, vx, vy, vz);", "!physics!")
        };

        private static Dictionary<string, BuiltinFunctionInfo> _builtinMap;
        public static Dictionary<string, BuiltinFunctionInfo> BuiltinMap
        {
            get
            {
                if (_builtinMap == null)
                {
                    _builtinMap = new Dictionary<string, BuiltinFunctionInfo>(StringComparer.Ordinal);
                    foreach (var fn in BuiltinFunctions)
                    {
                        if (!_builtinMap.ContainsKey(fn.Name))
                            _builtinMap[fn.Name] = fn;
                    }
                }
                return _builtinMap;
            }
        }
    }
}

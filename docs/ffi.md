# Спецификация универсального модуля !ffi! (docs/ffi.md)

Модуль `!ffi!` предоставляет универсальный механизм Foreign Function Interface (FFI) для прямого взаимодействия с нативными динамическими библиотеками (DLL в Windows, .so в Linux) по соглашению вызовов C ABI без необходимости модификации или перекомпиляции компилятора GrassGreen.

### Подключение модуля
```grass
!GrassGreen!
!ffi!
```

### Функции API

- `ffi_open(lib_name)` — Загружает библиотеку в адресное пространство процесса (`LoadLibraryA` в Windows, `dlopen` в Linux). Возвращает 64-битный дескриптор библиотеки (handle) или 0 при ошибке.
- `ffi_sym(lib_handle, func_name)` — Возвращает указатель на экспортируемую C-функцию (`GetProcAddress` в Windows, `dlsym` в Linux) по имени `func_name`.
- `ffi_call(func_ptr, args_array)` — Вызывает нативную функцию по указателю `func_ptr` с передачей аргументов из массива `args_array`.
  - Поддерживает от 0 до 10 аргументов (числа, указатели, буферы, строки C-string).
  - Строки автоматически передаются как `const char*` с завершающим нулем.
  - Массивы чисел преобразуются в непрерывный байтовый буфер `uint8_t*`.
  - Числа передаются как 64-битные значения (`uintptr_t`).
  - Возвращает числовой результат выполнения функции (регистр RAX / EAX).
- `ffi_close(lib_handle)` — Выгружает ранее загруженную библиотеку (`FreeLibrary` / `dlclose`).

### Пример вызова системных функций Windows API

```grass
!GrassGreen!
!ffi!

int main() {
    take kernel32 = ffi_open("kernel32.dll");
    if (kernel32 != 0) {
        take get_tick = ffi_sym(kernel32, "GetTickCount");
        if (get_tick != 0) {
            take uptime = ffi_call(get_tick, []);
            gout << "Uptime: " << uptime << " ms" << endl;
        }
        ffi_close(kernel32);
    }

    take user32 = ffi_open("user32.dll");
    if (user32 != 0) {
        take msgbox = ffi_sym(user32, "MessageBoxA");
        if (msgbox != 0) {
            ffi_call(msgbox, [0, "Привет из GrassGreen FFI!", "Системное окно", 0]);
        }
        ffi_close(user32);
    }

    return 0;
}
```

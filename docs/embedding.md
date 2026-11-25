# Встраивание GrassVM в C и C++ (libgrassvm C-API)

Библиотека `libgrassvm` (`grassvm.dll` / `grassvm.lib` на Windows, `libgrassvm.so` на Linux) позволяет встраивать виртуальную машину GrassGreen в любые нативные C/C++ приложения, игровые движки и системные утилиты.

---

## Заголовочный интерфейс (include/grassvm.h)

- `grg_vm_create()` — создание независимого изолированного экземпляра виртуальной машины.
- `grg_vm_destroy(vm)` — освобождение ресурсов виртуальной машины.
- `grg_vm_load_file(vm, path)` — компиляция и выполнение `.grass` файла.
- `grg_vm_eval(vm, code)` — выполнение строкового фрагмента кода на лету.
- `grg_vm_set_int(vm, name, val)` — установка 64-битной целочисленной переменной в глобальную область видимости.
- `grg_vm_set_str(vm, name, val)` — передача строки в скриптовое окружение.
- `grg_vm_call(vm, func_name)` — прямой вызов функции GrassGreen по имени.
- `grg_vm_get_error(vm)` — получение текста последней ошибки выполнения.

---

## Пример встраивания на C++

```cpp
#include <iostream>
#include "grassvm.h"

int main() {
    GrgVM* vm = grg_vm_create();
    if (!vm) return 1;

    grg_vm_set_int(vm, "external_factor", 42);
    grg_vm_set_str(vm, "app_version", "v4.3.0");

    const char* script =
        "!GrassGreen!\n"
        "int main() {\n"
        "    gout << \"Factor: \" << external_factor << endl;\n"
        "    return 0;\n"
        "}\n";

    if (grg_vm_eval(vm, script) != 0) {
        std::cerr << "Script error: " << grg_vm_get_error(vm) << std::endl;
    }

    grg_vm_destroy(vm);
    return 0;
}
```

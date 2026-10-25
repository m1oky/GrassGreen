<div align="center">

  <img src="GrassGreen/logo.svg" alt="GrassGreen Logo" width="120" height="120" />

  # GrassGreen

  **Экспериментальный компилируемый язык программирования со стек-машиной (Stack VM) и встроенным PE-пакером.**

  [![C++20](https://img.shields.io/badge/Language-C%2B%2B20-00599C?style=flat-square&logo=c%2B%2B)](https://isocpp.org/)
  [![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6?style=flat-square&logo=windows)](https://microsoft.com)
  [![License: MIT](https://img.shields.io/badge/License-MIT-green?style=flat-square)](LICENSE)

</div>

---

**GrassGreen** — пет-проект независимого языка программирования, написанный с нуля на C++20 без внешних библиотек и зависимостей. 

Компилятор транслирует исходный код в компактный кастомный байт-код, который либо исполняется встроенной стек-машиной (VM), либо линкуется с рантаймом в самостоятельный бинарный `.exe` файл под Windows (PE-формат).

---

## 🛠 Технические особенности

* **Стек-машина (Stack VM):** выполнение через набор собственных опкодов (`OP_PUSH`, `OP_ADD`, `OP_JUMP_IF_FALSE`, `OP_CALL_BUILTIN` и др.).
* **Автономные бинарники:** встроенный PE-пакер сшивает байт-код с готовым рантаймом. На целевой системе не требуются установленные интерпретаторы, Python или MSVC Redistributable.
* **Монолитное ядро:** системные модули (математика, прямое выделение памяти в куче, WinAPI диалоги) встроены прямо в диспетчер инструкций ВМ без накладных расходов на внешний FFI.
* **Синтаксис:** C-подобная структура программы с точкой входа `int main()`, процедурами `take`, циклом `for` и строгой блочной областью видимости `{}`.

---

## 💻 Примеры кода

### 1. Базовый синтаксис и вычисления

```cpp
!GrassGreen!

int main() {
    take counter = 0;
    
    // Цикл от 0 до 10
    for (take i = 0; i < 10; i++) {
        counter = counter + i;
    }
    
    cout << "Результат суммы: " << counter << endl;
    return 0;
}

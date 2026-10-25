<div align="center">

  <img src="logo.svg" alt="GrassGreen Logo" width="130" height="130" />

  # 🌿 GrassGreen Programming Language
  ### *Высокопроизводительный байт-код язык с синтаксисом C++ и встроенным компилятором в автономные .exe*

  [![Version](https://img.shields.io/badge/GrassGreen-v3.0.0-00ffcc?style=for-the-badge&logo=visual-studio-code&logoColor=white)](https://github.com)
  [![Standard](https://img.shields.io/badge/C%2B%2B-20-blue?style=for-the-badge&logo=c%2B%2B)](https://isocpp.org/)
  [![Platform](https://img.shields.io/badge/Platform-Windows-0078D6?style=for-the-badge&logo=windows)](https://microsoft.com)
  [![Arch](https://img.shields.io/badge/Arch-x64-brightgreen?style=for-the-badge)](https://github.com)

  <p align="center">
    <b>GrassGreen</b> — это независимый компилируемый язык программирования со стек-машиной (Stack VM), упаковщиком PE .exe файлов, встроенным графическим интерфейсом Fluent UI, квантовым эмулятором и поддержкой синтаксиса в стиле C++.
  </p>

</div>

---

## ⚡ Главные возможности

- 🚀 **Ультра-компактный байт-код:** бинарный размер скрипта занимает от 40 до 300 байт.
- 📦 **Встроенный PE-Packer:** упаковка любого `.grass` файла в самостоятельный `.exe`, работающий на любом ПК с Windows без установки интерпертатора.
- 💻 **Эргономика C++:** поддержка классической точки входа `int main() {}`, процедур `take`, циклов `for`, потоков `cout` / `cin` и строгого разбора через `{}`.
- 🎨 **Нативная экосистема VS Code:** автоматическая привязка фирменного векторного логотипа `.svg`, синтаксической подсветки, автодополнения и компиляции по `Ctrl + Shift + B`.
- 🧮 **Модульность:** библиотеки для квантовых расчётов (`!CoreMath!`), плавной графики (`!FastMath!`), управления сырой ОЗУ (`!memory!`) и базы данных (`!database!`).

---

## 📂 Структура проекта

```text
GrassGreen/
├── bin/                 # Скомпилированный компилятор (grassc.exe) и утилиты
├── src/                 # Исходный код C++ (ядро компилятора, ВМ, модули)
│   ├── grassc.cpp       # Главный компилятор, парсер и виртуальная машина
│   ├── FastMath.cpp     # Быстрая математика и интерполяция
│   ├── CoreMath.cpp     # Квантовый эмулятор и крипто-функции
│   ├── GrassMemory.cpp  # Менеджер оперативной памяти
│   ├── GrassRandom.cpp  # Генератор псевдослучайных последовательностей
│   └── GrassWeb.cpp     # Веб-сервер и рендерер страниц
├── docs/                # Словари и документация команд
├── examples/            # Примеры рабочих программ (.grass)
├── logo.svg             # Официальная векторная иконка языка
└── README.md            # Документация проекта

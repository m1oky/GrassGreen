# GrassGreen (G&G) — Расширение для Visual Studio Code

Официальное расширение Visual Studio Code для суверенного системного языка программирования GrassGreen 4.2 (G&G).

---

## Возможности

- **TextMate подсветка синтаксиса:**
  - Препроцессорные директивы модулей (`!GrassGreen!`, `!hardware!`, `!asm!`, `!keyboard!`, `!interface!`, `!audio!`, `!FastMath!`, `!thread!`, `!mmio!`, `!sys!`, ...)
  - Прямой опрос оборудования (`!hardware!`): `hw_ram_total()`, `hw_cpu_vendor()`, `hw_board_vendor()`, `proc_mem`, `hw_pci_count`, `hw_reboot()`
  - Встроенный ассемблер ядра (`!asm!`): блоки `asm { ... }`, регистры `CR0-CR4`, прерывания `cli`, `sti`, `hlt`, порты `in`/`out`
  - Контроллер клавиатуры PS/2 (`!keyboard!`): скан-коды, модификаторы, построчный ввод строк с Backspace
  - Векторный GUI VD+: шапки `header`, скругления `border-radius-app`, вкладки `tab`, `switch`, часы `clock`, слои `z-index`, частицы `particles`
  - Потоковый ввод-вывод: `gout`, `gin`, `endl`, операторы `<<` и `>>`
  - Кодировки: директивы `use utf - 65001;` / `use utf - 8;`
  - Ключевые слова: `take`, `set`, `volatile`, `struct`, `array`, `thread`, `async`
  - C-ABI типы данных: `int`, `void`, `string`, `float`, `double`, `u8`, `u16`, `u32`, `u64`, `bool`
- **Автодополнение и подсказки (IntelliSense):**
  - Подстановка названий модулей при вводе символа `!`
  - Быстрый ввод потоков `gout` и `gin`
  - Полный каталог системных функций ядра, железа, аудио и GUI с описанием параметров на русском языке
  - 32 готовых сниппета: `main`, `hardware`, `kernel_multiboot`, `ui_app`, `audio_tone`, `fn`, `thread`, `async`, `mmio_read` и др.
- **Сборка и запуск:**
  - `Ctrl + Shift + B` — компиляция файла с оптимизацией `-O3` в нативный исполняемый файл
  - `F5` — мгновенная компиляция и запуск программы во встроенном терминале
  - Команда сборки Multiboot bare-metal OS ядра (`.bin`)
- **Иконки файлов:**
  - Векторные иконки файлов с расширением `.grass` и `.gdb`

---

## Быстрый старт

1. Создайте файл `kernel.grass`:
   ```cpp
   !GrassGreen!
   !hardware!

   int main() {
       take user;
       take cmd;
       use utf - 65001;

       gout << "GrassGreen OS v4.2 Kernel [Multiboot Online]" << endl;
       gout << "CPU: " << hw_cpu_vendor() << " " << hw_cpu_brand() << endl;
       gout << "RAM: " << hw_ram_total() << " MB total" << endl;
       return 0;
   }
   ```
2. Скомпилируйте ядро:
   ```bash
   grg --kernel --multiboot kernel.grass -o rqw.bin
   ```
3. Запустите в QEMU:
   ```bash
   qemu-system-x86_64 -kernel rqw.bin -serial stdio
   ```

---

## Ссылки

- Официальный сайт: https://grassgreen.info
- Исходный код: https://github.com/m1oky/GrassGreen

// =============================================================================
//  GrassGreen 4.2.0 — Модуль низкоуровневой сборки ядра и загрузочных образов
//  Поддержка флагов --multiboot, --kernel (.bin), --sys (.sys), --iso (.iso)
//  100% C++20 без сторонних утилит (xorriso, grub-mkrescue, nasm не требуются)
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace GrassBoot {

    // ── 1. Сборка бинарного файла ядра (--kernel / --multiboot) ──
    // Создает плоский машинный файл ядра (.bin) с заголовками Multiboot 1/2
    bool build_kernel_bin(const std::string& output_bin,
                          const std::vector<uint8_t>& bytecode,
                          const std::vector<uint8_t>& asm_machine_code,
                          bool include_multiboot,
                          bool lang_ru);

    // ── 2. Сборка системного драйвера ядра (--sys) ──
    // Создает бинарный модуль драйвера (.sys) с сигнатурой GSYS и таблицей диспетчеризации
    bool build_driver_sys(const std::string& output_sys,
                          const std::vector<uint8_t>& bytecode,
                          const std::string& driver_name,
                          bool lang_ru);

    // ── 3. Генерация готового загрузочного ISO-образа (--iso) ──
    // Создает загрузочный образ ISO 9660 (El Torito No-Emulation)
    // Образ сразу готов к запуску в QEMU, VirtualBox или записи на USB
    bool build_bootable_iso(const std::string& output_iso,
                            const std::vector<uint8_t>& bytecode,
                            const std::vector<uint8_t>& asm_machine_code,
                            const std::string& volume_id,
                            bool lang_ru);

} // namespace GrassBoot

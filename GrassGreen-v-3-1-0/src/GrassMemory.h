#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace GrassMemory {
    // 1. Текущее потребление ОЗУ процессом (в МБ)
    double usit();

    // 2. Дефрагментация и сжатие кучи
    void separ();

    // 3. Автоматический режим распределения памяти
    void set_auto(bool enable);
    bool is_auto();

    // 4. Принудительный сброс неиспользуемой памяти в ОС
    void clean();

    // 5. Данные об ОЗУ всего компьютера (свободно / всего в ГБ)
    std::string get_system_free();

    // 6. Выделение сырого байтового буфера (возвращает id блока)
    int alloc_block(size_t bytes);

    // 7. Освобождение блока памяти по id
    bool free_block(int id);

    // 8. Установка жесткого лимита памяти в МБ (0 = без лимита)
    void set_limit(double mb);
    bool is_limit_exceeded();

    // 9. Отладочный дамп состояния памяти
    void dump(size_t stack_size, const std::vector<std::string>& stack_preview,
              const std::vector<std::pair<std::string, std::string>>& vars_preview);
}
#ifndef GRASS_SYS_H
#define GRASS_SYS_H

#include <string>

namespace GrassSys {
    // Буфер обмена Windows (Clipboard)
    bool clip_set(const std::string& text);
    std::string clip_get();

    // Параметры экрана
    int screen_w();
    int screen_h();

    // Идентификация системы и пользователя
    std::string pc_name();
    std::string user_name();
    std::string get_env(const std::string& key);

    // Мониторинг аппаратного обеспечения
    int cpu_cores();
    double ram_total_mb();
    double ram_free_mb();
    double uptime_sec();
}

#endif // GRASS_SYS_H
#ifndef GRASS_TIME_H
#define GRASS_TIME_H

#include <string>
#include <cstdint>

namespace GrassTime {
    // Базовые команды из docs/time.txt
    void start_time();
    double stop_time();
    std::string conclus_time();

    // Расширенные утилиты
    double now_ms();
    double timestamp();
    std::string get_date();
    std::string get_time_str();
    void sleep_ms(int ms);
}

#endif // GRASS_TIME_H
#pragma once
#include <string>

namespace GrassRandom {
    // Генерация случайного числа (по умолчанию от 0 до 100 или min..max)
    double get_random(double min_val = 0.0, double max_val = 100.0);

    // Генерация случайных символов/текста (язык "rus" / "en", опционально длина)
    std::string get_random_txt(const std::string& lang, int length = 1);

    // Остановка (заморозка) текущего состояния генератора
    void stop_random();

    // Возобновление работы генератора
    void play_random();

    // Проверка состояния
    bool is_stopped();
}
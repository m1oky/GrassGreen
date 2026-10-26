#include "GrassRandom.h"
#include <random>
#include <vector>
#include <chrono>
#include <algorithm>

namespace GrassRandom {
    static std::mt19937_64 g_rng((uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    static bool g_is_stopped = false;
    static double g_last_num = 0.0;
    static std::string g_last_txt = "";

    // Алфавиты
    static const std::vector<std::string> RUS_LETTERS = {
        "а", "б", "в", "г", "д", "е", "ё", "ж", "з", "и", "й",
        "к", "л", "м", "н", "о", "п", "р", "с", "т", "у", "ф",
        "х", "ц", "ч", "ш", "щ", "ъ", "ы", "ь", "э", "ю", "я"
    };
    static const std::string ENG_LETTERS = "abcdefghijklmnopqrstuvwxyz";

    double get_random(double min_val, double max_val) {
        if (g_is_stopped) return g_last_num;

        if (min_val > max_val) std::swap(min_val, max_val);
        std::uniform_real_distribution<double> dist(min_val, max_val);
        g_last_num = std::round(dist(g_rng) * 1000.0) / 1000.0; // Округление до 3 знаков
        return g_last_num;
    }

    std::string get_random_txt(const std::string& lang, int length) {
        if (g_is_stopped) return g_last_txt;
        if (length <= 0) length = 1;

        std::string result = "";
        std::string l = lang;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);

        if (l == "rus" || l == "ru") {
            std::uniform_int_distribution<size_t> dist(0, RUS_LETTERS.size() - 1);
            for (int i = 0; i < length; ++i) {
                result += RUS_LETTERS[dist(g_rng)];
            }
        } else {
            std::uniform_int_distribution<size_t> dist(0, ENG_LETTERS.size() - 1);
            for (int i = 0; i < length; ++i) {
                result += ENG_LETTERS[dist(g_rng)];
            }
        }

        g_last_txt = result;
        return g_last_txt;
    }

    void stop_random() {
        g_is_stopped = true;
    }

    void play_random() {
        g_is_stopped = false;
    }

    bool is_stopped() {
        return g_is_stopped;
    }
}
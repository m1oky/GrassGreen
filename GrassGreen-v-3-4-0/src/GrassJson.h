#ifndef GRASS_JSON_H
#define GRASS_JSON_H

#include <string>

namespace GrassJson {
    // Чтение и валидация
    std::string get(const std::string& json_str, const std::string& path);
    std::string set(const std::string& json_str, const std::string& key, const std::string& val);
    bool is_valid(const std::string& json_str);
    std::string minify(const std::string& json_str);

    // Сборщик объектов (Builder)
    void builder_clear();
    void builder_add_str(const std::string& key, const std::string& val);
    void builder_add_num(const std::string& key, double val);
    std::string builder_stringify();
}

#endif // GRASS_JSON_H
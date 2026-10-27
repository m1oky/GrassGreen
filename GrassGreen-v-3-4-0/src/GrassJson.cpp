#include "GrassJson.h"
#include <vector>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace GrassJson {

    static std::vector<std::pair<std::string, std::string>> builder_entries;

    static void skip_ws(const std::string& s, size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
            i++;
        }
    }

    static std::string parse_str(const std::string& s, size_t& i) {
        if (i >= s.size() || s[i] != '"') return "";
        i++; // Пропускаем открывающую кавычку
        std::string res;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                i++;
                if (s[i] == 'n') res += '\n';
                else if (s[i] == 't') res += '\t';
                else if (s[i] == '"') res += '"';
                else if (s[i] == '\\') res += '\\';
                else res += s[i];
            } else {
                res += s[i];
            }
            i++;
        }
        if (i < s.size() && s[i] == '"') i++;
        return res;
    }

    // Рекурсивный поиск значения по пути ("user.name" или "id")
    std::string get(const std::string& json_str, const std::string& path) {
        std::vector<std::string> keys;
        std::stringstream ss(path);
        std::string item;
        while (getline(ss, item, '.')) {
            if (!item.empty()) keys.push_back(item);
        }
        if (keys.empty()) return "";

        size_t pos = 0;
        std::string current_val = "";

        for (size_t k = 0; k < keys.size(); ++k) {
            std::string target_key = "\"" + keys[k] + "\"";
            size_t key_pos = json_str.find(target_key, pos);
            if (key_pos == std::string::npos) return "";

            pos = key_pos + target_key.size();
            skip_ws(json_str, pos);

            if (pos >= json_str.size() || json_str[pos] != ':') return "";
            pos++; // Пропускаем ':'
            skip_ws(json_str, pos);

            if (pos >= json_str.size()) return "";

            // Если это строка
            if (json_str[pos] == '"') {
                current_val = parse_str(json_str, pos);
            } 
            // Если это вложенный объект
            else if (json_str[pos] == '{') {
                size_t start = pos;
                int depth = 0;
                while (pos < json_str.size()) {
                    if (json_str[pos] == '{') depth++;
                    else if (json_str[pos] == '}') {
                        depth--;
                        if (depth == 0) { pos++; break; }
                    }
                    pos++;
                }
                current_val = json_str.substr(start, pos - start);
            }
            // Число, bool или null
            else {
                size_t start = pos;
                while (pos < json_str.size() && json_str[pos] != ',' && json_str[pos] != '}' && json_str[pos] != ']') {
                    pos++;
                }
                current_val = json_str.substr(start, pos - start);
                size_t last = current_val.find_last_not_of(" \t\r\n");
                if (last != std::string::npos) current_val = current_val.substr(0, last + 1);
            }
        }
        return current_val;
    }

    std::string set(const std::string& json_str, const std::string& key, const std::string& val) {
        std::string trimmed = minify(json_str);
        if (trimmed.empty() || trimmed == "{}") {
            return "{\"" + key + "\":\"" + val + "\"}";
        }

        std::string search_key = "\"" + key + "\":";
        size_t p = trimmed.find(search_key);
        if (p != std::string::npos) {
            // Ключ уже существует — обновляем
            size_t val_start = p + search_key.size();
            size_t val_end = val_start;
            if (trimmed[val_start] == '"') {
                val_end = trimmed.find('"', val_start + 1);
                if (val_end != std::string::npos) val_end++;
            } else {
                while (val_end < trimmed.size() && trimmed[val_end] != ',' && trimmed[val_end] != '}') {
                    val_end++;
                }
            }
            return trimmed.substr(0, val_start) + "\"" + val + "\"" + trimmed.substr(val_end);
        }

        // Добавляем новый ключ в конец объекта
        if (trimmed.back() == '}') {
            trimmed.pop_back();
            if (trimmed.size() > 1 && trimmed.back() != '{') trimmed += ",";
            trimmed += "\"" + key + "\":\"" + val + "\"}";
            return trimmed;
        }
        return json_str;
    }

    bool is_valid(const std::string& json_str) {
        int braces = 0, brackets = 0;
        bool in_str = false;
        for (size_t i = 0; i < json_str.size(); ++i) {
            char c = json_str[i];
            if (c == '"' && (i == 0 || json_str[i - 1] != '\\')) {
                in_str = !in_str;
            }
            if (!in_str) {
                if (c == '{') braces++;
                else if (c == '}') braces--;
                else if (c == '[') brackets++;
                else if (c == ']') brackets--;
                if (braces < 0 || brackets < 0) return false;
            }
        }
        return (braces == 0 && brackets == 0 && !in_str && !json_str.empty());
    }

    std::string minify(const std::string& json_str) {
        std::string res;
        bool in_str = false;
        for (size_t i = 0; i < json_str.size(); ++i) {
            char c = json_str[i];
            if (c == '"' && (i == 0 || json_str[i - 1] != '\\')) in_str = !in_str;
            if (in_str || (!isspace((unsigned char)c))) {
                res += c;
            }
        }
        return res;
    }

    void builder_clear() {
        builder_entries.clear();
    }

    void builder_add_str(const std::string& key, const std::string& val) {
        builder_entries.push_back({key, "\"" + val + "\""});
    }

    void builder_add_num(const std::string& key, double val) {
        std::ostringstream ss;
        if (val == (long long)val) ss << (long long)val;
        else ss << val;
        builder_entries.push_back({key, ss.str()});
    }

    std::string builder_stringify() {
        std::string res = "{";
        for (size_t i = 0; i < builder_entries.size(); ++i) {
            res += "\"" + builder_entries[i].first + "\":" + builder_entries[i].second;
            if (i + 1 < builder_entries.size()) res += ",";
        }
        res += "}";
        return res;
    }

} // namespace GrassJson
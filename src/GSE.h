#ifndef GSE_H
#define GSE_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>

// Глобальный кэш переменных для интеграции с ВМ и GUI
extern std::map<std::string, std::wstring> variables_cache;

// Запись в базе данных Grass Storage Engine (GSE)
struct GSERecord {
    std::string key;
    std::string value;
    std::string table = "default";
    bool is_encrypted = false;
};

namespace GrassDB {
    void init_default_schema();

    bool open(const std::string& filename);
    bool save(const std::string& filename = "");
    bool backup(const std::string& backup_path);
    bool export_json(const std::string& json_path);

    void use_table(const std::string& table_name);
    std::string current_table();

    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key, const std::string& default_val = "");
    bool has(const std::string& key);
    bool remove(const std::string& key);
    void clear();
    size_t count();

    long long add_int(const std::string& key, long long delta);

    std::string hash_string(const std::string& input);
    void encrypt_key(const std::string& key, const std::string& secret_salt = "GrassGreenKey");
    std::string decrypt_key(const std::string& key, const std::string& secret_salt = "GrassGreenKey");

    std::vector<std::pair<std::string, std::string>> find_by_prefix(const std::string& prefix);

    std::string exec_command(const std::string& raw_line);

    const std::map<std::string, GSERecord>& get_all_records();
}

bool GrassStorageEngine_Load(const std::string& filename);

#endif // GSE_H
#include <windows.h>
#include "GSE.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdint>

using namespace std;

// Глобальный кэш переменных
map<string, wstring> variables_cache;


wstring utf8_to_wstring_gse(const string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (len <= 0) return L"";
    wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
    return wstr;
}

namespace GrassDB {

    static map<string, GSERecord> db_store;
    static string active_filepath = "storage.gdb";
    static string active_table = "default";
    static bool auto_save_enabled = false;

    static string trim_str(const string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static string strip_quotes(string s) {
        s = trim_str(s);
        if (s.size() >= 4 && s.substr(0, 2) == "!!" && s.substr(s.size() - 2) == "!!") {
            return s.substr(2, s.size() - 4);
        }
        if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
            return s.substr(1, s.size() - 2);
        }
        return s;
    }

    static string normalize_key(string key) {
        key = trim_str(key);
        if (key.size() >= 2 && key.substr(key.size() - 2) == "()") {
            key = key.substr(0, key.size() - 2);
        }
        return key;
    }

    static string get_current_timestamp() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        ostringstream oss;
        oss << setfill('0') << setw(4) << st.wYear << "-"
            << setw(2) << st.wMonth << "-"
            << setw(2) << st.wDay << " "
            << setw(2) << st.wHour << ":"
            << setw(2) << st.wMinute << ":"
            << setw(2) << st.wSecond;
        return oss.str();
    }

    static void sync_to_legacy_cache(const string& clean_key, const string& val) {
        wstring wval = utf8_to_wstring_gse(val);
        variables_cache[clean_key] = wval;
        variables_cache[clean_key + "()"] = wval;
    }

    // 64-битный FNV-1a хэш для токенов авторизации и проверки целостности
    string hash_string(const string& input) {
        uint64_t h_val = 14695981039346656037ULL;
        for (unsigned char c : input) {
            h_val ^= c;
            h_val *= 1099511628211ULL;
        }
        ostringstream oss;
        oss << "gg_" << hex << setw(16) << setfill('0') << h_val;
        return oss.str();
    }

    // Симметричное шифрование в HEX для защиты токенов и паролей в файле .gdb
    static string xor_hex_cipher(const string& text, const string& salt, bool encode) {
        if (salt.empty()) return text;
        if (encode) {
            ostringstream oss;
            for (size_t i = 0; i < text.size(); ++i) {
                unsigned char b = static_cast<unsigned char>(text[i]) ^ static_cast<unsigned char>(salt[i % salt.size()]);
                oss << hex << setw(2) << setfill('0') << (int)b;
            }
            return oss.str();
        } else {
            string out;
            for (size_t i = 0; i + 1 < text.size(); i += 2) {
                string byte_str = text.substr(i, 2);
                unsigned char b = (unsigned char)strtol(byte_str.c_str(), NULL, 16);
                out.push_back((char)(b ^ static_cast<unsigned char>(salt[(i / 2) % salt.size()])));
            }
            return out;
        }
    }

    // Регистрация всех стандартных системных ключей из gdb.txt
    void init_default_schema() {
        struct DefaultPair { const char* k; const char* v; const char* tbl; };
        static const DefaultPair defaults[] = {
            {"user", "Player", "auth"},
            {"last_log", get_current_timestamp().c_str(), "auth"},
            {"remember_me", "1", "auth"},
            {"auth_token_hash", hash_string("default_session").c_str(), "auth"},

            {"net_tunnel_protocol", "VLESS-Reality", "network"},
            {"net_baypass_state", "0", "network"},
            {"net_bypass_state", "0", "network"},
            {"net_gateway_route", "127.0.0.1:10808", "network"},
            {"net_bypass_config", "vless_reality.json", "network"},
            {"net_ping_ms", "24", "network"},

            {"env_target_runtime", "fabric-1.20.1", "runtime"},
            {"env_exec_flags", "-XX:+UseG1GC -XX:+ParallelRefProcEnabled", "runtime"},
            {"env_allocated_ram", "4096", "runtime"},
            {"env_java_path", "C:/Program Files/Java/bin/javaw.exe", "runtime"},

            {"sys_fps_stabilizer", "1", "system"},
            {"sys_gpu_force_integrated", "0", "system"}
        };

        for (const auto& item : defaults) {
            if (!db_store.count(item.k)) {
                GSERecord rec;
                rec.key = item.k;
                rec.value = item.v;
                rec.table = item.tbl;
                db_store[item.k] = rec;
                sync_to_legacy_cache(item.k, item.v);
            }
        }
    }

    void use_table(const string& table_name) {
        active_table = table_name.empty() ? "default" : strip_quotes(table_name);
    }

    string current_table() {
        return active_table;
    }

    void set(const string& raw_key, const string& raw_value) {
        string k = normalize_key(raw_key);
        if (k.empty()) return;

        string v = strip_quotes(raw_value);

        // Автоматическая подстановка времени и хэша для ключей из gdb.txt
        if (k == "last_log" && (v == "auto" || v == "now")) {
            v = get_current_timestamp();
        } else if (k == "auth_token_hash" && v == "auto") {
            string seed = get("user", "user") + "_" + get_current_timestamp();
            v = hash_string(seed);
        }

        // Опечатка-синоним из gdb.txt: синхронизируем оба варианта написания
        if (k == "net_baypass_state") {
            db_store["net_bypass_state"] = { "net_bypass_state", v, active_table, false };
            sync_to_legacy_cache("net_bypass_state", v);
        } else if (k == "net_bypass_state") {
            db_store["net_baypass_state"] = { "net_baypass_state", v, active_table, false };
            sync_to_legacy_cache("net_baypass_state", v);
        }

        GSERecord rec;
        rec.key = k;
        rec.value = v;
        rec.table = active_table;
        rec.is_encrypted = false;
        db_store[k] = rec;

        sync_to_legacy_cache(k, v);
        if (auto_save_enabled) save(active_filepath);
    }

    string get(const string& raw_key, const string& default_val) {
        string k = normalize_key(raw_key);
        auto it = db_store.find(k);
        if (it != db_store.end()) {
            return it->second.value;
        }
        return default_val;
    }

    bool has(const string& raw_key) {
        return db_store.count(normalize_key(raw_key)) > 0;
    }

    bool remove(const string& raw_key) {
        string k = normalize_key(raw_key);
        variables_cache.erase(k);
        variables_cache.erase(k + "()");
        bool erased = db_store.erase(k) > 0;
        if (erased && auto_save_enabled) save(active_filepath);
        return erased;
    }

    void clear() {
        db_store.clear();
        variables_cache.clear();
    }

    size_t count() {
        return db_store.size();
    }

    long long add_int(const string& raw_key, long long delta) {
        string cur = get(raw_key, "0");
        long long val = 0;
        try { val = stoll(cur); } catch (...) { val = 0; }
        val += delta;
        set(raw_key, to_string(val));
        return val;
    }

    void encrypt_key(const string& raw_key, const string& secret_salt) {
        string k = normalize_key(raw_key);
        if (db_store.count(k) && !db_store[k].is_encrypted) {
            db_store[k].value = xor_hex_cipher(db_store[k].value, secret_salt, true);
            db_store[k].is_encrypted = true;
            sync_to_legacy_cache(k, db_store[k].value);
        }
    }

    string decrypt_key(const string& raw_key, const string& secret_salt) {
        string k = normalize_key(raw_key);
        if (db_store.count(k)) {
            if (db_store[k].is_encrypted) {
                return xor_hex_cipher(db_store[k].value, secret_salt, false);
            }
            return db_store[k].value;
        }
        return "";
    }

    vector<pair<string, string>> find_by_prefix(const string& prefix) {
        vector<pair<string, string>> result;
        string clean_pref = strip_quotes(prefix);
        for (const auto& kv : db_store) {
            if (kv.first.find(clean_pref) == 0) {
                result.push_back({ kv.first, kv.second.value });
            }
        }
        return result;
    }

    bool open(const string& filename) {
        init_default_schema();
        active_filepath = strip_quotes(filename);

        ifstream file(active_filepath);
        if (!file.is_open()) return false;

        string first_line;
        if (getline(file, first_line)) {
            first_line = trim_str(first_line);
            if (first_line != "!database!" && first_line != "!gdb!") {
                cout << "[GSE Error]: Invalid library header in " << active_filepath << " (expected !database!)" << endl;
                file.close();
                return false;
            }
        }

        active_table = "default";
        string line;
        while (getline(file, line)) {
            exec_command(line);
        }

        file.close();
        cout << "[GSE Success]: Loaded " << db_store.size() << " records from " << active_filepath << endl;
        return true;
    }

    bool save(const string& filename) {
        string target = filename.empty() ? active_filepath : strip_quotes(filename);
        if (target.empty()) target = "storage.gdb";

        ofstream file(target, ios::trunc);
        if (!file.is_open()) return false;

        file << "!database!\n";
        file << "* Grass Storage Engine (GSE v3.0) Database File\n\n";

        // Группируем записи по таблицам для аккуратного сохранения
        map<string, vector<GSERecord>> grouped;
        for (const auto& kv : db_store) {
            grouped[kv.second.table].push_back(kv.second);
        }

        for (const auto& tbl_group : grouped) {
            if (tbl_group.first != "default") {
                file << "table(" << tbl_group.first << ")\n";
            }
            for (const auto& rec : tbl_group.second) {
                file << rec.key << "(" << rec.value << ")\n";
            }
            if (tbl_group.first != "default") {
                file << "end-table\n\n";
            }
        }

        file.close();
        return true;
    }

    bool backup(const string& backup_path) {
        return save(backup_path.empty() ? (active_filepath + ".bak") : backup_path);
    }

    bool export_json(const string& json_path) {
        ofstream out(strip_quotes(json_path), ios::trunc);
        if (!out.is_open()) return false;

        out << "{\n";
        size_t idx = 0, total = db_store.size();
        for (const auto& kv : db_store) {
            out << "  \"" << kv.first << "\": {\n"
                << "    \"value\": \"" << kv.second.value << "\",\n"
                << "    \"table\": \"" << kv.second.table << "\"\n"
                << "  }" << (++idx < total ? "," : "") << "\n";
        }
        out << "}\n";
        out.close();
        return true;
    }

    string exec_command(const string& raw_line) {
        string line = trim_str(raw_line);
        if (line.empty() || line[0] == '*' || line.substr(0, 2) == "//" ||
            line == "!database!" || line == "!gdb!") {
            return "";
        }

        if (line == "end-table") {
            active_table = "default";
            return "";
        }
        if (line == "db_save" || line == "db_save()") {
            save();
            return "OK";
        }
        if (line == "db_clear" || line == "db_clear()") {
            clear();
            return "OK";
        }

        size_t open_p = line.find('(');
        size_t close_p = line.rfind(')');
        if (open_p == string::npos || close_p == string::npos || close_p < open_p) {
            return "";
        }

        string cmd = trim_str(line.substr(0, open_p));
        string arg_content = trim_str(line.substr(open_p + 1, close_p - open_p - 1));

        // Вспомогательное разделение на 2 аргумента через запятую
        auto split_two = [](const string& src, string& a1, string& a2) {
            size_t c = src.find(',');
            if (c != string::npos) {
                a1 = trim_str(src.substr(0, c));
                a2 = trim_str(src.substr(c + 1));
            } else {
                a1 = trim_str(src);
                a2 = "";
            }
        };

        // --- Обработка новых системных команд СУБД ---
        if (cmd == "table") {
            use_table(arg_content);
            return active_table;
        }
        if (cmd == "db_open") {
            return open(arg_content) ? "1" : "0";
        }
        if (cmd == "db_save") {
            return save(arg_content) ? "1" : "0";
        }
        if (cmd == "db_backup") {
            return backup(arg_content) ? "1" : "0";
        }
        if (cmd == "db_export_json") {
            return export_json(arg_content) ? "1" : "0";
        }
        if (cmd == "db_autosave") {
            auto_save_enabled = (arg_content == "1" || arg_content == "true" || arg_content == "on");
            return auto_save_enabled ? "1" : "0";
        }
        if (cmd == "db_set") {
            string k, v;
            split_two(arg_content, k, v);
            set(k, v);
            return v;
        }
        if (cmd == "db_get") {
            return get(arg_content);
        }
        if (cmd == "db_del" || cmd == "db_remove") {
            return remove(arg_content) ? "1" : "0";
        }
        if (cmd == "db_has" || cmd == "db_exists") {
            return has(arg_content) ? "1" : "0";
        }
        if (cmd == "db_inc") {
            string k, d;
            split_two(arg_content, k, d);
            long long delta = 1;
            if (!d.empty()) { try { delta = stoll(d); } catch (...) {} }
            return to_string(add_int(k, delta));
        }
        if (cmd == "db_dec") {
            string k, d;
            split_two(arg_content, k, d);
            long long delta = 1;
            if (!d.empty()) { try { delta = stoll(d); } catch (...) {} }
            return to_string(add_int(k, -delta));
        }
        if (cmd == "db_hash") {
            return hash_string(strip_quotes(arg_content));
        }
        if (cmd == "db_encrypt") {
            encrypt_key(arg_content);
            return get(arg_content);
        }
        if (cmd == "db_decrypt") {
            return decrypt_key(arg_content);
        }

        // --- Стандартная запись или чтение ключа: key(value) или key() ---
        if (arg_content.empty()) {
            return get(cmd);
        } else {
            set(cmd, arg_content);
            return get(cmd);
        }
    }

    const map<string, GSERecord>& get_all_records() {
        return db_store;
    }

} // namespace GrassDB

bool GrassStorageEngine_Load(const string& filename) {
    return GrassDB::open(filename);
}
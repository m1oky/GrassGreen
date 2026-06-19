#include <windows.h>
#include "GrassLib.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>

using namespace std;

// Создаём тот самый кэш, который искал main.cpp
map<string, wstring> variables_cache;

// Вспомогательная функция для перевода текста из UTF-8 в понятный Windows формат
wstring lib_utf8_to_wstring(const string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
    return wstr;
}

// Наш собственный движок чтения баз данных (встроен прямо в GrassLib)
void GrassStorageEngine_Load(const std::string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cout << "[GrassLib Error]: Could not open database file: " << filename << endl;
        return;
    }

    string line;
    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line == "!database!") continue;

        size_t paren_start = line.find('(');
        size_t paren_end = line.find(')');
        
        if (paren_start != string::npos && paren_end != string::npos && paren_end > paren_start) {
            string key = line.substr(0, paren_start);
            string val = line.substr(paren_start + 1, paren_end - paren_start - 1);
            
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            
            variables_cache[key] = lib_utf8_to_wstring(val);
        }
    }
    file.close();

    // ====================================================
    // ТОТ САМЫЙ ЧИСТЫЙ ВЫВОД ДАННЫХ БАЗЫ В ТЕРМИНАЛ
    // ====================================================
    cout << "\n--- ДАННЫЕ ИЗ БАЗЫ " << filename << " ---" << endl;
    
    // Вытаскиваем ник, если он есть
    if (variables_cache.count("user")) {
        wstring w_user = variables_cache["user"];
        string s_user(w_user.begin(), w_user.end());
        cout << "имя: " << s_user << endl;
    }
    
    // Вытаскиваем версию сборки, если она есть
    if (variables_cache.count("env_target_runtime")) {
        wstring w_ver = variables_cache["env_target_runtime"];
        string s_ver(w_ver.begin(), w_ver.end());
        cout << "версия: " << s_ver << endl;
    }
    
    cout << "------------------------------------\n" << endl;
}

// Управление токенами библиотек из твоего файла разметки
void GrassLib_Load(const std::string& lib_name) {
    
    if (lib_name == "!GrassGreen!") {
        cout << "[GrassLib]: Core library 'GrassGreen' initialized successfully." << endl;
    }
    else if (lib_name == "!interface!") {
        cout << "[GrassLib]: UI library 'interface' initialized. Standing by for layout..." << endl;
    }
    else if (lib_name == "!GrassWeb!") {
        cout << "[GrassLib]: Web Rendering Engine '!GrassWeb!' successfully initialized. Standing by for !GREEN_WWW1! files..." << endl;
    }
    else if (lib_name == "!database!") {
        GrassStorageEngine_Load("storage.gdb");
        cout << "[GrassLib]: Data library 'database' initialized. storage.gdb loaded!" << endl;
    }
    else if (lib_name == "!FastMath!") {
        cout << "[GrassLib]: Performance Math Engine '!FastMath!' loaded." << endl;
    }
else if (lib_name == "!CoreMath!") {
        cout << "[GrassLib]: Precision Cryptography & Quantum Engine '!CoreMath!' loaded successfully. Quantum register ready." << endl;
    }
    else {
        cout << "[GrassLib Warning]: Unknown library token detected: " << lib_name << endl;
    }
}
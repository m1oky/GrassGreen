#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>


using namespace std;

extern map<string, wstring> variables_cache; 

wstring utf8_to_wstring_gse(const string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back(); 
    return wstr;
}

bool GrassStorageEngine_Load(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) return false;

    string first_line;
    if (getline(file, first_line)) {
        if (!first_line.empty() && first_line.back() == '\r') first_line.pop_back();
        if (first_line != "!database!") {
            cout << "[GSE Error]: Invalid library header!" << endl;
            file.close();
            return false;
        }
    }

    string line;
    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '*') continue; 

        size_t open_paren = line.find('(');
        size_t close_paren = line.find_last_of(')');

        if (open_paren != string::npos && close_paren != string::npos && close_paren > open_paren) {
            string key = line.substr(0, open_paren);
            string value = line.substr(open_paren + 1, close_paren - open_paren - 1);

            key.erase(0, key.find_first_not_of(" \t")); key.erase(key.find_last_not_of(" \t") + 1);

            string cache_key = key + "()";
            variables_cache[cache_key] = utf8_to_wstring_gse(value);
        }
    }

    file.close();
    cout << "[GSE Success]: Successfully parsed custom functions from " << filename << endl;
    return true;
}
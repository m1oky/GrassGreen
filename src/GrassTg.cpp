#include "GrassTg.h"
#include <windows.h>
#include <wininet.h>
#include <sstream>
#include <iomanip>
#include <vector>

std::string GrassTg::bot_token = "";
std::string GrassTg::last_user = "";
std::string GrassTg::last_chat_id = "";
std::string GrassTg::last_msg_id = "";
std::string GrassTg::last_callback = "";
int64_t GrassTg::last_update_id = 0;

std::string GrassTg::urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return escaped.str();
}

std::string GrassTg::httpGet(const std::string& url) {
    // INTERNET_OPEN_TYPE_PRECONFIG подтягивает системные сетевые настройки и прокси
    HINTERNET hInternet = InternetOpenA("GrassGreenBot/3.5", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return "";

    DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE;
    HINTERNET hFile = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, flags, 0);
    if (!hFile) {
        InternetCloseHandle(hInternet);
        return "";
    }

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hFile, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response.append(buffer, bytesRead);
    }

    InternetCloseHandle(hFile);
    InternetCloseHandle(hInternet);
    return response;
}

// Декодирование \uXXXX и спецсимволов JSON в чистый UTF-8
static std::string decodeJsonString(const std::string& str) {
    std::string res;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            char next = str[i + 1];
            if (next == 'u' && i + 5 < str.length()) {
                std::string hexStr = str.substr(i + 2, 4);
                char* endp = nullptr;
                uint32_t cp = std::strtoul(hexStr.c_str(), &endp, 16);
                if (endp == hexStr.c_str() + 4) {
                    if (cp <= 0x7F) {
                        res.push_back(static_cast<char>(cp));
                    } else if (cp <= 0x7FF) {
                        res.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
                        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else if (cp <= 0xFFFF) {
                        res.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                        res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    i += 5;
                    continue;
                }
            } else if (next == 'n') { res.push_back('\n'); i++; continue; }
            else if (next == 'r') { res.push_back('\r'); i++; continue; }
            else if (next == 't') { res.push_back('\t'); i++; continue; }
            else if (next == '"') { res.push_back('"');  i++; continue; }
            else if (next == '\\') { res.push_back('\\'); i++; continue; }
            else if (next == '/') { res.push_back('/');  i++; continue; }
        }
        res.push_back(str[i]);
    }
    return res;
}

std::string GrassTg::extractField(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return "";
    pos += key.length() + 3;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.length()) return "";

    std::string raw;
    if (json[pos] == '"') {
        pos++;
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        raw = json.substr(pos, end - pos);
    } else {
        size_t end = json.find_first_of(",}] \r\n", pos);
        if (end == std::string::npos) end = json.length();
        raw = json.substr(pos, end - pos);
    }

    return decodeJsonString(raw);
}

void GrassTg::init(const std::string& token) {
    bot_token = token;
    last_update_id = 0;
    last_chat_id = "";
    last_user = "";
    last_msg_id = "";
    last_callback = "";
}

std::string GrassTg::poll(int timeout_sec) {
    if (bot_token.empty()) return "";

    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/getUpdates?offset=" + std::to_string(last_update_id + 1) +
                      "&limit=1&timeout=" + std::to_string(timeout_sec);
    std::string res = httpGet(url);

    if (res.find("\"ok\":true") == std::string::npos || res.find("\"result\":[]") != std::string::npos) {
        return "";
    }

    std::string upd_str = extractField(res, "update_id");
    if (!upd_str.empty()) {
        try {
            last_update_id = std::stoll(upd_str);
        } catch (...) {}
    }

    // Обработка нажатий на инлайн-кнопки
    size_t cb_pos = res.find("\"callback_query\":");
    if (cb_pos != std::string::npos) {
        std::string cb_block = res.substr(cb_pos);
        last_callback = extractField(cb_block, "data");
        size_t from_pos = cb_block.find("\"from\":");
        if (from_pos != std::string::npos) {
            std::string user = extractField(cb_block.substr(from_pos), "username");
            if (user.empty()) user = extractField(cb_block.substr(from_pos), "first_name");
            last_user = user;
        }
        size_t msg_pos = cb_block.find("\"message\":");
        if (msg_pos != std::string::npos) {
            std::string sub = cb_block.substr(msg_pos);
            last_msg_id = extractField(sub, "message_id");
            size_t chat_pos = sub.find("\"chat\":");
            if (chat_pos != std::string::npos) {
                last_chat_id = extractField(sub.substr(chat_pos), "id");
            }
        }
        return "[BUTTON]:" + last_callback;
    }

    last_callback = "";

    // Обработка обычных входящих сообщений
    size_t msg_pos = res.find("\"message\":");
    if (msg_pos != std::string::npos) {
        std::string sub = res.substr(msg_pos);
        last_msg_id = extractField(sub, "message_id");
        size_t from_pos = sub.find("\"from\":");
        if (from_pos != std::string::npos) {
            std::string user = extractField(sub.substr(from_pos), "username");
            if (user.empty()) user = extractField(sub.substr(from_pos), "first_name");
            last_user = user;
        }
        size_t chat_pos = sub.find("\"chat\":");
        if (chat_pos != std::string::npos) {
            last_chat_id = extractField(sub.substr(chat_pos), "id");
        }
        return extractField(sub, "text");
    }

    return "";
}

std::string GrassTg::get_user() { return last_user; }
std::string GrassTg::get_chat_id() { return last_chat_id; }
std::string GrassTg::get_msg_id() { return last_msg_id; }
std::string GrassTg::get_callback() { return last_callback; }

bool GrassTg::send(const std::string& chat_id, const std::string& text) {
    if (bot_token.empty() || chat_id.empty()) return false;
    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/sendMessage?chat_id=" + urlEncode(chat_id) +
                      "&text=" + urlEncode(text);
    return (httpGet(url).find("\"ok\":true") != std::string::npos);
}

bool GrassTg::reply(const std::string& text) {
    if (last_chat_id.empty()) return false;
    return send(last_chat_id, text);
}

// Кнопки меню внизу экрана ("Кнопка 1|Кнопка 2")
bool GrassTg::send_btn(const std::string& chat_id, const std::string& text, const std::string& buttons) {
    if (bot_token.empty() || chat_id.empty()) return false;
    std::string kb = "{\"keyboard\":[[";
    std::stringstream ss(buttons);
    std::string item;
    bool first = true;
    while (std::getline(ss, item, '|')) {
        if (!first) kb += ",";
        kb += "{\"text\":\"" + item + "\"}";
        first = false;
    }
    kb += "]],\"resize_keyboard\":true}";

    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/sendMessage?chat_id=" + urlEncode(chat_id) +
                      "&text=" + urlEncode(text) +
                      "&reply_markup=" + urlEncode(kb);
    return (httpGet(url).find("\"ok\":true") != std::string::npos);
}

// Инлайн-кнопки прямо под постом ("Текст=data1|Текст2=data2")
bool GrassTg::send_inline(const std::string& chat_id, const std::string& text, const std::string& buttons) {
    if (bot_token.empty() || chat_id.empty()) return false;
    std::string kb = "{\"inline_keyboard\":[[";
    std::stringstream ss(buttons);
    std::string item;
    bool first = true;
    while (std::getline(ss, item, '|')) {
        size_t eq = item.find('=');
        std::string title = (eq != std::string::npos) ? item.substr(0, eq) : item;
        std::string data = (eq != std::string::npos) ? item.substr(eq + 1) : item;
        if (!first) kb += ",";
        kb += "{\"text\":\"" + title + "\",\"callback_data\":\"" + data + "\"}";
        first = false;
    }
    kb += "]]}";

    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/sendMessage?chat_id=" + urlEncode(chat_id) +
                      "&text=" + urlEncode(text) +
                      "&reply_markup=" + urlEncode(kb);
    return (httpGet(url).find("\"ok\":true") != std::string::npos);
}

// Запуск Web App прямо из Telegram
bool GrassTg::send_webapp(const std::string& chat_id, const std::string& text, const std::string& btn_title, const std::string& url_app) {
    if (bot_token.empty() || chat_id.empty()) return false;
    std::string kb = "{\"inline_keyboard\":[[{\"text\":\"" + btn_title + "\",\"web_app\":{\"url\":\"" + url_app + "\"}}]]}";
    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/sendMessage?chat_id=" + urlEncode(chat_id) +
                      "&text=" + urlEncode(text) +
                      "&reply_markup=" + urlEncode(kb);
    return (httpGet(url).find("\"ok\":true") != std::string::npos);
}

bool GrassTg::send_photo(const std::string& chat_id, const std::string& photo_url, const std::string& caption) {
    if (bot_token.empty() || chat_id.empty()) return false;
    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/sendPhoto?chat_id=" + urlEncode(chat_id) +
                      "&photo=" + urlEncode(photo_url) +
                      "&caption=" + urlEncode(caption);
    return (httpGet(url).find("\"ok\":true") != std::string::npos);
}

bool GrassTg::send_dice(const std::string& chat_id, const std::string& emoji) {
    if (bot_token.empty() || chat_id.empty()) return false;
    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/sendDice?chat_id=" + urlEncode(chat_id) +
                      "&emoji=" + urlEncode(emoji);
    return (httpGet(url).find("\"ok\":true") != std::string::npos);
}

bool GrassTg::edit(const std::string& chat_id, const std::string& msg_id, const std::string& new_text) {
    if (bot_token.empty() || chat_id.empty() || msg_id.empty()) return false;
    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/editMessageText?chat_id=" + urlEncode(chat_id) +
                      "&message_id=" + urlEncode(msg_id) +
                      "&text=" + urlEncode(new_text);
    return (httpGet(url).find("\"ok\":true") != std::string::npos);
}

bool GrassTg::del(const std::string& chat_id, const std::string& msg_id) {
    if (bot_token.empty() || chat_id.empty() || msg_id.empty()) return false;
    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/deleteMessage?chat_id=" + urlEncode(chat_id) +
                      "&message_id=" + urlEncode(msg_id);
    return (httpGet(url).find("\"ok\":true") != std::string::npos);
}
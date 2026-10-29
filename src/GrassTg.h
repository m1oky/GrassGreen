#ifndef GRASS_TG_H
#define GRASS_TG_H

#include <string>
#include <cstdint>

class GrassTg {
private:
    static std::string bot_token;
    static std::string last_user;
    static std::string last_chat_id;
    static std::string last_msg_id;
    static std::string last_callback;
    static int64_t last_update_id;

    static std::string urlEncode(const std::string& value);
    static std::string httpGet(const std::string& url);
    static std::string extractField(const std::string& json, const std::string& key);

public:
    static void init(const std::string& token);
    static std::string poll(int timeout_sec);
    static std::string get_user();
    static std::string get_chat_id();
    static std::string get_msg_id();
    static std::string get_callback();

    static bool send(const std::string& chat_id, const std::string& text);
    static bool reply(const std::string& text);
    static bool send_btn(const std::string& chat_id, const std::string& text, const std::string& buttons);
    static bool send_inline(const std::string& chat_id, const std::string& text, const std::string& buttons);
    static bool send_webapp(const std::string& chat_id, const std::string& text, const std::string& btn_title, const std::string& url);
    static bool send_photo(const std::string& chat_id, const std::string& photo_url, const std::string& caption);
    static bool send_dice(const std::string& chat_id, const std::string& emoji);
    static bool edit(const std::string& chat_id, const std::string& msg_id, const std::string& new_text);
    static bool del(const std::string& chat_id, const std::string& msg_id);
};

#endif // GRASS_TG_H
#include <string>
#include <iostream>

namespace GrassGreen {
    void telegram_init(const std::string& token) {
        std::cout << "Initialized with token: " << token << std::endl;
    }

    void send_message(int chat_id, const std::string& text) {
        std::cout << "Message sent to chat ID: " << chat_id << ", text: " << text << std::endl;
    }

    void send_photo(int chat_id, const std::string& photo_path) {
        std::cout << "Photo sent to chat ID: " << chat_id << ", path: " << photo_path << std::endl;
    }

    void send_document(int chat_id, const std::string& document_path) {
        std::cout << "Document sent to chat ID: " << chat_id << ", path: " << document_path << std::endl;
    }
}

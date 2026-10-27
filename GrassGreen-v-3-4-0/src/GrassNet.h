#ifndef GRASS_NET_H
#define GRASS_NET_H

#include <string>

namespace GrassNet {
    // Настройка агента и API
    void set_agent(const std::string& browser, const std::string& device);
    void set_api(const std::string& api_key);
    std::string get_user_agent();

    // Клиентские операции
    bool connect_host(const std::string& ip_or_host, int port = 80);
    std::string send_request(const std::string& url, const std::string& method = "GET", const std::string& body = "");
    int get_last_status();

    // Серверные операции (create_serv / accept)
    bool create_server(const std::string& ip, int port, const std::string& password, const std::string& certain_ip);
    std::string accept_request();
    void sync_async(int delay_ms = 10);

    // Сетевые утилиты
    double ping(const std::string& host, int port = 80);
    std::string resolve_ip(const std::string& domain);
    std::string get_local_ip();
    bool download_file(const std::string& url, const std::string& filepath);
    void close_all();
}

#endif // GRASS_NET_H
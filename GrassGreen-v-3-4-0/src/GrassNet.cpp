#include "GrassNet.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#include <urlmon.h>
#include <chrono>
#include <iostream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")

namespace GrassNet {

    static bool wsa_initialized = false;
    static std::string current_browser = "GrassGreenNet/3.1";
    static std::string current_device = "Windows NT 10.0; Win64; x64";
    static std::string current_api_key = "";
    static int last_http_status = 0;

    static SOCKET client_sock = INVALID_SOCKET;
    static SOCKET server_sock = INVALID_SOCKET;
    static std::string srv_password = "";
    static std::string srv_certain_ip = "";
    static int srv_port = 8080;

    static void ensure_wsa() {
        if (!wsa_initialized) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
                wsa_initialized = true;
            }
        }
    }

    void set_agent(const std::string& browser, const std::string& device) {
        if (!browser.empty()) current_browser = browser;
        if (!device.empty()) current_device = device;
    }

    void set_api(const std::string& api_key) {
        current_api_key = api_key;
    }

    std::string get_user_agent() {
        return "Mozilla/5.0 (" + current_device + ") " + current_browser;
    }

    int get_last_status() {
        return last_http_status;
    }

    std::string resolve_ip(const std::string& domain) {
        ensure_wsa();
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(domain.c_str(), nullptr, &hints, &res) != 0 || !res) {
            return "";
        }

        char ip_str[INET_ADDRSTRLEN] = {0};
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)res->ai_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
        freeaddrinfo(res);
        return std::string(ip_str);
    }

    std::string get_local_ip() {
        ensure_wsa();
        char hostname[256] = {0};
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            return resolve_ip(hostname);
        }
        return "127.0.0.1";
    }

    bool connect_host(const std::string& ip_or_host, int port) {
        ensure_wsa();
        if (client_sock != INVALID_SOCKET) {
            closesocket(client_sock);
            client_sock = INVALID_SOCKET;
        }

        std::string target_ip = resolve_ip(ip_or_host);
        if (target_ip.empty()) target_ip = ip_or_host;

        client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client_sock == INVALID_SOCKET) return false;

        // Таймаут подключения 3 секунды
        DWORD timeout = 3000;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        inet_pton(AF_INET, target_ip.c_str(), &addr.sin_addr);

        if (::connect(client_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(client_sock);
            client_sock = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    double ping(const std::string& host, int port) {
        ensure_wsa();
        auto t1 = std::chrono::high_resolution_clock::now();
        bool ok = connect_host(host, port);
        auto t2 = std::chrono::high_resolution_clock::now();
        if (client_sock != INVALID_SOCKET) {
            closesocket(client_sock);
            client_sock = INVALID_SOCKET;
        }
        if (!ok) return -1.0;
        return std::chrono::duration<double, std::milli>(t2 - t1).count();
    }

    std::string send_request(const std::string& url, const std::string& method, const std::string& body) {
        std::string ua = get_user_agent();
        HINTERNET hInternet = InternetOpenA(ua.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return "[GrassNet Error]: Не удалось инициализировать WinINet";

        std::string headers = "User-Agent: " + ua + "\r\n";
        if (!current_api_key.empty()) {
            headers += "Authorization: Bearer " + current_api_key + "\r\n";
            headers += "X-API-Key: " + current_api_key + "\r\n";
        }

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        if (url.find("https://") == 0) {
            flags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
        }

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), headers.c_str(), (DWORD)headers.length(), flags, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            last_http_status = 0;
            return "[GrassNet Error]: Не удалось подключиться к " + url;
        }

        DWORD status_code = 0;
        DWORD status_len = sizeof(status_code);
        HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status_code, &status_len, NULL);
        last_http_status = (int)status_code;

        std::string response;
        char buffer[4096];
        DWORD bytes_read = 0;
        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytes_read) && bytes_read > 0) {
            response.append(buffer, bytes_read);
        }

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return response;
    }

    bool create_server(const std::string& ip, int port, const std::string& password, const std::string& certain_ip) {
        ensure_wsa();
        if (server_sock != INVALID_SOCKET) {
            closesocket(server_sock);
        }

        srv_port = port;
        srv_password = password;
        srv_certain_ip = certain_ip;

        server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_sock == INVALID_SOCKET) return false;

        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in service = {};
        service.sin_family = AF_INET;
        service.sin_port = htons((u_short)port);
        if (ip.empty() || ip == "0.0.0.0") {
            service.sin_addr.s_addr = INADDR_ANY;
        } else {
            inet_pton(AF_INET, ip.c_str(), &service.sin_addr);
        }

        if (::bind(server_sock, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
            closesocket(server_sock);
            server_sock = INVALID_SOCKET;
            return false;
        }

        if (listen(server_sock, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(server_sock);
            server_sock = INVALID_SOCKET;
            return false;
        }

        std::cout << "[GrassNet Server]: Запущен на " << (ip.empty() ? "127.0.0.1" : ip)
                  << ":" << port << " (Filter IP: " << (certain_ip.empty() ? "ALL" : certain_ip) << ")\n";
        return true;
    }

    std::string accept_request() {
        if (server_sock == INVALID_SOCKET) {
            return "[GrassNet Error]: Сервер не запущен (вызови create_serv)";
        }

        // Таймаут ожидания 100 мс, чтобы не зависать намертво при тестах
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(server_sock, &readSet);
        timeval tv = { 0, 100000 }; // 100 ms

        int sel = select(0, &readSet, NULL, NULL, &tv);
        if (sel <= 0) {
            return "IDLE (нет входящих пакетов)";
        }

        sockaddr_in client_info = {};
        int addr_len = sizeof(client_info);
        SOCKET client = ::accept(server_sock, (struct sockaddr*)&client_info, &addr_len);
        if (client == INVALID_SOCKET) return "";

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_info.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::string incoming_ip(client_ip);

        // Проверка certainIP
        if (!srv_certain_ip.empty() && srv_certain_ip != "0.0.0.0" && incoming_ip != srv_certain_ip) {
            std::string deny = "HTTP/1.1 403 Forbidden\r\n\r\nAccess Denied by GrassNet certainIP policy.";
            ::send(client, deny.c_str(), (int)deny.size(), 0);
            closesocket(client);
            return "[Blocked IP]: " + incoming_ip;
        }

        char buf[2048] = {0};
        int rec = recv(client, buf, sizeof(buf) - 1, 0);
        std::string data = (rec > 0) ? std::string(buf, rec) : "";

        std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nGrassNet Server ACK";
        ::send(client, reply.c_str(), (int)reply.size(), 0);
        closesocket(client);
        return data;
    }

    void sync_async(int delay_ms) {
        if (delay_ms > 0) Sleep(delay_ms);
    }

    bool download_file(const std::string& url, const std::string& filepath) {
        HRESULT hr = URLDownloadToFileA(NULL, url.c_str(), filepath.c_str(), 0, NULL);
        return SUCCEEDED(hr);
    }

    void close_all() {
        if (client_sock != INVALID_SOCKET) { closesocket(client_sock); client_sock = INVALID_SOCKET; }
        if (server_sock != INVALID_SOCKET) { closesocket(server_sock); server_sock = INVALID_SOCKET; }
    }

} // namespace GrassNet
#include <winsock2.h> 
#include <windows.h>
#include "GrassWeb.h"
#include "WebTheme.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>

using namespace std;

static string ExtractArg(const string& line, const string& cmd) {
    size_t start = line.find('(');
    size_t end = line.rfind(')'); // Ищем строго внешнюю скобку с конца строки
    if (start != string::npos && end != string::npos && end > start) {
        return line.substr(start + 1, end - start - 1);
    }
    return "";
}

static void ExtractTwoArgs(const string& line, string& arg1, string& arg2) {
    size_t start = line.find('(');
    size_t end = line.rfind(')');
    if (start != string::npos && end != string::npos && end > start) {
        string content = line.substr(start + 1, end - start - 1);
        size_t comma = content.find(',');
        if (comma != string::npos) {
            arg1 = content.substr(0, comma);
            arg2 = content.substr(comma + 1);
            arg1.erase(0, arg1.find_first_not_of(" \t")); arg1.erase(arg1.find_last_not_of(" \t") + 1);
            arg2.erase(0, arg2.find_first_not_of(" \t")); arg2.erase(arg2.find_last_not_of(" \t") + 1);
        }
    }
}

static void ExtractThreeArgs(const string& line, string& arg1, string& arg2, string& arg3) {
    string content = ExtractArg(line, "");
    stringstream ss(content);
    getline(ss, arg1, ','); getline(ss, arg2, ','); getline(ss, arg3, ',');
    arg1.erase(0, arg1.find_first_not_of(" \t")); arg1.erase(arg1.find_last_not_of(" \t") + 1);
    arg2.erase(0, arg2.find_first_not_of(" \t")); arg2.erase(arg2.find_last_not_of(" \t") + 1);
    arg3.erase(0, arg3.find_first_not_of(" \t")); arg3.erase(arg3.find_last_not_of(" \t") + 1);
}

string CompileGrassToHTML(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) return "<h1>404 Error: Файл " + filename + " не найден!</h1>";

    stringstream html_content;
    html_content << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n";
    html_content << "<title>GrassGreen Engine</title>\n";
    html_content << "<style>\n" << BASE_CSS << "\n</style>\n</head>\n<body>\n";
    html_content << "<div style=\"display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 80vh;\">\n";

    string line;
    string current_color = "#ffffff", current_bg = "", current_radius = "0";
    string current_grad1 = "", current_grad2 = "", current_border_sz = "", current_border_col = "";
    string current_w = "", current_h = "", neon_color = "", anim_type = "", anim_speed = "";
    string flip_target = "", current_id = ""; 
    
    // Переменные состояния для транслятора логики
    bool in_script = false; 
    string current_js_target = ""; 

    bool is_bold = false, is_glass = false, is_neon = false, is_circle = false, anim_loop = true;

    auto ResetStyles = [&]() {
        current_color = "#ffffff"; current_bg = ""; current_radius = "0";
        current_grad1 = ""; current_grad2 = ""; current_border_sz = ""; current_border_col = "";
        current_w = ""; current_h = ""; neon_color = ""; anim_type = ""; anim_speed = ""; flip_target = ""; current_id = "";
        is_bold = false; is_glass = false; is_neon = false; is_circle = false; anim_loop = true;
    };

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        int start_pos = 0;
        while (start_pos < line.length() && (line[start_pos] == ' ' || line[start_pos] == '\t')) start_pos++;
        string trimmed = line.substr(start_pos);
        
        if (trimmed.empty() || trimmed.find("**") == 0 || trimmed.find("!GREEN_WWW1!") != string::npos || trimmed.find("!GrassWeb!") != string::npos) continue;

        // ==========================================
        // ОБРАБОТКА БЛОКА ЛОГИКИ (SCRIPT)
        // ==========================================
        if (trimmed == "script-start") { 
            in_script = true; 
            html_content << "<script>\n"; 
            continue; 
        }
        if (trimmed == "script-end") { 
            in_script = false; 
            html_content << "</script>\n"; 
            continue; 
        }

        if (in_script) {
            // Твой синтаксис выбора объекта: id(имя):
            if (trimmed.find("id(") == 0 && trimmed.find("):") != string::npos) {
                size_t p_end = trimmed.find("):");
                current_js_target = trimmed.substr(3, p_end - 3);
                continue;
            }

            // Команда отслеживания клика на выбранном объекте
            if (trimmed == "on-click") {
                html_content << "document.getElementById('" << current_js_target << "').addEventListener('click', function() {\n";
                continue;
            }
            if (trimmed == "end-click") { 
                html_content << "});\n"; 
                continue; 
            }

            // Твой кастомный CHEK с поддержкой val() вместо длинного JS кода
            if (trimmed.find("chek(") == 0) {
                string condition = ExtractArg(trimmed, "chek");
                
                // Переводим компактное val(переменная) в понятный для браузера JS-код
                size_t v_start = condition.find("val Hoy(");
                if (v_start == string::npos) v_start = condition.find("val(");
                
                if (v_start != string::npos) {
                    size_t v_open = condition.find("(", v_start);
                    size_t v_close = condition.find(")", v_open);
                    if (v_open != string::npos && v_close != string::npos) {
                        string input_id = condition.substr(v_open + 1, v_close - v_open - 1);
                        string js_replacement = "document.getElementById('" + input_id + "').value";
                        condition.replace(v_start, v_close - v_start + 1, js_replacement);
                    }
                }
                
                html_content << "  if (" << condition << ") {\n";
                continue;
            }
            
            if (trimmed == "else") { 
                html_content << "  } else {\n"; 
                continue; 
            }
            if (trimmed == "end-chek") { 
                html_content << "  }\n"; 
                continue; 
            }

            // Твоя команда перекраски объекта (ИСПРАВЛЕНА ОШИБКА С СИНТАКСИСОМ!)
            if (trimmed.find("repaint-color(") == 0) {
                string col = ExtractArg(trimmed, "repaint-color");
                html_content << "    document.getElementById('" << current_js_target << "').style.color = '" << col << "';\n";
                html_content << "    document.getElementById('" << current_js_target << "').style.setProperty('--neon-color', '" << col << "');\n";
                continue;
            }

            if (trimmed.find("hide(") == 0) { 
                html_content << "    document.getElementById('" << ExtractArg(trimmed, "hide") << "').style.display = 'none';\n"; 
                continue; 
            }
            if (trimmed.find("show(") == 0) { 
                html_content << "    document.getElementById('" << ExtractArg(trimmed, "show") << "').style.display = 'block';\n"; 
                continue; 
            }
            if (trimmed.find("alert(") == 0) { 
                html_content << "    alert('" << ExtractArg(trimmed, "alert") << "');\n"; 
                continue; 
            }

            continue;
        }

        // ==========================================
        // ПАРСЕР ИНТЕРФЕЙСА СТРАНИЦЫ
        // ==========================================
        if (trimmed == "row-start") { html_content << "<div class=\"grass-row\">\n"; continue; }
        if (trimmed == "row-end")   { html_content << "</div>\n"; continue; }
        if (trimmed == "col-start") { html_content << "<div class=\"grass-col\">\n"; continue; }
        if (trimmed == "col-end")   { html_content << "</div>\n"; continue; }
        if (trimmed == "box-start") { html_content << "<div class=\"grass-box\">\n"; continue; }
        if (trimmed == "box-end")   { html_content << "</div>\n"; continue; }

        if (trimmed.find("id(") == 0) { current_id = ExtractArg(trimmed, "id"); continue; }
        if (trimmed.find("color(") == 0) { current_color = ExtractArg(trimmed, "color"); continue; }
        if (trimmed.find("bg(") == 0) { current_bg = ExtractArg(trimmed, "bg"); continue; }
        if (trimmed.find("round-ob(") == 0) { current_radius = ExtractArg(trimmed, "round-ob"); continue; }
        if (trimmed.find("grad(") == 0) { ExtractTwoArgs(trimmed, current_grad1, current_grad2); continue; }
        if (trimmed.find("size(") == 0) { ExtractTwoArgs(trimmed, current_w, current_h); continue; }
        if (trimmed.find("border(") == 0) { ExtractTwoArgs(trimmed, current_border_sz, current_border_col); continue; }
        if (trimmed.find("thick()") != string::npos) { is_bold = true; continue; }
        if (trimmed.find("glass()") != string::npos) { is_glass = true; continue; }
        if (trimmed.find("circle()") != string::npos) { is_circle = true; continue; }
        if (trimmed.find("neon(") == 0) { is_neon = true; neon_color = ExtractArg(trimmed, "neon"); continue; }
        if (trimmed.find("animate(") == 0) { ExtractTwoArgs(trimmed, anim_type, anim_speed); continue; }
        if (trimmed == "stop" || trimmed == "stop()") { anim_loop = false; continue; }
        if (trimmed.find("flip(") == 0) { flip_target = ExtractArg(trimmed, "flip"); continue; }

        if (trimmed.find("video-bg(") == 0) {
            string video_url = ExtractArg(trimmed, "video-bg");
            html_content << "<video class=\"bg-video\" autoplay loop muted playsinline><source src=\"" << video_url << "\" type=\"video/mp4\"></video>\n";
            continue;
        }

        if (trimmed.find("image(") == 0) {
            string img_url = ExtractArg(trimmed, "image");
            string style = "border-radius: " + current_radius + "px;";
            if (!current_w.empty()) style += " width: " + current_w + "px;";
            if (!current_h.empty()) style += " height: " + current_h + "px;";
            if (is_neon) style += " box-shadow: 0 0 15px " + neon_color + "; border: 2px solid " + neon_color + ";";
            
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<img src=\"" << img_url << "\" class=\"grass-img\" style=\"" << style << "\"" << id_attr << " alt=\"image\">\n";
            ResetStyles(); continue;
        }

        if (trimmed.find("checkbox = ") == 0) {
            string txt = trimmed.substr(11);
            string style = is_neon ? "--neon-color: " + neon_color + "; color: " + neon_color + ";" : "color: " + current_color + ";";
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            
            html_content << "<label class=\"grass-checkbox\" style=\"" << style << "\"><input type=\"checkbox\"" << id_attr << ">" << txt << "</label>\n";
            ResetStyles(); continue;
        }

        if (trimmed.find("slider(") == 0) {
            string s_min, s_max, s_start; 
            ExtractThreeArgs(trimmed, s_min, s_max, s_start);
            string style = is_neon ? "--neon-color: " + neon_color + ";" : "";
            if (!current_w.empty()) style += " width: " + current_w + "px;";
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            
            html_content << "<input type=\"range\" min=\"" << s_min << "\" max=\"" << s_max << "\" value=\"" << s_start << "\" class=\"grass-slider\" style=\"" << style << "\"" << id_attr << ">\n<br>\n";
            ResetStyles(); continue;
        }

        if (trimmed.find("drop-list(") == 0) {
            string content = ExtractArg(trimmed, "drop-list");
            stringstream ss(content); 
            string item;
            string style = is_neon ? "border-color: " + neon_color + "; color: " + neon_color + ";" : "";
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            
            html_content << "<select class=\"grass-drop\" style=\"" << style << "\"" << id_attr << ">\n";
            while (getline(ss, item, ',')) {
                item.erase(0, item.find_first_not_of(" \t")); 
                item.erase(item.find_last_not_of(" \t") + 1);
                html_content << "  <option value=\"" << item << "\">" << item << "</option>\n";
            }
            html_content << "</select>\n<br>\n";
            ResetStyles(); continue;
        }

        if (trimmed.find("link(") == 0) {
            string url = ExtractArg(trimmed, "link");
            size_t eq_pos = trimmed.find("=");
            string text = (eq_pos != string::npos) ? trimmed.substr(eq_pos + 1) : "Ссылка";
            text.erase(0, text.find_first_not_of(" \t"));
            
            string classes = is_bold ? "thick" : "";
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            
            html_content << "<a href=\"/" << url << "\" style=\"color: " << current_color << "; text-decoration: none; padding: 10px;\" class=\"" << classes << "\"" << id_attr << ">" << text << "</a\n>";
            ResetStyles(); continue;
        }

        if (trimmed.find("G1 ") == 0 || trimmed.find("G2 ") == 0 || trimmed.find("G3 ") == 0 || trimmed.find("G4 ") == 0 || trimmed.find("G5 ") == 0) {
            string tag = trimmed.substr(0, 2); 
            string text = trimmed.substr(3);
            string html_tag = "h" + tag.substr(1, 1);
            
            string classes = is_bold ? "thick " : ""; 
            if (is_neon) classes += "neon ";
            
            string style = "color: " + current_color + "; margin-bottom: 5px;";
            if (is_neon) style += " --neon-color: " + neon_color + "; text-shadow: 0 0 10px " + neon_color + ";";
            if (!anim_type.empty()) style += " animation: anim-" + anim_type + " " + anim_speed + (anim_loop ? " infinite" : " 1 forwards") + " ease-in-out;";
            
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<" << html_tag << " class=\"" << classes << "\" style=\"" << style << "\"" << id_attr << ">" << text << "</" << html_tag << ">\n";
            ResetStyles(); continue;
        }

        if (trimmed.find("button = ") == 0) {
            string btn_text = trimmed.substr(9); 
            string classes = "";
            
            if (is_glass) classes += "glass "; 
            if (is_neon) classes += "neon "; 
            if (is_bold) classes += "thick "; 
            if (is_circle) classes += "circle ";
            
            string style = "border-radius: " + current_radius + "px;";
            if (!current_bg.empty()) style += " background-color: " + current_bg + ";";
            if (!current_border_sz.empty() && !current_border_col.empty()) style += " border: " + current_border_sz + "px solid " + current_border_col + ";";
            if (!current_w.empty() && !current_h.empty()) style += " width: " + current_w + "px; height: " + current_h + "px;";
            if (!is_neon) style += " color: " + current_color + ";"; 
            if (is_neon) style += " --neon-color: " + neon_color + ";";
            if (!anim_type.empty()) style += " animation: anim-" + anim_type + " " + anim_speed + (anim_loop ? " infinite" : " 1 forwards") + " ease-in-out;";
            
            string onclick = !flip_target.empty() ? " onclick=\"window.location.href='/" + flip_target + "'\"" : "";
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            
            html_content << "<button class=\"" << classes << "\" style=\"" << style << "\"" << onclick << id_attr << ">" << btn_text << "</button>\n";
            ResetStyles(); continue;
        }

        if (trimmed.find("enter = ") == 0) {
            string placeholder = trimmed.substr(8); 
            string classes = "";
            
            if (is_glass) classes += "glass "; 
            if (is_neon) classes += "neon "; 
            if (is_bold) classes += "thick ";
            
            string style = "background: rgba(0,0,0,0.4); color: #fff; border: 1px solid #444; padding: 15px; margin: 10px; font-size: 16px; outline: none; text-align: center; border-radius: " + current_radius + "px;";
            if (!current_w.empty()) style += " width: " + current_w + "px;";
            if (is_neon) style += " --neon-color: " + neon_color + "; border-color: " + neon_color + "; box-shadow: 0 0 10px " + neon_color + " inset;";
            if (!anim_type.empty()) style += " animation: anim-" + anim_type + " " + anim_speed + (anim_loop ? " infinite" : " 1 forwards") + " ease-in-out;";
            
            string input_type = (placeholder.find("пароль") != string::npos || placeholder.find("Пароль") != string::npos) ? "password" : "text";
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            
            html_content << "<input type=\"" << input_type << "\" class=\"" << classes << "\" style=\"" << style << "\"" << id_attr << " placeholder=\"" << placeholder << "\">\n";
            ResetStyles(); continue;
        }
    }
    
    html_content << "</div>\n</body>\n</html>";
    file.close();
    return html_content.str();
}

void GrassWeb_LoadPage(const string& filename) {
    WSADATA wsaData; 
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
    
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in server_addr; 
    server_addr.sin_family = AF_INET; 
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    server_addr.sin_port = htons(8080);
    
    if (bind(server_socket, (SOCKADDR*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) { 
        closesocket(server_socket); 
        WSACleanup(); 
        return; 
    }
    
    listen(server_socket, SOMAXCONN);
    cout << "\n[GrassServer]: RUNNING -> http://127.0.0.1:8080/\n" << endl;
    ShellExecuteA(NULL, "open", "http://127.0.0.1:8080/", NULL, NULL, SW_SHOWNORMAL);
    
    while (true) {
        SOCKET client_socket = accept(server_socket, NULL, NULL); 
        if (client_socket == INVALID_SOCKET) continue;
        
        char buffer[2048] = {0}; 
        recv(client_socket, buffer, 2048, 0); 
        string request(buffer);
        
        if (request.find("GET ") == 0) {
            size_t start = 4; 
            size_t end = request.find(" ", start); 
            string path = request.substr(start, end - start);
            
            if (path == "/") path = "/" + filename;
            string target_file = path.substr(1);
            
            if (target_file != "favicon.ico") {
                string html_body = CompileGrassToHTML(target_file);
                
                string http_response = "HTTP/1.1 200 OK\r\n"
                                       "Content-Type: text/html; charset=UTF-8\r\n"
                                       "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                                       "Connection: close\r\n\r\n" + html_body;
                                       
                send(client_socket, http_response.c_str(), http_response.length(), 0);
            }
        }
        closesocket(client_socket);
    }
    closesocket(server_socket); 
    WSACleanup();
}
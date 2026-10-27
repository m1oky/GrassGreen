#include <winsock2.h> 
#include <windows.h>
#include "GrassWeb.h"
#include "WebTheme.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <map>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

static string TrimStr(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static string StripQuotesWeb(string s) {
    s = TrimStr(s);
    if (s.size() >= 4 && s.substr(0, 2) == "!!" && s.substr(s.size() - 2) == "!!") {
        return s.substr(2, s.size() - 4);
    }
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

static string ExtractArg(const string& line, const string& cmd = "") {
    size_t start = line.find('(');
    size_t end = line.rfind(')');
    if (start != string::npos && end != string::npos && end > start) {
        return TrimStr(line.substr(start + 1, end - start - 1));
    }
    return "";
}

static void ExtractTwoArgs(const string& line, string& arg1, string& arg2) {
    string content = ExtractArg(line);
    size_t comma = content.find(',');
    if (comma != string::npos) {
        arg1 = TrimStr(content.substr(0, comma));
        arg2 = TrimStr(content.substr(comma + 1));
    } else {
        arg1 = TrimStr(content);
        arg2 = "";
    }
}

static void ExtractThreeArgs(const string& line, string& arg1, string& arg2, string& arg3) {
    string content = ExtractArg(line);
    stringstream ss(content);
    getline(ss, arg1, ',');
    getline(ss, arg2, ',');
    getline(ss, arg3, ',');
    arg1 = TrimStr(arg1);
    arg2 = TrimStr(arg2);
    arg3 = TrimStr(arg3);
}

// Встроенный словарь векторных иконок для команды icon(название)
static string GetIconSymbol(const string& name) {
    static const map<string, string> icons = {
        {"search", "🔍"}, {"settings", "⚙️"}, {"user", "👤"}, {"home", "🏠"},
        {"star", "⭐"}, {"heart", "❤️"}, {"check", "✔️"}, {"close", "✖️"},
        {"play", "▶️"}, {"lock", "🔒"}, {"key", "🔑"}, {"fire", "🔥"},
        {"bolt", "⚡"}, {"code", "💻"}, {"download", "⬇️"}, {"bell", "🔔"}
    };
    string key = StripQuotesWeb(name);
    if (icons.count(key)) return icons.at(key);
    return key;
}

// Замена val(id), is-checked(id) и load(key) внутри JS-выражений и условий
static string TranslateJSExpressions(string expr) {
    // 1. Замена всех вхождений val(id) или val Hoy(id)
    while (true) {
        size_t pos = expr.find("val Hoy(");
        size_t len_prefix = 8;
        if (pos == string::npos) {
            pos = expr.find("val(");
            len_prefix = 4;
        }
        if (pos == string::npos) break;
        size_t close_p = expr.find(')', pos + len_prefix);
        if (close_p == string::npos) break;
        string id = StripQuotesWeb(expr.substr(pos + len_prefix, close_p - (pos + len_prefix)));
        string repl = "document.getElementById('" + id + "').value";
        expr.replace(pos, close_p - pos + 1, repl);
    }

    // 2. Замена is-checked(id) для проверки чекбоксов
    while (true) {
        size_t pos = expr.find("is-checked(");
        if (pos == string::npos) break;
        size_t close_p = expr.find(')', pos + 11);
        if (close_p == string::npos) break;
        string id = StripQuotesWeb(expr.substr(pos + 11, close_p - (pos + 11)));
        string repl = "document.getElementById('" + id + "').checked";
        expr.replace(pos, close_p - pos + 1, repl);
    }

    // 3. Замена load(key) для чтения из localStorage
    while (true) {
        size_t pos = expr.find("load(");
        if (pos == string::npos) break;
        size_t close_p = expr.find(')', pos + 5);
        if (close_p == string::npos) break;
        string key = StripQuotesWeb(expr.substr(pos + 5, close_p - (pos + 5)));
        string repl = "(localStorage.getItem('" + key + "') || '')";
        expr.replace(pos, close_p - pos + 1, repl);
    }

    return expr;
}

// Преобразование строки с поддержкой {переменная} в шаблонную строку JS
static string FormatJSTextValue(string raw) {
    raw = TrimStr(raw);
    if (raw.find("val(") == 0 || raw.find("load(") == 0) {
        return TranslateJSExpressions(raw);
    }
    raw = StripQuotesWeb(raw);
    // Поддержка вставки переменных через {var_name}
    size_t open_b = raw.find('{');
    while (open_b != string::npos) {
        size_t close_b = raw.find('}', open_b);
        if (close_b != string::npos) {
            raw.insert(open_b, "$");
            open_b = raw.find('{', close_b + 2);
        } else break;
    }
    return "`" + raw + "`";
}

string CompileGrassToHTML(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) return "<h1>404 Error: Файл " + filename + " не найден!</h1>";

    stringstream head_links;
    stringstream html_content;

    string line;
    string current_color = "#ffffff", current_bg = "", current_radius = "8";
    string current_grad1 = "", current_grad2 = "", current_border_sz = "", current_border_col = "";
    string current_w = "", current_h = "", neon_color = "", anim_type = "", anim_speed = "";
    string flip_target = "", current_id = "";
    string current_font = "", current_align = "", current_opacity = "", current_blur = "";
    string shadow_blur = "", shadow_alpha = "";
    string pad_in = "", margin_out_x = "", margin_out_y = "";
    string pos_x = "", pos_y = "", current_icon = "";
    string glass_blur = "12", glass_alpha = "0.05";

    bool in_script = false;
    string current_js_target = "";

    bool is_bold = false, is_glass = false, is_neon = false, is_circle = false, is_hidden = false, anim_loop = true;

    auto ResetStyles = [&]() {
        current_color = "#ffffff"; current_bg = ""; current_radius = "8";
        current_grad1 = ""; current_grad2 = ""; current_border_sz = ""; current_border_col = "";
        current_w = ""; current_h = ""; neon_color = ""; anim_type = ""; anim_speed = "";
        flip_target = ""; current_id = "";
        current_font = ""; current_align = ""; current_opacity = ""; current_blur = "";
        shadow_blur = ""; shadow_alpha = "";
        pad_in = ""; margin_out_x = ""; margin_out_y = "";
        pos_x = ""; pos_y = ""; current_icon = "";
        glass_blur = "12"; glass_alpha = "0.05";
        is_bold = false; is_glass = false; is_neon = false; is_circle = false; is_hidden = false; anim_loop = true;
    };

    // Сборка общей строки инлайн-стилей для любого элемента
    auto BuildCommonStyle = [&](bool include_text_color = true) -> string {
        string st = "";
        if (include_text_color && !is_neon && !current_color.empty()) st += "color: " + current_color + "; ";
        if (!current_bg.empty()) st += "background-color: " + current_bg + "; ";
        if (!current_grad1.empty() && !current_grad2.empty()) {
            st += "background: linear-gradient(135deg, " + current_grad1 + ", " + current_grad2 + "); ";
        }
        if (!current_radius.empty() && current_radius != "0") st += "border-radius: " + current_radius + "px; ";
        if (!current_w.empty()) st += "width: " + current_w + "px; ";
        if (!current_h.empty()) st += "height: " + current_h + "px; ";
        if (!current_border_sz.empty() && !current_border_col.empty()) {
            st += "border: " + current_border_sz + "px solid " + current_border_col + "; ";
        }
        if (!current_font.empty()) st += "font-family: '" + StripQuotesWeb(current_font) + "', sans-serif; ";
        if (!current_align.empty()) st += "text-align: " + current_align + "; ";
        if (!current_opacity.empty()) st += "opacity: " + current_opacity + "; ";
        if (!current_blur.empty()) st += "filter: blur(" + current_blur + "px); ";
        if (!shadow_blur.empty()) {
            string a = shadow_alpha.empty() ? "0.6" : shadow_alpha;
            st += "box-shadow: 0 10px " + shadow_blur + "px rgba(0, 0, 0, " + a + "); ";
        }
        if (!pad_in.empty()) st += "padding: " + pad_in + "px; ";
        if (!margin_out_x.empty() && !margin_out_y.empty()) {
            st += "margin: " + margin_out_y + "px " + margin_out_x + "px; ";
        } else if (!margin_out_x.empty()) {
            st += "margin: " + margin_out_x + "px; ";
        }
        if (!pos_x.empty() && !pos_y.empty()) {
            st += "position: absolute; left: " + pos_x + "px; top: " + pos_y + "px; ";
        }
        if (is_glass) {
            st += "background: rgba(255, 255, 255, " + glass_alpha + ") !important; ";
            st += "backdrop-filter: blur(" + glass_blur + "px); -webkit-backdrop-filter: blur(" + glass_blur + "px); ";
        }
        if (is_neon) {
            st += "--neon-color: " + neon_color + "; ";
        }
        if (!anim_type.empty()) {
            string spd = anim_speed.empty() ? "1.5s" : anim_speed;
            if (spd.back() != 's') spd += "s";
            st += "animation: anim-" + anim_type + " " + spd + (anim_loop ? " infinite" : " 1 forwards") + " ease-in-out; ";
        }
        if (is_hidden) st += "display: none; ";
        return st;
    };

    while (getline(file, line)) {
        string trimmed = TrimStr(line);
        if (trimmed.empty() || trimmed.find("**") == 0 || trimmed.find("//") == 0 ||
            trimmed.find("!GREEN_WWW1!") != string::npos || trimmed.find("!GrassWeb!") != string::npos) {
            continue;
        }

        // ==========================================
        // 1. БЛОК JAVASCRIPT ЛОГИКИ (SCRIPT)
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
            // Выбор активного объекта: id(имя):
            if (trimmed.find("id(") == 0 && trimmed.find("):") != string::npos) {
                size_t p_end = trimmed.find("):");
                current_js_target = StripQuotesWeb(trimmed.substr(3, p_end - 3));
                continue;
            }

            // Клик: on-click, on-click(id) или on_click(id)
            if (trimmed == "on-click" || trimmed.find("on-click(") == 0 || trimmed.find("on_click(") == 0) {
                string target = ExtractArg(trimmed);
                if (target.empty()) target = current_js_target;
                html_content << "document.getElementById('" << StripQuotesWeb(target) << "')?.addEventListener('click', function() {\n";
                if (!target.empty()) current_js_target = StripQuotesWeb(target);
                continue;
            }
            if (trimmed == "end-click") { html_content << "});\n"; continue; }

            // Ввод текста или ползунка: on-input(id)
            if (trimmed.find("on-input(") == 0 || trimmed.find("on_input(") == 0) {
                string target = ExtractArg(trimmed);
                if (target.empty()) target = current_js_target;
                html_content << "document.getElementById('" << StripQuotesWeb(target) << "')?.addEventListener('input', function() {\n";
                current_js_target = StripQuotesWeb(target);
                continue;
            }
            if (trimmed == "end-input") { html_content << "});\n"; continue; }

            // Наведение мыши: on-hover(id)
            if (trimmed.find("on-hover(") == 0) {
                string target = ExtractArg(trimmed);
                if (target.empty()) target = current_js_target;
                html_content << "document.getElementById('" << StripQuotesWeb(target) << "')?.addEventListener('mouseenter', function() {\n";
                current_js_target = StripQuotesWeb(target);
                continue;
            }
            if (trimmed == "end-hover") { html_content << "});\n"; continue; }

            // Нажатие клавиши: on-key(Enter)
            if (trimmed.find("on-key(") == 0) {
                string key_name = StripQuotesWeb(ExtractArg(trimmed));
                html_content << "document.addEventListener('keydown', function(e) { if (e.key === '" << key_name << "') {\n";
                continue;
            }
            if (trimmed == "end-key") { html_content << "}});\n"; continue; }

            // Таймер-цикл: every(1000) ... end-every
            if (trimmed.find("every(") == 0) {
                string ms = ExtractArg(trimmed);
                html_content << "setInterval(function() {\n";
                html_content << "  const __interval_ms = " << (ms.empty() ? "1000" : ms) << ";\n";
                continue;
            }
            if (trimmed.find("end-every") == 0) {
                string ms = ExtractArg(trimmed);
                html_content << "}, " << (ms.empty() ? "1000" : ms) << ");\n";
                continue;
            }

            // Задержка: wait(500) ... end-wait
            if (trimmed.find("wait(") == 0) {
                string ms = ExtractArg(trimmed);
                html_content << "setTimeout(function() {\n";
                continue;
            }
            if (trimmed.find("end-wait") == 0) {
                string ms = ExtractArg(trimmed);
                html_content << "}, " << (ms.empty() ? "500" : ms) << ");\n";
                continue;
            }

            // Условие chek(...)
            if (trimmed.find("chek(") == 0) {
                string condition = TranslateJSExpressions(ExtractArg(trimmed));
                html_content << "  if (" << condition << ") {\n";
                continue;
            }
            if (trimmed == "else") { html_content << "  } else {\n"; continue; }
            if (trimmed == "end-chek") { html_content << "  }\n"; continue; }

            // Создание/изменение переменной: var(score = 0)
            if (trimmed.find("var(") == 0) {
                string expr = TranslateJSExpressions(ExtractArg(trimmed));
                size_t eq = expr.find('=');
                if (eq != string::npos) {
                    string vname = TrimStr(expr.substr(0, eq));
                    string vval = TrimStr(expr.substr(eq + 1));
                    html_content << "  window." << vname << " = " << vval << ";\n";
                }
                continue;
            }

            // Увеличение числа в переменной: add-var(score, 1)
            if (trimmed.find("add-var(") == 0) {
                string vname, delta;
                ExtractTwoArgs(trimmed, vname, delta);
                html_content << "  window." << vname << " = (Number(window." << vname << ") || 0) + (" << TranslateJSExpressions(delta) << ");\n";
                continue;
            }

            // Изменение текста элемента: set-text(id, текст) или set-text(текст)
            if (trimmed.find("set-text(") == 0) {
                string a1, a2;
                ExtractTwoArgs(trimmed, a1, a2);
                string target = a2.empty() ? current_js_target : StripQuotesWeb(a1);
                string txt_expr = a2.empty() ? a1 : a2;
                html_content << "  if(document.getElementById('" << target << "')) document.getElementById('" << target << "').innerText = " << FormatJSTextValue(txt_expr) << ";\n";
                continue;
            }

            // Изменение значения поля ввода: set-val(id, значение)
            if (trimmed.find("set-val(") == 0) {
                string a1, a2;
                ExtractTwoArgs(trimmed, a1, a2);
                string target = a2.empty() ? current_js_target : StripQuotesWeb(a1);
                string val_expr = a2.empty() ? a1 : a2;
                html_content << "  if(document.getElementById('" << target << "')) document.getElementById('" << target << "').value = " << FormatJSTextValue(val_expr) << ";\n";
                continue;
            }

            // Очистка поля ввода: clear-val(id)
            if (trimmed.find("clear-val(") == 0) {
                string target = ExtractArg(trimmed);
                if (target.empty()) target = current_js_target;
                html_content << "  if(document.getElementById('" << StripQuotesWeb(target) << "')) document.getElementById('" << StripQuotesWeb(target) << "').value = '';\n";
                continue;
            }

            // Перекраска текста и неона: repaint-color(цвет) или repaint-color(id, цвет)
            if (trimmed.find("repaint-color(") == 0) {
                string a1, a2;
                ExtractTwoArgs(trimmed, a1, a2);
                string target = a2.empty() ? current_js_target : StripQuotesWeb(a1);
                string col = StripQuotesWeb(a2.empty() ? a1 : a2);
                html_content << "  if(document.getElementById('" << target << "')) {\n";
                html_content << "    document.getElementById('" << target << "').style.color = '" << col << "';\n";
                html_content << "    document.getElementById('" << target << "').style.setProperty('--neon-color', '" << col << "');\n";
                html_content << "  }\n";
                continue;
            }

            // Перекраска фона: repaint-bg(цвет) или repaint-bg(id, цвет)
            if (trimmed.find("repaint-bg(") == 0) {
                string a1, a2;
                ExtractTwoArgs(trimmed, a1, a2);
                string target = a2.empty() ? current_js_target : StripQuotesWeb(a1);
                string col = StripQuotesWeb(a2.empty() ? a1 : a2);
                if (target == "body" || target.empty()) {
                    html_content << "  document.body.style.backgroundColor = '" << col << "';\n";
                } else {
                    html_content << "  if(document.getElementById('" << target << "')) document.getElementById('" << target << "').style.background = '" << col << "';\n";
                }
                continue;
            }

            // Скрытие / Показ / Переключение: hide(id), show(id), toggle(id)
            if (trimmed.find("hide(") == 0) {
                string t = ExtractArg(trimmed);
                if (t.empty()) t = current_js_target;
                html_content << "  if(document.getElementById('" << StripQuotesWeb(t) << "')) document.getElementById('" << StripQuotesWeb(t) << "').style.display = 'none';\n";
                continue;
            }
            if (trimmed.find("show(") == 0) {
                string t = ExtractArg(trimmed);
                if (t.empty()) t = current_js_target;
                html_content << "  if(document.getElementById('" << StripQuotesWeb(t) << "')) document.getElementById('" << StripQuotesWeb(t) << "').style.display = '';\n";
                continue;
            }
            if (trimmed.find("toggle(") == 0) {
                string t = ExtractArg(trimmed);
                if (t.empty()) t = current_js_target;
                t = StripQuotesWeb(t);
                html_content << "  if(document.getElementById('" << t << "')) {\n";
                html_content << "    const el = document.getElementById('" << t << "');\n";
                html_content << "    el.style.display = (el.style.display === 'none') ? '' : 'none';\n";
                html_content << "  }\n";
                continue;
            }

            // Смена картинки: set-img(id, url)
            if (trimmed.find("set-img(") == 0) {
                string a1, a2;
                ExtractTwoArgs(trimmed, a1, a2);
                string target = a2.empty() ? current_js_target : StripQuotesWeb(a1);
                string url = StripQuotesWeb(a2.empty() ? a1 : a2);
                html_content << "  if(document.getElementById('" << target << "')) document.getElementById('" << target << "').src = '" << url << "';\n";
                continue;
            }

            // Запуск анимации из JS: play-anim(id, shake, 0.5s)
            if (trimmed.find("play-anim(") == 0) {
                string target, atype, aspd;
                ExtractThreeArgs(trimmed, target, atype, aspd);
                if (aspd.empty()) { aspd = atype; atype = target; target = current_js_target; }
                if (aspd.empty()) aspd = "0.6s";
                if (aspd.back() != 's') aspd += "s";
                html_content << "  if(document.getElementById('" << StripQuotesWeb(target) << "')) {\n";
                html_content << "    const el = document.getElementById('" << StripQuotesWeb(target) << "');\n";
                html_content << "    el.style.animation = 'none'; el.offsetHeight;\n";
                html_content << "    el.style.animation = 'anim-" << StripQuotesWeb(atype) << " " << aspd << " 1 ease-in-out';\n";
                html_content << "  }\n";
                continue;
            }

            // Сдвиг элемента: move(id, x, y)
            if (trimmed.find("move(") == 0) {
                string target, mx, my;
                ExtractThreeArgs(trimmed, target, mx, my);
                html_content << "  if(document.getElementById('" << StripQuotesWeb(target) << "')) {\n";
                html_content << "    const el = document.getElementById('" << StripQuotesWeb(target) << "');\n";
                html_content << "    el.style.position = 'absolute'; el.style.left = '" << mx << "px'; el.style.top = '" << my << "px';\n";
                html_content << "  }\n";
                continue;
            }

            // Всплывающее окно и консоль: alert(), log()
            if (trimmed.find("alert(") == 0) {
                html_content << "  alert(" << FormatJSTextValue(ExtractArg(trimmed)) << ");\n";
                continue;
            }
            if (trimmed.find("log(") == 0) {
                html_content << "  console.log(" << FormatJSTextValue(ExtractArg(trimmed)) << ");\n";
                continue;
            }

            // Память браузера: save(key, val)
            if (trimmed.find("save(") == 0) {
                string k, v;
                ExtractTwoArgs(trimmed, k, v);
                html_content << "  localStorage.setItem('" << StripQuotesWeb(k) << "', " << FormatJSTextValue(v) << ");\n";
                continue;
            }

            // Навигация и звук: go(url), reload(), sound(url)
            if (trimmed.find("go(") == 0) {
                string u = StripQuotesWeb(ExtractArg(trimmed));
                html_content << "  window.location.href = '" << u << "';\n";
                continue;
            }
            if (trimmed == "reload" || trimmed == "reload()") {
                html_content << "  window.location.reload();\n";
                continue;
            }
            if (trimmed.find("sound(") == 0) {
                string u = StripQuotesWeb(ExtractArg(trimmed));
                html_content << "  new Audio('" << u << "').play();\n";
                continue;
            }

            continue;
        }

        // ==========================================
        // 2. ПАРСЕР HTML / CSS РАЗМЕТКИ (WEB.txt)
        // ==========================================

        // Подключение внешних шаблонов/стилей: con = (ссылка)
        if (trimmed.find("con") == 0 && trimmed.find('=') != string::npos) {
            string url = ExtractArg(trimmed);
            if (url.empty()) url = TrimStr(trimmed.substr(trimmed.find('=') + 1));
            url = StripQuotesWeb(url);
            head_links << "<link rel=\"stylesheet\" href=\"" << url << "\">\n";
            continue;
        }

        // Именование элемента через <name>/имя или id(имя)
        if (trimmed.find("<name>/") == 0) {
            current_id = TrimStr(trimmed.substr(7));
            continue;
        }
        if (trimmed.find("id(") == 0) { current_id = StripQuotesWeb(ExtractArg(trimmed)); continue; }

        // Контейнеры (поддерживают предварительную стилизацию!)
        if (trimmed == "row-start") {
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<div class=\"grass-row\" style=\"" << BuildCommonStyle(false) << "\"" << id_attr << ">\n";
            ResetStyles(); continue;
        }
        if (trimmed == "row-end") { html_content << "</div>\n"; continue; }

        if (trimmed == "col-start") {
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<div class=\"grass-col\" style=\"" << BuildCommonStyle(false) << "\"" << id_attr << ">\n";
            ResetStyles(); continue;
        }
        if (trimmed == "col-end") { html_content << "</div>\n"; continue; }

        if (trimmed == "box-start") {
            string classes = "grass-box ";
            if (is_glass) classes += "glass ";
            if (is_neon) classes += "neon ";
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<div class=\"" << classes << "\" style=\"" << BuildCommonStyle(true) << "\"" << id_attr << ">\n";
            ResetStyles(); continue;
        }
        if (trimmed == "box-end") { html_content << "</div>\n"; continue; }

        // Модификаторы стилей из WEB.txt
        if (trimmed.find("color(") == 0) { current_color = StripQuotesWeb(ExtractArg(trimmed)); continue; }
        if (trimmed.find("bg(") == 0) { current_bg = StripQuotesWeb(ExtractArg(trimmed)); continue; }
        if (trimmed.find("round-ob(") == 0) { current_radius = ExtractArg(trimmed); continue; }
        if (trimmed.find("grad(") == 0) { ExtractTwoArgs(trimmed, current_grad1, current_grad2); continue; }
        if (trimmed.find("size(") == 0) { ExtractTwoArgs(trimmed, current_w, current_h); continue; }
        if (trimmed.find("border(") == 0) { ExtractTwoArgs(trimmed, current_border_sz, current_border_col); continue; }
        if (trimmed.find("shadowbox(") == 0) { ExtractTwoArgs(trimmed, shadow_blur, shadow_alpha); continue; }
        if (trimmed.find("font-text(") == 0 || trimmed.find("font-fam(") == 0) { current_font = ExtractArg(trimmed); continue; }
        if (trimmed.find("space-out(") == 0) { ExtractTwoArgs(trimmed, margin_out_x, margin_out_y); continue; }
        if (trimmed.find("space(") == 0) { ExtractTwoArgs(trimmed, pad_in, margin_out_x); continue; }
        if (trimmed.find("opacity(") == 0) { current_opacity = ExtractArg(trimmed); continue; }
        if (trimmed.find("blur(") == 0) { current_blur = ExtractArg(trimmed); continue; }
        if (trimmed.find("align(") == 0) { current_align = StripQuotesWeb(ExtractArg(trimmed)); continue; }
        if (trimmed.find("position(") == 0) { ExtractTwoArgs(trimmed, pos_x, pos_y); continue; }
        if (trimmed.find("icon(") == 0) { current_icon = GetIconSymbol(ExtractArg(trimmed)); continue; }
        if (trimmed == "hide()" || trimmed == "hide") { is_hidden = true; continue; }
        if (trimmed.find("thick()") != string::npos || trimmed == "thick") { is_bold = true; continue; }
        if (trimmed.find("glass(") == 0) {
            is_glass = true;
            string b, a;
            ExtractTwoArgs(trimmed, b, a);
            if (!b.empty()) glass_blur = b;
            if (!a.empty()) glass_alpha = a;
            continue;
        }
        if (trimmed.find("circle()") != string::npos || trimmed == "circle") { is_circle = true; continue; }
        if (trimmed.find("neon(") == 0) { is_neon = true; neon_color = StripQuotesWeb(ExtractArg(trimmed)); continue; }
        if (trimmed.find("animate(") == 0) { ExtractTwoArgs(trimmed, anim_type, anim_speed); continue; }
        if (trimmed == "stop" || trimmed == "stop()") { anim_loop = false; continue; }
        if (trimmed.find("flip(") == 0) { flip_target = StripQuotesWeb(ExtractArg(trimmed)); continue; }

        // Фоновое видео: video-bg(ссылка)
        if (trimmed.find("video-bg(") == 0) {
            string video_url = StripQuotesWeb(ExtractArg(trimmed));
            html_content << "<video class=\"bg-video\" autoplay loop muted playsinline><source src=\"" << video_url << "\" type=\"video/mp4\"></video>\n";
            continue;
        }

        // Картинка: image(ссылка)
        if (trimmed.find("image(") == 0) {
            string img_url = StripQuotesWeb(ExtractArg(trimmed));
            string classes = "grass-img ";
            if (is_circle) classes += "circle ";
            if (is_neon) classes += "neon ";
            string style = BuildCommonStyle(false);
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<img src=\"" << img_url << "\" class=\"" << classes << "\" style=\"" << style << "\"" << id_attr << " alt=\"image\">\n";
            ResetStyles(); continue;
        }

        // Чекбокс: checkbox = Текст
        if (trimmed.find("checkbox") == 0 && trimmed.find('=') != string::npos) {
            string txt = TrimStr(trimmed.substr(trimmed.find('=') + 1));
            string style = BuildCommonStyle(true);
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<label class=\"grass-checkbox\" style=\"" << style << "\"><input type=\"checkbox\"" << id_attr << "> " << txt << "</label>\n";
            ResetStyles(); continue;
        }

        // Ползунок: slider(min, max, start)
        if (trimmed.find("slider(") == 0) {
            string s_min, s_max, s_start;
            ExtractThreeArgs(trimmed, s_min, s_max, s_start);
            string style = BuildCommonStyle(false);
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<input type=\"range\" min=\"" << s_min << "\" max=\"" << s_max << "\" value=\"" << s_start << "\" class=\"grass-slider\" style=\"" << style << "\"" << id_attr << ">\n";
            ResetStyles(); continue;
        }

        // Выпадающий список: drop-list(item1, item2, item3)
        if (trimmed.find("drop-list(") == 0) {
            string content = ExtractArg(trimmed);
            stringstream ss(content);
            string item;
            string classes = "grass-drop ";
            if (is_glass) classes += "glass ";
            if (is_neon) classes += "neon ";
            string style = BuildCommonStyle(true);
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<select class=\"" << classes << "\" style=\"" << style << "\"" << id_attr << ">\n";
            while (getline(ss, item, ',')) {
                item = TrimStr(item);
                html_content << "  <option value=\"" << item << "\">" << item << "</option>\n";
            }
            html_content << "</select>\n";
            ResetStyles(); continue;
        }

        // Ссылка: link(url) = Текст
        if (trimmed.find("link(") == 0) {
            string url = StripQuotesWeb(ExtractArg(trimmed));
            size_t eq_pos = trimmed.find('=');
            string text = (eq_pos != string::npos) ? TrimStr(trimmed.substr(eq_pos + 1)) : url;
            if (!current_icon.empty()) text = current_icon + " " + text;

            if (url.find("http://") != 0 && url.find("https://") != 0 && !url.empty() && url[0] != '/') {
                url = "/" + url;
            }
            string classes = is_bold ? "thick " : "";
            if (is_neon) classes += "neon ";
            string style = "text-decoration: none; padding: 8px 12px; display: inline-block; " + BuildCommonStyle(true);
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<a href=\"" << url << "\" class=\"" << classes << "\" style=\"" << style << "\"" << id_attr << ">" << text << "</a>\n";
            ResetStyles(); continue;
        }

        // Заголовки и текст G1..G5 (от маленького G1 до огромного G5, либо классические h1..h5)
        if ((trimmed.size() >= 3 && trimmed[0] == 'G' && trimmed[1] >= '1' && trimmed[1] <= '5' && trimmed[2] == ' ')) {
            int level = trimmed[1] - '0'; // G1..G5
            string text = TrimStr(trimmed.substr(3));
            if (!current_icon.empty()) text = current_icon + " " + text;

            // В WEB.txt G1..G5 идут от маленького к большому: задаём точный размер шрифта
            const int font_sizes[6] = {0, 15, 20, 26, 34, 46};
            string html_tag = "div";
            string classes = is_bold ? "thick " : "";
            if (is_neon) classes += "neon ";

            string style = "font-size: " + to_string(font_sizes[level]) + "px; margin: 8px 0; " + BuildCommonStyle(true);
            if (is_neon) style += "text-shadow: 0 0 12px " + neon_color + "; ";

            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<" << html_tag << " class=\"" << classes << "\" style=\"" << style << "\"" << id_attr << ">" << text << "</" << html_tag << ">\n";
            ResetStyles(); continue;
        }

        // Кнопка: button = Текст
        if (trimmed.find("button") == 0 && trimmed.find('=') != string::npos) {
            string btn_text = TrimStr(trimmed.substr(trimmed.find('=') + 1));
            if (!current_icon.empty()) btn_text = "<span>" + current_icon + "</span> <span>" + btn_text + "</span>";

            string classes = "";
            if (is_glass) classes += "glass ";
            if (is_neon) classes += "neon ";
            if (is_bold) classes += "thick ";
            if (is_circle) classes += "circle ";

            string style = BuildCommonStyle(true);
            string onclick = "";
            if (!flip_target.empty()) {
                string target_url = flip_target;
                if (target_url.find("http") != 0 && target_url[0] != '/') target_url = "/" + target_url;
                onclick = " onclick=\"window.location.href='" + target_url + "'\"";
            }
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<button class=\"" << classes << "\" style=\"" << style << "\"" << onclick << id_attr << ">" << btn_text << "</button>\n";
            ResetStyles(); continue;
        }

        // Поле ввода: enter = Плейсхолдер
        if (trimmed.find("enter") == 0 && trimmed.find('=') != string::npos) {
            string placeholder = TrimStr(trimmed.substr(trimmed.find('=') + 1));
            string classes = "";
            if (is_glass) classes += "glass ";
            if (is_neon) classes += "neon ";
            if (is_bold) classes += "thick ";

            string style = "background: rgba(0,0,0,0.45); border: 1px solid #444; padding: 14px 18px; margin: 10px; font-size: 16px; outline: none; text-align: center; " + BuildCommonStyle(true);
            string input_type = (placeholder.find("пароль") != string::npos || placeholder.find("Пароль") != string::npos || placeholder.find("password") != string::npos) ? "password" : "text";
            string id_attr = !current_id.empty() ? " id=\"" + current_id + "\"" : "";
            html_content << "<input type=\"" << input_type << "\" class=\"" << classes << "\" style=\"" << style << "\"" << id_attr << " placeholder=\"" << placeholder << "\">\n";
            ResetStyles(); continue;
        }
    }

    file.close();

    stringstream full_page;
    full_page << "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"UTF-8\">\n";
    full_page << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    full_page << "<title>GrassGreen Web Engine</title>\n";
    full_page << head_links.str();
    full_page << "<style>\n" << BASE_CSS << "\n</style>\n</head>\n<body>\n";
    full_page << "<div style=\"display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 85vh;\">\n";
    full_page << html_content.str();
    full_page << "</div>\n</body>\n</html>";

    return full_page.str();
}

void GrassWeb_LoadPage(const string& filename) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

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
    cout << "\n[GrassWeb]: Сервер запущен -> http://127.0.0.1:8080/\n" << endl;
    ShellExecuteA(NULL, "open", "http://127.0.0.1:8080/", NULL, NULL, SW_SHOWNORMAL);

    while (true) {
        SOCKET client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) continue;

        char buffer[4096] = {0};
        recv(client_socket, buffer, 4096, 0);
        string request(buffer);

        if (request.find("GET ") == 0) {
            size_t start = 4;
            size_t end = request.find(" ", start);
            string path = request.substr(start, end - start);

            if (path == "/") path = "/" + filename;
            string target_file = path.substr(1);

            if (target_file != "favicon.ico") {
                string html_body = CompileGrassToHTML(target_file);
                string http_response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n"
                    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                    "Connection: close\r\n\r\n" + html_body;
                send(client_socket, http_response.c_str(), (int)http_response.length(), 0);
            }
        }
        closesocket(client_socket);
    }
    closesocket(server_socket);
    WSACleanup();
}
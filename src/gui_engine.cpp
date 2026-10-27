#include "gui_engine.h"
#include <commctrl.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <urlmon.h>
#include <gdiplus.h>
#include <mmsystem.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")

using namespace std;
using namespace Gdiplus;

// Внешний глобальный кэш переменных
extern map<string, wstring> variables_cache;

namespace GrassGUI {

    wstring window_title = L"GrassGreen UI";
    int win_w = 640, win_h = 480;
    COLORREF bg_color = RGB(18, 19, 26);
    bool window_is_glass = false;

    wstring bg_image_path = L"";
    wstring app_icon_path = L"";
    Gdiplus::Image* pBgImage = NULL;

    vector<UIElement> elements;
    map<string, string> input_values_utf8;

    int last_idx = -1;
    bool trigger_show = false;
    string pending_switch_file = "";

    WNDPROC OldEditProc = NULL;
    WNDPROC OldButtonProc = NULL;

    // --- Вспомогательные функции ---

    void cleanParentheses(string& str) {
        string res;
        for (char c : str) if (c != '(' && c != ')') res += c;
        str = res;
    }

    void cleanCarriageReturn(string& str) {
        if (!str.empty() && str.back() == '\r') str.pop_back();
    }

    string trim(const string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    string stripQuotes(string val) {
        val = trim(val);
        if (val.length() >= 4 && val.substr(0, 2) == "!!" && val.substr(val.length() - 2) == "!!") {
            return val.substr(2, val.length() - 4);
        }
        if (val.length() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            return val.substr(1, val.length() - 2);
        }
        return val;
    }

    wstring utf8_to_wstring(const string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        if (len <= 0) return L"";
        wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
        if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
        return wstr;
    }

    string wstring_to_utf8(const wstring& wstr) {
        if (wstr.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (len <= 0) return "";
        string str(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
        if (!str.empty() && str.back() == '\0') str.pop_back();
        return str;
    }

    void unpack_cached_vars(wstring& text) {
        size_t start = text.find(L"{");
        while (start != wstring::npos) {
            size_t end = text.find(L"}", start);
            if (end != wstring::npos) {
                wstring var_name = text.substr(start + 1, end - start - 1);
                string s_name = wstring_to_utf8(var_name);
                if (variables_cache.count(s_name)) {
                    text.replace(start, end - start + 1, variables_cache[s_name]);
                    start = text.find(L"{");
                    continue;
                }
            }
            break;
        }
    }

    COLORREF hex_to_rgb(string hex) {
        hex = stripQuotes(hex);
        if (hex.empty()) return RGB(18, 19, 26);
        if (hex[0] == '#') hex = hex.substr(1);
        if (hex.length() != 6) return RGB(18, 19, 26);
        int r = 18, g = 19, b = 26;
        stringstream ss1(hex.substr(0, 2)); ss1 >> std::hex >> r;
        stringstream ss2(hex.substr(2, 2)); ss2 >> std::hex >> g;
        stringstream ss3(hex.substr(4, 2)); ss3 >> std::hex >> b;
        return RGB(r, g, b);
    }

    void parse_two_args_fixed(string str, int& x, int& y) {
        cleanParentheses(str);
        for (char& c : str) if (c == ',') c = ' ';
        stringstream parser(str);
        parser >> x >> y;
    }

    int find_target_index(string arg) {
        cleanParentheses(arg);
        arg = stripQuotes(arg);
        if (arg.empty()) return last_idx;
        for (size_t i = 0; i < elements.size(); i++) {
            if (elements[i].id == arg) return (int)i;
        }
        return last_idx;
    }

    void MakeRoundedRectPath(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL radius) {
        if (radius <= 0.5f) {
            path.AddRectangle(RectF(x, y, w, h));
            return;
        }
        REAL d = radius * 2.0f;
        if (d > w) d = w;
        if (d > h) d = h;
        path.AddArc(x, y, d, d, 180, 90);
        path.AddArc(x + w - d, y, d, d, 270, 90);
        path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        path.AddArc(x, y + h - d, d, d, 90, 90);
        path.CloseFigure();
    }

    // --- Обработчики оконных сообщений для элементов ---

    LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp) {
        switch (uMsg) {
            case WM_MOUSEMOVE: {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                for (auto& elem : elements) {
                    if (elem.hwnd == hwnd && !elem.is_hovered) {
                        elem.is_hovered = true;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                break;
            }
            case WM_MOUSELEAVE: {
                for (auto& elem : elements) {
                    if (elem.hwnd == hwnd && elem.is_hovered) {
                        elem.is_hovered = false;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                break;
            }
        }
        return CallWindowProc(OldButtonProc, hwnd, uMsg, wp, lp);
    }

    LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp) {
        switch (uMsg) {
            case WM_SETFOCUS: {
                for (auto& elem : elements) {
                    if (elem.hwnd == hwnd && elem.is_placeholder) {
                        SetWindowTextW(hwnd, L"");
                        elem.is_placeholder = false;
                        PostMessage(hwnd, EM_SETSEL, 0, 0);
                    }
                }
                break;
            }
            case WM_KILLFOCUS: {
                wchar_t buf[512];
                GetWindowTextW(hwnd, buf, 512);
                for (auto& elem : elements) {
                    if (elem.hwnd == hwnd) {
                        if (wcslen(buf) == 0) {
                            elem.is_placeholder = true;
                            SetWindowTextW(hwnd, elem.text.empty() ? L"Введите текст..." : elem.text.c_str());
                        } else if (!elem.id.empty()) {
                            input_values_utf8[elem.id] = wstring_to_utf8(buf);
                        }
                    }
                }
                break;
            }
        }
        return CallWindowProc(OldEditProc, hwnd, uMsg, wp, lp);
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp) {
        switch (uMsg) {
            case WM_TIMER: {
                if (wp == 1) {
                    bool still_animating = false;
                    for (auto& elem : elements) {
                        if (elem.animated && elem.alpha < 255) {
                            elem.alpha += 30; // Плавное 60 FPS появление без лагов
                            if (elem.alpha >= 255) {
                                elem.alpha = 255;
                            } else {
                                still_animating = true;
                            }
                            if (elem.hwnd) InvalidateRect(elem.hwnd, NULL, FALSE);
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (!still_animating) {
                        KillTimer(hwnd, 1); // Выключаем таймер сразу, чтобы не жрать FPS
                    }
                }
                return 0;
            }
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wp;
                SetBkMode(hdc, TRANSPARENT);
                return (INT_PTR)GetStockObject(NULL_BRUSH);
            }
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wp;
                HWND child = (HWND)lp;
                for (auto& elem : elements) {
                    if (elem.hwnd == child) {
                        if (elem.is_placeholder) SetTextColor(hdc, RGB(120, 120, 130));
                        else SetTextColor(hdc, elem.txt_color);
                    }
                }
                SetBkColor(hdc, RGB(25, 25, 33));
                static HBRUSH editBg = CreateSolidBrush(RGB(25, 25, 33));
                return (INT_PTR)editBg;
            }
            case WM_COMMAND: {
                if (HIWORD(wp) == BN_CLICKED) {
                    HWND clickedHwnd = (HWND)lp;
                    for (auto& elem : elements) {
                        if (elem.hwnd == clickedHwnd) {
                            // Запуск программы
                            if (elem.has_run_action && !elem.run_target.empty()) {
                                WinExec(elem.run_target.c_str(), SW_SHOWNORMAL);
                            }
                            // Перенос текста из поля в метку
                            if (elem.has_click_action) {
                                wstring captured = L"";
                                for (auto& in_el : elements) {
                                    if (in_el.type == "enter" && (in_el.id == elem.target_input_id || elem.target_input_id.empty())) {
                                        wchar_t buf[512] = {0};
                                        if (!in_el.is_placeholder) GetWindowTextW(in_el.hwnd, buf, 512);
                                        captured = buf;
                                        if (!in_el.id.empty()) input_values_utf8[in_el.id] = wstring_to_utf8(captured);
                                        if (elem.trigger_clear_on_click) {
                                            SetWindowTextW(in_el.hwnd, L"");
                                        }
                                        break;
                                    }
                                }
                                for (auto& lbl_el : elements) {
                                    if (lbl_el.type == "label" && (lbl_el.id == elem.target_label_id || elem.target_label_id == lbl_el.id)) {
                                        if (!captured.empty()) lbl_el.text = captured;
                                    }
                                }
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                            // Очистка ввода
                            if (elem.trigger_clear_on_click && !elem.has_click_action) {
                                for (auto& in_el : elements) {
                                    if (in_el.type == "enter" && in_el.hwnd) {
                                        SetWindowTextW(in_el.hwnd, L"");
                                        in_el.is_placeholder = true;
                                        SetWindowTextW(in_el.hwnd, in_el.text.empty() ? L"Введите текст..." : in_el.text.c_str());
                                    }
                                }
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                            // Переключение экрана
                            if (elem.has_switch_event && !elem.switch_target.empty()) {
                                pending_switch_file = elem.switch_target;
                                DestroyWindow(hwnd);
                                return 0;
                            }
                        }
                    }
                }
                break;
            }
            case WM_DRAWITEM: {
                LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lp;
                if (pDIS->CtlType == ODT_BUTTON) {
                    Graphics graphics(pDIS->hDC);
                    graphics.SetSmoothingMode(SmoothingMode::SmoothingModeAntiAlias);
                    graphics.SetTextRenderingHint(TextRenderingHint::TextRenderingHintClearTypeGridFit);

                    int w = pDIS->rcItem.right - pDIS->rcItem.left;
                    int h = pDIS->rcItem.bottom - pDIS->rcItem.top;

                    for (auto& elem : elements) {
                        if (elem.hwnd == pDIS->hwndItem) {
                            BYTE r = GetRValue(elem.txt_color);
                            BYTE g = GetGValue(elem.txt_color);
                            BYTE b = GetBValue(elem.txt_color);

                            BYTE br = GetRValue(elem.btn_color);
                            BYTE bg_c = GetGValue(elem.btn_color);
                            BYTE bb = GetBValue(elem.btn_color);

                            BYTE alpha_val = elem.is_glass ? 120 : (BYTE)elem.alpha;

                            Color btnColor(alpha_val, br, bg_c, bb);
                            Color lineColor(alpha_val, r, g, b);
                            Color textColor(alpha_val, 255, 255, 255);

                            if (elem.is_hovered || (pDIS->itemState & ODS_FOCUS)) {
                                btnColor = Color(min(255, alpha_val + 30), min(255, br + 20), min(255, bg_c + 20), min(255, bb + 30));
                                lineColor = Color(255, r, g, b);
                                textColor = Color(255, r, g, b);
                            }
                            if (pDIS->itemState & ODS_SELECTED) {
                                btnColor = Color(255, r, g, b);
                                textColor = Color(255, 15, 15, 20);
                            }

                            GraphicsPath path;
                            MakeRoundedRectPath(path, 1.0f, 1.0f, (REAL)(w - 2), (REAL)(h - 2), (REAL)elem.radius);

                            SolidBrush brush(btnColor);
                            Pen pen(lineColor, 2.0f);
                            graphics.FillPath(&brush, &path);
                            graphics.DrawPath(&pen, &path);

                            FontFamily fontFamily(elem.f_style.c_str());
                            Gdiplus::Font font(&fontFamily, (REAL)max(8, elem.f_size - 2), FontStyleBold, UnitPixel);
                            SolidBrush textBrush(textColor);

                            StringFormat stringFormat;
                            stringFormat.SetAlignment(StringAlignmentCenter);
                            stringFormat.SetLineAlignment(StringAlignmentCenter);

                            REAL pad = (REAL)elem.pad;
                            RectF rectF(pad, 0, (REAL)w - pad * 2.0f, (REAL)h);
                            graphics.DrawString(elem.text.c_str(), -1, &font, rectF, &stringFormat, &textBrush);
                            break;
                        }
                    }
                    return TRUE;
                }
                break;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);

                Bitmap backBuffer(win_w, win_h, PixelFormat32bppARGB);
                Graphics graphics(&backBuffer);
                graphics.SetSmoothingMode(SmoothingMode::SmoothingModeAntiAlias);
                graphics.SetTextRenderingHint(TextRenderingHint::TextRenderingHintClearTypeGridFit);

                if (pBgImage && pBgImage->GetLastStatus() == Ok) {
                    graphics.DrawImage(pBgImage, 0, 0, win_w, win_h);
                } else {
                    SolidBrush bgBrush(Color(255, GetRValue(bg_color), GetGValue(bg_color), GetBValue(bg_color)));
                    graphics.FillRectangle(&bgBrush, 0, 0, win_w, win_h);
                }

                for (auto& elem : elements) {
                    // Тени
                    if (elem.has_shadow) {
                        GraphicsPath shadowPath;
                        MakeRoundedRectPath(shadowPath, (REAL)(elem.x + 3), (REAL)(elem.y + 4), (REAL)elem.w, (REAL)elem.h, (REAL)elem.radius);
                        SolidBrush shadowBrush(Color(100, 0, 0, 0));
                        graphics.FillPath(&shadowBrush, &shadowPath);
                    }

                    // Отрисовка меток
                    if (elem.type == "label") {
                        BYTE a = elem.is_glass ? 130 : (BYTE)elem.alpha;
                        if (elem.has_custom_btn_color || elem.is_glass || elem.radius > 0) {
                            GraphicsPath lblPath;
                            MakeRoundedRectPath(lblPath, (REAL)elem.x, (REAL)elem.y, (REAL)elem.w, (REAL)elem.h, (REAL)elem.radius);
                            SolidBrush boxBrush(Color(elem.is_glass ? 90 : 200, GetRValue(elem.btn_color), GetGValue(elem.btn_color), GetBValue(elem.btn_color)));
                            graphics.FillPath(&boxBrush, &lblPath);
                        }

                        FontFamily fontFamily(elem.f_style.c_str());
                        Gdiplus::Font font(&fontFamily, (REAL)elem.f_size, FontStyleBold, UnitPixel);
                        SolidBrush textBrush(Color(a, GetRValue(elem.txt_color), GetGValue(elem.txt_color), GetBValue(elem.txt_color)));

                        StringFormat format;
                        if (elem.align == 1) format.SetAlignment(StringAlignmentCenter);
                        else if (elem.align == 3) format.SetAlignment(StringAlignmentFar);
                        else format.SetAlignment(StringAlignmentNear);
                        format.SetLineAlignment(StringAlignmentCenter);

                        RectF rectF((REAL)(elem.x + elem.pad), (REAL)elem.y, (REAL)(elem.w - elem.pad * 2), (REAL)elem.h);
                        graphics.DrawString(elem.text.c_str(), -1, &font, rectF, &format, &textBrush);
                    }
                    else if (elem.type == "icon" && !elem.media_path.empty()) {
                        Image img(elem.media_path.c_str());
                        if (img.GetLastStatus() == Ok) {
                            graphics.DrawImage(&img, elem.x, elem.y, elem.w, elem.h);
                        }
                    }
                }

                Graphics screenGraphics(hdc);
                screenGraphics.DrawImage(&backBuffer, 0, 0);

                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_DESTROY:
                KillTimer(hwnd, 1);
                if (pBgImage) { delete pBgImage; pBgImage = NULL; }
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, uMsg, wp, lp);
    }

    // --- Публичные функции API ---

    void reset() {
        window_title = L"GrassGreen UI";
        win_w = 640; win_h = 480;
        bg_color = RGB(18, 19, 26);
        window_is_glass = false;
        bg_image_path.clear();
        app_icon_path.clear();
        if (pBgImage) { delete pBgImage; pBgImage = NULL; }
        elements.clear();
        last_idx = -1;
        trigger_show = false;
        pending_switch_file.clear();
    }

    void set_variable(const string& name, const string& val) {
        variables_cache[name] = utf8_to_wstring(val);
    }

    string get_input_value(const string& element_id) {
        if (input_values_utf8.count(element_id)) return input_values_utf8[element_id];
        return "";
    }

    void exec_command(const string& raw_line) {
        string line = raw_line;
        cleanCarriageReturn(line);
        string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '*' || trimmed.substr(0, 2) == "//") return;

        if (trimmed.find("set ") == 0) {
            stringstream vss(trimmed.substr(4));
            string name, eq, val;
            vss >> name >> eq;
            getline(vss, val);
            val = stripQuotes(val);
            variables_cache[name] = utf8_to_wstring(val);
            return;
        }

        if (trimmed.find("animate-") == 0) {
            string target = trimmed.substr(8);
            int idx = find_target_index(target);
            if (idx != -1) {
                elements[idx].animated = true;
                elements[idx].alpha = 0;
            }
            return;
        }

        // БЕЗОПАСНЫЙ РАЗБОР КОМАНДЫ (скобки внутри текста !!...!! больше ничего не ломают!)
        string command;
        string inline_arg = "";

        if (trimmed.find("button") == 0 || trimmed.find("label") == 0 ||
            trimmed.find("enter") == 0  || trimmed.find("icon") == 0 || trimmed.find("video") == 0) {
            stringstream ss(trimmed);
            ss >> command;
            inline_arg = trim(trimmed.substr(command.length()));
        } else {
            size_t paren_start = trimmed.find('(');
            if (paren_start != string::npos) {
                command = trim(trimmed.substr(0, paren_start));
                inline_arg = trimmed.substr(paren_start);
            } else {
                stringstream ss(trimmed);
                ss >> command;
                inline_arg = trim(trimmed.substr(command.length()));
            }
        }

        // Обработка команд
        if (command == "window") {
            last_idx = -1;
        }
        else if (command == "title" || command == "name") {
            string val = inline_arg.empty() ? trimmed.substr(command.length()) : inline_arg;
            cleanParentheses(val);
            val = stripQuotes(val);
            if (command == "name" && last_idx != -1 && !inline_arg.empty()) {
                elements[last_idx].id = val;
            } else {
                window_title = utf8_to_wstring(val);
                unpack_cached_vars(window_title);
            }
        }
        else if (command == "size") {
            cleanParentheses(inline_arg);
            if (inline_arg.find(',') != string::npos) {
                int w = 640, h = 480;
                parse_two_args_fixed(inline_arg, w, h);
                if (last_idx != -1) { elements[last_idx].w = w; elements[last_idx].h = h; }
                else { win_w = w; win_h = h; }
            } else {
                try {
                    int val = stoi(inline_arg);
                    if (last_idx != -1) elements[last_idx].w = val;
                    else win_w = val;
                } catch (...) {}
            }
        }
        else if (command == "size-top") {
            cleanParentheses(inline_arg);
            try {
                int val = stoi(inline_arg);
                if (last_idx != -1) elements[last_idx].h = val;
                else win_h = val;
            } catch (...) {}
        }
        else if (command == "color") {
            cleanParentheses(inline_arg);
            bg_color = hex_to_rgb(inline_arg);
        }
        else if (command == "theme") {
            cleanParentheses(inline_arg);
            string th = stripQuotes(inline_arg);
            if (th == "light") bg_color = RGB(240, 242, 245);
            else if (th == "emerald" || th == "grass") bg_color = RGB(10, 28, 22);
            else if (th == "cyber") bg_color = RGB(20, 10, 35);
            else bg_color = RGB(18, 19, 26);
        }
        else if (command == "fon") {
            cleanParentheses(inline_arg);
            bg_image_path = utf8_to_wstring(stripQuotes(inline_arg));
            if (pBgImage) delete pBgImage;
            pBgImage = Gdiplus::Image::FromFile(bg_image_path.c_str());
        }
        else if (command == "iconP") {
            cleanParentheses(inline_arg);
            app_icon_path = utf8_to_wstring(stripQuotes(inline_arg));
        }
        else if (command == "button" || command == "label" || command == "enter") {
            string val = stripQuotes(inline_arg);
            UIElement elem;
            elem.type = command;
            elem.text = utf8_to_wstring(val);
            unpack_cached_vars(elem.text);
            elements.push_back(elem);
            last_idx = (int)elements.size() - 1;
        }
        else if (command == "icon" || command == "video") {
            cleanParentheses(inline_arg);
            string val = stripQuotes(inline_arg);
            UIElement elem;
            elem.type = command;
            elem.media_path = utf8_to_wstring(val);
            elem.w = 200; elem.h = 150;
            elements.push_back(elem);
            last_idx = (int)elements.size() - 1;
        }
        else if (command == "id" || command == "name_el") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].id = stripQuotes(inline_arg);
        }
        else if (command == "window-ob") {
            int arg1 = 0, arg2 = 0;
            parse_two_args_fixed(inline_arg, arg1, arg2);
            if (last_idx != -1) {
                elements[last_idx].x = arg1;
                elements[last_idx].y = arg2;
            }
        }
        else if (command == "middle") {
            int idx = find_target_index(inline_arg);
            if (idx != -1) elements[idx].align = 1;
        }
        else if (command == "left") {
            int idx = find_target_index(inline_arg);
            if (idx != -1) elements[idx].align = 2;
        }
        else if (command == "right") {
            int idx = find_target_index(inline_arg);
            if (idx != -1) elements[idx].align = 3;
        }
        else if (command == "text-color") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].txt_color = hex_to_rgb(inline_arg);
        }
        else if (command == "button-color") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].btn_color = hex_to_rgb(inline_arg);
                elements[last_idx].has_custom_btn_color = true;
            }
        }
        else if (command == "font-size") {
            cleanParentheses(inline_arg);
            try { if (last_idx != -1) elements[last_idx].f_size = stoi(inline_arg); } catch (...) {}
        }
        else if (command == "font-style") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].f_style = utf8_to_wstring(stripQuotes(inline_arg));
        }
        else if (command == "radius") {
            cleanParentheses(inline_arg);
            try { if (last_idx != -1) elements[last_idx].radius = stoi(inline_arg); } catch (...) {}
        }
        else if (command == "pad") {
            cleanParentheses(inline_arg);
            try { if (last_idx != -1) elements[last_idx].pad = stoi(inline_arg); } catch (...) {}
        }
        else if (command == "shadow") {
            int idx = find_target_index(inline_arg);
            if (idx != -1) elements[idx].has_shadow = true;
        }
        else if (command == "glass") {
            if (last_idx != -1) elements[last_idx].is_glass = true;
            else window_is_glass = true;
        }
        else if (command == "clear") {
            if (last_idx != -1 && elements[last_idx].type == "button") {
                elements[last_idx].trigger_clear_on_click = true;
            } else {
                elements.clear();
                last_idx = -1;
            }
        }
        else if (command == "click-ob") {
            cleanParentheses(inline_arg);
            for (char& c : inline_arg) if (c == ',') c = ' ';
            stringstream css(inline_arg);
            string in_id, lbl_id;
            css >> in_id >> lbl_id;
            if (last_idx != -1) {
                elements[last_idx].has_click_action = true;
                elements[last_idx].target_input_id = in_id;
                elements[last_idx].target_label_id = lbl_id.empty() ? in_id : lbl_id;
            }
        }
        else if (command == "switch") {
            cleanParentheses(inline_arg);
            if (last_idx != -1) {
                elements[last_idx].has_switch_event = true;
                elements[last_idx].switch_target = stripQuotes(inline_arg);
            }
        }
        else if (command == "run") {
            cleanParentheses(inline_arg);
            if (last_idx != -1 && elements[last_idx].type == "button") {
                elements[last_idx].run_target = stripQuotes(inline_arg);
                elements[last_idx].has_run_action = true;
            }
        }
        else if (command == "show") {
            trigger_show = true;
        }
    }

    int show_window() {
        GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icex);

        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"GrassUIMaximumClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);

        DWORD dwExStyle = window_is_glass ? WS_EX_LAYERED : 0;
        // Флаг WS_CLIPCHILDREN устраняет мерцание кнопок и фона!
        DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        RECT wr = { 0, 0, win_w, win_h };
        AdjustWindowRectEx(&wr, dwStyle, FALSE, dwExStyle);

        HWND hwndMain = CreateWindowExW(
            dwExStyle, L"GrassUIMaximumClass", window_title.c_str(), dwStyle,
            CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );
        if (!hwndMain) {
            GdiplusShutdown(gdiplusToken);
            return 1;
        }

        if (window_is_glass) {
            SetLayeredWindowAttributes(hwndMain, 0, 235, LWA_ALPHA);
        }

        if (!app_icon_path.empty()) {
            Bitmap* iconBmp = Bitmap::FromFile(app_icon_path.c_str());
            if (iconBmp && iconBmp->GetLastStatus() == Ok) {
                HICON hIcon = NULL;
                iconBmp->GetHICON(&hIcon);
                if (hIcon) {
                    SendMessageW(hwndMain, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                    SendMessageW(hwndMain, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
                }
                delete iconBmp;
            }
        }

        bool has_animations = false;

        for (size_t i = 0; i < elements.size(); i++) {
            auto& elem = elements[i];
            if (elem.align == 1) elem.x = (win_w - elem.w) / 2;
            else if (elem.align == 2) elem.x = 40;
            else if (elem.align == 3) elem.x = win_w - elem.w - 40;

            if (elem.animated) has_animations = true;
            if (elem.type == "label" || elem.type == "icon") continue;

            if (elem.type == "video" && !elem.media_path.empty()) {
                wstring cmd = L"open \"" + elem.media_path + L"\" type mpegvideo alias grass_vid";
                mciSendStringW(cmd.c_str(), NULL, 0, NULL);
                mciSendStringW(L"play grass_vid repeat", NULL, 0, NULL);
                continue;
            }

            wstring win32_class = L"STATIC";
            DWORD style = WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS;

            if (elem.type == "button") {
                win32_class = L"BUTTON";
                style |= BS_OWNERDRAW | WS_TABSTOP;
            } else if (elem.type == "enter") {
                win32_class = L"EDIT";
                style |= ES_AUTOHSCROLL | WS_TABSTOP | ES_CENTER | WS_BORDER;
            }

            wstring init_text = elem.text.empty() && elem.type == "enter" ? L"Введите текст..." : elem.text;
            elem.hwnd = CreateWindowExW(
                0, win32_class.c_str(), init_text.c_str(), style,
                elem.x, elem.y, elem.w, elem.h,
                hwndMain, (HMENU)(i + 100), GetModuleHandle(NULL), NULL
            );

            if (!elem.hwnd) continue;

            // УДАЛЯЕМ ПРЯМОУГОЛЬНЫЕ ХИТБОКСЫ КНОПКИ ЧЕРЕЗ SetWindowRgn
            if (elem.radius > 0) {
                HRGN hRgn = CreateRoundRectRgn(0, 0, elem.w + 1, elem.h + 1, elem.radius * 2, elem.radius * 2);
                SetWindowRgn(elem.hwnd, hRgn, TRUE);
            }

            if (elem.type == "enter") {
                OldEditProc = (WNDPROC)SetWindowLongPtr(elem.hwnd, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
            } else if (elem.type == "button") {
                OldButtonProc = (WNDPROC)SetWindowLongPtr(elem.hwnd, GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
            }

            HFONT hFont = CreateFontW(
                elem.f_size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, elem.f_style.c_str()
            );
            SendMessageW(elem.hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        if (has_animations) {
            SetTimer(hwndMain, 1, 16, NULL);
        }

        ShowWindow(hwndMain, SW_SHOWNORMAL);
        UpdateWindow(hwndMain);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        mciSendStringW(L"close grass_vid", NULL, 0, NULL);
        GdiplusShutdown(gdiplusToken);

        if (!pending_switch_file.empty()) {
            string next_file = pending_switch_file;
            return run_from_file(next_file);
        }
        return 0;
    }

    int run_from_lines(const vector<string>& lines) {
        reset();
        for (const string& line : lines) {
            exec_command(line);
        }
        if (trigger_show) return show_window();
        return 0;
    }

    int run_from_file(const string& filepath) {
        ifstream file(filepath);
        if (!file.is_open()) return 1;
        vector<string> lines;
        string line;
        while (getline(file, line)) {
            lines.push_back(line);
        }
        file.close();
        return run_from_lines(lines);
    }

} // namespace GrassGUI
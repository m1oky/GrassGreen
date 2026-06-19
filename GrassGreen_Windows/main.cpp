#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <urlmon.h>
#include <gdiplus.h>
#include "GrassLib.h"
#include "FastMath.h"
#include "CoreMath.h"
#include "GrassWeb.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace std;
using namespace Gdiplus;

// Глобальные параметры окна
wstring window_title = L"GrassGreen UI";
int win_w = 640, win_h = 480; 
COLORREF bg_color = RGB(15, 15, 20); 

wstring bg_image_path = L"";
Gdiplus::Image* pBgImage = NULL; 

vector<UIElement> elements;
bool global_trigger_show = false; // Переменная стала глобальной для связи с веб-движком!

WNDPROC OldEditProc = NULL;
WNDPROC OldButtonProc = NULL; 

wstring current_input = L"0";
double first_num = 0.0;
wstring current_op = L"";
bool is_new_entry = true;

extern map<string, wstring> variables_cache;

void cleanParentheses(string& str) {
    string res;
    for (char c : str) if (c != '(' && c != ')') res += c;
    str = res;
}

void cleanCarriageReturn(string& str) {
    if (!str.empty() && str.back() == '\r') str.pop_back();
}

wstring utf8_to_wstring(const string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
    return wstr;
}

void unpack_cached_vars(wstring& text) {
    size_t start = text.find(L"{");
    while (start != wstring::npos) {
        size_t end = text.find(L"}", start);
        if (end != wstring::npos) {
            wstring var_name = text.substr(start + 1, end - start - 1);
            string s_name(var_name.begin(), var_name.end());
            if (s_name.length() > 2 && s_name.substr(s_name.length() - 2) == "()") {
                s_name = s_name.substr(0, s_name.length() - 2);
            }
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
    if (hex.empty() || hex[0] != '#') return RGB(15, 15, 20);
    if (hex.length() != 7) return RGB(15, 15, 20);
    int r, g, b;
    stringstream ss1(hex.substr(1, 2)); ss1 >> std::hex >> r;
    stringstream ss2(hex.substr(3, 2)); ss2 >> std::hex >> g;
    stringstream ss3(hex.substr(5, 2)); ss3 >> std::hex >> b;
    return RGB(r, g, b);
}

void parse_two_args(string arg_str, int& arg1, int& arg2) {
    cleanParentheses(arg_str);
    size_t comma = arg_str.find(',');
    if (comma != string::npos) {
        string s1 = arg_str.substr(0, comma);
        string s2 = arg_str.substr(comma + 1);
        s1.erase(0, s1.find_first_not_of(" \t")); s1.erase(s1.find_last_not_of(" \t") + 1);
        s2.erase(0, s2.find_first_not_of(" \t")); s2.erase(s2.find_last_not_of(" \t") + 1);
        try { arg1 = stoi(s1); arg2 = stoi(s2); } catch (...) { arg1 = 0; arg2 = 0; }
    }
}

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp) {
    switch (uMsg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
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
                }
            }
            break;
        }
        case WM_KILLFOCUS: {
            wchar_t buf[256]; GetWindowTextW(hwnd, buf, 256);
            if (wcslen(buf) == 0) {
                for (auto& elem : elements) {
                    if (elem.hwnd == hwnd) {
                        elem.is_placeholder = true;
                        SetWindowTextW(hwnd, elem.text.c_str());
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
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetBkMode(hdc, TRANSPARENT); 
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wp;
            SetBkColor(hdc, RGB(25, 25, 33));
            SetTextColor(hdc, RGB(0, 255, 204));
            static HBRUSH editBg = CreateSolidBrush(RGB(25, 25, 33));
            return (INT_PTR)editBg;
        }
        case WM_COMMAND: {
            if (HIWORD(wp) == BN_CLICKED) {
                HWND clicked_hwnd = (HWND)lp;
                string btn_id = "";
                wstring btn_text = L"";
                
                for (auto& elem : elements) {
                    if (elem.hwnd == clicked_hwnd) {
                        btn_id = elem.id;
                        btn_text = elem.text;
                        break;
                    }
                }

                if (!btn_id.empty()) {
                    size_t display_idx = -1;
                    size_t history_idx = -1;
                    for (size_t i = 0; i < elements.size(); i++) {
                        if (elements[i].id == "calc_display") display_idx = i;
                        if (elements[i].id == "calc_history") history_idx = i;
                    }

                    if (display_idx != -1 && history_idx != -1) {
                        if ((btn_text >= L"0" && btn_text <= L"9") || btn_text == L".") {
                            if (is_new_entry) {
                                current_input = (btn_text == L".") ? L"0." : btn_text;
                                is_new_entry = false;
                            } else {
                                if (!(btn_text == L"." && current_input.find(L".") != wstring::npos)) {
                                    if (current_input == L"0" && btn_text != L".") current_input = btn_text;
                                    else current_input += btn_text;
                                }
                            }
                            elements[display_idx].text = current_input;
                        }
                        else if (btn_text == L"C" || btn_text == L"CE") {
                            current_input = L"0";
                            if (btn_text == L"C") { 
                                first_num = 0.0; 
                                current_op = L""; 
                                elements[history_idx].text = L""; 
                            }
                            is_new_entry = true;
                            elements[display_idx].text = current_input;
                        }
                        else if (btn_text == L"+/-") {
                            if (current_input != L"0") {
                                if (current_input[0] == L'-') current_input.erase(0, 1);
                                else current_input = L"-" + current_input;
                                elements[display_idx].text = current_input;
                            }
                        }
                        else if (btn_text == L"+" || btn_text == L"-" || btn_text == L"*" || btn_text == L"/") {
                            string s_input(current_input.begin(), current_input.end());
                            try { first_num = stod(s_input); } catch(...) { first_num = 0.0; }
                            current_op = btn_text;
                            is_new_entry = true;
                            elements[history_idx].text = current_input + L" " + current_op;
                        }
                        else if (btn_text == L"=") {
                            if (!current_op.empty()) {
                                wstringstream expr_ss;
                                expr_ss << first_num << current_op << current_input;
                                wstring w_expr = expr_ss.str();
                                string s_expr(w_expr.begin(), w_expr.end());

                                double result = FastMath_Calculate(s_expr);

                                if (result == 0.0 && s_expr.find_first_of("+-*/") != string::npos) {
                                    string s_input(current_input.begin(), current_input.end());
                                    double second_num = 0.0;
                                    try { second_num = stod(s_input); } catch(...) {}
                                    if (current_op == L"+") result = first_num + second_num;
                                    else if (current_op == L"-") result = first_num - second_num;
                                    else if (current_op == L"*") result = first_num * second_num;
                                    else if (current_op == L"/") {
                                        if (second_num != 0.0) result = first_num / second_num;
                                    }
                                }

                                wstringstream wss; wss << result;
                                wstring w_res = wss.str();

                                wstringstream hist_ss;
                                hist_ss << first_num << L" " << current_op << L" " << current_input << L" =";
                                elements[history_idx].text = hist_ss.str();
                                current_input = w_res;
                                elements[display_idx].text = current_input;
                                current_op = L"";
                                is_new_entry = true;
                            }
                        }
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                }

                for (auto& elem : elements) {
                    if (elem.hwnd == clicked_hwnd && elem.has_run_action) {
                        WinExec(elem.run_target.c_str(), SW_SHOWNORMAL);
                    }
                }
            }
            break;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lp;
            if (pDIS->CtlType == ODT_BUTTON) {
                Graphics graphics(pDIS->hDC);
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

                int w = pDIS->rcItem.right - pDIS->rcItem.left;
                int h = pDIS->rcItem.bottom - pDIS->rcItem.top;
                Color btnColor(190, 18, 18, 24); 
                Color lineColor(140, 0, 255, 204); 

                for (auto& elem : elements) {
                    if (elem.hwnd == pDIS->hwndItem) {
                        if (elem.is_hovered) {
                            btnColor = Color(220, 28, 28, 38);
                            lineColor = Color(255, GetRValue(elem.txt_color), GetGValue(elem.txt_color), GetBValue(elem.txt_color));
                        }
                        if (pDIS->itemState & ODS_SELECTED) {
                            btnColor = Color(255, 0, 200, 160);
                        }
                        SolidBrush brush(btnColor);
                        Pen pen(lineColor, 2);
                        graphics.FillRectangle(&brush, 0, 0, w, h);
                        graphics.DrawRectangle(&pen, 1, 1, w - 2, h - 2);

                        FontFamily fontFamily(elem.f_style.c_str());
                        Gdiplus::Font font(&fontFamily, (REAL)(elem.f_size - 2), FontStyleBold, UnitPixel);
                        SolidBrush textBrush(Color(255, GetRValue(elem.txt_color), GetGValue(elem.txt_color), GetBValue(elem.txt_color)));
                        
                        StringFormat stringFormat;
                        stringFormat.SetAlignment(StringAlignmentCenter);
                        stringFormat.SetLineAlignment(StringAlignmentCenter);
                        
                        RectF rectF(0, 0, (REAL)w, (REAL)h);
                        graphics.DrawString(elem.text.c_str(), -1, &font, rectF, &stringFormat, &textBrush);
                        break;
                    }
                }
                return TRUE;
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            Graphics graphics(hdc);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            if (pBgImage && pBgImage->GetLastStatus() == Ok) {
                graphics.DrawImage(pBgImage, 0, 0, win_w, win_h);
            } else {
                SolidBrush bgBrush(Color(255, GetRValue(bg_color), GetGValue(bg_color), GetBValue(bg_color)));
                graphics.FillRectangle(&bgBrush, 0, 0, win_w, win_h);
            }

            for (auto& elem : elements) {
                if (elem.type == "label") {
                    FontFamily fontFamily(elem.f_style.c_str());
                    Gdiplus::Font font(&fontFamily, (REAL)elem.f_size, FontStyleBold, UnitPixel);
                    SolidBrush textBrush(Color(255, GetRValue(elem.txt_color), GetGValue(elem.txt_color), GetBValue(elem.txt_color)));
                    
                    StringFormat format;
                    if (elem.align == 1) format.SetAlignment(StringAlignmentCenter);
                    else if (elem.align == 3) format.SetAlignment(StringAlignmentFar);
                    else format.SetAlignment(StringAlignmentNear);
                    
                    RectF rectF((REAL)elem.x, (REAL)elem.y, (REAL)elem.w, (REAL)elem.h);
                    graphics.DrawString(elem.text.c_str(), -1, &font, rectF, &format, &textBrush);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            if (pBgImage) delete pBgImage;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wp, lp);
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    ifstream file(argv[1]);
    if (!file.is_open()) return 1;

    vector<string> lines;
    string line_text;
    bool multi_line_comment = false;
    bool text_off = false;

    while (getline(file, line_text)) {
        cleanCarriageReturn(line_text);
        
        if (line_text == "exit") return 0;
        if (line_text.find("@text_off") != string::npos) { text_off = true; continue; }
        if (line_text.find("**") == 0) { multi_line_comment = !multi_line_comment; continue; }
        if (multi_line_comment) continue;
        if (!line_text.empty() && line_text.front() == '*') continue;

        if (line_text.find("wait(") == 0) {
            size_t p_end = line_text.find(')');
            if (p_end != string::npos) {
                string ms_str = line_text.substr(5, p_end - 5);
                try { int ms = stoi(ms_str); Sleep(ms); } catch(...) {}
            }
            continue;
        }

        if (!line_text.empty() && line_text.front() == '!') {
            size_t end_pos = line_text.find('!', 1);
            if (end_pos != string::npos) {
                string lib_token = line_text.substr(0, end_pos + 1);
                if (!text_off) GrassLib_Load(lib_token);

                // Запуск нашего веб-парсера страниц при подключении библиотеки
                if (lib_token == "!GrassWeb!") {
                    GrassWeb_LoadPage(argv[1]);
                }
                continue;
            }
        }

        if (line_text.find("log = ") == 0) {
            if (!text_off) {
                string log_msg = line_text.substr(6);
                if (log_msg == "text;") {
                    cout << endl;
                }
                else if (log_msg.find("qb_init") != string::npos || 
                         log_msg.find("h(") != string::npos || 
                         log_msg.find("q_rand") != string::npos || 
                         log_msg.find("pi(") != string::npos || 
                         log_msg.find("pow_e") != string::npos || 
                         log_msg.find("gcd") != string::npos) 
                {
                    string q_res = CoreMath_Calculate(log_msg);
                    cout << "[GrassQuantumLog]: " << q_res << endl;
                }
                else if (log_msg.find('(') != string::npos && log_msg.find(')') != string::npos) {
                    double f_res = FastMath_Calculate(log_msg);
                    cout << "[GrassFastMathLog]: " << f_res << endl;
                }
                else {
                    cout << "[GrassLog]: " << log_msg << endl;
                }
            }
            continue;
        }

        if (line_text.find("set ") == 0) {
            stringstream vss(line_text.substr(4));
            string name, eq, val; vss >> name >> eq; getline(vss, val);
            if (!val.empty() && val[0] == ' ') val = val.substr(1);
            if (val.substr(0, 2) == "!!") val = val.substr(2, val.length() - 4);
            variables_cache[name] = utf8_to_wstring(val);
        }
        lines.push_back(line_text);
    }
    file.close();

    int last_idx = -1; 

    for (string line : lines) {
        if (line.empty() || line[0] == '*') continue;
        if (line.front() == '!') continue; 

        int start_pos = 0;
        while (start_pos < line.length() && (line[start_pos] == ' ' || line[start_pos] == '\t')) start_pos++;
        string trimmed = line.substr(start_pos);
        if (trimmed.empty()) continue;

        string command = "";
        string inline_arg = "";
        size_t paren_start = trimmed.find('(');
        if (paren_start != string::npos) {
            command = trimmed.substr(0, paren_start);
            inline_arg = trimmed.substr(paren_start); 
        } else {
            stringstream ss(trimmed); ss >> command;
        }

        if (command == "window") { last_idx = -1; }
        else if (command == "name") {
            string val = trimmed.substr(4); 
            while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val = val.substr(1);
            if (val.substr(0, 2) == "!!") val = val.substr(2, val.length() - 4);
            window_title = utf8_to_wstring(val);
            unpack_cached_vars(window_title);
        }
        else if (command == "size") {
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            try { int val = stoi(inline_arg); if (last_idx != -1) elements[last_idx].w = val; else win_w = val; } catch(...) {}
        }
        else if (command == "size-top") {
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            try { int val = stoi(inline_arg); if (last_idx != -1) elements[last_idx].h = val; else win_h = val; } catch(...) {}
        }
        else if (command == "color") {
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            bg_color = hex_to_rgb(inline_arg);
        }
        else if (command == "fon") {
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            bg_image_path = utf8_to_wstring(inline_arg);
            pBgImage = Gdiplus::Image::FromFile(bg_image_path.c_str());
        }
        else if (command == "button" || command == "label" || command == "enter") {
            string val = trimmed.substr(command.length());
            while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val = val.substr(1);
            if (val.substr(0, 2) == "!!") val = val.substr(2, val.length() - 4);

            UIElement elem;
            elem.type = command;
            elem.text = utf8_to_wstring(val);
            unpack_cached_vars(elem.text); 
            elements.push_back(elem);
            last_idx = elements.size() - 1; 
        }
        else if (command == "id" || command == "name_el") { 
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].id = inline_arg;
        }
        else if (command == "window-ob") {
            int arg1 = 0, arg2 = 0; 
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            parse_two_args(inline_arg, arg1, arg2);
            if (last_idx != -1) { 
                elements[last_idx].x = arg1; 
                elements[last_idx].y = arg2; 
            }
        }
        else if (command == "middle") { if (last_idx != -1) elements[last_idx].align = 1; }
        else if (command == "left") { if (last_idx != -1) elements[last_idx].align = 2; }
        else if (command == "right") { if (last_idx != -1) elements[last_idx].align = 3; }
        else if (command == "text-color") {
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].txt_color = hex_to_rgb(inline_arg);
        }
        else if (command == "font-size") {
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            try { if (last_idx != -1) elements[last_idx].f_size = stoi(inline_arg); } catch(...) {}
        }
        else if (command == "font-style") {
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].f_style = utf8_to_wstring(inline_arg);
        }
        else if (command == "run") {
            if (inline_arg.empty()) { stringstream ss(trimmed); ss >> command >> inline_arg; }
            cleanParentheses(inline_arg);
            if (last_idx != -1 && elements[last_idx].type == "button") {
                elements[last_idx].run_target = inline_arg;
                elements[last_idx].has_run_action = true;
            }
        }
        else if (command == "show") { global_trigger_show = true; }
    }

    if (global_trigger_show) {
        WNDCLASSEXW wc = {0}; wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"GrassUIMaximumClass"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);

        DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT wr = {0, 0, win_w, win_h}; AdjustWindowRectEx(&wr, dwStyle, FALSE, 0);

        HWND hwndMain = CreateWindowExW(0, L"GrassUIMaximumClass", window_title.c_str(), dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, GetModuleHandle(NULL), NULL);
        if (!hwndMain) return 1;

        for (size_t i = 0; i < elements.size(); i++) {
            auto& elem = elements[i];
            wstring win32_class = L"STATIC";
            DWORD style = WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS;

            if (elem.align == 1) elem.x = (win_w - elem.w) / 2;       
            else if (elem.align == 2) elem.x = 40;                     
            else if (elem.align == 3) elem.x = win_w - elem.w - 40;    

            if (elem.type == "button") { 
                win32_class = L"BUTTON"; 
                style |= BS_OWNERDRAW | WS_TABSTOP; 
            }
            else if (elem.type == "enter") { 
                win32_class = L"EDIT"; 
                style |= ES_AUTOHSCROLL | WS_TABSTOP | ES_CENTER; 
            }
            else if (elem.type == "label") {
                style |= SS_OWNERDRAW; 
            }

            elem.hwnd = CreateWindowExW(0, win32_class.c_str(), elem.text.c_str(), style, elem.x, elem.y, elem.w, elem.h, hwndMain, (HMENU)(i + 100), GetModuleHandle(NULL), NULL);

            if (elem.type == "enter") {
                OldEditProc = (WNDPROC)SetWindowLongPtr(elem.hwnd, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
            }
            if (elem.type == "button") {
                OldButtonProc = (WNDPROC)SetWindowLongPtr(elem.hwnd, GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
            }

            HFONT hFont = CreateFontW(elem.f_size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, elem.f_style.c_str());
            SendMessageW(elem.hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        std::cout << "[Engine Debug]: Reached window creation!" << std::endl;
        ShowWindow(hwndMain, SW_SHOWNORMAL); UpdateWindow(hwndMain);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    }
    GdiplusShutdown(gdiplusToken);
    return 0;
}
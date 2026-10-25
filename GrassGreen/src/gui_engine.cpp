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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace std;
using namespace Gdiplus;

struct UIElement {
    string type;      
    string id;        
    wstring text;      
    int x = 0, y = 0; 
    int w = 150, h = 35; 
    int align = 0; 
    COLORREF txt_color = RGB(0, 255, 204); 
    int f_size = 18;
    wstring f_style = L"Segoe UI";
    HWND hwnd = NULL;
    
    bool has_click_action = false;
    string target_input_id;
    string target_label_id;
    bool trigger_clear_on_click = false;
    
    bool has_file_open = false;
    string file_to_open;
    bool has_switch_event = false;
    string switch_target;

    wstring media_path;
    bool is_url = false;
    bool animated = false;
    int alpha = 0; 

    bool is_glass = false;       
    bool has_run_action = false; 
    string run_target;           

    bool is_hovered = false; 
    bool is_placeholder = true; 
};

wstring window_title = L"GrassGreen UI";
int win_w = 640, win_h = 480; 
COLORREF bg_color = RGB(15, 15, 20); 
HBRUSH bg_brush = NULL;

wstring bg_image_path = L"";
Gdiplus::Image* pBgImage = NULL; 

vector<UIElement> elements;
map<string, wstring> variables_cache; 

WNDPROC OldEditProc = NULL;
WNDPROC OldButtonProc = NULL; 

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
            
            // Переводим имя переменной в обычную строку для поиска в кэше
            string s_name(var_name.begin(), var_name.end());
            
            if (variables_cache.count(s_name)) {
                text.replace(start, end - start + 1, variables_cache[s_name]);
                // Ищем следующую переменную с самого начала строки
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

void parse_two_args_fixed(string str, int& x, int& y) {
    cleanParentheses(str);
    for (char &c : str) if (c == ',') c = ' ';
    stringstream parser(str);
    parser >> x >> y;
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
                    PostMessage(hwnd, EM_SETSEL, 0, 0);
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
                        SetWindowTextW(hwnd, L"Введите ваш игровой ник...");
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
            HWND child = (HWND)lp;
            for (auto& elem : elements) {
                if (elem.hwnd == child) {
                    if (elem.type == "label") {
                        SetTextColor(hdc, elem.txt_color);
                    }
                    SetBkMode(hdc, TRANSPARENT);
                    return (INT_PTR)GetStockObject(NULL_BRUSH);
                }
            }
            SetBkMode(hdc, TRANSPARENT); 
            return (INT_PTR)GetStockObject(NULL_BRUSH);
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wp;
            HWND child = (HWND)lp;
            for (auto& elem : elements) {
                if (elem.hwnd == child) {
                    if (elem.is_placeholder) SetTextColor(hdc, RGB(120, 120, 130));
                    else SetTextColor(hdc, RGB(255, 255, 255));
                }
            }
            SetBkColor(hdc, RGB(25, 25, 33));
            static HBRUSH editBg = CreateSolidBrush(RGB(25, 25, 33));
            return (INT_PTR)editBg;
        }
        case WM_COMMAND: {
            if (HIWORD(wp) == BN_CLICKED) {
                for (auto& elem : elements) {
                    if (elem.hwnd == (HWND)lp && elem.has_run_action) {
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
                graphics.SetSmoothingMode(SmoothingMode::SmoothingModeAntiAlias);
                graphics.SetTextRenderingHint(TextRenderingHint::TextRenderingHintClearTypeGridFit);

                int w = pDIS->rcItem.right - pDIS->rcItem.left;
                int h = pDIS->rcItem.bottom - pDIS->rcItem.top;

                for (auto& elem : elements) {
                    if (elem.hwnd == pDIS->hwndItem) {
                        BYTE r = GetRValue(elem.txt_color);
                        BYTE g = GetGValue(elem.txt_color);
                        BYTE b = GetBValue(elem.txt_color);

                        Color btnColor(210, 18, 18, 24); 
                        Color lineColor(140, 0, 255, 204); 
                        Color textColor(255, 255, 255, 255); 

                        if (elem.is_hovered || (pDIS->itemState & ODS_FOCUS)) {
                            btnColor = Color(240, 28, 28, 38); 
                            lineColor = Color(255, r, g, b); 
                            textColor = Color(255, r, g, b);
                        }
                        if (pDIS->itemState & ODS_SELECTED) {
                            btnColor = Color(255, r, g, b);  
                            textColor = Color(255, 15, 15, 20); 
                        }

                        SolidBrush brush(btnColor);
                        Pen pen(lineColor, 2);
                        graphics.FillRectangle(&brush, 0, 0, w, h);
                        graphics.DrawRectangle(&pen, 1, 1, w - 2, h - 2);

                        FontFamily fontFamily(elem.f_style.c_str());
                        Gdiplus::Font font(&fontFamily, (REAL)(elem.f_size - 2), FontStyleBold, UnitPixel);
                        SolidBrush textBrush(textColor);
                        
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
            graphics.SetSmoothingMode(SmoothingMode::SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(TextRenderingHint::TextRenderingHintClearTypeGridFit);

            if (pBgImage && pBgImage->GetLastStatus() == Ok) {
                graphics.DrawImage(pBgImage, 0, 0, win_w, win_h);
                for (auto& elem : elements) {
                    if (elem.hwnd) {
                        UpdateWindow(elem.hwnd);
                    }
                }
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
            if (bg_brush) DeleteObject(bg_brush);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wp, lp);
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;

    // 1. ПОДКЛЮЧАЕМ БИБЛИОТЕКУ ГРАФИКИ GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // 2. ИНИЦИАЛИЗИРУЕМ БИБЛИОТЕКУ СТАНДАРТНЫХ ЭЛЕМЕНТОВ (COMMCTRL)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    // Подключаем все необходимые классы элементов управления (кнопки, поля ввода, статики)
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES; 
    InitCommonControlsEx(&icex);

   
    // Чтение файла ui.grass
    ifstream file(argv[1]);
    if (!file.is_open()) return 1;

    vector<string> lines;
    string line_text;
    while (getline(file, line_text)) {
        cleanCarriageReturn(line_text);
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
    bool trigger_show = false;

    // Парсинг разметки
    for (string line : lines) {
        if (line.empty() || line[0] == '*') continue;
        int start_pos = 0;
        while (start_pos < line.length() && (line[start_pos] == ' ' || line[start_pos] == '\t')) start_pos++;
        string trimmed = line.substr(start_pos);
        if (trimmed.empty()) continue;

        stringstream ss(trimmed);
        string command; ss >> command;
        string inline_arg = "";
        size_t paren_start = trimmed.find('(');
        if (paren_start != string::npos) {
            inline_arg = trimmed.substr(paren_start);
            command = trimmed.substr(0, paren_start);
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
            if (inline_arg.empty()) ss >> inline_arg; cleanParentheses(inline_arg);
            try { int val = stoi(inline_arg); if (last_idx != -1) elements[last_idx].w = val; else win_w = val; } catch(...) {}
        }
        else if (command == "size-top") {
            if (inline_arg.empty()) ss >> inline_arg; cleanParentheses(inline_arg);
            try { int val = stoi(inline_arg); if (last_idx != -1) elements[last_idx].h = val; else win_h = val; } catch(...) {}
        }
        else if (command == "color") {
            if (inline_arg.empty()) ss >> inline_arg; cleanParentheses(inline_arg);
            bg_color = hex_to_rgb(inline_arg);
        }
        else if (command == "fon") {
            if (inline_arg.empty()) ss >> inline_arg; cleanParentheses(inline_arg);
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
            if (inline_arg.empty()) ss >> inline_arg; cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].id = inline_arg;
        }
        else if (command == "window-ob") {
            string full_arg = inline_arg;
            if (full_arg.empty()) getline(ss, full_arg);
            int arg1 = 0, arg2 = 0;
            parse_two_args_fixed(full_arg, arg1, arg2);
            if (last_idx != -1) { 
                elements[last_idx].x = arg1; 
                elements[last_idx].y = arg2; 
            }
        }
        else if (command == "middle") { if (last_idx != -1) elements[last_idx].align = 1; }
        else if (command == "left") { if (last_idx != -1) elements[last_idx].align = 2; }
        else if (command == "right") { if (last_idx != -1) elements[last_idx].align = 3; }
        else if (command == "text-color") {
            if (inline_arg.empty()) ss >> inline_arg; cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].txt_color = hex_to_rgb(inline_arg);
        }
        else if (command == "font-size") {
            if (inline_arg.empty()) ss >> inline_arg; cleanParentheses(inline_arg);
            try { if (last_idx != -1) elements[last_idx].f_size = stoi(inline_arg); } catch(...) {}
        }
        else if (command == "font-style") {
            if (inline_arg.empty()) ss >> inline_arg; cleanParentheses(inline_arg);
            if (last_idx != -1) elements[last_idx].f_style = utf8_to_wstring(inline_arg);
        }
        else if (command == "glass") {
            if (last_idx != -1) elements[last_idx].is_glass = true;
        }
        else if (command == "run") {
            string full_run = inline_arg;
            if (full_run.empty()) getline(ss, full_run);
            cleanParentheses(full_run);
            if (last_idx != -1 && elements[last_idx].type == "button") {
                elements[last_idx].run_target = full_run;
                elements[last_idx].has_run_action = true;
            }
        }
        else if (command == "show") { trigger_show = true; }
    }

    // РИСУЕМ ОКНО С ПОДКЛЮЧЕННЫМИ БИБЛИОТЕКАМИ ЕЛЕМЕНТОВ
    if (trigger_show) {
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
            // Включаем WS_CHILD и WS_VISIBLE обязательно, чтобы Win32 прорисовала их
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
                style |= ES_AUTOHSCROLL | WS_TABSTOP | ES_CENTER | WS_BORDER; 
            }
            else if (elem.type == "label") {
                win32_class = L"STATIC";
                style |= SS_OWNERDRAW; 
            }

            // Создаем элемент на базе подключенной библиотеки классов Windows
            elem.hwnd = CreateWindowExW(0, win32_class.c_str(), elem.text.c_str(), style, elem.x, elem.y, elem.w, elem.h, hwndMain, (HMENU)(i + 100), GetModuleHandle(NULL), NULL);

            if (!elem.hwnd) continue;

            if (elem.type == "enter") {
                OldEditProc = (WNDPROC)SetWindowLongPtr(elem.hwnd, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
            }
            if (elem.type == "button") {
                OldButtonProc = (WNDPROC)SetWindowLongPtr(elem.hwnd, GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
            }

            HFONT hFont = CreateFontW(elem.f_size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, elem.f_style.c_str());
            SendMessageW(elem.hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        cout << "[Engine Debug]: Reached window creation!" << endl;
        ShowWindow(hwndMain, SW_SHOWNORMAL); UpdateWindow(hwndMain);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    }
    GdiplusShutdown(gdiplusToken);
    return 0;
}
#include "GrassLib.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;
using namespace Gdiplus;

map<string, wstring> variables_cache;

static wstring g_win_title = L"GrassGreen UI";
static int g_win_w = 640, g_win_h = 480;
static COLORREF g_bg_color = RGB(15, 15, 20);
static wstring g_bg_image_path = L"";
static Gdiplus::Image* g_pBgImage = nullptr;
static vector<UIElement> g_elements;
static WNDPROC OldButtonProc = nullptr;

wstring utf8_to_wstring(const string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
    if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
    return wstr;
}

string wstring_to_utf8(const wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
    if (!str.empty() && str.back() == '\0') str.pop_back();
    return str;
}

COLORREF hex_to_rgb(const string& hex) {
    if (hex.length() != 7 || hex[0] != '#') return RGB(15, 15, 20);
    int r = 15, g = 15, b = 20;
    stringstream(hex.substr(1, 2)) >> std::hex >> r;
    stringstream(hex.substr(3, 2)) >> std::hex >> g;
    stringstream(hex.substr(5, 2)) >> std::hex >> b;
    return RGB(r, g, b);
}

bool GrassStorageEngine_Load(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) return false;

    string line;
    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '*' || line == "!database!") continue;

        size_t p1 = line.find('(');
        size_t p2 = line.rfind(')');
        if (p1 != string::npos && p2 != string::npos && p2 > p1) {
            string key = line.substr(0, p1);
            string val = line.substr(p1 + 1, p2 - p1 - 1);
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            variables_cache[key] = utf8_to_wstring(val);
        }
    }
    file.close();
    return true;
}

void GrassLib_Load(const string& lib_name) {
    if (lib_name == "!database!" || lib_name == "database") {
        GrassStorageEngine_Load("storage.gdb");
    }
}

static LRESULT CALLBACK BtnSubclass(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp) {
    if (uMsg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        for (auto& el : g_elements) {
            if (el.hwnd == hwnd && !el.is_hovered) {
                el.is_hovered = true;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
    } else if (uMsg == WM_MOUSELEAVE) {
        for (auto& el : g_elements) {
            if (el.hwnd == hwnd && el.is_hovered) {
                el.is_hovered = false;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
    }
    return CallWindowProc(OldButtonProc, hwnd, uMsg, wp, lp);
}

static LRESULT CALLBACK GrassWndProc(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp) {
    switch (uMsg) {
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wp;
            SetBkColor(hdc, RGB(25, 25, 33));
            SetTextColor(hdc, RGB(0, 255, 204));
            static HBRUSH editBg = CreateSolidBrush(RGB(25, 25, 33));
            return (INT_PTR)editBg;
        }
        case WM_COMMAND: {
            if (HIWORD(wp) == BN_CLICKED) {
                for (auto& el : g_elements) {
                    if (el.hwnd == (HWND)lp && el.has_run_action) {
                        WinExec(el.run_target.c_str(), SW_SHOWNORMAL);
                    }
                }
            }
            break;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lp;
            if (pDIS->CtlType == ODT_BUTTON) {
                Graphics g(pDIS->hDC);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
                int w = pDIS->rcItem.right - pDIS->rcItem.left;
                int h = pDIS->rcItem.bottom - pDIS->rcItem.top;

                for (auto& el : g_elements) {
                    if (el.hwnd == pDIS->hwndItem) {
                        BYTE r = GetRValue(el.txt_color), gr = GetGValue(el.txt_color), b = GetBValue(el.txt_color);
                        BYTE alpha = el.is_glass ? 130 : 215;
                        Color btnCol = el.is_hovered ? Color(240, 30, 30, 44) : Color(alpha, 18, 18, 26);
                        Color borderCol = el.is_hovered ? Color(255, r, gr, b) : Color(160, r, gr, b);

                        SolidBrush br(btnCol);
                        Pen pen(borderCol, 2);
                        g.FillRectangle(&br, 0, 0, w, h);
                        g.DrawRectangle(&pen, 1, 1, w - 2, h - 2);

                        FontFamily ff(el.f_style.c_str());
                        Gdiplus::Font font(&ff, (REAL)(el.f_size - 2), FontStyleBold, UnitPixel);
                        SolidBrush tBr(Color(255, r, gr, b));
                        StringFormat sf;
                        sf.SetAlignment(StringAlignmentCenter);
                        sf.SetLineAlignment(StringAlignmentCenter);
                        g.DrawString(el.text.c_str(), -1, &font, RectF(0, 0, (REAL)w, (REAL)h), &sf, &tBr);
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
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBM = CreateCompatibleBitmap(hdc, g_win_w, g_win_h);
            HGDIOBJ oldBM = SelectObject(memDC, memBM);

            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            if (g_pBgImage && g_pBgImage->GetLastStatus() == Ok) {
                g.DrawImage(g_pBgImage, 0, 0, g_win_w, g_win_h);
            } else {
                SolidBrush bgBr(Color(255, GetRValue(g_bg_color), GetGValue(g_bg_color), GetBValue(g_bg_color)));
                g.FillRectangle(&bgBr, 0, 0, g_win_w, g_win_h);
            }

            for (auto& el : g_elements) {
                if (el.type == "label") {
                    FontFamily ff(el.f_style.c_str());
                    Gdiplus::Font font(&ff, (REAL)el.f_size, FontStyleBold, UnitPixel);
                    SolidBrush tBr(Color(255, GetRValue(el.txt_color), GetGValue(el.txt_color), GetBValue(el.txt_color)));
                    StringFormat sf;
                    if (el.align == 1) sf.SetAlignment(StringAlignmentCenter);
                    else if (el.align == 3) sf.SetAlignment(StringAlignmentFar);
                    else sf.SetAlignment(StringAlignmentNear);
                    g.DrawString(el.text.c_str(), -1, &font, RectF((REAL)el.x, (REAL)el.y, (REAL)el.w, (REAL)el.h), &sf, &tBr);
                }
            }

            BitBlt(hdc, 0, 0, g_win_w, g_win_h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBM);
            DeleteObject(memBM);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            if (g_pBgImage) { delete g_pBgImage; g_pBgImage = nullptr; }
            for (auto& el : g_elements) {
                if (el.hfont) DeleteObject(el.hfont);
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wp, lp);
}

namespace GrassGUI {
    void InitWindow(const string& title, int w, int h, const string& bg_hex, const string& bg_img) {
        g_win_title = utf8_to_wstring(title);
        g_win_w = w; g_win_h = h;
        g_bg_color = hex_to_rgb(bg_hex);
        g_bg_image_path = utf8_to_wstring(bg_img);
        g_elements.clear();
    }

    void AddElement(const string& type, const string& id, const string& text,
                    int x, int y, int w, int h, int align, const string& color_hex,
                    int font_size, const string& font_family, bool glass, const string& run_cmd) {
        UIElement el;
        el.type = type; el.id = id; el.text = utf8_to_wstring(text);
        el.x = x; el.y = y; el.w = w; el.h = h; el.align = align;
        el.txt_color = hex_to_rgb(color_hex);
        el.f_size = font_size;
        el.f_style = utf8_to_wstring(font_family.empty() ? "Segoe UI" : font_family);
        el.is_glass = glass;
        if (!run_cmd.empty()) {
            el.has_run_action = true;
            el.run_target = run_cmd;
        }
        g_elements.push_back(el);
    }

    void Show() {
        GdiplusStartupInput gsi;
        ULONG_PTR token;
        GdiplusStartup(&token, &gsi, NULL);

        if (!g_bg_image_path.empty()) {
            g_pBgImage = Gdiplus::Image::FromFile(g_bg_image_path.c_str());
        }

        INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icex);

        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_HREDRAW | CS_VREDRAW, GrassWndProc, 0, 0,
                           GetModuleHandle(NULL), NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL,
                           L"GrassGreenNativeClass", NULL };
        RegisterClassExW(&wc);

        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT wr = { 0, 0, g_win_w, g_win_h };
        AdjustWindowRectEx(&wr, style, FALSE, 0);

        HWND hwnd = CreateWindowExW(0, L"GrassGreenNativeClass", g_win_title.c_str(), style,
                                    CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                                    NULL, NULL, GetModuleHandle(NULL), NULL);

        for (size_t i = 0; i < g_elements.size(); ++i) {
            auto& el = g_elements[i];
            if (el.align == 1) el.x = (g_win_w - el.w) / 2;
            else if (el.align == 2) el.x = 40;
            else if (el.align == 3) el.x = g_win_w - el.w - 40;

            if (el.type == "button") {
                el.hwnd = CreateWindowExW(0, L"BUTTON", el.text.c_str(),
                                          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
                                          el.x, el.y, el.w, el.h, hwnd, (HMENU)(i + 100), GetModuleHandle(NULL), NULL);
                OldButtonProc = (WNDPROC)SetWindowLongPtr(el.hwnd, GWLP_WNDPROC, (LONG_PTR)BtnSubclass);
                el.hfont = CreateFontW(el.f_size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, el.f_style.c_str());
                SendMessageW(el.hwnd, WM_SETFONT, (WPARAM)el.hfont, TRUE);
            } else if (el.type == "enter") {
                el.hwnd = CreateWindowExW(0, L"EDIT", el.text.c_str(),
                                          WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_CENTER | WS_BORDER,
                                          el.x, el.y, el.w, el.h, hwnd, (HMENU)(i + 100), GetModuleHandle(NULL), NULL);
                el.hfont = CreateFontW(el.f_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, el.f_style.c_str());
                SendMessageW(el.hwnd, WM_SETFONT, (WPARAM)el.hfont, TRUE);
            }
        }

        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        GdiplusShutdown(token);
    }
}
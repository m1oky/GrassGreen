// =============================================================================
//  GrassGreen 4.3.0 (G&G) — Sovereign Graphical Installer (setup.exe)
//  Классический строгий Windows-стиль: белый основной фон, чёрный текст.
// =============================================================================

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "payload_data.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

// Идентификаторы элементов управления
#define ID_BTN_BROWSE    1001
#define ID_BTN_INSTALL   1002
#define ID_BTN_LAUNCH    1003
#define ID_BTN_EXIT      1004
#define ID_EDIT_PATH     1005
#define ID_CHK_PATH      1006
#define ID_CHK_ASSOC     1007
#define ID_CHK_GDB       1008
#define ID_CHK_DESKTOP   1009
#define ID_PROGRESS      1010
#define ID_LIST_LOG      1011

// Классическая строгая палитра Windows (Белый основной, черный/темный второстепенный)
static const COLORREF COLOR_WHITE         = RGB(255, 255, 255); // Чистый белый
static const COLORREF COLOR_HEADER_BG    = RGB(248, 249, 250); // Светло-серый баннер
static const COLORREF COLOR_TEXT_BLACK   = RGB(20, 20, 20);    // Основной глубокий черный
static const COLORREF COLOR_TEXT_MUTED   = RGB(100, 110, 120); // Аккуратный серый текст
static const COLORREF COLOR_BORDER_LINE  = RGB(218, 222, 228); // Тонкая серая линия
static const COLORREF COLOR_LOG_BG       = RGB(250, 252, 255); // Фон списка действий

static HBRUSH g_hbrWhite = NULL;
static HBRUSH g_hbrHeader = NULL;
static HBRUSH g_hbrLog = NULL;

static HFONT  g_hFontTitle = NULL;
static HFONT  g_hFontSubtitle = NULL;
static HFONT  g_hFontNormal = NULL;
static HFONT  g_hFontBold = NULL;
static HFONT  g_hFontCode = NULL;

static HWND g_hEditPath = NULL;
static HWND g_hChkPath = NULL;
static HWND g_hChkAssoc = NULL;
static HWND g_hChkGdb = NULL;
static HWND g_hChkDesktop = NULL;
static HWND g_hProgress = NULL;
static HWND g_hListLog = NULL;
static HWND g_hBtnInstall = NULL;
static HWND g_hBtnLaunch = NULL;
static HWND g_hBtnExit = NULL;

static bool g_isInstalled = false;

// -----------------------------------------------------------------------------
// Вспомогательная функция: добавление записи в лог
// -----------------------------------------------------------------------------
void AddLogMessage(HWND hwndLog, const std::wstring& msg) {
    if (!hwndLog) return;
    int idx = (int)SendMessageW(hwndLog, LB_ADDSTRING, 0, (LPARAM)msg.c_str());
    SendMessageW(hwndLog, LB_SETCURSEL, idx, 0);
    UpdateWindow(hwndLog);
}

// -----------------------------------------------------------------------------
// Добавление папки в PATH через реестр HKCU и HKLM + широковещательное оповещение
// -----------------------------------------------------------------------------
bool AddDirectoryToSystemPath(const std::wstring& dirToAdd, HWND hwndLog) {
    bool okUser = false;
    HKEY hKey = NULL;

    // 1. Модификация пользовательского окружения HKCU\Environment
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        wchar_t currentPath[32768] = { 0 };
        DWORD bufSize = sizeof(currentPath);
        DWORD valType = REG_SZ;

        LONG res = RegQueryValueExW(hKey, L"Path", NULL, &valType, (LPBYTE)currentPath, &bufSize);
        if (res != ERROR_SUCCESS) {
            currentPath[0] = 0;
            valType = REG_SZ;
        }

        std::wstring pathStr = currentPath;
        if (pathStr.find(dirToAdd) == std::wstring::npos) {
            if (!pathStr.empty() && pathStr.back() != L';') {
                pathStr += L";";
            }
            pathStr += dirToAdd;

            LONG setRes = RegSetValueExW(hKey, L"Path", 0, valType,
                (const BYTE*)pathStr.c_str(), (DWORD)((pathStr.size() + 1) * sizeof(wchar_t)));
            if (setRes == ERROR_SUCCESS) {
                okUser = true;
                AddLogMessage(hwndLog, L"  -> Папка успешно добавлена в переменную PATH (HKCU)");
            }
        } else {
            okUser = true;
            AddLogMessage(hwndLog, L"  -> Каталог уже присутствует в переменной PATH.");
        }
        RegCloseKey(hKey);
    }

    // 2. Попытка модификации системного окружения HKLM (если запущен от имени администратора)
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        wchar_t currentPath[32768] = { 0 };
        DWORD bufSize = sizeof(currentPath);
        DWORD valType = REG_SZ;

        if (RegQueryValueExW(hKey, L"Path", NULL, &valType, (LPBYTE)currentPath, &bufSize) == ERROR_SUCCESS) {
            std::wstring pathStr = currentPath;
            if (pathStr.find(dirToAdd) == std::wstring::npos) {
                if (!pathStr.empty() && pathStr.back() != L';') {
                    pathStr += L";";
                }
                pathStr += dirToAdd;
                if (RegSetValueExW(hKey, L"Path", 0, valType,
                    (const BYTE*)pathStr.c_str(), (DWORD)((pathStr.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS) {
                    AddLogMessage(hwndLog, L"  -> Системный PATH (HKLM) обновлен.");
                }
            }
        }
        RegCloseKey(hKey);
    }

    // 3. Отправка широковещательного сообщения WM_SETTINGCHANGE
    AddLogMessage(hwndLog, L"  -> Оповещение подсистемы Windows (WM_SETTINGCHANGE)...");
    DWORD_PTR dwResult = 0;
    SendMessageTimeoutW(
        HWND_BROADCAST,
        WM_SETTINGCHANGE,
        0,
        (LPARAM)L"Environment",
        SMTO_ABORTIFHUNG,
        2000,
        &dwResult
    );

    return okUser;
}

// -----------------------------------------------------------------------------
// Регистрация файловых ассоциаций и иконок для .grass и .gdb
// -----------------------------------------------------------------------------
void RegisterFileAssociations(const std::wstring& grgExePath, const std::wstring& iconsDir, HWND hwndLog) {
    HKEY hKey = NULL;

    std::wstring grassIcoPath = iconsDir + L"\\grass.ico";
    std::wstring gdbIcoPath   = iconsDir + L"\\gdb.ico";

    // 1. Ассоциация .grass
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.grass", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        const wchar_t* progId = L"GrassGreenFile";
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)progId, (DWORD)((wcslen(progId) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    std::vector<std::wstring> grassProgIds = { L"GrassGreenFile", L"GrassGreen.Source", L"grass_auto_file" };
    for (const auto& pid : grassProgIds) {
        std::wstring kPath = L"Software\\Classes\\" + pid;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            const wchar_t* desc = L"Исходный файл GrassGreen (G&G)";
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)desc, (DWORD)((wcslen(desc) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        std::wstring icoKey = kPath + L"\\DefaultIcon";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, icoKey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            std::wstring icoEntry = L"\"" + grassIcoPath + L"\",0";
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)icoEntry.c_str(), (DWORD)((icoEntry.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        std::wstring cmdKey = kPath + L"\\shell\\open\\command";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, cmdKey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            std::wstring cmdVal = L"\"" + grgExePath + L"\" \"%1\"";
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)cmdVal.c_str(), (DWORD)((cmdVal.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    }

    // 2. Ассоциация .gdb (GrassDB)
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.gdb", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        const wchar_t* progId = L"GrassGreen.Database";
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)progId, (DWORD)((wcslen(progId) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    std::vector<std::wstring> gdbProgIds = { L"GrassGreen.Database", L"GrassGreenDB", L"gdb_auto_file" };
    for (const auto& pid : gdbProgIds) {
        std::wstring kPath = L"Software\\Classes\\" + pid;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            const wchar_t* desc = L"База данных GrassDB";
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)desc, (DWORD)((wcslen(desc) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        std::wstring icoKey = kPath + L"\\DefaultIcon";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, icoKey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            std::wstring icoEntry = L"\"" + gdbIcoPath + L"\",0";
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)icoEntry.c_str(), (DWORD)((icoEntry.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    }

    // Оповещаем оболочку Windows об обновлении иконок и ассоциаций
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    AddLogMessage(hwndLog, L"  -> Иконки logo.svg (grass.ico) и gdb_logo.svg (gdb.ico) привязаны в Windows Explorer.");
}

// -----------------------------------------------------------------------------
// Создание ярлыка консоли на Рабочем столе
// -----------------------------------------------------------------------------
void CreateDesktopConsoleShortcut(const std::wstring& targetBinDir, HWND hwndLog) {
    wchar_t desktopPath[MAX_PATH] = { 0 };
    if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath) == S_OK) {
        std::wstring batPath = std::wstring(desktopPath) + L"\\GrassGreen Console.bat";
        
        std::wstring content =
            L"@echo off\r\n"
            L"title GrassGreen 4.3.0 (G&G) Interactive Environment\r\n"
            L"echo ========================================================\r\n"
            L"echo   GrassGreen 4.3.0 (G&G) Console Environment\r\n"
            L"echo   Команда grg доступна для сборки и запуска скриптов!\r\n"
            L"echo ========================================================\r\n"
            L"echo.\r\n"
            L"call grg -V\r\n"
            L"echo.\r\n"
            L"cmd.exe /k \"cd /d %USERPROFILE%\"\r\n";

        HANDLE hFile = CreateFileW(batPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            std::string utf8Content;
            int len = WideCharToMultiByte(CP_OEMCP, 0, content.c_str(), -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                utf8Content.resize(len);
                WideCharToMultiByte(CP_OEMCP, 0, content.c_str(), -1, &utf8Content[0], len, NULL, NULL);
                DWORD written = 0;
                WriteFile(hFile, utf8Content.c_str(), (DWORD)strlen(utf8Content.c_str()), &written, NULL);
            }
            CloseHandle(hFile);
            AddLogMessage(hwndLog, L"  -> Создан ярлык 'GrassGreen Console.bat' на Рабочем столе.");
        }
    }
}

// -----------------------------------------------------------------------------
// Поток установки
// -----------------------------------------------------------------------------
DWORD WINAPI InstallWorkerThread(LPVOID lpParam) {
    HWND hWnd = (HWND)lpParam;
    
    wchar_t installRoot[MAX_PATH] = { 0 };
    GetWindowTextW(g_hEditPath, installRoot, MAX_PATH);
    std::wstring rootPath = installRoot;
    if (rootPath.empty()) {
        rootPath = L"C:\\GrassGreen";
    }

    bool optPath    = (SendMessageW(g_hChkPath, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool optAssoc   = (SendMessageW(g_hChkAssoc, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool optDesktop = (SendMessageW(g_hChkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED);

    EnableWindow(g_hBtnInstall, FALSE);
    EnableWindow(g_hEditPath, FALSE);
    EnableWindow(g_hChkPath, FALSE);
    EnableWindow(g_hChkAssoc, FALSE);
    if (g_hChkGdb) EnableWindow(g_hChkGdb, FALSE);
    EnableWindow(g_hChkDesktop, FALSE);

    SendMessageW(g_hProgress, PBM_SETRANGE32, 0, 100);
    SendMessageW(g_hProgress, PBM_SETPOS, 5, 0);

    AddLogMessage(g_hListLog, L"Начало установки GrassGreen 4.3.0...");
    AddLogMessage(g_hListLog, L"Целевая папка: " + rootPath);

    // 1. Создание каталогов
    std::wstring binDir      = rootPath + L"\\bin";
    std::wstring iconsDir    = rootPath + L"\\icons";
    std::wstring examplesDir = rootPath + L"\\examples";

    SHCreateDirectoryExW(NULL, rootPath.c_str(), NULL);
    SHCreateDirectoryExW(NULL, binDir.c_str(), NULL);
    SHCreateDirectoryExW(NULL, iconsDir.c_str(), NULL);
    SHCreateDirectoryExW(NULL, examplesDir.c_str(), NULL);
    AddLogMessage(g_hListLog, L"[1/6] Создана структура каталогов (bin, icons, examples)...");
    SendMessageW(g_hProgress, PBM_SETPOS, 20, 0);
    Sleep(60);

    // 2. Распаковка grg.exe и grassc.exe
    std::wstring grgPath   = binDir + L"\\grg.exe";
    std::wstring grasscPath = binDir + L"\\grassc.exe";

    HANDLE hGrg = CreateFileW(grgPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hGrg == INVALID_HANDLE_VALUE) {
        wchar_t localApp[MAX_PATH] = { 0 };
        SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localApp);
        rootPath = std::wstring(localApp) + L"\\GrassGreen";
        binDir = rootPath + L"\\bin";
        iconsDir = rootPath + L"\\icons";
        examplesDir = rootPath + L"\\examples";
        SHCreateDirectoryExW(NULL, rootPath.c_str(), NULL);
        SHCreateDirectoryExW(NULL, binDir.c_str(), NULL);
        SHCreateDirectoryExW(NULL, iconsDir.c_str(), NULL);
        SHCreateDirectoryExW(NULL, examplesDir.c_str(), NULL);
        grgPath = binDir + L"\\grg.exe";
        grasscPath = binDir + L"\\grassc.exe";
        SetWindowTextW(g_hEditPath, rootPath.c_str());
        AddLogMessage(g_hListLog, L"  -> Перенаправление установки в " + rootPath);
        hGrg = CreateFileW(grgPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    if (hGrg != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hGrg, G_PAYLOAD_GRG_EXE, (DWORD)G_PAYLOAD_GRG_EXE_SIZE, &written, NULL);
        CloseHandle(hGrg);
        CopyFileW(grgPath.c_str(), grasscPath.c_str(), FALSE);
        AddLogMessage(g_hListLog, L"[2/6] Установлены бинарники компилятора grg.exe и grassc.exe (" +
            std::to_wstring(G_PAYLOAD_GRG_EXE_SIZE / 1024) + L" КБ)...");
    } else {
        AddLogMessage(g_hListLog, L"[ОШИБКА] Не удалось создать исполняемые файлы.");
        EnableWindow(g_hBtnInstall, TRUE);
        return 1;
    }
    SendMessageW(g_hProgress, PBM_SETPOS, 45, 0);
    Sleep(60);

    // 3. Распаковка иконок и векторных логотипов
    auto writeBinaryFile = [](const std::wstring& path, const unsigned char* data, size_t sz) {
        HANDLE hF = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hF != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(hF, data, (DWORD)sz, &w, NULL);
            CloseHandle(hF);
        }
    };
    auto writeTextFile = [](const std::wstring& path, const char* text) {
        HANDLE hF = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hF != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(hF, text, (DWORD)strlen(text), &w, NULL);
            CloseHandle(hF);
        }
    };

    writeBinaryFile(iconsDir + L"\\grass.ico", G_PAYLOAD_GRASS_ICO, G_PAYLOAD_GRASS_ICO_SIZE);
    writeBinaryFile(iconsDir + L"\\gdb.ico", G_PAYLOAD_GDB_ICO, G_PAYLOAD_GDB_ICO_SIZE);
    writeTextFile(iconsDir + L"\\logo.svg", G_PAYLOAD_LOGO_SVG);
    writeTextFile(iconsDir + L"\\gdb_logo.svg", G_PAYLOAD_GDB_LOGO_SVG);
    if (G_PAYLOAD_VSIX_SIZE > 0) {
        std::wstring vsixPath = binDir + L"\\grassgreen-4.3.0.vsix";
        writeBinaryFile(vsixPath, G_PAYLOAD_VSIX, G_PAYLOAD_VSIX_SIZE);
        AddLogMessage(g_hListLog, L"  -> Распаковано расширение VS Code grassgreen-4.3.0.vsix...");
        _wsystem((L"code --install-extension \"" + vsixPath + L"\" --force >nul 2>&1").c_str());
    }
    AddLogMessage(g_hListLog, L"[3/6] Распакованы иконки grass.ico, gdb.ico и логотипы logo.svg, gdb_logo.svg...");
    SendMessageW(g_hProgress, PBM_SETPOS, 60, 0);
    Sleep(60);

    // 4. Запись bat/cmd лаунчеров и примеров
    writeTextFile(binDir + L"\\grg.cmd", G_PAYLOAD_GRG_CMD);
    writeTextFile(binDir + L"\\grass.cmd", G_PAYLOAD_GRASS_CMD);
    writeTextFile(binDir + L"\\gg.cmd", G_PAYLOAD_GG_CMD);
    writeTextFile(examplesDir + L"\\io_test.grass", G_PAYLOAD_IO_TEST);
    AddLogMessage(g_hListLog, L"[4/6] Установлены командные лаунчеры (grg.cmd, grass.cmd, gg.cmd)...");
    SendMessageW(g_hProgress, PBM_SETPOS, 75, 0);
    Sleep(60);

    // 5. Интеграция в системный PATH
    if (optPath) {
        AddLogMessage(g_hListLog, L"[5/6] Настройка переменной окружения PATH...");
        AddDirectoryToSystemPath(binDir, g_hListLog);
    } else {
        AddLogMessage(g_hListLog, L"[5/6] Настройка PATH пропущена пользователем.");
    }
    SendMessageW(g_hProgress, PBM_SETPOS, 88, 0);
    Sleep(60);

    // 6. Ассоциации файлов и ярлыки
    AddLogMessage(g_hListLog, L"[6/6] Настройка ассоциаций Проводника Windows...");
    if (optAssoc) {
        RegisterFileAssociations(grgPath, iconsDir, g_hListLog);
    }
    if (optDesktop) {
        CreateDesktopConsoleShortcut(binDir, g_hListLog);
    }

    SendMessageW(g_hProgress, PBM_SETPOS, 100, 0);
    AddLogMessage(g_hListLog, L"--------------------------------------------------");
    AddLogMessage(g_hListLog, L"Установка успешно завершена!");
    AddLogMessage(g_hListLog, L"Команда grg доступна в любом окне терминала.");

    g_isInstalled = true;
    EnableWindow(g_hBtnLaunch, TRUE);
    SetWindowTextW(g_hBtnInstall, L"Готово");
    EnableWindow(g_hBtnExit, TRUE);

    return 0;
}

// -----------------------------------------------------------------------------
// Автоматическая тихая установка без графического интерфейса
// -----------------------------------------------------------------------------
void PerformSilentInstall() {
    std::wstring rootPath = L"C:\\GrassGreen";
    std::wstring binDir      = rootPath + L"\\bin";
    std::wstring iconsDir    = rootPath + L"\\icons";
    std::wstring examplesDir = rootPath + L"\\examples";

    SHCreateDirectoryExW(NULL, rootPath.c_str(), NULL);
    SHCreateDirectoryExW(NULL, binDir.c_str(), NULL);
    SHCreateDirectoryExW(NULL, iconsDir.c_str(), NULL);
    SHCreateDirectoryExW(NULL, examplesDir.c_str(), NULL);

    std::wstring grgPath   = binDir + L"\\grg.exe";
    std::wstring grasscPath = binDir + L"\\grassc.exe";

    HANDLE hGrg = CreateFileW(grgPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hGrg == INVALID_HANDLE_VALUE) {
        wchar_t localApp[MAX_PATH] = { 0 };
        SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localApp);
        rootPath = std::wstring(localApp) + L"\\GrassGreen";
        binDir = rootPath + L"\\bin";
        iconsDir = rootPath + L"\\icons";
        examplesDir = rootPath + L"\\examples";
        SHCreateDirectoryExW(NULL, rootPath.c_str(), NULL);
        SHCreateDirectoryExW(NULL, binDir.c_str(), NULL);
        SHCreateDirectoryExW(NULL, iconsDir.c_str(), NULL);
        SHCreateDirectoryExW(NULL, examplesDir.c_str(), NULL);
        grgPath = binDir + L"\\grg.exe";
        grasscPath = binDir + L"\\grassc.exe";
        hGrg = CreateFileW(grgPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    if (hGrg != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hGrg, G_PAYLOAD_GRG_EXE, (DWORD)G_PAYLOAD_GRG_EXE_SIZE, &written, NULL);
        CloseHandle(hGrg);
        CopyFileW(grgPath.c_str(), grasscPath.c_str(), FALSE);
    }

    auto writeBinaryFile = [](const std::wstring& path, const unsigned char* data, size_t sz) {
        HANDLE hF = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hF != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(hF, data, (DWORD)sz, &w, NULL);
            CloseHandle(hF);
        }
    };
    auto writeTextFile = [](const std::wstring& path, const char* text) {
        HANDLE hF = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hF != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(hF, text, (DWORD)strlen(text), &w, NULL);
            CloseHandle(hF);
        }
    };

    writeBinaryFile(iconsDir + L"\\grass.ico", G_PAYLOAD_GRASS_ICO, G_PAYLOAD_GRASS_ICO_SIZE);
    writeBinaryFile(iconsDir + L"\\gdb.ico", G_PAYLOAD_GDB_ICO, G_PAYLOAD_GDB_ICO_SIZE);
    writeTextFile(iconsDir + L"\\logo.svg", G_PAYLOAD_LOGO_SVG);
    writeTextFile(iconsDir + L"\\gdb_logo.svg", G_PAYLOAD_GDB_LOGO_SVG);

    writeTextFile(binDir + L"\\grg.cmd", G_PAYLOAD_GRG_CMD);
    writeTextFile(binDir + L"\\grass.cmd", G_PAYLOAD_GRASS_CMD);
    writeTextFile(binDir + L"\\gg.cmd", G_PAYLOAD_GG_CMD);
    writeTextFile(examplesDir + L"\\io_test.grass", G_PAYLOAD_IO_TEST);

    AddDirectoryToSystemPath(binDir, NULL);
    RegisterFileAssociations(grgPath, iconsDir, NULL);
    CreateDesktopConsoleShortcut(binDir, NULL);
}

// -----------------------------------------------------------------------------
// Выбор папки через стандартный диалог Windows
// -----------------------------------------------------------------------------
void BrowseForInstallFolder(HWND hwndParent) {
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwndParent;
    bi.lpszTitle = L"Выберите папку для установки GrassGreen 4.3.0:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl != NULL) {
        wchar_t selectedFolder[MAX_PATH] = { 0 };
        if (SHGetPathFromIDListW(pidl, selectedFolder)) {
            SetWindowTextW(g_hEditPath, selectedFolder);
        }
        CoTaskMemFree(pidl);
    }
}

// -----------------------------------------------------------------------------
// Главная оконная процедура
// -----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // Создание шрифтов
        g_hFontTitle = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontSubtitle = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontBold = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontCode = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        // Кисти для классической белой темы
        g_hbrWhite = CreateSolidBrush(COLOR_WHITE);
        g_hbrHeader = CreateSolidBrush(COLOR_HEADER_BG);
        g_hbrLog = CreateSolidBrush(COLOR_LOG_BG);

        // 1. Поле ввода пути
        HWND hLblPath = CreateWindowW(L"STATIC", L"Папка для установки:", WS_CHILD | WS_VISIBLE,
            30, 85, 200, 20, hWnd, NULL, NULL, NULL);
        SendMessageW(hLblPath, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"C:\\GrassGreen",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            30, 108, 420, 26, hWnd, (HMENU)ID_EDIT_PATH, NULL, NULL);
        SendMessageW(g_hEditPath, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hBtnBrowse = CreateWindowW(L"BUTTON", L"Обзор...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            460, 107, 100, 28, hWnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);
        SendMessageW(hBtnBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // 2. Чекбоксы опций (классические системные)
        g_hChkPath = CreateWindowW(L"BUTTON", L"Добавить GrassGreen в системный PATH (команда grg)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            30, 146, 530, 22, hWnd, (HMENU)ID_CHK_PATH, NULL, NULL);
        SendMessageW(g_hChkPath, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_hChkPath, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hChkAssoc = CreateWindowW(L"BUTTON", L"Зарегистрировать иконки для .grass (logo.svg) и .gdb (gdb_logo.svg)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            30, 172, 530, 22, hWnd, (HMENU)ID_CHK_ASSOC, NULL, NULL);
        SendMessageW(g_hChkAssoc, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_hChkAssoc, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hChkDesktop = CreateWindowW(L"BUTTON", L"Создать ярлык командной строки на Рабочем столе",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            30, 198, 530, 22, hWnd, (HMENU)ID_CHK_DESKTOP, NULL, NULL);
        SendMessageW(g_hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(g_hChkDesktop, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // 3. Прогресс-бар
        g_hProgress = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            30, 230, 530, 16, hWnd, (HMENU)ID_PROGRESS, NULL, NULL);

        // 4. Список логов установки
        g_hListLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            30, 256, 530, 174, hWnd, (HMENU)ID_LIST_LOG, NULL, NULL);
        SendMessageW(g_hListLog, WM_SETFONT, (WPARAM)g_hFontCode, TRUE);
        AddLogMessage(g_hListLog, L"Готов к установке GrassGreen 4.3.0 (G&G).");
        AddLogMessage(g_hListLog, L"Нажмите 'Установить' для распаковки файлов и регистрации в системе.");

        // 5. Кнопки внизу
        g_hBtnInstall = CreateWindowW(L"BUTTON", L"Установить", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            30, 444, 130, 34, hWnd, (HMENU)ID_BTN_INSTALL, NULL, NULL);
        SendMessageW(g_hBtnInstall, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnLaunch = CreateWindowW(L"BUTTON", L"Запустить терминал", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
            170, 444, 180, 34, hWnd, (HMENU)ID_BTN_LAUNCH, NULL, NULL);
        SendMessageW(g_hBtnLaunch, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnExit = CreateWindowW(L"BUTTON", L"Выход", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            440, 444, 120, 34, hWnd, (HMENU)ID_BTN_EXIT, NULL, NULL);
        SendMessageW(g_hBtnExit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // 1. Белый фон для рабочей области
        FillRect(hdc, &rcClient, g_hbrWhite);

        // 2. Верхний заголовочный баннер
        RECT rcBanner = { 0, 0, rcClient.right, 70 };
        FillRect(hdc, &rcBanner, g_hbrHeader);

        // Тонкая разделительная линия под шапкой
        HPEN hPenLine = CreatePen(PS_SOLID, 1, COLOR_BORDER_LINE);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPenLine);
        MoveToEx(hdc, 0, 70, NULL);
        LineTo(hdc, rcClient.right, 70);

        // Тонкая линия над кнопками внизу
        MoveToEx(hdc, 0, 436, NULL);
        LineTo(hdc, rcClient.right, 436);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPenLine);

        // Текст заголовка (строгий глубокий черный)
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COLOR_TEXT_BLACK);
        HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFontTitle);
        TextOutW(hdc, 30, 14, L"GrassGreen 4.3.0 (G&G) — Мастер Установки", 40);

        // Текст подзаголовка (аккуратный серый)
        SetTextColor(hdc, COLOR_TEXT_MUTED);
        SelectObject(hdc, g_hFontSubtitle);
        TextOutW(hdc, 32, 42, L"Автоматическая установка компилятора, регистрация иконок и настройка PATH", 74);

        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, COLOR_TEXT_BLACK);
        SetBkColor(hdcStatic, COLOR_WHITE);
        return (INT_PTR)g_hbrWhite;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, COLOR_TEXT_BLACK);
        SetBkColor(hdcEdit, COLOR_WHITE);
        return (INT_PTR)g_hbrWhite;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdcList = (HDC)wParam;
        SetTextColor(hdcList, COLOR_TEXT_BLACK);
        SetBkColor(hdcList, COLOR_LOG_BG);
        return (INT_PTR)g_hbrLog;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case ID_BTN_BROWSE:
            BrowseForInstallFolder(hWnd);
            break;

        case ID_BTN_INSTALL: {
            CreateThread(NULL, 0, InstallWorkerThread, (LPVOID)hWnd, 0, NULL);
            break;
        }

        case ID_BTN_LAUNCH: {
            wchar_t installRoot[MAX_PATH] = { 0 };
            GetWindowTextW(g_hEditPath, installRoot, MAX_PATH);
            std::wstring binDir = std::wstring(installRoot) + L"\\bin";
            
            std::wstring cmdArgs = L"/k \"set PATH=" + binDir + L";%PATH% && title GrassGreen 4.3.0 Console && grg -V\"";
            ShellExecuteW(NULL, L"open", L"cmd.exe", cmdArgs.c_str(), NULL, SW_SHOW);
            break;
        }

        case ID_BTN_EXIT:
            PostQuitMessage(0);
            break;
        }
        break;
    }

    case WM_DESTROY: {
        if (g_hbrWhite) DeleteObject(g_hbrWhite);
        if (g_hbrHeader) DeleteObject(g_hbrHeader);
        if (g_hbrLog) DeleteObject(g_hbrLog);
        if (g_hFontTitle) DeleteObject(g_hFontTitle);
        if (g_hFontSubtitle) DeleteObject(g_hFontSubtitle);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontCode) DeleteObject(g_hFontCode);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Точка входа WinMain
// -----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    std::wstring fullCmdLine = GetCommandLineW();
    if (fullCmdLine.find(L"/silent") != std::wstring::npos ||
        fullCmdLine.find(L"--silent") != std::wstring::npos ||
        fullCmdLine.find(L"/s") != std::wstring::npos ||
        fullCmdLine.find(L"-s") != std::wstring::npos ||
        fullCmdLine.find(L"/y") != std::wstring::npos) {
        PerformSilentInstall();
        return 0;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"GrassGreenSetupClass";
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex)) {
        return 1;
    }

    int winWidth = 600;
    int winHeight = 530;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int xPos = (screenW - winWidth) / 2;
    int yPos = (screenH - winHeight) / 2;

    HWND hWnd = CreateWindowW(
        L"GrassGreenSetupClass",
        L"GrassGreen 4.3.0 (G&G) — Установка компилятора",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        xPos, yPos, winWidth, winHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

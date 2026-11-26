#include "cli/GrassInstaller.h"
#include "GrassVersion.h"
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;
namespace fs = std::filesystem;

void InstallGrassGreenEcosystem() {
    string self_path_str;
#if defined(_WIN32)
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    self_path_str = self_path;
#else
    char buf[1024];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        self_path_str = buf;
    } else {
        self_path_str = "grassc";
    }
#endif
    fs::path exe_path(self_path_str);
    fs::path root_dir = exe_path.parent_path();
    if (root_dir.filename() == "bin") root_dir = root_dir.parent_path();

    cout << "\n=== [GrassGreen (G&G) v" << GRASS_VERSION << " Installer] ===\n";

#if defined(_WIN32)
    {
        ofstream grgcmd(root_dir / "grg.cmd");
        grgcmd << "@echo off\n\"%~dp0bin\\grassc.exe\" %*\n";
    }
    {
        ofstream gcmd(root_dir / "grass.cmd");
        gcmd << "@echo off\n\"%~dp0bin\\grassc.exe\" ./! %*\n";
    }
    {
        ofstream ggcmd(root_dir / "gg.cmd");
        ggcmd << "@echo off\n\"%~dp0bin\\grassc.exe\" ./! %*\n";
    }
    fs::create_directories(root_dir / "bin");
    fs::path run_cmd_path = root_dir / "bin" / "grass-run.cmd";
    {
        ofstream runcmd(run_cmd_path, ios::binary);
        runcmd << "@echo off\r\n"
               << "chcp 65001 >nul\r\n"
               << "if \"%~1\"==\"\" goto :eof\r\n"
               << "cd /d \"%~dp1\"\r\n"
               << "title GrassGreen - %~nx1\r\n"
               << "if exist \"%~dp0grassc.exe\" (\r\n"
               << "    \"%~dp0grassc.exe\" -r \"%~1\"\r\n"
               << ") else (\r\n"
               << "    \"%~dp0grg.exe\" -r \"%~1\"\r\n"
               << ")\r\n"
               << "set \"GG_EXIT=%ERRORLEVEL%\"\r\n"
               << "echo.\r\n"
               << "echo ========================================================\r\n"
               << "echo [GrassGreen] Process exited with code %GG_EXIT%.\r\n"
               << "echo ========================================================\r\n"
               << "pause\r\n"
               << "exit /b %GG_EXIT%\r\n";
    }
    try {
        fs::copy_file(exe_path, root_dir / "bin" / "grg.exe", fs::copy_options::overwrite_existing);
    } catch (...) {}

    // Регистрация ассоциации .grass в Проводнике Windows (HKCU\Software\Classes)
    {
        std::string cmd_str = "\"" + run_cmd_path.string() + "\" \"%1\"";
        HKEY hk = NULL;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\.grass", 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL) == ERROR_SUCCESS) {
            const char* pid = "GrassGreenFile";
            RegSetValueExA(hk, NULL, 0, REG_SZ, (const BYTE*)pid, (DWORD)(strlen(pid) + 1));
            RegCloseKey(hk);
        }
        for (const char* pid : { "GrassGreenFile", "GrassGreen.Source", "grass_auto_file" }) {
            std::string openKey = std::string("Software\\Classes\\") + pid + "\\shell\\open\\command";
            if (RegCreateKeyExA(HKEY_CURRENT_USER, openKey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL) == ERROR_SUCCESS) {
                RegSetValueExA(hk, NULL, 0, REG_SZ, (const BYTE*)cmd_str.c_str(), (DWORD)(cmd_str.size() + 1));
                RegCloseKey(hk);
            }
        }
        for (const char* app : { "grg.exe", "grassc.exe" }) {
            std::string appKey = std::string("Software\\Classes\\Applications\\") + app + "\\shell\\open\\command";
            if (RegCreateKeyExA(HKEY_CURRENT_USER, appKey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL) == ERROR_SUCCESS) {
                RegSetValueExA(hk, NULL, 0, REG_SZ, (const BYTE*)cmd_str.c_str(), (DWORD)(cmd_str.size() + 1));
                RegCloseKey(hk);
            }
        }
    }
    cout << "[+] Созданы системные команды: grg <файл.grass>, grass <файл.grass>, gg <файл.grass> и bin/grass-run.cmd\n";
    cout << "[+] Ассоциация Проводника Windows для .grass настроена с паузой после завершения.\n";
    cout << "[+] Бинарник grg.exe продублирован в bin/grg.exe для интеграции в PATH.\n";
#else
    try {
        fs::copy_file(exe_path, root_dir / "bin" / "grg", fs::copy_options::overwrite_existing);
    } catch (...) {}
    cout << "[+] Бинарник grg продублирован в bin/grg для интеграции в PATH.\n";
#endif

    const char* userprofile = getenv("USERPROFILE");
#if !defined(_WIN32)
    if (!userprofile) userprofile = getenv("HOME");
#endif
    if (userprofile) {
        fs::path ext_dir = fs::path(userprofile) / ".vscode" / "extensions" / ("m1oky.grassgreen-lang-" GRASS_VERSION);
        fs::create_directories(ext_dir / "syntaxes");
        fs::create_directories(ext_dir / "snippets");

        ofstream svg(ext_dir / "logo.svg");
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">\n"
            << "  <defs>\n"
            << "    <linearGradient id=\"ggGrad\" x1=\"0%\" y1=\"100%\" x2=\"100%\" y2=\"0%\">\n"
            << "      <stop offset=\"0%\" stop-color=\"#00b359\"/>\n"
            << "      <stop offset=\"100%\" stop-color=\"#00ffcc\"/>\n"
            << "    </linearGradient>\n"
            << "  </defs>\n"
            << "  <rect x=\"8\" y=\"8\" width=\"112\" height=\"112\" rx=\"28\" fill=\"#0d1117\" stroke=\"url(#ggGrad)\" stroke-width=\"6\"/>\n"
            << "  <path d=\"M40 88 C40 52, 68 36, 88 28 C76 54, 72 72, 74 92\" fill=\"none\" stroke=\"url(#ggGrad)\" stroke-width=\"10\" stroke-linecap=\"round\"/>\n"
            << "  <path d=\"M36 92 C52 72, 72 64, 92 60\" fill=\"none\" stroke=\"#00ff88\" stroke-width=\"8\" stroke-linecap=\"round\"/>\n"
            << "  <circle cx=\"88\" cy=\"28\" r=\"6\" fill=\"#00ffcc\"/>\n"
            << "</svg>\n";
        svg.close();

        ofstream gdb_svg(ext_dir / "gdb_logo.svg");
        gdb_svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">\n"
                << "  <defs>\n"
                << "    <linearGradient id=\"gdbGrad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">\n"
                << "      <stop offset=\"0%\" stop-color=\"#00ffcc\"/>\n"
                << "      <stop offset=\"100%\" stop-color=\"#0088ff\"/>\n"
                << "    </linearGradient>\n"
                << "  </defs>\n"
                << "  <rect x=\"8\" y=\"8\" width=\"112\" height=\"112\" rx=\"28\" fill=\"#0d1117\" stroke=\"url(#gdbGrad)\" stroke-width=\"6\"/>\n"
                << "  <ellipse cx=\"64\" cy=\"36\" rx=\"34\" ry=\"12\" fill=\"#161b22\" stroke=\"url(#gdbGrad)\" stroke-width=\"5\"/>\n"
                << "  <path d=\"M30 36 V64 C30 71, 45 76, 64 76 C83 76, 98 71, 98 64 V36\" fill=\"none\" stroke=\"url(#gdbGrad)\" stroke-width=\"5\"/>\n"
                << "  <path d=\"M30 64 V92 C30 99, 45 104, 64 104 C83 104, 98 99, 98 92 V64\" fill=\"none\" stroke=\"url(#gdbGrad)\" stroke-width=\"5\"/>\n"
                << "  <circle cx=\"80\" cy=\"56\" r=\"4\" fill=\"#00ffcc\"/>\n"
                << "  <circle cx=\"80\" cy=\"84\" r=\"4\" fill=\"#0088ff\"/>\n"
                << "</svg>\n";
        gdb_svg.close();

        try {
            fs::copy_file(ext_dir / "logo.svg", root_dir / "logo.svg", fs::copy_options::overwrite_existing);
            fs::copy_file(ext_dir / "gdb_logo.svg", root_dir / "gdb_logo.svg", fs::copy_options::overwrite_existing);
        } catch (...) {}

        ofstream lconf(ext_dir / "language-configuration.json");
        lconf << "{\n"
              << "  \"comments\": { \"lineComment\": \"//\", \"blockComment\": [\"/*\", \"*/\"] },\n"
              << "  \"brackets\": [ [\"{\", \"}\"], [\"[\", \"]\"], [\"(\", \")\"] ],\n"
              << "  \"autoClosingPairs\": [\n"
              << "    { \"open\": \"{\", \"close\": \"}\" },\n"
              << "    { \"open\": \"[\", \"close\": \"]\" },\n"
              << "    { \"open\": \"(\", \"close\": \")\" },\n"
              << "    { \"open\": \"\\\"\", \"close\": \"\\\"\" },\n"
              << "    { \"open\": \"'\", \"close\": \"'\" },\n"
              << "    { \"open\": \"!\", \"close\": \"!\" }\n"
              << "  ]\n"
              << "}\n";
        lconf.close();

        ofstream tm(ext_dir / "syntaxes" / "grassgreen.tmLanguage.json");
        tm << "{\n"
           << "  \"name\": \"GrassGreen\",\n"
           << "  \"scopeName\": \"source.grass\",\n"
           << "  \"patterns\": [\n"
           << "    { \"name\": \"comment.line.grass\", \"match\": \"(//.*$|^\\\\s*\\\\*.*$)\" },\n"
           << "    { \"name\": \"comment.block.grass\", \"begin\": \"/\\\\*|\\\\*\\\\*\", \"end\": \"\\\\*/|\\\\*\\\\*\" },\n"
           << "    { \"name\": \"entity.name.tag.library.grass\", \"match\": \"![A-Za-z0-9_]+!\" },\n"
           << "    { \"name\": \"keyword.control.directive.grass\", \"match\": \"@[A-Za-z0-9_]+\" },\n"
           << "    { \"name\": \"string.quoted.double.grass\", \"begin\": \"\\\"\", \"end\": \"\\\"\" },\n"
           << "    { \"name\": \"string.quoted.single.grass\", \"begin\": \"'\", \"end\": \"'\" },\n"
           << "    { \"name\": \"constant.other.color.grass\", \"match\": \"#[0-9a-fA-F]{3,8}\\\\b\" },\n"
           << "    { \"name\": \"keyword.control.grass\", \"match\": \"\\\\b(if|else|chek|check|for|while|again|loop|break|stop|stoped|return|exit|wait|sleep|window|gout|gin|cout|cin|endl|text|script-start|script-end|on-click|end-click|on-input|end-input|on-hover|end-hover|on-key|end-key|every|end-every|end-wait|end-chek|table|end-table|box-start|box-end|row-start|row-end|col-start|col-end)\\\\b\" },\n"
           << "    { \"name\": \"storage.type.grass\", \"match\": \"\\\\b(int|take|void|func|alt|fn|string|str|float|double|num|bool|auto|set|var|let|fany|invent)\\\\b\" },\n"
           << "    { \"name\": \"support.function.builtin.grass\", \"match\": \"\\\\b(main|gin\\\\.getline|gin|input\\\\.getline|input|print|println|ask|random|random_txt|stop_random|play_random|memory_usit|memory_separ|memory_auto|memory_clean|memory_free|memory_alloc|memory_free_block|memory_limit|memory_dump|len|length|upper|lower|substr|replace|contains|trim|to_str|to_num|typeof|clear|cls|color|time|clock_ms|beep|msgbox|system|sqrt|discor|sin|cos|abs|round|floor|ceil|pow|clamp|min|max|rand|lerp|creat|redact|read|exists|remove|open|run|db_open|db_save|db_set|db_get|db_del|db_has|db_inc|db_dec|db_clear|db_backup|db_export_json|db_hash|db_encrypt|db_decrypt|gui_cmd|gui_get|gui_set|gui_show|gui_file|web_start|web_compile|surface|auto_size_display|border-radius-app|border_radius_app|topmost|z-index|z_index|layer|clock|clock-format|engine|vd_plus|gradient|glow|border|hover-color|active-color|placeholder|title|size-top|theme|fon|iconP|button|label|enter|icon|video|window-ob|middle|left|right|text-color|button-color|font-size|font-style|radius|pad|shadow|glass|click-ob|switch|set-text|set-val|clear-val|repaint-color|repaint-bg|hide|show|toggle|play-anim)\\\\b\" },\n"
           << "    { \"name\": \"constant.numeric.grass\", \"match\": \"\\\\b\\\\d+(\\\\.\\\\d+)?\\\\b\" },\n"
           << "    { \"name\": \"constant.language.boolean.grass\", \"match\": \"\\\\b(true|false)\\\\b\" }\n"
           << "  ]\n"
           << "}\n";
        tm.close();

        ofstream snip(ext_dir / "snippets" / "grassgreen.json");
        snip << "{\n"
             << "  \"Main Entry Point\": {\n"
             << "    \"prefix\": [\"main\", \"int main\"],\n"
             << "    \"body\": [\"!GrassGreen!\", \"\", \"int main() {\", \"\\t$0\", \"\\treturn 0;\", \"}\"],\n"
             << "    \"description\": \"Главная функция int main() {}\"\n"
             << "  },\n"
             << "  \"GUI Window Block\": {\n"
             << "    \"prefix\": \"window\",\n"
             << "    \"body\": [\"!GrassGreen!\", \"!interface!\", \"\", \"int main() {\", \"\\twindow {\", \"\\t\\ttitle(\\\"${1:GrassGreen App}\\\");\", \"\\t\\tsize(520, 480);\", \"\\t\\tcolor(#12131a);\", \"\\t\\tbutton \\\"${2:Запустить}\\\" {\", \"\\t\\t\\twindow-ob(160, 200);\", \"\\t\\t\\tsize(200, 44);\", \"\\t\\t\\tradius(10);\", \"\\t\\t\\tshadow();\", \"\\t\\t}\", \"\\t\\tshow();\", \"\\t}\", \"\\treturn 0;\", \"}\"],\n"
             << "    \"description\": \"Создание графического окна GrassGUI в едином синтаксисе\"\n"
             << "  }\n"
             << "}\n";
        snip.close();

        ofstream pkg(ext_dir / "package.json");
        pkg << "{\n"
            << "  \"name\": \"grassgreen-lang\",\n"
            << "  \"displayName\": \"GrassGreen Official\",\n"
            << "  \"description\": \"Official language support for GrassGreen (.grass, .gdb)\",\n"
            << "  \"version\": \"" << GRASS_VERSION << "\",\n"
            << "  \"publisher\": \"m1oky\",\n"
            << "  \"engines\": { \"vscode\": \"^1.60.0\" },\n"
            << "  \"contributes\": {\n"
            << "    \"languages\": [\n"
            << "      {\n"
            << "        \"id\": \"grassgreen\",\n"
            << "        \"aliases\": [\"GrassGreen\", \"grass\"],\n"
            << "        \"extensions\": [\".grass\"],\n"
            << "        \"configuration\": \"./language-configuration.json\",\n"
            << "        \"icon\": { \"light\": \"./logo.svg\", \"dark\": \"./logo.svg\" }\n"
            << "      },\n"
            << "      {\n"
            << "        \"id\": \"grassdb\",\n"
            << "        \"aliases\": [\"GrassDB\", \"gdb\"],\n"
            << "        \"extensions\": [\".gdb\"],\n"
            << "        \"configuration\": \"./language-configuration.json\",\n"
            << "        \"icon\": { \"light\": \"./gdb_logo.svg\", \"dark\": \"./gdb_logo.svg\" }\n"
            << "      }\n"
            << "    ],\n"
            << "    \"grammars\": [\n"
            << "      {\n"
            << "        \"language\": \"grassgreen\",\n"
            << "        \"scopeName\": \"source.grass\",\n"
            << "        \"path\": \"./syntaxes/grassgreen.tmLanguage.json\"\n"
            << "      },\n"
            << "      {\n"
            << "        \"language\": \"grassdb\",\n"
            << "        \"scopeName\": \"source.grass\",\n"
            << "        \"path\": \"./syntaxes/grassgreen.tmLanguage.json\"\n"
            << "      }\n"
            << "    ],\n"
            << "    \"snippets\": [\n"
            << "      {\n"
            << "        \"language\": \"grassgreen\",\n"
            << "        \"path\": \"./snippets/grassgreen.json\"\n"
            << "      }\n"
            << "    ]\n"
            << "  }\n"
            << "}\n";
        pkg.close();
        cout << "[+] Установлены иконки VS Code: logo.svg (.grass) и gdb_logo.svg (.gdb)!\n";
    }

    fs::create_directories(root_dir / ".vscode");
    {
        ofstream vset(root_dir / ".vscode" / "settings.json");
        vset << "{\n  \"files.associations\": { \"*.grass\": \"grassgreen\", \"*.gdb\": \"grassdb\" }\n}\n";
    }
    {
        ofstream tasks(root_dir / ".vscode" / "tasks.json");
        tasks << "{\n"
              << "  \"version\": \"2.0.0\",\n"
              << "  \"tasks\": [\n"
              << "    {\n"
              << "      \"label\": \"Run GrassGreen Script\",\n"
              << "      \"type\": \"shell\",\n"
              << "      \"command\": \"& \\\"${workspaceFolder}\\\\bin\\\\grassc.exe\\\" ./! \\\"${file}\\\"\",\n"
              << "      \"group\": { \"kind\": \"build\", \"isDefault\": true },\n"
              << "      \"presentation\": { \"reveal\": \"always\", \"panel\": \"shared\" }\n"
              << "    }\n"
              << "  ]\n"
              << "}\n";
    }
    cout << "[+] Настроена сборка по Ctrl+Shift+B!\n";
    cout << "=== Установка завершена! Перезапусти окно VS Code (Reload Window) ===\n\n";
}

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
        tm << R"GRASS_TM({
  "name": "GrassGreen",
  "scopeName": "source.grass",
  "patterns": [
    { "name": "comment.line.double-slash.grass", "match": "//.*$" },
    { "name": "comment.block.grass", "begin": "/\\*", "end": "\\*/" },
    { "name": "entity.name.tag.library.grass", "match": "![A-Za-z0-9_+\\-]+!" },
    { "name": "keyword.control.directive.grass", "match": "@[A-Za-z0-9_]+" },
    { "name": "keyword.control.import.grass", "match": "\\b(import|include|using|namespace|as)\\b" },
    { "name": "string.quoted.double.grass", "begin": "\"", "end": "\"", "patterns": [{ "name": "constant.character.escape.grass", "match": "\\\\." }] },
    { "name": "string.quoted.single.grass", "begin": "'", "end": "'", "patterns": [{ "name": "constant.character.escape.grass", "match": "\\\\." }] },
    { "name": "constant.other.color.grass", "match": "#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{4}|[0-9a-fA-F]{3})\\b" },
    { "name": "keyword.control.grass", "match": "\\b(if|else|check|chek|for|while|again|loop|break|stop|continue|next|switch|case|default|return|exit|wait|sleep|new|delete|match|window|gout|gin|cout|cin|endl|script-start|script-end|on-click|end-click|on-input|end-input|on-hover|end-hover|on-key|end-key|every|end-every|end-wait|end-chek|table|end-table|box-start|box-end|row-start|row-end|col-start|col-end)\\b" },
    { "name": "storage.modifier.grass", "match": "\\b(public|private|protected|final|virtual|override|abstract|static|const|inline|export|volatile|extends|implements)\\b" },
    { "name": "storage.type.grass", "match": "\\b(class|struct|enum|int|take|void|fn|fun|func|alt|string|String|str|float|double|num|bool|char|byte|ptr|vec|vector|array|auto|set|var|let|size_t|u8|u16|u32|u64|i8|i16|i32|i64|f32|f64|Vec3|Mat4)\\b" },
    { "name": "variable.language.this.grass", "match": "\\b(this|super|self)\\b" },
    { "name": "constant.language.grass", "match": "\\b(true|false|null|nullptr|nil|BLACK|BLUE|GREEN|CYAN|RED|MAGENTA|BROWN|LIGHT_GRAY|DARK_GRAY|LIGHT_BLUE|LIGHT_GREEN|LIGHT_CYAN|LIGHT_RED|LIGHT_MAGENTA|YELLOW|WHITE)\\b" },
    { "match": "\\b(?:class|struct|enum(?:\\s+class|\\s+struct)?)\\s+([A-Za-z_][A-Za-z0-9_]*)", "captures": { "1": { "name": "entity.name.type.class.grass" } } },
    { "match": "\\bnew\\s+([A-Za-z_][A-Za-z0-9_]*)", "captures": { "1": { "name": "entity.name.type.class.grass" } } },
    { "name": "support.function.builtin.grass", "match": "\\b(main|gin\\.getline|gin|input\\.getline|input|print|println|ask|push|push_back|pop|pop_back|size|len|length|empty|clear|reserve|capacity|data|upper|lower|substr|replace|contains|trim|to_str|to_string|to_num|typeof|cls|color|time|clock_ms|beep|msgbox|system|sqrt|discor|sin|cos|tan|atan2|abs|round|floor|ceil|pow|clamp|min|max|rand|lerp|read_key|draw_box|menu|progress_bar|alloc|free|realloc|sizeof|v3_[A-Za-z0-9_]+|m4_[A-Za-z0-9_]+|g3d_[A-Za-z0-9_]+|g2d_[A-Za-z0-9_]+|phys_[A-Za-z0-9_]+|gfx_[A-Za-z0-9_]+|fs_[A-Za-z0-9_]+|hw_[A-Za-z0-9_]+|db_[A-Za-z0-9_]+|json_[A-Za-z0-9_]+|gui_[A-Za-z0-9_]+|web_[A-Za-z0-9_]+)\\b" },
    { "match": "\\b([A-Za-z_][A-Za-z0-9_]*)\\s*(?=\\()", "captures": { "1": { "name": "entity.name.function.grass" } } },
    { "name": "constant.numeric.grass", "match": "\\b(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\\d+(?:\\.\\d+)?)\\b" },
    { "name": "keyword.operator.grass", "match": "::|->|=>|<<|>>|==|!=|<=|>=|&&|\\|\\||\\+=|-=|\\*=|/=|\\+\\+|--|[+\\-*/%&|^~!<>?=]" }
  ]
}
)GRASS_TM";
        tm.close();

        ofstream snip(ext_dir / "snippets" / "grassgreen.json");
        snip << "{\n"
             << "  \"Main Entry Point\": {\n"
             << "    \"prefix\": [\"main\", \"int main\"],\n"
             << "    \"body\": [\"!GrassGreen!\", \"\", \"int main() {\", \"\\t$0\", \"\\treturn 0;\", \"}\"],\n"
             << "    \"description\": \"Main entry point int main()\"\n"
             << "  },\n"
             << "  \"Class Definition\": {\n"
             << "    \"prefix\": \"class\",\n"
             << "    \"body\": [\"public class ${1:MyClass} {\", \"public:\", \"\\t${1:MyClass}() {\", \"\\t\\t$0\", \"\\t}\", \"}\"],\n"
             << "    \"description\": \"OOP Class template\"\n"
             << "  },\n"
             << "  \"GUI Window Block\": {\n"
             << "    \"prefix\": \"window\",\n"
             << "    \"body\": [\"!GrassGreen!\", \"!interface!\", \"\", \"int main() {\", \"\\twindow {\", \"\\t\\ttitle(\\\"${1:GrassGreen App}\\\");\", \"\\t\\tsize(520, 480);\", \"\\t\\tcolor(#12131a);\", \"\\t\\tbutton \\\"${2:Run}\\\" {\", \"\\t\\t\\twindow-ob(160, 200);\", \"\\t\\t\\tsize(200, 44);\", \"\\t\\t\\tradius(10);\", \"\\t\\t\\tshadow();\", \"\\t\\t}\", \"\\t\\tshow();\", \"\\t}\", \"\\treturn 0;\", \"}\"],\n"
             << "    \"description\": \"GrassGUI window block\"\n"
             << "  }\n"
             << "}\n";
        snip.close();

        ofstream extjs(ext_dir / "extension.js");
        extjs << R"GRASS_JS('use strict';
const vscode = require('vscode');
const fs = require('fs');
const path = require('path');

function stripStringsAndComments(text) {
    let out = '';
    let i = 0;
    const n = text.length;
    while (i < n) {
        const ch = text[i];
        const nx = i + 1 < n ? text[i + 1] : '';
        if (ch === '/' && nx === '/') {
            while (i < n && text[i] !== '\n') { out += ' '; i++; }
            continue;
        }
        if (ch === '/' && nx === '*') {
            out += '  '; i += 2;
            while (i < n && !(text[i] === '*' && text[i + 1] === '/')) {
                out += text[i] === '\n' ? '\n' : ' ';
                i++;
            }
            if (i + 1 < n) { out += '  '; i += 2; }
            continue;
        }
        if (ch === '"' || ch === "'") {
            const q = ch;
            out += q; i++;
            while (i < n && text[i] !== q) {
                if (text[i] === '\\' && i + 1 < n) { out += '  '; i += 2; }
                else { out += text[i] === '\n' ? '\n' : ' '; i++; }
            }
            if (i < n) { out += q; i++; }
            continue;
        }
        out += ch;
        i++;
    }
    return out;
}

function findMatchingBrace(text, openIdx) {
    let depth = 0;
    for (let i = openIdx; i < text.length; i++) {
        if (text[i] === '{') depth++;
        else if (text[i] === '}') {
            depth--;
            if (depth === 0) return i;
        }
    }
    return text.length - 1;
}

function findImportedFiles(rawText, baseDir, visited, outFiles) {
    if (!baseDir || visited.size >= 64) return;
    const candidates = [];
    const bangRe = /!([A-Za-z0-9_+\-]+)!/g;
    let m;
    while ((m = bangRe.exec(rawText)) !== null) {
        candidates.push(m[1]);
    }
    const impRe = /\bimport\s+"([^"]+)"/g;
    while ((m = impRe.exec(rawText)) !== null) {
        candidates.push(m[1]);
    }
    for (const mod of candidates) {
        const fileNames = mod.endsWith('.grass') ? [mod] : [mod + '.grass', mod];
        const searchRoots = [baseDir, 'C:\\GrassGreen\\stdlib'];
        for (const root of searchRoots) {
            for (const fn of fileNames) {
                const full = path.resolve(root, fn);
                const key = full.toLowerCase();
                if (visited.has(key)) continue;
                try {
                    if (fs.existsSync(full) && fs.statSync(full).isFile()) {
                        visited.add(key);
                        const content = fs.readFileSync(full, 'utf8');
                        outFiles.push({ file: full, text: content });
                        findImportedFiles(content, path.dirname(full), visited, outFiles);
                        break;
                    }
                } catch (_) {}
            }
        }
    }
}

function createSymbolTable() {
    return {
        enums: new Map(),
        classes: new Map(),
        functions: new Map(),
        vars: new Map(),
        classSpans: []
    };
}

function parseParamsString(paramStr, scopeVars) {
    const res = [];
    if (!paramStr || !paramStr.trim()) return res;
    let depth = 0;
    let cur = '';
    const parts = [];
    for (const ch of paramStr) {
        if (ch === '<' || ch === '(' || ch === '[') depth++;
        else if (ch === '>' || ch === ')' || ch === ']') depth--;
        if (ch === ',' && depth === 0) { parts.push(cur); cur = ''; }
        else cur += ch;
    }
    if (cur.trim()) parts.push(cur);
    for (const p of parts) {
        const vecM = /^\s*(?:vec|vector)\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*\*?\s*>\s*\*?\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(p);
        if (vecM) {
            res.push({ name: vecM[2], type: 'vec<' + vecM[1] + '>' });
            if (scopeVars) scopeVars.set(vecM[2], { type: 'vec', vecElem: vecM[1] });
            continue;
        }
        const m = /^\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\*?\s+([A-Za-z_][A-Za-z0-9_]*)/.exec(p);
        if (m) {
            res.push({ name: m[2], type: m[1] });
            if (scopeVars) scopeVars.set(m[2], { type: m[1] });
        }
    }
    return res;
}

function parseSourceIntoTable(rawText, table, isCurrentFile) {
    const clean = stripStringsAndComments(rawText);

    // 1. Enums
    const enumRe = /\benum(?:\s+(?:class|struct))?\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([^}]*)\}/g;
    let m;
    while ((m = enumRe.exec(clean)) !== null) {
        const ename = m[1];
        const body = m[2];
        const entries = [];
        for (const item of body.split(/[,;\n]/)) {
            const em = /^\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(item);
            if (em) entries.push(em[1]);
        }
        table.enums.set(ename, entries);
    }

    // 2. Classes & Structs
    const classRe = /(?:(?:public|private|protected|final|abstract|export)\s+)*\b(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*(?::\s*(?:public|private|protected)?\s*([A-Za-z_][A-Za-z0-9_]*)|extends\s+([A-Za-z_][A-Za-z0-9_]*)))?(?:\s+implements\s+[^{]+)?\s*\{/g;
    const classRanges = [];
    while ((m = classRe.exec(clean)) !== null) {
        const cname = m[2];
        const baseName = m[3] || m[4] || '';
        const openBrace = clean.indexOf('{', m.index + m[0].length - 1);
        if (openBrace < 0) continue;
        const closeBrace = findMatchingBrace(clean, openBrace);
        classRanges.push({ start: openBrace, end: closeBrace, name: cname });
        if (isCurrentFile) table.classSpans.push({ start: openBrace, end: closeBrace, name: cname });

        const cls = table.classes.get(cname) || { name: cname, base: baseName, fields: new Map(), methods: new Map() };
        if (baseName) cls.base = baseName;
        const body = clean.slice(openBrace + 1, closeBrace);

        // Scan top-level members inside class (brace depth 0 relative to class body)
        let i = 0;
        let depth = 0;
        let stmtStart = 0;
        while (i < body.length) {
            const ch = body[i];
            if (ch === '{') {
                if (depth === 0) {
                    const header = body.slice(stmtStart, i).trim();
                    const endBlock = findMatchingBrace(body, i);
                    const methM = /(?:(?:public|private|protected|final|virtual|override|static|inline)\s+)*(?:([A-Za-z_][A-Za-z0-9_<>:\s\*]*?)\s+)?([~]?[A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/.exec(header);
                    if (methM) {
                        const mname = methM[2];
                        const retT = (methM[1] || (mname === cname ? cname : 'void')).trim();
                        const params = parseParamsString(methM[3], null);
                        if (!mname.startsWith('~')) {
                            cls.methods.set(mname, { name: mname, ret: retT, params });
                        }
                    }
                    i = endBlock + 1;
                    stmtStart = i;
                    continue;
                }
                depth++;
            } else if (ch === '}') {
                if (depth > 0) depth--;
            } else if ((ch === ';' || ch === '\n') && depth === 0) {
                const line = body.slice(stmtStart, i).replace(/^\s*(?:public|private|protected)\s*:\s*/g, '').trim();
                stmtStart = i + 1;
                if (line && !line.includes('(')) {
                    const vecF = /^(?:(?:public|private|protected|final|static|const)\s+)*(?:vec|vector)\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*\*?\s*>\s*\*?\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(line);
                    if (vecF) {
                        cls.fields.set(vecF[2], { name: vecF[2], type: 'vec', vecElem: vecF[1] });
                    } else {
                        const fldM = /^(?:(?:public|private|protected|final|static|const)\s+)*([A-Za-z_][A-Za-z0-9_:]*)\s*\*?\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*=.*)?$/.exec(line);
                        if (fldM && !['public', 'private', 'protected', 'return'].includes(fldM[1])) {
                            cls.fields.set(fldM[2], { name: fldM[2], type: fldM[1] });
                        } else {
                            const colM = /^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(?:vec|vector)\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>/.exec(line);
                            if (colM) cls.fields.set(colM[1], { name: colM[1], type: 'vec', vecElem: colM[2] });
                        }
                    }
                }
            }
            i++;
        }
        table.classes.set(cname, cls);
    }

    // 3. Global Functions & Local/Global Variables
    const fnRe = /(?:(?:public|private|protected|static|inline|export)\s+)*(vec\s*<[^>]+>|vector\s*<[^>]+>|[A-Za-z_][A-Za-z0-9_:]*)\s*\*?\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*\{/g;
    while ((m = fnRe.exec(clean)) !== null) {
        const insideClass = classRanges.some(r => m.index > r.start && m.index < r.end);
        if (insideClass) continue;
        const fname = m[2];
        if (['if', 'while', 'for', 'switch', 'check'].includes(fname)) continue;
        const params = parseParamsString(m[3], isCurrentFile ? table.vars : null);
        table.functions.set(fname, { name: fname, ret: m[1].trim(), params });
    }

    // Variables: vec<T> v;, Type v(args);, Type v = ..., take v = ...
    const scanText = isCurrentFile ? clean : '';
    if (scanText) {
        const lines = scanText.split(/\r?\n/);
        for (const rawLine of lines) {
            const line = rawLine.trim();
            if (!line || line.startsWith('//')) continue;

            const vecVar = /\b(?:vec|vector)\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*\*?\s*>\s*\*?\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(line);
            if (vecVar && !line.includes('(')) {
                table.vars.set(vecVar[2], { type: 'vec', vecElem: vecVar[1] });
                continue;
            }
            const takeNew = /\btake\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*new\s+([A-Za-z_][A-Za-z0-9_]*)/.exec(line);
            if (takeNew) {
                table.vars.set(takeNew[1], { type: takeNew[2] });
                continue;
            }
            const takeCtor = /\btake\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(/.exec(line);
            if (takeCtor && table.classes.has(takeCtor[2])) {
                table.vars.set(takeCtor[1], { type: takeCtor[2] });
                continue;
            }
            const takeArr = /\btake\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\[/.exec(line);
            if (takeArr) {
                table.vars.set(takeArr[1], { type: 'array' });
                continue;
            }
            const takeAny = /\btake\s+([A-Za-z_][A-Za-z0-9_]*)\s*=/.exec(line);
            if (takeAny) {
                table.vars.set(takeAny[1], { type: 'auto' });
                continue;
            }
            const typedCtor = /^(?:(?:const|final|static)\s+)*([A-Za-z_][A-Za-z0-9_]*)\s*\*?\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\([^)]*\)|=|;|$)/.exec(line);
            if (typedCtor) {
                const t = typedCtor[1];
                const v = typedCtor[2];
                if (!['return', 'if', 'else', 'while', 'for', 'delete', 'new', 'class', 'struct', 'enum', 'import', 'using', 'namespace', 'public', 'private', 'protected'].includes(t)) {
                    table.vars.set(v, { type: t });
                }
            }
        }
    }
}

function getAllClassMembers(table, className, visited = new Set()) {
    const out = { fields: new Map(), methods: new Map() };
    if (!className || visited.has(className)) return out;
    visited.add(className);
    const cls = table.classes.get(className);
    if (!cls) return out;
    if (cls.base) {
        const baseMem = getAllClassMembers(table, cls.base, visited);
        for (const [k, v] of baseMem.fields) out.fields.set(k, v);
        for (const [k, v] of baseMem.methods) out.methods.set(k, v);
    }
    for (const [k, v] of cls.fields) out.fields.set(k, v);
    for (const [k, v] of cls.methods) out.methods.set(k, v);
    return out;
}

const VEC_METHODS = [
    { name: 'push', detail: 'push(elem) -> void' },
    { name: 'push_back', detail: 'push_back(elem) -> void' },
    { name: 'pop', detail: 'pop() -> void' },
    { name: 'pop_back', detail: 'pop_back() -> void' },
    { name: 'size', detail: 'size() -> int' },
    { name: 'empty', detail: 'empty() -> bool' },
    { name: 'clear', detail: 'clear() -> void' },
    { name: 'reserve', detail: 'reserve(cap) -> void' },
    { name: 'capacity', detail: 'capacity() -> int' },
    { name: 'data', detail: 'data() -> T*' }
];

const BUILTIN_KEYWORDS = [
    'class', 'struct', 'enum', 'public', 'private', 'protected', 'final', 'virtual', 'override',
    'extends', 'implements', 'new', 'delete', 'this', 'import', 'using', 'namespace', 'as',
    'vec', 'vector', 'take', 'int', 'float', 'double', 'string', 'String', 'bool', 'char', 'byte', 'void',
    'if', 'else', 'for', 'while', 'break', 'continue', 'return', 'true', 'false', 'null', 'nullptr',
    'gout', 'gin', 'endl'
];

const BUILTIN_FUNCS = [
    'system', 'print', 'println', 'len', 'length', 'upper', 'lower', 'trim', 'substr', 'contains', 'replace',
    'to_str', 'to_num', 'sqrt', 'sin', 'cos', 'abs', 'min', 'max', 'pow', 'floor', 'ceil', 'round',
    'alloc', 'free', 'realloc', 'sizeof', 'color', 'clear', 'cls', 'read_key', 'clock_ms', 'sleep'
];

function activate(context) {
    const provider = vscode.languages.registerCompletionItemProvider(
        ['grassgreen', 'grassdb'],
        {
            provideCompletionItems(document, position) {
                const rawText = document.getText();
                const docPath = document.uri && document.uri.fsPath ? document.uri.fsPath : '';
                const baseDir = docPath ? path.dirname(docPath) : '';

                const table = createSymbolTable();
                const imported = [];
                const visited = new Set(docPath ? [docPath.toLowerCase()] : []);
                findImportedFiles(rawText, baseDir, visited, imported);
                for (const imp of imported) {
                    parseSourceIntoTable(imp.text, table, false);
                }
                parseSourceIntoTable(rawText, table, true);

                const cursorOffset = document.offsetAt(position);
                let currentClass = '';
                for (const span of table.classSpans) {
                    if (cursorOffset >= span.start && cursorOffset <= span.end) {
                        currentClass = span.name;
                    }
                }

                const linePrefix = document.lineAt(position).text.slice(0, position.character);

                // 1. Scope operator: EnumName::
                const scopeM = /([A-Za-z_][A-Za-z0-9_]*)::[A-Za-z0-9_]*$/.exec(linePrefix);
                if (scopeM) {
                    const sym = scopeM[1];
                    const items = [];
                    if (table.enums.has(sym)) {
                        for (const entry of table.enums.get(sym)) {
                            const it = new vscode.CompletionItem(entry, vscode.CompletionItemKind.EnumMember);
                            it.detail = sym + '::' + entry;
                            items.push(it);
                        }
                    }
                    return items;
                }

                // 2. Member access: obj. / ptr-> / vec[i]. / this. / EnumName.
                const memM = /([A-Za-z_][A-Za-z0-9_]*)(\[[^\]]*\])?(?:\.|->)[A-Za-z0-9_]*$/.exec(linePrefix);
                if (memM) {
                    const objName = memM[1];
                    const hasIndex = Boolean(memM[2]);
                    const items = [];

                    if (table.enums.has(objName) && !hasIndex) {
                        for (const entry of table.enums.get(objName)) {
                            const it = new vscode.CompletionItem(entry, vscode.CompletionItemKind.EnumMember);
                            it.detail = objName + '.' + entry;
                            items.push(it);
                        }
                        return items;
                    }

                    let resolvedType = '';
                    let resolvedVecElem = '';
                    if (objName === 'this' && currentClass) {
                        resolvedType = currentClass;
                    } else if (table.vars.has(objName)) {
                        const vb = table.vars.get(objName);
                        resolvedType = vb.type;
                        resolvedVecElem = vb.vecElem || '';
                    } else if (currentClass) {
                        const clsMem = getAllClassMembers(table, currentClass);
                        if (clsMem.fields.has(objName)) {
                            const fld = clsMem.fields.get(objName);
                            resolvedType = fld.type;
                            resolvedVecElem = fld.vecElem || '';
                        }
                    }

                    if ((resolvedType === 'vec' || resolvedType === 'array') && !hasIndex) {
                        for (const vm of VEC_METHODS) {
                            const it = new vscode.CompletionItem(vm.name, vscode.CompletionItemKind.Method);
                            it.detail = vm.detail;
                            it.insertText = new vscode.SnippetString(vm.name + '($1)');
                            items.push(it);
                        }
                        return items;
                    }

                    const targetClass = (resolvedType === 'vec' && hasIndex) ? resolvedVecElem : resolvedType;
                    if (targetClass && table.classes.has(targetClass)) {
                        const members = getAllClassMembers(table, targetClass);
                        for (const fld of members.fields.values()) {
                            const it = new vscode.CompletionItem(fld.name, vscode.CompletionItemKind.Field);
                            it.detail = (fld.type === 'vec' ? ('vec<' + (fld.vecElem || '') + '>') : fld.type) + ' ' + fld.name;
                            items.push(it);
                        }
                        for (const meth of members.methods.values()) {
                            const it = new vscode.CompletionItem(meth.name, vscode.CompletionItemKind.Method);
                            it.detail = meth.ret + ' ' + meth.name + '(' + meth.params.map(p => p.type + ' ' + p.name).join(', ') + ')';
                            it.insertText = new vscode.SnippetString(meth.name + '($1)');
                            items.push(it);
                        }
                        return items;
                    }
                    return items;
                }

                // 3. General completions (variables, class members if inside method, classes, enums, functions, builtins)
                const items = [];
                const seen = new Set();
                const pushItem = (label, kind, detail) => {
                    if (seen.has(label)) return;
                    seen.add(label);
                    const it = new vscode.CompletionItem(label, kind);
                    if (detail) it.detail = detail;
                    items.push(it);
                };

                for (const [vname, vb] of table.vars) {
                    const tdesc = vb.type === 'vec' ? ('vec<' + (vb.vecElem || '') + '>') : vb.type;
                    pushItem(vname, vscode.CompletionItemKind.Variable, tdesc + ' ' + vname);
                }
                if (currentClass && table.classes.has(currentClass)) {
                    const members = getAllClassMembers(table, currentClass);
                    for (const fld of members.fields.values()) {
                        pushItem(fld.name, vscode.CompletionItemKind.Field, fld.type + ' ' + fld.name + ' (this)');
                    }
                    for (const meth of members.methods.values()) {
                        pushItem(meth.name, vscode.CompletionItemKind.Method, meth.ret + ' ' + meth.name + '()');
                    }
                }
                for (const [cname, cls] of table.classes) {
                    pushItem(cname, vscode.CompletionItemKind.Class, 'class ' + cls.name);
                }
                for (const [ename, entries] of table.enums) {
                    pushItem(ename, vscode.CompletionItemKind.Enum, 'enum ' + ename);
                    for (const entry of entries) {
                        pushItem(entry, vscode.CompletionItemKind.EnumMember, ename + '::' + entry);
                    }
                }
                for (const [fname, fn] of table.functions) {
                    const it = new vscode.CompletionItem(fname, vscode.CompletionItemKind.Function);
                    it.detail = fn.ret + ' ' + fname + '(' + fn.params.map(p => p.type + ' ' + p.name).join(', ') + ')';
                    seen.add(fname);
                    items.push(it);
                }
                for (const fn of BUILTIN_FUNCS) pushItem(fn, vscode.CompletionItemKind.Function, 'builtin');
                for (const kw of BUILTIN_KEYWORDS) pushItem(kw, vscode.CompletionItemKind.Keyword, 'keyword');

                return items;
            }
        },
        '.', '>', ':'
    );
    context.subscriptions.push(provider);
}

function deactivate() {}

module.exports = { activate, deactivate };
)GRASS_JS";
        extjs.close();

        ofstream pkg(ext_dir / "package.json");
        pkg << "{\n"
            << "  \"name\": \"grassgreen-lang\",\n"
            << "  \"displayName\": \"GrassGreen Official\",\n"
            << "  \"description\": \"Official language support and live IntelliSense for GrassGreen (.grass, .gdb)\",\n"
            << "  \"version\": \"" << GRASS_VERSION << "\",\n"
            << "  \"publisher\": \"m1oky\",\n"
            << "  \"engines\": { \"vscode\": \"^1.60.0\" },\n"
            << "  \"activationEvents\": [\"onLanguage:grassgreen\", \"onLanguage:grassdb\"],\n"
            << "  \"main\": \"./extension.js\",\n"
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
        cout << "[+] Installed VS Code extension (syntax, autocomplete, icons).\n";
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

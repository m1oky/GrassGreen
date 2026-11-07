# GrassGreen Language, Encoding & Layout Specification (`docs/languages.md`)

This document defines the language, encoding, and keyboard layout subsystem in GrassGreen v4.2.0, centered around the restored `use utf -` directive and layout management.

---

## 1. Overview & Purpose

In terminal applications, operating system kernels, and graphical user interfaces, character representation depends on two critical components:
1. **Character Encoding (Code Page):** Dictates how binary byte streams in `gout <<` and `gin >>` map to visual glyphs in the terminal window.
2. **Keyboard Layout & Scancode Translation:** Dictates how physical hardware scancodes (from PS/2 ports `0x60`/`0x64` in bare-metal or keyboard events in hosted mode) translate into meaningful characters (e.g. English Latin vs. Russian Cyrillic).

The `use utf -` directive allows programmers to configure both with a single instruction at the beginning of a source file or dynamically at runtime.

---

## 2. Syntax

GrassGreen supports flexible, natural syntax variations:

```grass
// 1. By Sequential Layout ID (Recommended)
use utf - 8;       // Universal UTF-8 (Worldwide multi-language support)
use utf - 2;       // Russian Cyrillic (ЙЦУКЕН)
use utf - 1;       // English (US ASCII)

// 2. By Operating System Code Page
use utf - 65001;   // Windows / Linux UTF-8 Code Page
use utf - 1251;    // Windows Cyrillic Code Page
use utf - 866;     // DOS / Hardware VGA Cyrillic Code Page

// 3. By ISO Language Tag
use utf - ru;      // Russian
use utf - en;      // English
use utf - de;      // German

// 4. Compact Notation (without spaces)
use utf-8;
```

The directive can be placed:
* **Globally (Top-level):** Immediately after `!GrassGreen!` to configure terminal and engine encoding before `main()` executes.
* **Locally (Inside functions):** Inside `int main()` or any function to dynamically switch layouts during program execution.

---

## 3. Numbering Systems & Layout Reference

GrassGreen provides three complete numbering methods so developers can choose whichever convention is most familiar.

### Method 1: Sequential ID Numbering (Index 0 to 16)

| ID | Language / Layout | Script | Default Code Page | Description |
|:---:|:---|:---|:---:|:---|
| **`0`** | **Universal UTF-8** | Unicode | `65001` | Alias for universal multi-byte UTF-8 encoding |
| **`1`** | **English (US / UK)** | Latin (QWERTY) | `437` / `1252` | Standard US/UK ASCII and Western European |
| **`2`** | **Russian (RU)** | Cyrillic (ЙЦУКЕН) | `1251` / `866` | Full Cyrillic alphabet with Russian PS/2 scancode map |
| **`3`** | **German (DE)** | Latin (QWERTZ) | `1252` | German layout with umlauts (`ä`, `ö`, `ü`, `ß`) |
| **`4`** | **French (FR)** | Latin (AZERTY) | `1252` | French layout with accented letters (`é`, `è`, `ç`) |
| **`5`** | **Spanish (ES)** | Latin (QWERTY-ES) | `1252` | Spanish layout with `ñ`, `¿`, `¡` |
| **`6`** | **Italian (IT)** | Latin (Italiano) | `1252` | Italian layout |
| **`7`** | **Portuguese (PT/BR)** | Latin (Português) | `1252` | Portuguese layout with `ç`, `ã`, `õ` |
| **`8`** | **UTF-8 Standard** | Worldwide Unicode | `65001` | **Canonical Default**: Full UTF-8 multi-language support |
| **`9`** | **Ukrainian (UA)** | Cyrillic (UA) | `1251` | Ukrainian layout with `і`, `ї`, `є`, `ґ` |
| **`10`** | **Polish (PL)** | Central Latin | `1250` | Polish layout with `ą`, `ć`, `ę`, `ł`, `ń`, `ó`, `ś`, `ź`, `ż` |
| **`11`** | **Turkish (TR)** | Turkish Latin | `1254` | Turkish layout with `ç`, `ğ`, `ı`, `ö`, `ş`, `ü` |
| **`12`** | **Chinese (ZH)** | Hanzi (Simplified) | `936` | GBK / UTF-8 Simplified Chinese |
| **`13`** | **Japanese (JA)** | Kanji / Kana | `932` | Shift-JIS / UTF-8 Japanese |
| **`14`** | **Korean (KO)** | Hangul | `949` | EUC-KR / UTF-8 Korean |
| **`15`** | **Arabic (AR)** | Arabic | `1256` | Arabic right-to-left layout |
| **`16`** | **UTF-16** | Unicode 16-bit | `1200` | UTF-16 Little Endian |
| **`32`** | **UTF-32** | Unicode 32-bit | `12000` | UTF-32 Little Endian |

---

### Method 2: Standard Operating System Code Pages

Developers familiar with Windows console code pages or DOS OEM hardware fonts can pass the standard Code Page number directly:

| Code Page | Identifier | Target Language / Environment |
|:---:|:---|:---|
| **`65001`** | `CP_UTF8` | Worldwide Unicode (Universal modern standard) |
| **`1251`** | `Windows-1251` | Russian & Cyrillic ANSI (Windows desktop standard) |
| **`866`** | `CP866` | DOS / Bare-metal VGA text buffer Cyrillic |
| **`1252`** | `Windows-1252` | Western European & US Latin (ANSI standard) |
| **`437`** | `CP437` | Original IBM PC / BIOS OEM character set |
| **`1250`** | `Windows-1250` | Central European (Polish, Czech, Slovak, Hungarian) |
| **`1253`** | `Windows-1253` | Modern Greek |
| **`1254`** | `Windows-1254` | Turkish |
| **`1256`** | `Windows-1256` | Arabic |
| **`936`** | `CP936` | Simplified Chinese (GBK) |
| **`932`** | `CP932` | Japanese (Shift-JIS) |
| **`949`** | `CP949` | Korean (EUC-KR) |
| **`1200`** | `UTF-16LE` | 16-bit Unicode |

---

### Method 3: ISO Language Tags & String Identifiers

When writing readable scripts, text aliases can be supplied:

| Tag String | Normalized Name | Resolved ID | Resolved Code Page |
|:---|:---|:---:|:---:|
| `"utf-8"`, `"utf8"`, `"unicode"` | Universal UTF-8 | `8` | `65001` |
| `"ru"`, `"rus"`, `"russian"`, `"cyrillic"` | Russian Cyrillic | `2` | `1251` / `866` |
| `"en"`, `"eng"`, `"english"`, `"us"` | English US | `1` | `437` / `1252` |
| `"de"`, `"ger"`, `"german"` | German | `3` | `1252` |
| `"fr"`, `"fra"`, `"french"` | French | `4` | `1252` |
| `"es"`, `"spa"`, `"spanish"` | Spanish | `5` | `1252` |

---

## 4. Hardware Keyboard Layout Mapping (Bare-Metal & PS/2)

When developing operating systems or bare-metal kernels via `--kernel` and `!keyboard!`, setting the layout via `use utf - 2;` or `use utf - ru;` activates the internal **PS/2 Set 1 Scancode Translator**:

* Key `0x10` ('Q' on US layout) automatically produces `"й"` (or `"Й"` with Shift/CapsLock).
* Key `0x11` ('W' on US layout) automatically produces `"ц"` (or `"Ц"`).
* Key `0x12` ('E' on US layout) automatically produces `"у"` (or `"У"`).
* Full ЙЦУКЕН translation across rows 1, 2, 3, and punctuation (`.` and `,`).

This allows `gin >> command;` and `kbd_read_line()` to produce genuine Russian text on bare metal without any external host operating system.

---

## 5. Code Examples

### Example 1: Standard Multi-Language Console Application

```grass
!GrassGreen!
use utf - 8;

int main() {
    gout << "Привет, мир! GrassGreen поддерживает русский язык из коробки." << endl;
    gout << "Введите ваше имя: ";
    
    take name;
    gin >> name;
    
    gout << "Здравствуйте, " << name << "! Рады вас видеть." << endl;
    return 0;
}
```

### Example 2: Explicit Russian Layout with Code Page 1251

```grass
!GrassGreen!
use utf - 1251;

int main() {
    gout << "Терминал настроен на кодовую страницу Windows-1251." << endl;
    return 0;
}
```

### Example 3: Dynamic Runtime Layout Switching

```grass
!GrassGreen!
use utf - 1; // Start in English

int main() {
    gout << "Mode 1: English layout active." << endl;

    // Switch to Russian layout at runtime
    use utf - 2;
    gout << "Режим 2: Русская раскладка активирована." << endl;

    // Switch to Universal UTF-8
    use utf - 8;
    gout << "Mode 3: Universal UTF-8 active." << endl;
    return 0;
}
```

### Example 4: Bare-Metal Kernel with Russian PS/2 Input

```grass
!GrassGreen!
!keyboard!
use utf - 2;

int main() {
    gout << "GrassGreen OS: PS/2 контроллер настроен на русскую раскладку." << endl;
    gout << "Введите команду ядра: ";
    
    take cmd;
    gin >> cmd;
    
    gout << "Получена команда: " << cmd << endl;
    return 0;
}
```

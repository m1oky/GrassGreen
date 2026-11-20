# GrassGreen Visual Studio Extension (2022 / 2026)

Официальное расширение (VSIX) для поддержки кастомного системного языка программирования **GrassGreen** (файлы `.grass`) в Microsoft Visual Studio 2022 и 2026.

## Архитектура проекта

Проект разработан на C# (.NET Framework 4.8) с использованием Managed Extensibility Framework (MEF) и Visual Studio SDK:

1. **Регистрация типа контента и расширения (`ContentType/`)**:
   - `GrassContentTypeDefinition`: объявляет `ContentTypeDefinition("grassgreen")`, унаследованный от `"code"`.
   - `FileExtensionToContentTypeDefinition`: связывает файлы `.grass` с типом контента `"grassgreen"`.
   - `GrassGreenPackage.pkgdef`: регистрирует файловую ассоциацию, описание и фирменную иконку языка (`Resources/GrassGreen.ico`) для Solution Explorer.

2. **Лексер и классификатор подсветки (`Classification/`, `Parser/`)**:
   - `GrassLexer`: лексический анализатор языка GrassGreen (директивы `!... !`, строковые литералы с экранированием, блочные и строчные комментарии, шестнадцатеричные и двоичные числа, HEX-цвета, потоковый ввод-вывод `gout` / `gin` / `endl`, ключевые слова `take`, `set`, `struct`, типы данных, встроенные функции и операторы).
   - `GrassClassifier` & `GrassClassifierProvider`: реализация `IClassifier` и `IClassifierProvider` на базе стандартных категорий Visual Studio (`PredefinedClassificationTypeNames`).

3. **Синтаксический парсер и анализ ошибок в реальном времени (`ErrorTagging/`, `Parser/`)**:
   - `GrassParser`: строит абстрактное синтаксическое дерево (AST) и таблицу символов (Symbol Table) при каждом изменении буфера редактора (`ITextBuffer`).
   - `GrassErrorTagger` & `GrassErrorTaggerProvider`: реализует `ITagger<IErrorTag>` (`PredefinedErrorTypeNames.SyntaxError`).
   - Подсвечивает синтаксические ошибки красной волнистой линией (Squiggles):
     - Незакрытые строковые литералы и блочные комментарии.
     - Нарушение баланса скобок (`{}`, `()`, `[]`).
     - Некорректный синтаксис объявления переменных через `take`.
     - Использование устаревших ключевых слов (например, `log` вместо `gout << ... << endl`).
     - Неизвестные директивы модулей и библиотек.
     - Ошибки в синтаксисе директивы `use utf - <id>;`.

4. **Интеллектуальное автодополнение IntelliSense (`Completion/`)**:
   - `GrassAsyncCompletionSource` & `GrassAsyncCompletionSourceProvider`: современный асинхронный API автодополнения Visual Studio (`IAsyncCompletionSource`).
   - `GrassCompletionSource` & `GrassCompletionSourceProvider`: стандартный fallback-провайдер `ICompletionSource`.
   - Предлагает:
     - Все директивы ядра и библиотек (`!GrassGreen!`, `!baremetal!`, `!hardware!`, `!time!`, `!interface!`, `!audio!`, `!asm!` и др.).
     - Ключевые слова, управляющие конструкции и операторы.
     - Типы данных (`int`, `void`, `string`, `u8`..`u64`, `bool`, `array` и др.).
     - Стандартные потоки ввода-вывода (`gout`, `gin`, `endl`).
     - Встроенные функции стандартных библиотек с полными сигнатурами и документацией.
     - **Контекстные пользовательские переменные и функции**: динамически извлекаются из AST открытого файла (включая переменные `take <имя>`) с информацией о типе, области видимости и строке объявления.

5. **Всплывающие подсказки QuickInfo (`QuickInfo/`)**:
   - `GrassQuickInfoSource` & `GrassQuickInfoSourceProvider`: реализует `IAsyncQuickInfoSource` для отображения документации при наведении курсора на ключевые слова, библиотеки, функции и переменные.

6. **Сборка и запуск активного файла по горячим клавишам (`Execution/`, `Commands/`)**:
   - **F5** / **Ctrl+F5**: запуск интерпретатора/виртуальной машины GrassGreen для активного документа.
   - **Ctrl+Shift+B**: компиляция текущего документа в нативный исполняемый файл `.exe` с оптимизацией `-O3`.
   - `GrassExecutionService`:
     - Автоматическое сохранение активного документа перед запуском или сборкой (`dte.ActiveDocument.Save()`).
     - Вывод детального лога компиляции в отдельную панель Output Visual Studio («GrassGreen»).
     - Умный поиск компилятора `grassc.exe` / `grg.exe` (каталоги проекта `bin\`, `C:\GrassGreen\bin\`, системная переменная `PATH`).
     - Запуск скомпилированной программы или интерпретатора в отдельном интерактивном окне командной строки для полноценного консольного ввода-вывода.
   - `GrassEditorCommandFilter`: перехват команд редактора через `IOleCommandTarget` на уровне `IVsTextView`.
   - `GrassGreenPackage`: пакетная регистрация перехвата команд `VSStd97CmdID.Start`, `StartNoDebug`, `BuildSln` и `BuildSel` с проверкой активности `.grass` документа.

---

## Сборка проекта

Для сборки расширения используется Visual Studio MSBuild:

```powershell
# Из корня репозитория или папки GrassGreen-VS:
& "C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe" GrassGreen-VS\GrassGreen.VisualStudio.csproj /p:Configuration=Release
```

После завершения сборки готовый пакет расширения находится по пути:
- `GrassGreen-VS\GrassGreen.VisualStudio.vsix` (или `GrassGreen-VS\bin\Release\GrassGreen.VisualStudio.vsix`).

---

## Установка и запуск

### Способ 1. Установка в Visual Studio 2022 / 2026
1. Закройте все запущенные экземпляры Visual Studio.
2. Дважды кликните по файлу `GrassGreen-VS\GrassGreen.VisualStudio.vsix` или выполните команду:
   ```cmd
   VSIXInstaller.exe GrassGreen-VS\GrassGreen.VisualStudio.vsix
   ```
3. Подтвердите установку расширения для установленной версии Visual Studio.
4. Откройте любой файл с расширением `.grass` — подсветка, валидация синтаксиса и автодополнение активируются автоматически.

### Способ 2. Отладка и запуск в Экспериментальном инстансе (Experimental Instance)
При сборке в Visual Studio или через MSBuild расширение автоматически регистрируется в экспериментальном улье (`Exp`).
Для запуска экспериментального экземпляра:
1. В Visual Studio откройте проект `GrassGreen.VisualStudio.csproj` и нажмите **F5** (Debug -> Start Debugging).
2. Либо запустите Visual Studio с ключом `/rootsuffix Exp` вручную:
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\devenv.exe" /rootsuffix Exp
   ```
3. В открывшемся окне Visual Studio откройте любой проект или файл `.grass` (например, из папки `examples/` репозитория) для проверки подсветки, автодополнения и валидации синтаксиса.

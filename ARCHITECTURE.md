# Архитектура компилятора GrassGreen

Документ описывает модульную структуру кодовой базы GrassGreen после декомпозиции крупных файлов и служит картой для навигации и добавления новых возможностей.

---

## Карта директорий и модулей

### 1. `src/cli/` — Точка входа CLI, упаковщик и установщик
- `grassc.cpp` — разбор флагов командной строки (`CompilerOptions`), `FindDefaultGrassFile`, `ProcessCompilation`, `CompileAndRunFile`, `main`.
- `GrassInstaller.h` / `GrassInstaller.cpp` — установка экосистемы в Windows (`InstallGrassGreenEcosystem`), ассоциации `.grass`, контекстное меню и PATH.
- `GrassPacker.h` / `GrassPacker.cpp` — упаковка байт-кода в standalone `.exe` (`BuildStandaloneExe`, `GetCleanStubSize`, `CheckAndRunEmbeddedBytecode`).
- `GrassDiag.h` / `GrassDiag.cpp` — справка, версия, дизассемблер байт-кода и дамп AST (`PrintHelp`, `PrintVersion`, `DisassembleBytecode`, `DumpAST`).
- `gpm.cpp` — менеджер пакетов `gpm.exe`.

### 2. `src/frontend/` — Лексер, парсер, компилятор байт-кода и семантический анализ
- `GrassSyntax.h` — токены (`GrassToken`), узлы AST, `Lexer`, `BytecodeCompiler`, `GrassParser`.
- `GrassLexer.cpp` — лексический анализатор (`Lexer::tokenize`).
- `GrassSyntax.cpp` — ядро `BytecodeCompiler` и `GrassParser`, таблицы и общие методы.
- `GrassBytecodeExpr.cpp`, `GrassBytecodeUI.cpp`, `GrassBytecodeStmt.cpp`, `GrassBytecodeControl.cpp`, `GrassBytecodeDecl.cpp` — генерация байт-кода VM (выражения, UI-DSL, инструкции/ОС, циклы/переменные, структуры/классы/функции).
- `GrassParserExpr.cpp`, `GrassParserStmt.cpp`, `GrassParserDecl.cpp` — построение AST для выражений, инструкций/UI-DSL и объявлений.
- `GrassImport.h` / `GrassImport.cpp` — разрешение директив `!module!` и импортов `.grass`.
- `GrassLangGen.h` / `GrassLangGen.cpp` — многоязычная диагностика (RU/EN).
- `GrassSema.h` / `GrassSema.cpp` — ядро `GrassSema` (символы, области видимости, проверка программы).
- `src/frontend/sema/`:
  - `SemaBuiltinsCore.cpp` — регистрация сигнатур ядра (`io`, математика, строки, типы, память).
  - `SemaBuiltinsMedia.cpp` — сигнатуры `disp`, `gfx2d`, `gfx3d`, `math3d`, `physics`, `audio`, `baremetal`.
  - `SemaBuiltinsSys.cpp` — сигнатуры `GrassGreen`, `sys`, `proc`, `fsys`, `net`, `web`, `time`, `thread`, `json`, `tg`, `hardware`.
  - `SemaExpr.cpp` — вывод и проверка типов выражений (`vec<T>`, индексация, вызовы, ООП).
  - `SemaStmt.cpp` — анализ инструкций, объявлений, RAII, `return`, циклов.

### 3. `src/vm/` — Виртуальная машина и диспетчер встроенных функций
- `GrassVM.h` / `GrassVM.cpp` — класс `GrassVM`, `GrassSharedEnv`, глобальное состояние VM и цикл исполнения `execute()`.
- `src/vm/builtins/`:
  - `BuiltinCore.cpp` — базовый ввод-вывод, строки, математика, массивы, json-база.
  - `BuiltinMedia.cpp` — графика 2D/3D, `disp` UI, физика, `math3d`, аудио.
  - `BuiltinSys.cpp` — `system`, `sys`, `proc`, `fsys`, `net`, `web`, `time`, `thread`, `tg`, `hardware`.
  - `BuiltinLowLevel.cpp` — `baremetal` (`color`, `clear`, `draw_box`, `mouse_*`), порты/MMIO, низкоуровневая память.
- `GrassMemory.cpp`, `GrassOptimizer.cpp`, `GrassJIT.cpp`, `GrassFFI.cpp`, `grassvm_capi.cpp` — управляемая память VM, оптимизатор байт-кода, JIT, FFI и C API для `grassvm.dll`.

### 4. `src/native/` — SSA IR, оптимизации, кодогенератор x86/x64, PE/ELF/COFF
- `GrassIR.h` — типы IR, инструкции, базовые блоки, функции, модули, `IRBuilder`, SSA и аллокатор регистров.
- `GrassIR.cpp`, `GrassIRModule.cpp`, `GrassIRBuilder.cpp`, `GrassIRSSA.cpp`, `GrassIROptPasses.cpp` — реализация IR, построитель, Mem2Reg/Dominators/LinearScan, базовые проходы оптимизации.
- `GrassAdvancedOpt.h` / `GrassAdvancedOpt.cpp`, `GrassAdvancedOptLoops.cpp`, `GrassAdvancedOptVec.cpp` — инлайнинг, LICM, развертывание циклов, понижение силы операций и SIMD-векторизация (SSE2/AVX2).
- `GrassASTLowerer.h` / `GrassASTLowerer{Helpers,Decl,Stmt,Expr,Ops,Primary}.cpp` — понижение AST/токенов GrassGreen в `GrassIR`.
- `GrassNativeRuntime.h` — встроенный C-рантайм `kEmbeddedRuntimeC` для режима `--use-gcc`.
- `GrassNativeCodegen.h` / `GrassNativeCodegen.cpp` — делегаты ООП/памяти/stdlib и конвейер `NativeCompiler`.
- `GrassNativeX86Ctx.h` / `GrassNativeX86{Backend,Insts,Vec}.cpp` — трансляция IR в инструкции x86-64/x86 (скалярные, вызовы ABI, плавающая точка и SIMD).
- `GrassX86Encoder.h` / `GrassX86Encoder{,ALU,FP,SIMD}.cpp` — низкоуровневый ассемблерный энкодер x86/x64 (включая SSE2 XMM и AVX2 VEX YMM).
- `GrassObjWriter.h` / `GrassObjBase.cpp`, `GrassObjWriter.cpp`, `GrassObjELF.cpp`, `GrassObjPE.cpp` — запись `.obj` (COFF), `.o` (ELF32/ELF64), `.bin` и PE32+ `.exe`.
- `GrassObjRuntime.h` / `GrassObjRuntimeIO.cpp`, `GrassObjRuntimeSys.cpp` — автоинъекция автономных Win64-заглушек (куча, ввод-вывод, `system`, массивы, время, математика, VEH-обработчик сбоев).
- `GrassElfWriter.h` / `GrassElfWriter.cpp` — компоновка исполняемых ELF64-файлов.
- `GrassNativeStdlib.h` / `GrassNativeStdlib.cpp` и `src/native/stdlib/NativeStdlib{Core,Media,Sys}.cpp` — реестр встроенных функций и внешних OS FFI-символов для нативного компилятора.
- `GrassNativeOOP.cpp`, `GrassNativeMemory.cpp`, `GrassNativeDebug.cpp` — нативные хелперы ООП/массивов, RAII и отладочные таблицы.

### 5. `src/runtime/`, `src/media/`, `src/baremetal/` — Рантайм-библиотеки
- `src/runtime/` — ФС (`GrassFsys`), сеть (`GrassNet`, `GrassWeb`), процессы/ОС (`GrassProc`, `GrassSys`, `GrassHardware`), время (`GrassTime`), потоки (`GrassThread`), `GrassJson`, `GrassRandom`, `GrassTg`.
- `src/media/` — UI-движок (`gui_engine`), 2D (`Grass2DGraphics`), 3D (`GrassGfx3D`, `GrassMath3D`), физика (`GrassPhysics`), звук (`GrassAudio`), математика (`CoreMath`, `FastMath`).
- `src/baremetal/` — HAL, MMIO, клавиатура, ассемблер и загрузчик (`GrassBoot`, `GSE`).

### 6. Прочие директории
- `stdlib/` — `.grass`-описания стандартных модулей, подключаемых через `!module!`.
- `tests/` — модульные тесты C++ (`test_x86_enc.cpp`, `test_ir_opt.cpp`, `test_sema.cpp` и др.) и интеграционные тесты `.grass`.
- `examples/` — демонстрационные программы GrassGreen.
- `tools/` — быстрая параллельная сборка (`fast_build.py`).
- `installer/` — сборщик установщика `setup.exe` (`build_installer.bat`, `make_payload.cpp`, `setup.cpp`).

---

## Как добавить новую встроенную функцию (builtin)

Чтобы добавить новую встроенную функцию и поддержать ее сразу в семантическом анализаторе, VM и нативном компиляторе, достаточно открыть **3 файла** (по одному на каждый слой) и, при необходимости, объявить ее в `stdlib/`:

1. **Семантика (фронтенд):** `src/frontend/sema/SemaBuiltins<Core|Media|Sys>.cpp`
   Зарегистрируйте имя, типы аргументов и возвращаемый тип в функции соответствующей категории (`Core` — база/математика, `Media` — 2D/3D/UI/аудио/физика/baremetal, `Sys` — ОС/ФС/сеть/процессы/время).
2. **Виртуальная машина (`-r`):** `src/vm/builtins/Builtin<Core|Media|Sys|LowLevel>.cpp`
   Добавьте ветку обработки имени функции в соответствующий диспетчер `GrassVM::callBuiltin...`.
3. **Нативный компилятор (`--native`):** `src/native/stdlib/NativeStdlib<Core|Media|Sys>.cpp`
   Зарегистрируйте нативную сигнатуру в `populate_*` (при необходимости добавив машинную Win64-заглушку в `src/native/GrassObjRuntimeIO.cpp` или `GrassObjRuntimeSys.cpp`, а при необходимости понижения — в `src/native/GrassASTLowerer*.cpp` / `GrassNativeX86Vec.cpp`).

Если функция относится к импортируемому модулю `!name!`, также добавьте ее экспорт в `stdlib/<name>.grass`.

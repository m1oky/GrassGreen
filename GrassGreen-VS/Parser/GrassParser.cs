using System;
using System.Collections.Generic;
using GrassGreen.VisualStudio.Constants;

namespace GrassGreen.VisualStudio.Parser
{
    public class GrassParser
    {
        private readonly List<GrassToken> _tokens;
        private readonly int _count;
        private int _index;

        private readonly List<GrassSymbol> _symbols;
        private readonly List<GrassSyntaxError> _errors;
        private readonly HashSet<string> _includedModules;

        private string _currentScope = "global";
        private int _braceDepth = 0;

        public GrassParser(List<GrassToken> tokens)
        {
            _tokens = tokens ?? new List<GrassToken>();
            _count = _tokens.Count;
            _index = 0;

            _symbols = new List<GrassSymbol>();
            _errors = new List<GrassSyntaxError>();
            _includedModules = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        }

        private GrassToken Peek(int offset = 0)
        {
            int i = _index + offset;
            if (i >= _count)
                return _count > 0 ? _tokens[_count - 1] : default(GrassToken);
            return _tokens[i];
        }

        private GrassToken Advance()
        {
            GrassToken tok = Peek();
            if (_index < _count) _index++;
            return tok;
        }

        private bool Match(string text)
        {
            if (Peek().Text == text)
            {
                Advance();
                return true;
            }
            return false;
        }

        public GrassParseResult Parse()
        {
            // 1. First pass: Lexical error verification and brackets balance check
            CheckLexicalAndBrackets();

            // 2. Second pass: Parse statements, declarations, and extract symbols
            _index = 0;
            _braceDepth = 0;
            _currentScope = "global";

            while (_index < _count)
            {
                GrassToken tok = Peek();
                if (tok.Kind == GrassTokenKind.EndOfFile) break;

                // Directives (!GrassGreen!, !baremetal!, !ffi!, !langgen!, etc.)
                if (tok.Kind == GrassTokenKind.Directive)
                {
                    Advance();
                    if (!GrassLanguageData.Directives.Contains(tok.Text))
                    {
                        _errors.Add(new GrassSyntaxError(
                            $"Неизвестная библиотека: '{tok.Text}'. Допустимые директивы: !GrassGreen!, !ffi!, !langgen!, !fsys!, !baremetal!, !hardware!, !time!, !interface!, !FastMath!, !audio!, !asm!, !thread! и др.",
                            tok.Start, tok.Length, tok.Line, tok.Column));
                    }
                    else
                    {
                        _includedModules.Add(tok.Text);
                    }
                    continue;
                }

                // Skip comments
                if (tok.Kind == GrassTokenKind.CommentLine || tok.Kind == GrassTokenKind.CommentBlock)
                {
                    Advance();
                    continue;
                }

                // Deprecated keyword 'log'
                if (tok.Text == "log")
                {
                    _errors.Add(new GrassSyntaxError(
                        "Функция 'log' устарела и полностью удалена из GrassGreen! Используйте потоковый вывод: gout << ... << endl;",
                        tok.Start, tok.Length, tok.Line, tok.Column));
                    Advance();
                    continue;
                }

                // 'use utf' directive
                if (tok.Text == "use")
                {
                    ParseUseDirective();
                    continue;
                }

                // Function declaration: take name(args) { ... } or int main() { ... } or void run() { ... }
                if (IsFunctionDeclarationStart())
                {
                    ParseFunctionDeclaration();
                    continue;
                }

                // Struct declaration: struct Name { ... }
                if (tok.Text == "struct" && Peek(1).Kind == GrassTokenKind.Identifier)
                {
                    ParseStructDeclaration();
                    continue;
                }

                // Variable declaration: take x = ..., int x = ..., etc.
                if (IsVariableDeclarationStart())
                {
                    ParseVariableDeclaration();
                    continue;
                }

                // Imports
                if (tok.Text == "import" || tok.Text == "include")
                {
                    Advance();
                    if (Peek().Kind != GrassTokenKind.String)
                    {
                        _errors.Add(new GrassSyntaxError(
                            "Ожидалась строка с путем к файлу после 'import': import \"module.grass\";",
                            Peek().Start, Peek().Length, Peek().Line, Peek().Column));
                    }
                    else
                    {
                        Advance();
                    }
                    if (Peek().Text == ";") Advance();
                    continue;
                }

                // Scope tracking for blocks
                if (tok.Text == "{")
                {
                    _braceDepth++;
                    Advance();
                    continue;
                }
                if (tok.Text == "}")
                {
                    _braceDepth--;
                    if (_braceDepth <= 0)
                    {
                        _braceDepth = 0;
                        _currentScope = "global";
                    }
                    Advance();
                    continue;
                }

                Advance();
            }

            return new GrassParseResult(_tokens, _symbols, _errors, _includedModules);
        }

        private void CheckLexicalAndBrackets()
        {
            var bracketStack = new Stack<GrassToken>();

            foreach (var tok in _tokens)
            {
                // Check unterminated tokens
                if (tok.IsUnterminated)
                {
                    if (tok.Kind == GrassTokenKind.String)
                    {
                        _errors.Add(new GrassSyntaxError("Незакрытая строковая константа (ожидается '\"')", tok.Start, tok.Length, tok.Line, tok.Column));
                    }
                    else if (tok.Kind == GrassTokenKind.CommentBlock)
                    {
                        _errors.Add(new GrassSyntaxError("Незакрытый блочный комментарий (ожидается '*/')", tok.Start, tok.Length, tok.Line, tok.Column));
                    }
                    else if (tok.Kind == GrassTokenKind.Directive)
                    {
                        _errors.Add(new GrassSyntaxError("Незакрытая директива модуля (ожидается закрывающий '!')", tok.Start, tok.Length, tok.Line, tok.Column));
                    }
                    else if (tok.Kind == GrassTokenKind.Number)
                    {
                        _errors.Add(new GrassSyntaxError("Некорректная числовая константа", tok.Start, tok.Length, tok.Line, tok.Column));
                    }
                }

                // Bracket balance
                if (tok.Text == "{" || tok.Text == "(" || tok.Text == "[")
                {
                    bracketStack.Push(tok);
                }
                else if (tok.Text == "}" || tok.Text == ")" || tok.Text == "]")
                {
                    if (bracketStack.Count == 0)
                    {
                        _errors.Add(new GrassSyntaxError($"Лишняя закрывающая скобка '{tok.Text}'", tok.Start, tok.Length, tok.Line, tok.Column));
                    }
                    else
                    {
                        var openTok = bracketStack.Pop();
                        if (!AreMatchingBrackets(openTok.Text, tok.Text))
                        {
                            _errors.Add(new GrassSyntaxError(
                                $"Несоответствующая закрывающая скобка '{tok.Text}', ожидалась парная для '{openTok.Text}' со строки {openTok.Line}",
                                tok.Start, tok.Length, tok.Line, tok.Column));
                        }
                    }
                }
            }

            while (bracketStack.Count > 0)
            {
                var unclosed = bracketStack.Pop();
                _errors.Add(new GrassSyntaxError($"Незакрытая скобка '{unclosed.Text}'", unclosed.Start, unclosed.Length, unclosed.Line, unclosed.Column));
            }
        }

        private static bool AreMatchingBrackets(string open, string close)
        {
            return (open == "{" && close == "}") ||
                   (open == "(" && close == ")") ||
                   (open == "[" && close == "]");
        }

        private bool IsFunctionDeclarationStart()
        {
            GrassToken t0 = Peek();
            GrassToken t1 = Peek(1);
            GrassToken t2 = Peek(2);

            // e.g. int main(), take myFunc(), void run(), fn doSomething()
            bool isTypeOrFunc = t0.Text == "take" || t0.Text == "fn" || t0.Text == "fun" || t0.Text == "func" ||
                                t0.Text == "int" || t0.Text == "void" || t0.Text == "string" || t0.Text == "float" ||
                                t0.Text == "double" || t0.Text == "bool" || t0.Text == "u8" || t0.Text == "u16" ||
                                t0.Text == "u32" || t0.Text == "u64";

            return isTypeOrFunc && t1.Kind == GrassTokenKind.Identifier && t2.Text == "(";
        }

        private void ParseFunctionDeclaration()
        {
            GrassToken typeTok = Advance();
            GrassToken nameTok = Advance();
            Advance(); // '('

            var funcSymbol = new GrassSymbol(
                nameTok.Text,
                GrassSymbolKind.Function,
                typeTok.Text,
                nameTok.Line,
                nameTok.Column,
                nameTok.Start,
                nameTok.Length,
                "global");

            _currentScope = nameTok.Text;

            // Parse parameters
            while (Peek().Text != ")" && Peek().Kind != GrassTokenKind.EndOfFile)
            {
                if (Peek().Text == ",")
                {
                    Advance();
                    continue;
                }

                string paramType = "take";
                if (GrassLanguageData.Types.Contains(Peek().Text) || Peek().Text == "take")
                {
                    paramType = Advance().Text;
                }

                if (Peek().Kind == GrassTokenKind.Identifier)
                {
                    GrassToken paramTok = Advance();
                    funcSymbol.Parameters.Add($"{paramType} {paramTok.Text}");

                    var paramSymbol = new GrassSymbol(
                        paramTok.Text,
                        GrassSymbolKind.Parameter,
                        paramType,
                        paramTok.Line,
                        paramTok.Column,
                        paramTok.Start,
                        paramTok.Length,
                        _currentScope);
                    _symbols.Add(paramSymbol);
                }
                else
                {
                    break;
                }
            }

            if (Peek().Text == ")")
            {
                Advance();
            }

            if (Peek().Text == "{")
            {
                funcSymbol.BodyStart = Peek().Start;
                int depth = 0;
                for (int i = _index; i < _count; i++)
                {
                    if (_tokens[i].Text == "{") depth++;
                    else if (_tokens[i].Text == "}")
                    {
                        depth--;
                        if (depth == 0)
                        {
                            funcSymbol.BodyEnd = _tokens[i].Start + _tokens[i].Length;
                            break;
                        }
                    }
                }
            }

            _symbols.Add(funcSymbol);
        }

        private void ParseStructDeclaration()
        {
            Advance(); // 'struct'
            if (Peek().Kind != GrassTokenKind.Identifier) return;
            GrassToken nameTok = Advance(); // Name

            var structSym = new GrassSymbol(
                nameTok.Text,
                GrassSymbolKind.Struct,
                "struct",
                nameTok.Line,
                nameTok.Column,
                nameTok.Start,
                nameTok.Length,
                "global");

            if (Peek().Text == "{")
            {
                structSym.BodyStart = Peek().Start;
                Advance(); // '{'
                while (Peek().Text != "}" && Peek().Kind != GrassTokenKind.EndOfFile)
                {
                    if (Peek().Text == "," || Peek().Text == ";")
                    {
                        Advance();
                        continue;
                    }

                    // Check for method declaration:
                    // e.g. take foo(...) { ... } or void foo(...) { ... } or foo(...) { ... }
                    bool isMethodWithRet = (Peek().Kind == GrassTokenKind.Identifier || Peek().Kind == GrassTokenKind.Keyword || Peek().Kind == GrassTokenKind.Type) &&
                                           Peek(1).Kind == GrassTokenKind.Identifier && Peek(2).Text == "(";
                    bool isMethodNoRet = Peek().Kind == GrassTokenKind.Identifier && Peek(1).Text == "(";

                    if (isMethodWithRet || isMethodNoRet)
                    {
                        string retType = "take";
                        GrassToken methNameTok;
                        if (isMethodWithRet)
                        {
                            retType = Advance().Text;
                            methNameTok = Advance();
                        }
                        else
                        {
                            methNameTok = Advance();
                        }

                        var meth = new GrassStructMethod(methNameTok.Text, retType, methNameTok.Line);
                        Advance(); // '('

                        while (Peek().Text != ")" && Peek().Kind != GrassTokenKind.EndOfFile)
                        {
                            if (Peek().Text == ",")
                            {
                                Advance();
                                continue;
                            }
                            if (Peek().Kind == GrassTokenKind.Identifier || Peek().Kind == GrassTokenKind.Keyword || Peek().Kind == GrassTokenKind.Type)
                            {
                                GrassToken p0 = Advance();
                                if (Peek().Kind == GrassTokenKind.Identifier)
                                {
                                    GrassToken p1 = Advance();
                                    meth.Parameters.Add(p1.Text);
                                }
                                else
                                {
                                    meth.Parameters.Add(p0.Text);
                                }
                            }
                            else
                            {
                                Advance();
                            }
                        }

                        if (Peek().Text == ")") Advance();

                        if (Peek().Text == "{")
                        {
                            meth.BodyStart = Peek().Start;
                            Advance(); // '{'
                            int depth = 1;
                            while (depth > 0 && Peek().Kind != GrassTokenKind.EndOfFile)
                            {
                                if (Peek().Text == "{") depth++;
                                else if (Peek().Text == "}")
                                {
                                    depth--;
                                    if (depth == 0)
                                    {
                                        meth.BodyEnd = Peek().Start + Peek().Length;
                                        Advance(); // '}'
                                        break;
                                    }
                                }
                                Advance();
                            }
                        }

                        structSym.Methods.Add(meth);

                        // Also register as a callable symbol scoped to this struct
                        var methSymbol = new GrassSymbol(
                            meth.Name,
                            GrassSymbolKind.Function,
                            meth.ReturnType,
                            meth.Line,
                            methNameTok.Column,
                            methNameTok.Start,
                            methNameTok.Length,
                            structSym.Name);
                        methSymbol.Parameters.AddRange(meth.Parameters);
                        methSymbol.BodyStart = meth.BodyStart;
                        methSymbol.BodyEnd = meth.BodyEnd;
                        _symbols.Add(methSymbol);

                        continue;
                    }

                    // Field declaration
                    if (Peek().Kind == GrassTokenKind.Identifier || Peek().Kind == GrassTokenKind.Keyword || Peek().Kind == GrassTokenKind.Type)
                    {
                        GrassToken first = Advance();
                        if (first.Text == "take" || first.Text == "var" || first.Text == "let")
                        {
                            if (Peek().Kind == GrassTokenKind.Identifier)
                            {
                                GrassToken fieldName = Advance();
                                if (Peek().Text == "=")
                                {
                                    while (Peek().Text != ";" && Peek().Text != "," && Peek().Text != "}" && Peek().Kind != GrassTokenKind.EndOfFile)
                                    {
                                        Advance();
                                    }
                                }
                                structSym.Fields.Add(new GrassStructField(fieldName.Text, "take"));
                            }
                        }
                        else if (Peek().Text == ":" || Peek().Text == "=")
                        {
                            Advance(); // ':' or '='
                            GrassToken typeTok = Peek();
                            string fType = "take";
                            if (typeTok.Kind == GrassTokenKind.Identifier || typeTok.Kind == GrassTokenKind.Type)
                            {
                                fType = Advance().Text;
                            }
                            structSym.Fields.Add(new GrassStructField(first.Text, fType));
                        }
                        else if (Peek().Kind == GrassTokenKind.Identifier)
                        {
                            GrassToken nameT = Advance();
                            structSym.Fields.Add(new GrassStructField(nameT.Text, first.Text));
                        }
                        else
                        {
                            structSym.Fields.Add(new GrassStructField(first.Text, "take"));
                        }
                    }
                    else
                    {
                        Advance();
                    }

                    if (Peek().Text == "," || Peek().Text == ";")
                    {
                        Advance();
                    }
                }
                if (Peek().Text == "}")
                {
                    structSym.BodyEnd = Peek().Start + Peek().Length;
                    Advance();
                }
            }

            _symbols.Add(structSym);
        }

        private bool IsVariableDeclarationStart()
        {
            GrassToken t0 = Peek();
            if (t0.Text == "volatile" || t0.Text == "static" || t0.Text == "const")
            {
                t0 = Peek(1);
            }

            if (t0.Text == "take" || t0.Text == "set" || t0.Text == "var" || t0.Text == "let" ||
                t0.Text == "int" || t0.Text == "string" || t0.Text == "float" || t0.Text == "double" ||
                t0.Text == "bool" || t0.Text == "array" || t0.Text == "u8" || t0.Text == "u16" ||
                t0.Text == "u32" || t0.Text == "u64")
            {
                return true;
            }

            // Check custom struct type declaration: Point p;
            if (t0.Kind == GrassTokenKind.Identifier && _symbols.Exists(s => s.Kind == GrassSymbolKind.Struct && s.Name == t0.Text))
            {
                return true;
            }

            // Check void* ptr;
            if (t0.Text == "void" && Peek(1).Text == "*")
            {
                return true;
            }

            return false;
        }

        private void ParseVariableDeclaration()
        {
            GrassToken modTok = default(GrassToken);
            if (Peek().Text == "volatile" || Peek().Text == "static" || Peek().Text == "const")
            {
                modTok = Advance();
            }

            GrassToken declTok = Advance(); // 'take', 'set', 'int', struct name, etc.
            if (declTok.Text == "void" && Peek().Text == "*")
            {
                Advance(); // '*'
                declTok = new GrassToken(GrassTokenKind.Keyword, "void*", declTok.Start, declTok.Length + 1, declTok.Line, declTok.Column);
            }

            // Handle invalid 'take' syntax: e.g. take = 5; or take 123;
            if (declTok.Text == "take")
            {
                GrassToken next = Peek();
                if (next.Text == "=" || next.Kind == GrassTokenKind.Number || (next.Kind != GrassTokenKind.Identifier && next.Text != ";"))
                {
                    _errors.Add(new GrassSyntaxError(
                        "Некорректный синтаксис 'take': ожидается имя переменной или функции (например: 'take x = 10;')",
                        next.Start, next.Length, next.Line, next.Column));
                    return;
                }
                if (next.Text == ";")
                {
                    _errors.Add(new GrassSyntaxError(
                        "Ожидалось имя переменной после ключевого слова 'take'",
                        declTok.Start, declTok.Length, declTok.Line, declTok.Column));
                    return;
                }
            }

            // Parse variable names (e.g. take a = 1, b = 2;)
            while (Peek().Kind == GrassTokenKind.Identifier)
            {
                GrassToken varTok = Advance();
                string inferredType = declTok.Text;

                if (Peek().Text == "=")
                {
                    Advance(); // '='
                    GrassToken initTok = Peek();
                    if (initTok.Text == "[")
                    {
                        inferredType = "array";
                    }
                    else if (initTok.Kind == GrassTokenKind.Identifier && _symbols.Exists(s => s.Kind == GrassSymbolKind.Struct && s.Name == initTok.Text))
                    {
                        inferredType = initTok.Text;
                    }

                    if (Peek().Text == ";" || Peek().Kind == GrassTokenKind.EndOfFile)
                    {
                        _errors.Add(new GrassSyntaxError(
                            $"Ожидалось значение или выражение для инициализации переменной '{varTok.Text}'",
                            varTok.Start, varTok.Length, varTok.Line, varTok.Column));
                    }
                    else
                    {
                        SkipExpression();
                    }
                }

                var sym = new GrassSymbol(
                    varTok.Text,
                    GrassSymbolKind.Variable,
                    inferredType,
                    varTok.Line,
                    varTok.Column,
                    varTok.Start,
                    varTok.Length,
                    _currentScope);
                _symbols.Add(sym);

                if (Peek().Text == ",")
                {
                    Advance();
                    continue;
                }
                break;
            }

            if (Peek().Text == ";")
            {
                Advance();
            }
        }

        private void ParseUseDirective()
        {
            GrassToken useTok = Advance(); // 'use'
            if (Peek().Text.StartsWith("utf-", StringComparison.OrdinalIgnoreCase))
            {
                Advance();
                if (Peek().Text == ";") Advance();
                return;
            }

            if (!Match("utf") && !Match("UTF"))
            {
                _errors.Add(new GrassSyntaxError(
                    "Ожидалась директива 'utf' после 'use' (например: 'use utf - 8;' или 'use utf - 2;')",
                    useTok.Start, useTok.Length, useTok.Line, useTok.Column));
                return;
            }

            if (Peek().Text == "-")
            {
                Advance(); // '-'
            }

            GrassToken valTok = Peek();
            if (valTok.Kind == GrassTokenKind.Number || valTok.Kind == GrassTokenKind.Identifier || valTok.Kind == GrassTokenKind.String)
            {
                Advance();
            }
            else
            {
                _errors.Add(new GrassSyntaxError(
                    "Ожидался идентификатор раскладки или номер кодовой страницы после 'use utf -' (например: 8, 2, 65001, 1251, ru, en)",
                    valTok.Start, valTok.Length, valTok.Line, valTok.Column));
            }

            if (Peek().Text == ";")
            {
                Advance();
            }
        }

        private void SkipExpression()
        {
            int parens = 0;
            int brackets = 0;

            while (_index < _count)
            {
                GrassToken tok = Peek();
                if (tok.Kind == GrassTokenKind.EndOfFile) break;

                if (tok.Text == "(") parens++;
                else if (tok.Text == ")") { if (parens > 0) parens--; }
                else if (tok.Text == "[") brackets++;
                else if (tok.Text == "]") { if (brackets > 0) brackets--; }
                else if (parens == 0 && brackets == 0 && (tok.Text == ";" || tok.Text == "," || tok.Text == "{" || tok.Text == "}"))
                {
                    break;
                }
                Advance();
            }
        }
    }
}

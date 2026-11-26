using System;
using System.Collections.Generic;
using GrassGreen.VisualStudio.Constants;

namespace GrassGreen.VisualStudio.Parser
{
    public class GrassLexer
    {
        private readonly string _text;
        private readonly int _length;
        private int _pos;
        private int _line;
        private int _lineStart;

        public GrassLexer(string text)
        {
            _text = text ?? string.Empty;
            _length = _text.Length;
            _pos = 0;
            _line = 1;
            _lineStart = 0;
        }

        private char Peek(int offset = 0)
        {
            int p = _pos + offset;
            return p < _length ? _text[p] : '\0';
        }

        private char Advance()
        {
            if (_pos >= _length) return '\0';
            char c = _text[_pos++];
            if (c == '\n')
            {
                _line++;
                _lineStart = _pos;
            }
            return c;
        }

        public List<GrassToken> Tokenize()
        {
            var tokens = new List<GrassToken>();

            while (_pos < _length)
            {
                char c = Peek();

                // Whitespace
                if (char.IsWhiteSpace(c))
                {
                    Advance();
                    continue;
                }

                int start = _pos;
                int tokenLine = _line;
                int tokenCol = _pos - _lineStart + 1;

                // Line comment or Block comment
                if (c == '/' && Peek(1) == '/')
                {
                    Advance(); // /
                    Advance(); // /
                    while (_pos < _length && Peek() != '\n')
                    {
                        Advance();
                    }
                    int len = _pos - start;
                    tokens.Add(new GrassToken(GrassTokenKind.CommentLine, _text.Substring(start, len), start, len, tokenLine, tokenCol));
                    continue;
                }

                if (c == '/' && Peek(1) == '*')
                {
                    Advance(); // /
                    Advance(); // *
                    bool closed = false;
                    while (_pos < _length)
                    {
                        if (Peek() == '*' && Peek(1) == '/')
                        {
                            Advance(); // *
                            Advance(); // /
                            closed = true;
                            break;
                        }
                        Advance();
                    }
                    int len = _pos - start;
                    tokens.Add(new GrassToken(GrassTokenKind.CommentBlock, _text.Substring(start, len), start, len, tokenLine, tokenCol, !closed));
                    continue;
                }

                // String literal
                if (c == '"')
                {
                    Advance(); // opening "
                    bool closed = false;
                    while (_pos < _length)
                    {
                        char ch = Peek();
                        if (ch == '\\')
                        {
                            Advance(); // skip escape
                            if (_pos < _length) Advance();
                        }
                        else if (ch == '"')
                        {
                            Advance(); // closing "
                            closed = true;
                            break;
                        }
                        else if (ch == '\r' || ch == '\n')
                        {
                            // Unterminated on current line
                            break;
                        }
                        else
                        {
                            Advance();
                        }
                    }
                    int len = _pos - start;
                    tokens.Add(new GrassToken(GrassTokenKind.String, _text.Substring(start, len), start, len, tokenLine, tokenCol, !closed));
                    continue;
                }

                // Single-quoted string / char literal
                if (c == '\'')
                {
                    Advance(); // opening '
                    bool closed = false;
                    while (_pos < _length)
                    {
                        char ch = Peek();
                        if (ch == '\\')
                        {
                            Advance();
                            if (_pos < _length) Advance();
                        }
                        else if (ch == '\'')
                        {
                            Advance();
                            closed = true;
                            break;
                        }
                        else if (ch == '\r' || ch == '\n')
                        {
                            break;
                        }
                        else
                        {
                            Advance();
                        }
                    }
                    int len = _pos - start;
                    tokens.Add(new GrassToken(GrassTokenKind.String, _text.Substring(start, len), start, len, tokenLine, tokenCol, !closed));
                    continue;
                }

                // Directives: !GrassGreen!
                if (c == '!')
                {
                    // Check if it's a directive (!ident!) or logical NOT / !=
                    if (IsIdentStart(Peek(1)))
                    {
                        Advance(); // !
                        while (_pos < _length && (IsIdentChar(Peek()) || Peek() == '+' || Peek() == '-'))
                        {
                            Advance();
                        }
                        if (Peek() == '!')
                        {
                            Advance(); // closing !
                            int len = _pos - start;
                            string dirText = _text.Substring(start, len);
                            tokens.Add(new GrassToken(GrassTokenKind.Directive, dirText, start, len, tokenLine, tokenCol));
                            continue;
                        }
                        else
                        {
                            // Unfinished directive or operator followed by identifier
                            int len = _pos - start;
                            string text = _text.Substring(start, len);
                            tokens.Add(new GrassToken(GrassTokenKind.Directive, text, start, len, tokenLine, tokenCol, isUnterminated: true));
                            continue;
                        }
                    }
                    else if (Peek(1) == '=')
                    {
                        Advance(); Advance();
                        tokens.Add(new GrassToken(GrassTokenKind.Operator, "!=", start, 2, tokenLine, tokenCol));
                        continue;
                    }
                    else
                    {
                        Advance();
                        tokens.Add(new GrassToken(GrassTokenKind.Operator, "!", start, 1, tokenLine, tokenCol));
                        continue;
                    }
                }

                // Hex color: #RRGGBB or #RGB
                if (c == '#' && IsHexDigit(Peek(1)))
                {
                    Advance(); // #
                    while (_pos < _length && IsHexDigit(Peek()))
                    {
                        Advance();
                    }
                    int len = _pos - start;
                    tokens.Add(new GrassToken(GrassTokenKind.Constant, _text.Substring(start, len), start, len, tokenLine, tokenCol));
                    continue;
                }

                // Numbers: 0x..., 0b..., float, decimal
                if (char.IsDigit(c))
                {
                    if (c == '0' && (Peek(1) == 'x' || Peek(1) == 'X'))
                    {
                        Advance(); Advance(); // 0x
                        bool hasDigits = false;
                        while (_pos < _length && IsHexDigit(Peek()))
                        {
                            Advance();
                            hasDigits = true;
                        }
                        int len = _pos - start;
                        tokens.Add(new GrassToken(GrassTokenKind.Number, _text.Substring(start, len), start, len, tokenLine, tokenCol, !hasDigits));
                        continue;
                    }
                    else if (c == '0' && (Peek(1) == 'b' || Peek(1) == 'B'))
                    {
                        Advance(); Advance(); // 0b
                        bool hasDigits = false;
                        while (_pos < _length && (Peek() == '0' || Peek() == '1'))
                        {
                            Advance();
                            hasDigits = true;
                        }
                        int len = _pos - start;
                        tokens.Add(new GrassToken(GrassTokenKind.Number, _text.Substring(start, len), start, len, tokenLine, tokenCol, !hasDigits));
                        continue;
                    }
                    else
                    {
                        while (_pos < _length && char.IsDigit(Peek()))
                        {
                            Advance();
                        }
                        if (Peek() == '.' && char.IsDigit(Peek(1)))
                        {
                            Advance(); // .
                            while (_pos < _length && char.IsDigit(Peek()))
                            {
                                Advance();
                            }
                        }
                        if ((Peek() == 'e' || Peek() == 'E') && (char.IsDigit(Peek(1)) || ((Peek(1) == '+' || Peek(1) == '-') && char.IsDigit(Peek(2)))))
                        {
                            Advance();
                            if (Peek() == '+' || Peek() == '-') Advance();
                            while (_pos < _length && char.IsDigit(Peek()))
                            {
                                Advance();
                            }
                        }
                        int len = _pos - start;
                        tokens.Add(new GrassToken(GrassTokenKind.Number, _text.Substring(start, len), start, len, tokenLine, tokenCol));
                        continue;
                    }
                }

                // Identifiers, keywords, types, streams, constants, builtins
                if (IsIdentStart(c))
                {
                    while (_pos < _length && (IsIdentChar(Peek()) || Peek() == '-'))
                    {
                        // Check for kebab-case keywords like border-radius-app, window-ob, header-color, text-color
                        if (Peek() == '-' && !IsIdentChar(Peek(1)))
                            break;
                        Advance();
                    }
                    int len = _pos - start;
                    string word = _text.Substring(start, len);

                    GrassTokenKind kind = GrassTokenKind.Identifier;
                    if (GrassLanguageData.Keywords.Contains(word))
                        kind = GrassTokenKind.Keyword;
                    else if (GrassLanguageData.Types.Contains(word))
                        kind = GrassTokenKind.Type;
                    else if (GrassLanguageData.Streams.Contains(word))
                        kind = GrassTokenKind.StreamIo;
                    else if (GrassLanguageData.Constants.Contains(word))
                        kind = GrassTokenKind.Constant;
                    else if (GrassLanguageData.BuiltinMap.ContainsKey(word))
                        kind = GrassTokenKind.BuiltinFunction;

                    tokens.Add(new GrassToken(kind, word, start, len, tokenLine, tokenCol));
                    continue;
                }

                // Two-character operators
                if (c == '<' && Peek(1) == '<')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "<<", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '>' && Peek(1) == '>')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, ">>", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '+' && Peek(1) == '+')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "++", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '-' && Peek(1) == '-')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "--", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '+' && Peek(1) == '=')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "+=", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '-' && Peek(1) == '=')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "-=", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '*' && Peek(1) == '=')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "*=", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '/' && Peek(1) == '=')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "/=", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '-' && Peek(1) == '>')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "->", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '=' && Peek(1) == '>')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "=>", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '=' && Peek(1) == '=')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "==", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '<' && Peek(1) == '=')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "<=", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '>' && Peek(1) == '=')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, ">=", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '&' && Peek(1) == '&')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "&&", start, 2, tokenLine, tokenCol));
                    continue;
                }
                if (c == '|' && Peek(1) == '|')
                {
                    Advance(); Advance();
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, "||", start, 2, tokenLine, tokenCol));
                    continue;
                }

                // Single character operators and punctuation
                Advance();
                if ("+-*/%<>=&|^~".IndexOf(c) >= 0)
                {
                    tokens.Add(new GrassToken(GrassTokenKind.Operator, c.ToString(), start, 1, tokenLine, tokenCol));
                }
                else if ("{}()[];,:.".IndexOf(c) >= 0)
                {
                    tokens.Add(new GrassToken(GrassTokenKind.Punctuation, c.ToString(), start, 1, tokenLine, tokenCol));
                }
                else
                {
                    tokens.Add(new GrassToken(GrassTokenKind.Unknown, c.ToString(), start, 1, tokenLine, tokenCol));
                }
            }

            tokens.Add(new GrassToken(GrassTokenKind.EndOfFile, string.Empty, _length, 0, _line, _length - _lineStart + 1));
            return tokens;
        }

        private static bool IsIdentStart(char c)
        {
            return char.IsLetter(c) || c == '_' || c == '$';
        }

        private static bool IsIdentChar(char c)
        {
            return char.IsLetterOrDigit(c) || c == '_' || c == '$';
        }

        private static bool IsHexDigit(char c)
        {
            return (c >= '0' && c <= '9') ||
                   (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F');
        }
    }
}

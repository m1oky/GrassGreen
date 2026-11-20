using System.Collections.Generic;

namespace GrassGreen.VisualStudio.Parser
{
    public enum GrassSymbolKind
    {
        Variable,
        Function,
        Struct,
        Parameter
    }

    public class GrassStructField
    {
        public string Name { get; set; }
        public string TypeName { get; set; }

        public GrassStructField(string name, string typeName)
        {
            Name = name;
            TypeName = typeName ?? "take";
        }
    }

    public class GrassStructMethod
    {
        public string Name { get; set; }
        public string ReturnType { get; set; }
        public List<string> Parameters { get; set; }
        public int Line { get; set; }
        public int BodyStart { get; set; }
        public int BodyEnd { get; set; }

        public GrassStructMethod(string name, string returnType = "take", int line = 0)
        {
            Name = name;
            ReturnType = returnType ?? "take";
            Parameters = new List<string>();
            Line = line;
            BodyStart = 0;
            BodyEnd = 0;
        }

        public string GetSignature()
        {
            string p = string.Join(", ", Parameters);
            return $"{ReturnType} {Name}({p})";
        }
    }

    public class GrassSymbol
    {
        public string Name { get; set; }
        public GrassSymbolKind Kind { get; set; }
        public string TypeName { get; set; }
        public int Line { get; set; }
        public int Column { get; set; }
        public int Start { get; set; }
        public int Length { get; set; }
        public string ScopeName { get; set; }
        public List<string> Parameters { get; set; }
        public List<GrassStructField> Fields { get; set; }
        public List<GrassStructMethod> Methods { get; set; }
        public int BodyStart { get; set; }
        public int BodyEnd { get; set; }

        public GrassSymbol(string name, GrassSymbolKind kind, string typeName, int line, int column, int start, int length, string scopeName = "global")
        {
            Name = name;
            Kind = kind;
            TypeName = typeName ?? "take";
            Line = line;
            Column = column;
            Start = start;
            Length = length;
            ScopeName = scopeName;
            Parameters = new List<string>();
            Fields = new List<GrassStructField>();
            Methods = new List<GrassStructMethod>();
            BodyStart = 0;
            BodyEnd = 0;
        }

        public string GetDescription()
        {
            if (Kind == GrassSymbolKind.Function)
            {
                string paramList = string.Join(", ", Parameters);
                return $"Пользовательская функция: {TypeName} {Name}({paramList})\nОбъявлена на строке {Line}";
            }
            if (Kind == GrassSymbolKind.Struct)
            {
                string fieldsDesc = Fields.Count > 0
                    ? "\nПоля: " + string.Join(", ", Fields.ConvertAll(f => $"{f.Name}: {f.TypeName}"))
                    : "";
                string methodsDesc = Methods.Count > 0
                    ? "\nМетоды: " + string.Join(", ", Methods.ConvertAll(m => m.GetSignature()))
                    : "";
                return $"Структура: struct {Name}{fieldsDesc}{methodsDesc}\nОбъявлена на строке {Line}";
            }
            if (Kind == GrassSymbolKind.Parameter)
            {
                return $"Параметр функции: {TypeName} {Name}\nОбласть видимости: {ScopeName}\nСтрока {Line}";
            }
            return $"Переменная: {TypeName} {Name}\nОбъявлена на строке {Line} (область видимости: {ScopeName})";
        }
    }

    public class GrassSyntaxError
    {
        public string Message { get; set; }
        public int Start { get; set; }
        public int Length { get; set; }
        public int Line { get; set; }
        public int Column { get; set; }

        public GrassSyntaxError(string message, int start, int length, int line, int column)
        {
            Message = message;
            Length = length > 0 ? length : 1;
            Start = start;
            Line = line;
            Column = column;
        }

        public override string ToString()
        {
            return $"({Line}:{Column}) Ошибка синтаксиса: {Message}";
        }
    }

    public class GrassParseResult
    {
        public List<GrassToken> Tokens { get; }
        public List<GrassSymbol> Symbols { get; }
        public List<GrassSyntaxError> Errors { get; }
        public HashSet<string> IncludedModules { get; }
        public Dictionary<string, GrassSymbol> Structs { get; }

        public GrassParseResult(List<GrassToken> tokens, List<GrassSymbol> symbols, List<GrassSyntaxError> errors, HashSet<string> modules)
        {
            Tokens = tokens ?? new List<GrassToken>();
            Symbols = symbols ?? new List<GrassSymbol>();
            Errors = errors ?? new List<GrassSyntaxError>();
            IncludedModules = modules ?? new HashSet<string>();
            Structs = new Dictionary<string, GrassSymbol>(System.StringComparer.Ordinal);

            foreach (var sym in Symbols)
            {
                if (sym.Kind == GrassSymbolKind.Struct && !Structs.ContainsKey(sym.Name))
                {
                    Structs[sym.Name] = sym;
                }
            }
        }
    }
}

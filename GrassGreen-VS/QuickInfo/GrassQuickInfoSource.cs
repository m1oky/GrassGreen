using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.Language.Intellisense;
using Microsoft.VisualStudio.Text;
using GrassGreen.VisualStudio.Constants;
using GrassGreen.VisualStudio.Parser;

namespace GrassGreen.VisualStudio.QuickInfo
{
    public class GrassQuickInfoSource : IAsyncQuickInfoSource
    {
        private readonly ITextBuffer _buffer;
        private bool _disposed;

        public GrassQuickInfoSource(ITextBuffer buffer)
        {
            _buffer = buffer;
        }

        public Task<QuickInfoItem> GetQuickInfoItemAsync(IAsyncQuickInfoSession session, CancellationToken token)
        {
            if (_disposed || session == null) return Task.FromResult<QuickInfoItem>(null);

            SnapshotPoint? triggerPoint = session.GetTriggerPoint(_buffer.CurrentSnapshot);
            if (!triggerPoint.HasValue) return Task.FromResult<QuickInfoItem>(null);

            ITextSnapshot snapshot = _buffer.CurrentSnapshot;
            SnapshotPoint point = triggerPoint.Value;

            // Find word at point
            int start = point.Position;
            int end = point.Position;

            while (start > 0)
            {
                char ch = snapshot[start - 1];
                if (char.IsLetterOrDigit(ch) || ch == '_' || ch == '!' || ch == '-' || ch == '#')
                    start--;
                else
                    break;
            }

            while (end < snapshot.Length)
            {
                char ch = snapshot[end];
                if (char.IsLetterOrDigit(ch) || ch == '_' || ch == '!' || ch == '-' || ch == '#')
                    end++;
                else
                    break;
            }

            if (start >= end) return Task.FromResult<QuickInfoItem>(null);

            string word = snapshot.GetText(start, end - start);
            ITrackingSpan trackingSpan = snapshot.CreateTrackingSpan(new Span(start, end - start), SpanTrackingMode.EdgeInclusive);

            string tooltip = null;

            // 1. Directives
            foreach (var mod in GrassLanguageData.Modules)
            {
                if (string.Equals(mod.Name, word, StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(mod.Name, "!" + word + "!", StringComparison.OrdinalIgnoreCase))
                {
                    tooltip = $"Модуль GrassGreen: {mod.Name}\n\n{mod.Documentation}";
                    break;
                }
            }

            // 2. Builtin functions
            if (tooltip == null && GrassLanguageData.BuiltinMap.TryGetValue(word, out BuiltinFunctionInfo fn))
            {
                tooltip = $"{fn.Signature}\n\n{fn.Documentation}";
            }

            // 2.5 Struct methods, fields and 'this'
            if (tooltip == null && _buffer.Properties.TryGetProperty("GrassLatestParseResult", out GrassParseResult pr) && pr != null)
            {
                if (word == "this")
                {
                    if (pr.Structs != null)
                    {
                        foreach (var kvp in pr.Structs)
                        {
                            if (point.Position >= kvp.Value.BodyStart && point.Position <= kvp.Value.BodyEnd)
                            {
                                tooltip = $"Ключевое слово this: ссылка на экземпляр структуры struct {kvp.Value.Name}";
                                break;
                            }
                        }
                    }
                    if (tooltip == null)
                    {
                        tooltip = "Ключевое слово this: ссылка на текущий экземпляр структуры.";
                    }
                }
                else if (pr.Structs != null)
                {
                    foreach (var kvp in pr.Structs)
                    {
                        var m = kvp.Value.Methods?.Find(meth => meth.Name == word);
                        if (m != null)
                        {
                            tooltip = $"Метод структуры {kvp.Value.Name}: {m.GetSignature()}\nОбъявлен на строке {m.Line}";
                            break;
                        }
                        var f = kvp.Value.Fields?.Find(field => field.Name == word);
                        if (f != null)
                        {
                            tooltip = $"Поле структуры {kvp.Value.Name}: {f.TypeName} {f.Name}";
                            break;
                        }
                    }
                }
            }

            // 3. User symbols from AST
            if (tooltip == null && _buffer.Properties.TryGetProperty("GrassLatestParseResult", out GrassParseResult parseResult) && parseResult?.Symbols != null)
            {
                foreach (var sym in parseResult.Symbols)
                {
                    if (string.Equals(sym.Name, word, StringComparison.Ordinal))
                    {
                        tooltip = sym.GetDescription();
                        break;
                    }
                }
            }

            // 4. Stream I/O
            if (tooltip == null && GrassLanguageData.Streams.Contains(word))
            {
                tooltip = $"Стандартный потоковый ввод-вывод: {word}";
            }

            // 5. Keywords
            if (tooltip == null && GrassLanguageData.Keywords.Contains(word))
            {
                tooltip = $"Ключевое слово GrassGreen: {word}";
            }

            // 6. Types
            if (tooltip == null && GrassLanguageData.Types.Contains(word))
            {
                tooltip = $"Примитивный тип данных: {word}";
            }

            // 7. Constants
            if (tooltip == null && GrassLanguageData.Constants.Contains(word))
            {
                tooltip = $"Константа: {word}";
            }

            if (string.IsNullOrEmpty(tooltip))
                return Task.FromResult<QuickInfoItem>(null);

            return Task.FromResult(new QuickInfoItem(trackingSpan, tooltip));
        }

        public void Dispose()
        {
            _disposed = true;
        }
    }
}

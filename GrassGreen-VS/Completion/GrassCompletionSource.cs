using System;
using System.Collections.Generic;
using Microsoft.VisualStudio.Language.Intellisense;
using Microsoft.VisualStudio.Text;
using GrassGreen.VisualStudio.Constants;
using GrassGreen.VisualStudio.Parser;

namespace GrassGreen.VisualStudio.Completion
{
    public class GrassCompletionSource : ICompletionSource
    {
        private readonly ITextBuffer _buffer;
        private bool _disposed;

        public GrassCompletionSource(ITextBuffer buffer)
        {
            _buffer = buffer;
        }

        public void AugmentCompletionSession(ICompletionSession session, IList<CompletionSet> completionSets)
        {
            if (_disposed || session == null) return;

            SnapshotPoint? triggerPoint = session.GetTriggerPoint(_buffer.CurrentSnapshot);
            if (!triggerPoint.HasValue) return;

            ITextSnapshot snapshot = _buffer.CurrentSnapshot;
            SnapshotPoint point = triggerPoint.Value;

            // Check if inside string or comment
            ITextSnapshotLine line = point.GetContainingLine();
            string lineText = line.GetText();
            int col = point.Position - line.Start.Position;
            if (col > lineText.Length) col = lineText.Length;

            bool inQuote = false;
            for (int i = 0; i < col; i++)
            {
                if (lineText[i] == '"' && (i == 0 || lineText[i - 1] != '\\'))
                    inQuote = !inQuote;
            }
            if (inQuote) return;

            int commentIdx = lineText.IndexOf("//", StringComparison.Ordinal);
            if (commentIdx >= 0 && commentIdx < col) return;

            // Find tracking span
            int start = point.Position;
            while (start > 0)
            {
                char ch = snapshot[start - 1];
                if (char.IsLetterOrDigit(ch) || ch == '_' || ch == '!' || ch == '-')
                {
                    start--;
                }
                else
                {
                    break;
                }
            }

            ITrackingSpan trackingSpan = snapshot.CreateTrackingSpan(new Span(start, point.Position - start), SpanTrackingMode.EdgeInclusive);
            var completions = new List<Microsoft.VisualStudio.Language.Intellisense.Completion>();

            // Retrieve parse result
            GrassParseResult parseResult = null;
            if (_buffer.Properties.TryGetProperty("GrassLatestParseResult", out GrassParseResult res))
            {
                parseResult = res;
            }

            // Dot member check
            if (start > 0 && snapshot[start - 1] == '.')
            {
                int objEnd = start - 1;
                while (objEnd > 0 && char.IsWhiteSpace(snapshot[objEnd - 1])) objEnd--;
                int objStart = objEnd;
                while (objStart > 0 && (char.IsLetterOrDigit(snapshot[objStart - 1]) || snapshot[objStart - 1] == '_')) objStart--;

                string targetObj = objEnd > objStart ? snapshot.GetText(objStart, objEnd - objStart) : "";

                if (targetObj.Equals("input", StringComparison.OrdinalIgnoreCase))
                {
                    completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion("getline", "getline", "input.getline(prompt, target_var)", null, "Method"));
                }
                else if (targetObj.EndsWith("arr", StringComparison.OrdinalIgnoreCase) ||
                         targetObj.EndsWith("list", StringComparison.OrdinalIgnoreCase) ||
                         targetObj.EndsWith("items", StringComparison.OrdinalIgnoreCase))
                {
                    completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion("push", "push", "arr.push(val)", null, "Method"));
                    completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion("pop", "pop", "arr.pop()", null, "Method"));
                    completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion("len", "len", "arr.len()", null, "Method"));
                }
                else if (parseResult?.Structs != null)
                {
                    GrassSymbol structDef = null;
                    if (targetObj.Equals("this", StringComparison.Ordinal) || targetObj.Equals("this", StringComparison.OrdinalIgnoreCase))
                    {
                        // Find enclosing struct around point.Position
                        foreach (var kvp in parseResult.Structs)
                        {
                            if (point.Position >= kvp.Value.BodyStart && point.Position <= kvp.Value.BodyEnd)
                            {
                                structDef = kvp.Value;
                                break;
                            }
                        }
                    }
                    else
                    {
                        GrassSymbol varSym = parseResult.Symbols.Find(s => s.Name == targetObj);
                        if (varSym != null && parseResult.Structs.TryGetValue(varSym.TypeName, out GrassSymbol found)) structDef = found;
                        else if (parseResult.Structs.TryGetValue(targetObj, out GrassSymbol d)) structDef = d;
                    }

                    if (structDef != null)
                    {
                        foreach (var f in structDef.Fields)
                        {
                            completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(f.Name, f.Name, $"Поле: {f.TypeName} {f.Name}", null, "Field"));
                        }
                        foreach (var m in structDef.Methods)
                        {
                            completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(m.Name, m.Name, $"Метод: {m.GetSignature()}", null, "Method"));
                        }
                    }
                    else
                    {
                        // Fallback: offer fields and methods from all structs in file
                        var seen = new HashSet<string>(StringComparer.Ordinal);
                        foreach (var kvp in parseResult.Structs)
                        {
                            foreach (var f in kvp.Value.Fields)
                            {
                                if (seen.Add(f.Name))
                                    completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(f.Name, f.Name, $"Поле [{kvp.Value.Name}]: {f.TypeName} {f.Name}", null, "Field"));
                            }
                            foreach (var m in kvp.Value.Methods)
                            {
                                if (seen.Add(m.Name))
                                    completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(m.Name, m.Name, $"Метод [{kvp.Value.Name}]: {m.GetSignature()}", null, "Method"));
                            }
                        }
                    }
                }

                if (completions.Count > 0)
                {
                    completionSets.Add(new CompletionSet("GrassGreenMembers", "GrassGreen", trackingSpan, completions, null));
                    return;
                }
            }

            // Determine scope at point
            int caretPos = point.Position;
            GrassSymbol activeFunc = null;
            if (parseResult?.Symbols != null)
            {
                foreach (var sym in parseResult.Symbols)
                {
                    if (sym.Kind == GrassSymbolKind.Function && sym.BodyStart > 0 && sym.BodyEnd > 0)
                    {
                        if (caretPos >= sym.BodyStart && caretPos <= sym.BodyEnd)
                        {
                            activeFunc = sym;
                            break;
                        }
                    }
                }
            }

            // 1. Contextual user symbols (filtered by scope)
            if (parseResult?.Symbols != null)
            {
                var seen = new HashSet<string>(StringComparer.Ordinal);
                foreach (var sym in parseResult.Symbols)
                {
                    if (sym.Start >= caretPos) continue;

                    if (sym.Kind == GrassSymbolKind.Parameter)
                    {
                        if (activeFunc != null && sym.ScopeName == activeFunc.Name && seen.Add(sym.Name))
                        {
                            completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(sym.Name, sym.Name, sym.GetDescription(), null, "Parameter"));
                        }
                    }
                    else if (sym.Kind == GrassSymbolKind.Variable)
                    {
                        if (activeFunc != null && sym.ScopeName == activeFunc.Name)
                        {
                            if (seen.Add(sym.Name))
                                completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(sym.Name, sym.Name, sym.GetDescription(), null, "LocalVariable"));
                        }
                        else if (sym.ScopeName == "global")
                        {
                            if (seen.Add(sym.Name))
                                completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(sym.Name, sym.Name, sym.GetDescription(), null, "GlobalVariable"));
                        }
                    }
                    else if (sym.Kind == GrassSymbolKind.Function)
                    {
                        if (activeFunc != null && activeFunc.Name == "main" && sym.Name == "main") continue;
                        if (seen.Add(sym.Name))
                            completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(sym.Name, sym.Name, sym.GetDescription(), null, "Function"));
                    }
                    else if (sym.Kind == GrassSymbolKind.Struct)
                    {
                        if (seen.Add(sym.Name))
                            completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(sym.Name, sym.Name, sym.GetDescription(), null, "Struct"));
                    }
                }
            }

            // 2. Types
            foreach (var ty in GrassLanguageData.Types)
            {
                completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(ty, ty, $"Тип данных: {ty}", null, "Type"));
            }

            // 3. Keywords
            foreach (var kw in GrassLanguageData.Keywords)
            {
                completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(kw, kw, $"Ключевое слово: {kw}", null, "Keyword"));
            }

            // 4. Streams
            foreach (var st in GrassLanguageData.Streams)
            {
                completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(st, st, $"Потоковый ввод-вывод: {st}", null, "Stream"));
            }

            // 5. Constants
            foreach (var c in GrassLanguageData.Constants)
            {
                completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(c, c, $"Константа: {c}", null, "Constant"));
            }

            // 6. Directives
            foreach (var mod in GrassLanguageData.Modules)
            {
                completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(mod.Name, mod.Name, $"Директива {mod.Name}\n{mod.Documentation}", null, "Directive"));
            }

            // 7. Built-in functions filtered by module
            HashSet<string> incMods = parseResult?.IncludedModules ?? new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var fn in GrassLanguageData.BuiltinFunctions)
            {
                if (fn.Module == "core" || GrassLanguageData.IsModuleIncluded(fn.Module, incMods))
                {
                    completions.Add(new Microsoft.VisualStudio.Language.Intellisense.Completion(fn.Name, fn.Name, $"[{fn.Module}] {fn.Signature}\n{fn.Documentation}", null, "Builtin"));
                }
            }

            completionSets.Add(new CompletionSet(
                "GrassGreen",
                "GrassGreen",
                trackingSpan,
                completions,
                null));
        }

        public void Dispose()
        {
            _disposed = true;
        }
    }
}

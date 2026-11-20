using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.Core.Imaging;
using Microsoft.VisualStudio.Imaging;
using Microsoft.VisualStudio.Language.Intellisense.AsyncCompletion;
using Microsoft.VisualStudio.Language.Intellisense.AsyncCompletion.Data;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Adornments;
using GrassGreen.VisualStudio.Constants;
using GrassGreen.VisualStudio.Parser;

namespace GrassGreen.VisualStudio.Completion
{
    public class GrassAsyncCompletionSource : IAsyncCompletionSource
    {
        private static ImageElement CreateIcon(Microsoft.VisualStudio.Imaging.Interop.ImageMoniker moniker)
        {
            return new ImageElement(new ImageId(moniker.Guid, moniker.Id));
        }

        private static readonly ImageElement KeywordIcon = CreateIcon(KnownMonikers.IntellisenseKeyword);
        private static readonly ImageElement DirectiveIcon = CreateIcon(KnownMonikers.Module);
        private static readonly ImageElement TypeIcon = CreateIcon(KnownMonikers.Type);
        private static readonly ImageElement FunctionIcon = CreateIcon(KnownMonikers.Method);
        private static readonly ImageElement VariableIcon = CreateIcon(KnownMonikers.LocalVariable);
        private static readonly ImageElement ConstantIcon = CreateIcon(KnownMonikers.Constant);
        private static readonly ImageElement StreamIcon = CreateIcon(KnownMonikers.Property);
        private static readonly ImageElement FieldIcon = CreateIcon(KnownMonikers.Field);

        public CompletionStartData InitializeCompletion(CompletionTrigger trigger, SnapshotPoint triggerLocation, CancellationToken token)
        {
            // 1. Never trigger inside strings or comments
            if (IsInsideStringOrComment(triggerLocation))
            {
                return CompletionStartData.DoesNotParticipateInCompletion;
            }

            // 2. Explicit manual invoke (Ctrl+Space) always participates
            if (trigger.Reason == CompletionTriggerReason.Invoke)
            {
                SnapshotSpan span = FindApplicableSpan(triggerLocation);
                return new CompletionStartData(CompletionParticipation.ProvidesItems, span);
            }

            // 3. For typed characters, validate triggers strictly
            if (trigger.Reason == CompletionTriggerReason.Insertion)
            {
                char ch = trigger.Character;

                // Never popup on whitespace, Enter, Tab, semicolon, brackets, parentheses or commas
                if (char.IsWhiteSpace(ch) || ch == ';' || ch == '{' || ch == '}' || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == ',' || ch == ':')
                {
                    return CompletionStartData.DoesNotParticipateInCompletion;
                }

                // Allowed triggers: letters, underscore, dot (for members), exclamation mark (for directives)
                if (char.IsLetter(ch) || ch == '_' || ch == '.' || ch == '!')
                {
                    SnapshotSpan span = FindApplicableSpan(triggerLocation);
                    return new CompletionStartData(CompletionParticipation.ProvidesItems, span);
                }

                // Digits are only allowed when continuing an identifier (e.g. var1)
                if (char.IsDigit(ch) && triggerLocation.Position > 1)
                {
                    char prev = triggerLocation.Snapshot[triggerLocation.Position - 2];
                    if (char.IsLetter(prev) || prev == '_')
                    {
                        SnapshotSpan span = FindApplicableSpan(triggerLocation);
                        return new CompletionStartData(CompletionParticipation.ProvidesItems, span);
                    }
                }

                return CompletionStartData.DoesNotParticipateInCompletion;
            }

            // Backspace / deletion: participate only if cursor touches an identifier, dot, or directive
            if (trigger.Reason == CompletionTriggerReason.Backspace || trigger.Reason == CompletionTriggerReason.Deletion)
            {
                if (triggerLocation.Position > 0)
                {
                    char prev = triggerLocation.Snapshot[triggerLocation.Position - 1];
                    if (char.IsLetterOrDigit(prev) || prev == '_' || prev == '.' || prev == '!')
                    {
                        SnapshotSpan span = FindApplicableSpan(triggerLocation);
                        return new CompletionStartData(CompletionParticipation.ProvidesItems, span);
                    }
                }
            }

            return CompletionStartData.DoesNotParticipateInCompletion;
        }

        public Task<CompletionContext> GetCompletionContextAsync(
            IAsyncCompletionSession session,
            CompletionTrigger trigger,
            SnapshotPoint triggerLocation,
            SnapshotSpan applicableToSpan,
            CancellationToken token)
        {
            var builder = ImmutableArray.CreateBuilder<CompletionItem>();
            ITextSnapshot snapshot = triggerLocation.Snapshot;
            ITextBuffer buffer = snapshot.TextBuffer;

            // Retrieve or produce AST parse result
            GrassParseResult parseResult = null;
            if (buffer.Properties.TryGetProperty("GrassLatestParseResult", out GrassParseResult res))
            {
                parseResult = res;
            }
            else
            {
                var lexer = new GrassLexer(snapshot.GetText());
                var tokens = lexer.Tokenize();
                var parser = new GrassParser(tokens);
                parseResult = parser.Parse();
            }

            // Check if member access (dot completion)
            bool isDotAccess = false;
            string targetObj = null;
            int spanStart = applicableToSpan.Start.Position;

            if (spanStart > 0 && snapshot[spanStart - 1] == '.')
            {
                isDotAccess = true;
                int dotPos = spanStart - 1;
                int objEnd = dotPos;
                while (objEnd > 0 && char.IsWhiteSpace(snapshot[objEnd - 1]))
                {
                    objEnd--;
                }
                int objStart = objEnd;
                while (objStart > 0)
                {
                    char c = snapshot[objStart - 1];
                    if (char.IsLetterOrDigit(c) || c == '_') objStart--;
                    else break;
                }
                if (objEnd > objStart)
                {
                    targetObj = snapshot.GetText(objStart, objEnd - objStart);
                }
            }

            // --- DOT MEMBER COMPLETION ---
            if (isDotAccess)
            {
                AddMemberCompletions(builder, targetObj, parseResult, triggerLocation.Position);
                return Task.FromResult(new CompletionContext(builder.ToImmutable()));
            }

            // Check if typing a directive starting with '!'
            bool isTypingDirective = applicableToSpan.Length > 0 && applicableToSpan.GetText().StartsWith("!");

            // Determine active scope at caret position
            int caretPos = triggerLocation.Position;
            string activeScope = "global";
            GrassSymbol activeFunc = null;

            if (parseResult?.Symbols != null)
            {
                foreach (var sym in parseResult.Symbols)
                {
                    if (sym.Kind == GrassSymbolKind.Function && sym.BodyStart > 0 && sym.BodyEnd > 0)
                    {
                        if (caretPos >= sym.BodyStart && caretPos <= sym.BodyEnd)
                        {
                            activeScope = sym.Name;
                            activeFunc = sym;
                            break;
                        }
                    }
                }
            }

            // 1. User variables and parameters (Scope-Aware)
            if (parseResult?.Symbols != null)
            {
                var seenSymbols = new HashSet<string>(StringComparer.Ordinal);

                foreach (var sym in parseResult.Symbols)
                {
                    // Exclude symbols declared below cursor
                    if (sym.Start >= caretPos) continue;

                    if (sym.Kind == GrassSymbolKind.Parameter)
                    {
                        // Parameters of current function only
                        if (activeFunc != null && sym.ScopeName == activeFunc.Name)
                        {
                            if (!seenSymbols.Add(sym.Name)) continue;
                            AddSymbolItem(builder, sym, "00_" + sym.Name, VariableIcon, "(parameter)");
                        }
                    }
                    else if (sym.Kind == GrassSymbolKind.Variable)
                    {
                        if (activeFunc != null && sym.ScopeName == activeFunc.Name)
                        {
                            // Local variable of current function
                            if (!seenSymbols.Add(sym.Name)) continue;
                            AddSymbolItem(builder, sym, "00_" + sym.Name, VariableIcon, "(local)");
                        }
                        else if (sym.ScopeName == "global")
                        {
                            // Global variable
                            if (!seenSymbols.Add(sym.Name)) continue;
                            AddSymbolItem(builder, sym, "01_" + sym.Name, VariableIcon, "(global)");
                        }
                        // Variables from other functions are strictly ignored!
                    }
                }

                // 2. User functions
                foreach (var sym in parseResult.Symbols)
                {
                    if (sym.Kind == GrassSymbolKind.Function)
                    {
                        // Do not suggest main() inside main()
                        if (activeFunc != null && activeFunc.Name == "main" && sym.Name == "main") continue;
                        if (!seenSymbols.Add(sym.Name)) continue;
                        AddSymbolItem(builder, sym, "02_" + sym.Name, FunctionIcon, "()");
                    }
                }

                // 3. User structs
                foreach (var sym in parseResult.Symbols)
                {
                    if (sym.Kind == GrassSymbolKind.Struct)
                    {
                        if (!seenSymbols.Add(sym.Name)) continue;
                        AddSymbolItem(builder, sym, "03_" + sym.Name, TypeIcon, "(struct)");
                    }
                }
            }

            // 4. Primitive Types (03_)
            foreach (var ty in GrassLanguageData.Types)
            {
                var item = new CompletionItem(
                    displayText: ty,
                    source: this,
                    icon: TypeIcon,
                    filters: ImmutableArray<CompletionFilter>.Empty,
                    suffix: " (type)",
                    insertText: ty,
                    sortText: "03_" + ty,
                    filterText: ty,
                    attributeIcons: ImmutableArray<ImageElement>.Empty);
                item.Properties.AddProperty("GrassDescription", $"Тип данных GrassGreen: {ty}");
                builder.Add(item);
            }

            // 5. Cleaned Keywords (04_)
            foreach (var kw in GrassLanguageData.Keywords)
            {
                var item = new CompletionItem(
                    displayText: kw,
                    source: this,
                    icon: KeywordIcon,
                    filters: ImmutableArray<CompletionFilter>.Empty,
                    suffix: " (keyword)",
                    insertText: kw,
                    sortText: "04_" + kw,
                    filterText: kw,
                    attributeIcons: ImmutableArray<ImageElement>.Empty);
                item.Properties.AddProperty("GrassDescription", $"Ключевое слово языка GrassGreen: {kw}");
                builder.Add(item);
            }

            // 6. Streams (05_)
            foreach (var st in GrassLanguageData.Streams)
            {
                var item = new CompletionItem(
                    displayText: st,
                    source: this,
                    icon: StreamIcon,
                    filters: ImmutableArray<CompletionFilter>.Empty,
                    suffix: " (stream)",
                    insertText: st,
                    sortText: "05_" + st,
                    filterText: st,
                    attributeIcons: ImmutableArray<ImageElement>.Empty);
                item.Properties.AddProperty("GrassDescription", $"Потоковый ввод-вывод GrassGreen: {st}");
                builder.Add(item);
            }

            // 7. Built-in functions filtered by included modules or core (06_)
            HashSet<string> incMods = parseResult?.IncludedModules ?? new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var fn in GrassLanguageData.BuiltinFunctions)
            {
                if (fn.Module == "core" || GrassLanguageData.IsModuleIncluded(fn.Module, incMods))
                {
                    var item = new CompletionItem(
                        displayText: fn.Name,
                        source: this,
                        icon: FunctionIcon,
                        filters: ImmutableArray<CompletionFilter>.Empty,
                        suffix: "()",
                        insertText: fn.Name,
                        sortText: "06_" + fn.Name,
                        filterText: fn.Name,
                        attributeIcons: ImmutableArray<ImageElement>.Empty);
                    item.Properties.AddProperty("GrassDescription", $"Встроенная функция [{fn.Module}]: {fn.Signature}\n\n{fn.Documentation}");
                    builder.Add(item);
                }
            }

            // 8. Directives (07_ normally, or 00_ if typing '!')
            string dirSortPrefix = isTypingDirective ? "00_" : "07_";
            foreach (var mod in GrassLanguageData.Modules)
            {
                var item = new CompletionItem(
                    displayText: mod.Name,
                    source: this,
                    icon: DirectiveIcon,
                    filters: ImmutableArray<CompletionFilter>.Empty,
                    suffix: " (module)",
                    insertText: mod.Name,
                    sortText: dirSortPrefix + mod.Name,
                    filterText: mod.Name,
                    attributeIcons: ImmutableArray<ImageElement>.Empty);
                item.Properties.AddProperty("GrassDescription", $"Директива модуля GrassGreen:\n{mod.Name}\n\n{mod.Documentation}");
                builder.Add(item);
            }

            // 9. Constants (08_)
            foreach (var c in GrassLanguageData.Constants)
            {
                var item = new CompletionItem(
                    displayText: c,
                    source: this,
                    icon: ConstantIcon,
                    filters: ImmutableArray<CompletionFilter>.Empty,
                    suffix: " (constant)",
                    insertText: c,
                    sortText: "08_" + c,
                    filterText: c,
                    attributeIcons: ImmutableArray<ImageElement>.Empty);
                item.Properties.AddProperty("GrassDescription", $"Константа: {c}");
                builder.Add(item);
            }

            return Task.FromResult(new CompletionContext(builder.ToImmutable()));
        }

        public Task<object> GetDescriptionAsync(IAsyncCompletionSession session, CompletionItem item, CancellationToken token)
        {
            if (item.Properties.TryGetProperty("GrassDescription", out string desc))
            {
                return Task.FromResult<object>(desc);
            }
            return Task.FromResult<object>(item.DisplayText);
        }

        private void AddMemberCompletions(ImmutableArray<CompletionItem>.Builder builder, string targetObj, GrassParseResult parseResult, int caretPos = 0)
        {
            if (string.IsNullOrEmpty(targetObj)) return;

            // 1. input.getline
            if (targetObj.Equals("input", StringComparison.OrdinalIgnoreCase))
            {
                var item = new CompletionItem(
                    displayText: "getline",
                    source: this,
                    icon: FunctionIcon,
                    filters: ImmutableArray<CompletionFilter>.Empty,
                    suffix: "(prompt, target)",
                    insertText: "getline",
                    sortText: "00_getline",
                    filterText: "getline",
                    attributeIcons: ImmutableArray<ImageElement>.Empty);
                item.Properties.AddProperty("GrassDescription", "input.getline(prompt, target_var)\nСчитывание строки целиком вместе с пробелами до нажатия Enter.");
                builder.Add(item);
                return;
            }

            // 2. Check if targetObj is an array
            bool isArray = false;
            GrassSymbol varSym = parseResult?.Symbols?.Find(s => s.Name == targetObj);
            if (varSym != null && varSym.TypeName.Equals("array", StringComparison.OrdinalIgnoreCase))
            {
                isArray = true;
            }
            else if (targetObj.EndsWith("arr", StringComparison.OrdinalIgnoreCase) ||
                     targetObj.EndsWith("list", StringComparison.OrdinalIgnoreCase) ||
                     targetObj.EndsWith("items", StringComparison.OrdinalIgnoreCase))
            {
                isArray = true;
            }

            if (isArray)
            {
                var pushItem = new CompletionItem("push", this, FunctionIcon, ImmutableArray<CompletionFilter>.Empty, "(val)", "push", "00_push", "push", ImmutableArray<ImageElement>.Empty);
                pushItem.Properties.AddProperty("GrassDescription", "arr.push(val)\nДобавляет элемент val в конец динамического массива.");
                builder.Add(pushItem);

                var popItem = new CompletionItem("pop", this, FunctionIcon, ImmutableArray<CompletionFilter>.Empty, "()", "pop", "01_pop", "pop", ImmutableArray<ImageElement>.Empty);
                popItem.Properties.AddProperty("GrassDescription", "arr.pop()\nУдаляет и возвращает последний элемент динамического массива.");
                builder.Add(popItem);

                var lenItem = new CompletionItem("len", this, FunctionIcon, ImmutableArray<CompletionFilter>.Empty, "()", "len", "02_len", "len", ImmutableArray<ImageElement>.Empty);
                lenItem.Properties.AddProperty("GrassDescription", "arr.len()\nВозвращает текущую длину массива.");
                builder.Add(lenItem);
                return;
            }

            // 3. Struct member completion
            GrassSymbol structDef = null;
            if (targetObj.Equals("this", StringComparison.Ordinal) || targetObj.Equals("this", StringComparison.OrdinalIgnoreCase))
            {
                if (parseResult?.Structs != null)
                {
                    foreach (var kvp in parseResult.Structs)
                    {
                        if (caretPos >= kvp.Value.BodyStart && caretPos <= kvp.Value.BodyEnd)
                        {
                            structDef = kvp.Value;
                            break;
                        }
                    }
                }
            }
            else
            {
                if (varSym != null && parseResult?.Structs != null && parseResult.Structs.TryGetValue(varSym.TypeName, out GrassSymbol foundStruct))
                {
                    structDef = foundStruct;
                }
                else if (parseResult?.Structs != null && parseResult.Structs.TryGetValue(targetObj, out GrassSymbol directStruct))
                {
                    structDef = directStruct;
                }
            }

            if (structDef != null)
            {
                foreach (var f in structDef.Fields)
                {
                    var item = new CompletionItem(
                        displayText: f.Name,
                        source: this,
                        icon: FieldIcon,
                        filters: ImmutableArray<CompletionFilter>.Empty,
                        suffix: $" : {f.TypeName}",
                        insertText: f.Name,
                        sortText: "00_" + f.Name,
                        filterText: f.Name,
                        attributeIcons: ImmutableArray<ImageElement>.Empty);
                    item.Properties.AddProperty("GrassDescription", $"Поле структуры {structDef.Name}: {f.TypeName} {f.Name}");
                    builder.Add(item);
                }
                foreach (var m in structDef.Methods)
                {
                    string p = string.Join(", ", m.Parameters);
                    var item = new CompletionItem(
                        displayText: m.Name,
                        source: this,
                        icon: FunctionIcon,
                        filters: ImmutableArray<CompletionFilter>.Empty,
                        suffix: $"({p}) : {m.ReturnType}",
                        insertText: m.Name,
                        sortText: "00_" + m.Name,
                        filterText: m.Name,
                        attributeIcons: ImmutableArray<ImageElement>.Empty);
                    item.Properties.AddProperty("GrassDescription", $"Метод структуры {structDef.Name}: {m.GetSignature()}");
                    builder.Add(item);
                }
                return;
            }

            // Fallback: if struct type is not statically determined but structs exist in file, offer their fields & methods
            if (parseResult?.Structs != null && parseResult.Structs.Count > 0)
            {
                var seen = new HashSet<string>(StringComparer.Ordinal);
                foreach (var kvp in parseResult.Structs)
                {
                    foreach (var f in kvp.Value.Fields)
                    {
                        if (seen.Add(f.Name))
                        {
                            var item = new CompletionItem(
                                displayText: f.Name,
                                source: this,
                                icon: FieldIcon,
                                filters: ImmutableArray<CompletionFilter>.Empty,
                                suffix: $" : {f.TypeName} ({kvp.Key})",
                                insertText: f.Name,
                                sortText: "00_" + f.Name,
                                filterText: f.Name,
                                attributeIcons: ImmutableArray<ImageElement>.Empty);
                            item.Properties.AddProperty("GrassDescription", $"Поле структуры {kvp.Key}: {f.TypeName} {f.Name}");
                            builder.Add(item);
                        }
                    }
                    foreach (var m in kvp.Value.Methods)
                    {
                        if (seen.Add(m.Name))
                        {
                            string p = string.Join(", ", m.Parameters);
                            var item = new CompletionItem(
                                displayText: m.Name,
                                source: this,
                                icon: FunctionIcon,
                                filters: ImmutableArray<CompletionFilter>.Empty,
                                suffix: $"({p}) : {m.ReturnType} ({kvp.Key})",
                                insertText: m.Name,
                                sortText: "00_" + m.Name,
                                filterText: m.Name,
                                attributeIcons: ImmutableArray<ImageElement>.Empty);
                            item.Properties.AddProperty("GrassDescription", $"Метод структуры {kvp.Key}: {m.GetSignature()}");
                            builder.Add(item);
                        }
                    }
                }
            }
        }

        private void AddSymbolItem(ImmutableArray<CompletionItem>.Builder builder, GrassSymbol sym, string sortText, ImageElement icon, string suffix)
        {
            var item = new CompletionItem(
                displayText: sym.Name,
                source: this,
                icon: icon,
                filters: ImmutableArray<CompletionFilter>.Empty,
                suffix: $" {suffix} : {sym.TypeName}",
                insertText: sym.Name,
                sortText: sortText,
                filterText: sym.Name,
                attributeIcons: ImmutableArray<ImageElement>.Empty);
            item.Properties.AddProperty("GrassDescription", sym.GetDescription());
            builder.Add(item);
        }

        private static bool IsInsideStringOrComment(SnapshotPoint point)
        {
            ITextSnapshotLine line = point.GetContainingLine();
            string lineText = line.GetText();
            int col = point.Position - line.Start.Position;
            if (col > lineText.Length) col = lineText.Length;

            // Check single line comment
            int commentIdx = lineText.IndexOf("//", StringComparison.Ordinal);
            if (commentIdx >= 0 && commentIdx < col)
            {
                bool inQuotesBefore = false;
                for (int i = 0; i < commentIdx; i++)
                {
                    if (lineText[i] == '"' && (i == 0 || lineText[i - 1] != '\\'))
                        inQuotesBefore = !inQuotesBefore;
                }
                if (!inQuotesBefore) return true;
            }

            // Check quotation parity before caret
            bool insideQuote = false;
            for (int i = 0; i < col; i++)
            {
                if (lineText[i] == '"' && (i == 0 || lineText[i - 1] != '\\'))
                {
                    insideQuote = !insideQuote;
                }
            }
            if (insideQuote) return true;

            return false;
        }

        private SnapshotSpan FindApplicableSpan(SnapshotPoint point)
        {
            ITextSnapshot snapshot = point.Snapshot;
            int pos = point.Position;

            // If cursor is right after '.', the member prefix is empty
            if (pos > 0 && snapshot[pos - 1] == '.')
            {
                return new SnapshotSpan(snapshot, pos, 0);
            }

            int start = pos;
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

            int end = pos;
            while (end < snapshot.Length)
            {
                char ch = snapshot[end];
                if (char.IsLetterOrDigit(ch) || ch == '_' || ch == '!' || ch == '-')
                {
                    end++;
                }
                else
                {
                    break;
                }
            }

            return new SnapshotSpan(snapshot, Span.FromBounds(start, end));
        }
    }
}

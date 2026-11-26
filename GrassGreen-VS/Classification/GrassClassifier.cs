using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;
using GrassGreen.VisualStudio.Constants;
using GrassGreen.VisualStudio.Parser;

namespace GrassGreen.VisualStudio.Classification
{
    public class GrassClassifier : IClassifier
    {
        private readonly IClassificationTypeRegistryService _registry;
        private readonly IClassificationType _keywordType;
        private readonly IClassificationType _directiveType;
        private readonly IClassificationType _typeType;
        private readonly IClassificationType _builtinType;
        private readonly IClassificationType _functionType;
        private readonly IClassificationType _uiPropertyType;
        private readonly IClassificationType _stringType;
        private readonly IClassificationType _escapeType;
        private readonly IClassificationType _hexColorType;
        private readonly IClassificationType _numberType;
        private readonly IClassificationType _commentType;
        private readonly IClassificationType _operatorType;
        private readonly IClassificationType _constantType;

        private static readonly Regex StringPartRegex = new Regex(
            @"(\\(?:[\\'""ntr0abfv]|x[0-9A-Fa-f]{2}|u[0-9A-Fa-f]{4}|U[0-9A-Fa-f]{8}|[0-7]{1,3}))|(#[0-9A-Fa-f]{3,8}\b)",
            RegexOptions.Compiled);

#pragma warning disable CS0067
        public event EventHandler<ClassificationChangedEventArgs> ClassificationChanged;
#pragma warning restore CS0067

        public GrassClassifier(IClassificationTypeRegistryService registry)
        {
            _registry = registry;
            _keywordType = _registry.GetClassificationType(GrassClassificationDefinitions.Keyword);
            _directiveType = _registry.GetClassificationType(GrassClassificationDefinitions.Directive);
            _typeType = _registry.GetClassificationType(GrassClassificationDefinitions.Type);
            _builtinType = _registry.GetClassificationType(GrassClassificationDefinitions.Builtin);
            _functionType = _registry.GetClassificationType(GrassClassificationDefinitions.Function);
            _uiPropertyType = _registry.GetClassificationType(GrassClassificationDefinitions.UiProperty);
            _stringType = _registry.GetClassificationType(GrassClassificationDefinitions.String);
            _escapeType = _registry.GetClassificationType(GrassClassificationDefinitions.Escape);
            _hexColorType = _registry.GetClassificationType(GrassClassificationDefinitions.HexColor);
            _numberType = _registry.GetClassificationType(GrassClassificationDefinitions.Number);
            _commentType = _registry.GetClassificationType(GrassClassificationDefinitions.Comment);
            _operatorType = _registry.GetClassificationType(GrassClassificationDefinitions.Operator);
            _constantType = _registry.GetClassificationType(GrassClassificationDefinitions.Constant);
        }

        public IList<ClassificationSpan> GetClassificationSpans(SnapshotSpan span)
        {
            var result = new List<ClassificationSpan>();
            if (span.IsEmpty) return result;

            ITextSnapshotLine startLine = span.Start.GetContainingLine();
            ITextSnapshotLine endLine = span.End.GetContainingLine();
            var lineSpan = new SnapshotSpan(startLine.Start, endLine.End);

            string lineText = lineSpan.GetText();
            var lexer = new GrassLexer(lineText);
            var tokens = lexer.Tokenize();

            int lineOffset = lineSpan.Start.Position;

            for (int i = 0; i < tokens.Count; i++)
            {
                var tok = tokens[i];
                if (tok.Kind == GrassTokenKind.EndOfFile || tok.Length == 0) continue;

                int tokStart = lineOffset + tok.Start;
                int tokEnd = tokStart + tok.Length;

                int matchStart = Math.Max(tokStart, span.Start.Position);
                int matchEnd = Math.Min(tokEnd, span.End.Position);
                if (matchEnd <= matchStart) continue;

                if (tok.Kind == GrassTokenKind.String)
                {
                    AddStringSpans(span.Snapshot, lineOffset, tok, result);
                    continue;
                }

                bool isFollowedByParen = (i + 1 < tokens.Count && tokens[i + 1].Text == "(");
                IClassificationType classificationType = GetClassificationForToken(tok, isFollowedByParen);
                if (classificationType != null)
                {
                    var tokSpan = new SnapshotSpan(span.Snapshot, new Span(tokStart, tok.Length));
                    result.Add(new ClassificationSpan(tokSpan, classificationType));
                }
            }

            return result;
        }

        private void AddStringSpans(ITextSnapshot snapshot, int lineOffset, GrassToken tok, List<ClassificationSpan> result)
        {
            string raw = tok.Text ?? string.Empty;
            int basePos = lineOffset + tok.Start;
            int cursor = 0;

            var matches = StringPartRegex.Matches(raw);
            foreach (Match m in matches)
            {
                if (m.Index > cursor && _stringType != null)
                {
                    result.Add(new ClassificationSpan(
                        new SnapshotSpan(snapshot, new Span(basePos + cursor, m.Index - cursor)),
                        _stringType));
                }

                IClassificationType subType = m.Groups[1].Success ? _escapeType : _hexColorType;
                if (subType != null)
                {
                    result.Add(new ClassificationSpan(
                        new SnapshotSpan(snapshot, new Span(basePos + m.Index, m.Length)),
                        subType));
                }
                cursor = m.Index + m.Length;
            }

            if (cursor < raw.Length && _stringType != null)
            {
                result.Add(new ClassificationSpan(
                    new SnapshotSpan(snapshot, new Span(basePos + cursor, raw.Length - cursor)),
                    _stringType));
            }
        }

        private IClassificationType GetClassificationForToken(GrassToken tok, bool isFollowedByParen)
        {
            string text = tok.Text ?? string.Empty;
            switch (tok.Kind)
            {
                case GrassTokenKind.Keyword:
                    if (GrassLanguageData.Types.Contains(text))
                        return _typeType;
                    return _keywordType;
                case GrassTokenKind.Directive:
                    return _directiveType;
                case GrassTokenKind.Type:
                    return _typeType;
                case GrassTokenKind.BuiltinFunction:
                    if (GrassLanguageData.UiProperties.Contains(text))
                        return _uiPropertyType;
                    return _builtinType;
                case GrassTokenKind.StreamIo:
                case GrassTokenKind.Constant:
                    if (text.StartsWith("#", StringComparison.Ordinal))
                        return _hexColorType;
                    return _constantType;
                case GrassTokenKind.Identifier:
                    if (GrassLanguageData.UiProperties.Contains(text) || text.Contains("-"))
                        return _uiPropertyType;
                    if (isFollowedByParen)
                        return _functionType;
                    return null;
                case GrassTokenKind.String:
                    return _stringType;
                case GrassTokenKind.Number:
                    return _numberType;
                case GrassTokenKind.CommentLine:
                case GrassTokenKind.CommentBlock:
                    return _commentType;
                case GrassTokenKind.Operator:
                    return _operatorType;
                default:
                    return null;
            }
        }
    }
}

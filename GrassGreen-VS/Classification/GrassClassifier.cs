using System;
using System.Collections.Generic;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;
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
        private readonly IClassificationType _stringType;
        private readonly IClassificationType _numberType;
        private readonly IClassificationType _commentType;
        private readonly IClassificationType _operatorType;
        private readonly IClassificationType _constantType;

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
            _stringType = _registry.GetClassificationType(GrassClassificationDefinitions.String);
            _numberType = _registry.GetClassificationType(GrassClassificationDefinitions.Number);
            _commentType = _registry.GetClassificationType(GrassClassificationDefinitions.Comment);
            _operatorType = _registry.GetClassificationType(GrassClassificationDefinitions.Operator);
            _constantType = _registry.GetClassificationType(GrassClassificationDefinitions.Constant);
        }

        public IList<ClassificationSpan> GetClassificationSpans(SnapshotSpan span)
        {
            var result = new List<ClassificationSpan>();
            if (span.IsEmpty) return result;

            // Expand span to whole lines for accurate tokenization
            ITextSnapshotLine startLine = span.Start.GetContainingLine();
            ITextSnapshotLine endLine = span.End.GetContainingLine();
            var lineSpan = new SnapshotSpan(startLine.Start, endLine.End);

            string lineText = lineSpan.GetText();
            var lexer = new GrassLexer(lineText);
            var tokens = lexer.Tokenize();

            int lineOffset = lineSpan.Start.Position;

            foreach (var tok in tokens)
            {
                if (tok.Kind == GrassTokenKind.EndOfFile || tok.Length == 0) continue;

                IClassificationType classificationType = GetClassificationForToken(tok.Kind);
                if (classificationType != null)
                {
                    int tokStart = lineOffset + tok.Start;
                    int tokEnd = tokStart + tok.Length;

                    // Intersect with requested span
                    int matchStart = Math.Max(tokStart, span.Start.Position);
                    int matchEnd = Math.Min(tokEnd, span.End.Position);

                    if (matchEnd > matchStart)
                    {
                        var tokSpan = new SnapshotSpan(span.Snapshot, new Span(tokStart, tok.Length));
                        result.Add(new ClassificationSpan(tokSpan, classificationType));
                    }
                }
            }

            return result;
        }

        private IClassificationType GetClassificationForToken(GrassTokenKind kind)
        {
            switch (kind)
            {
                case GrassTokenKind.Keyword:
                    return _keywordType;
                case GrassTokenKind.Directive:
                    return _directiveType;
                case GrassTokenKind.Type:
                    return _typeType;
                case GrassTokenKind.BuiltinFunction:
                case GrassTokenKind.StreamIo:
                    return _builtinType;
                case GrassTokenKind.String:
                    return _stringType;
                case GrassTokenKind.Number:
                    return _numberType;
                case GrassTokenKind.CommentLine:
                case GrassTokenKind.CommentBlock:
                    return _commentType;
                case GrassTokenKind.Operator:
                    return _operatorType;
                case GrassTokenKind.Constant:
                    return _constantType;
                default:
                    return null;
            }
        }
    }
}

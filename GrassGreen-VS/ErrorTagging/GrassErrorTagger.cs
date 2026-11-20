using System;
using System.Collections.Generic;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Adornments;
using Microsoft.VisualStudio.Text.Tagging;
using GrassGreen.VisualStudio.Parser;

namespace GrassGreen.VisualStudio.ErrorTagging
{
    public class GrassErrorTagger : ITagger<IErrorTag>
    {
        private readonly ITextBuffer _buffer;
        private GrassParseResult _latestResult;

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged;

        public GrassErrorTagger(ITextBuffer buffer)
        {
            _buffer = buffer;
            _buffer.Changed += OnBufferChanged;
            ReParse(_buffer.CurrentSnapshot);
        }

        private void OnBufferChanged(object sender, TextContentChangedEventArgs e)
        {
            ReParse(e.After);
        }

        private void ReParse(ITextSnapshot snapshot)
        {
            string text = snapshot.GetText();
            var lexer = new GrassLexer(text);
            var tokens = lexer.Tokenize();
            var parser = new GrassParser(tokens);
            _latestResult = parser.Parse();

            // Store in buffer properties for Completion and QuickInfo providers
            _buffer.Properties["GrassLatestParseResult"] = _latestResult;

            TagsChanged?.Invoke(this, new SnapshotSpanEventArgs(new SnapshotSpan(snapshot, 0, snapshot.Length)));
        }

        public IEnumerable<ITagSpan<IErrorTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            if (_latestResult == null || _latestResult.Errors.Count == 0 || spans.Count == 0)
                yield break;

            ITextSnapshot snapshot = spans[0].Snapshot;

            foreach (var err in _latestResult.Errors)
            {
                int start = Math.Min(err.Start, snapshot.Length);
                int length = Math.Min(err.Length, snapshot.Length - start);
                if (length <= 0) length = 1;
                if (start + length > snapshot.Length)
                {
                    length = snapshot.Length - start;
                }
                if (length <= 0) continue;

                var errorSpan = new SnapshotSpan(snapshot, start, length);

                bool intersects = false;
                foreach (var span in spans)
                {
                    if (span.IntersectsWith(errorSpan))
                    {
                        intersects = true;
                        break;
                    }
                }

                if (intersects)
                {
                    yield return new TagSpan<IErrorTag>(
                        errorSpan,
                        new ErrorTag(PredefinedErrorTypeNames.SyntaxError, err.Message)
                    );
                }
            }
        }
    }
}

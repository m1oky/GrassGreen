using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Language.Intellisense;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Utilities;
using GrassGreen.VisualStudio.Constants;

namespace GrassGreen.VisualStudio.Completion
{
    [Export(typeof(ICompletionSourceProvider))]
    [ContentType(GrassConstants.ContentType)]
    [Name("GrassGreenCompletion")]
    public class GrassCompletionSourceProvider : ICompletionSourceProvider
    {
        public ICompletionSource TryCreateCompletionSource(ITextBuffer textBuffer)
        {
            if (textBuffer == null) return null;
            return new GrassCompletionSource(textBuffer);
        }
    }
}

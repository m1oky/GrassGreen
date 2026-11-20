using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Language.Intellisense.AsyncCompletion;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.Utilities;
using GrassGreen.VisualStudio.Constants;

namespace GrassGreen.VisualStudio.Completion
{
    [Export(typeof(IAsyncCompletionSourceProvider))]
    [Name("GrassGreenAsyncCompletion")]
    [ContentType(GrassConstants.ContentType)]
    public class GrassAsyncCompletionSourceProvider : IAsyncCompletionSourceProvider
    {
        public IAsyncCompletionSource GetOrCreate(ITextView textView)
        {
            if (textView == null) return null;
            return textView.Properties.GetOrCreateSingletonProperty(() => new GrassAsyncCompletionSource());
        }
    }
}

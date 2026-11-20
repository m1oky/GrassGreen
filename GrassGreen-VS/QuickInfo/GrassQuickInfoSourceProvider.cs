using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Language.Intellisense;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Utilities;
using GrassGreen.VisualStudio.Constants;

namespace GrassGreen.VisualStudio.QuickInfo
{
    [Export(typeof(IAsyncQuickInfoSourceProvider))]
    [Name("GrassGreenQuickInfo")]
    [ContentType(GrassConstants.ContentType)]
    [Order(Before = "Default Quick Info Presenter")]
    public class GrassQuickInfoSourceProvider : IAsyncQuickInfoSourceProvider
    {
        public IAsyncQuickInfoSource TryCreateQuickInfoSource(ITextBuffer textBuffer)
        {
            if (textBuffer == null) return null;
            return new GrassQuickInfoSource(textBuffer);
        }
    }
}

using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Tagging;
using Microsoft.VisualStudio.Utilities;
using GrassGreen.VisualStudio.Constants;

namespace GrassGreen.VisualStudio.ErrorTagging
{
    [Export(typeof(ITaggerProvider))]
    [ContentType(GrassConstants.ContentType)]
    [TagType(typeof(IErrorTag))]
    public class GrassErrorTaggerProvider : ITaggerProvider
    {
        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            if (buffer == null) return null;
            return buffer.Properties.GetOrCreateSingletonProperty(() => new GrassErrorTagger(buffer)) as ITagger<T>;
        }
    }
}

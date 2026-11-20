using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Utilities;
using GrassGreen.VisualStudio.Constants;

namespace GrassGreen.VisualStudio.ContentType
{
    public static class GrassContentTypeDefinition
    {
        [Export]
        [Name(GrassConstants.ContentType)]
        [BaseDefinition("code")]
        public static ContentTypeDefinition GrassContentType = null;

        [Export]
        [FileExtension(GrassConstants.FileExtension)]
        [ContentType(GrassConstants.ContentType)]
        public static FileExtensionToContentTypeDefinition GrassFileExtension = null;
    }
}

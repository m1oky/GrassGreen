using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Language.StandardClassification;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Utilities;

namespace GrassGreen.VisualStudio.Classification
{
    public static class GrassClassificationDefinitions
    {
        public const string Keyword = "grassgreen.keyword";
        public const string Directive = "grassgreen.directive";
        public const string Type = "grassgreen.type";
        public const string Builtin = "grassgreen.builtin";
        public const string Function = "grassgreen.function";
        public const string UiProperty = "grassgreen.uiproperty";
        public const string String = "grassgreen.string";
        public const string Escape = "grassgreen.escape";
        public const string HexColor = "grassgreen.hexcolor";
        public const string Number = "grassgreen.number";
        public const string Comment = "grassgreen.comment";
        public const string Operator = "grassgreen.operator";
        public const string Constant = "grassgreen.constant";

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Keyword)]
        [BaseDefinition(PredefinedClassificationTypeNames.Keyword)]
        public static ClassificationTypeDefinition KeywordDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Directive)]
        [BaseDefinition(PredefinedClassificationTypeNames.PreprocessorKeyword)]
        public static ClassificationTypeDefinition DirectiveDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Type)]
        [BaseDefinition(PredefinedClassificationTypeNames.Type)]
        public static ClassificationTypeDefinition TypeDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Builtin)]
        [BaseDefinition(PredefinedClassificationTypeNames.MarkupNode)]
        public static ClassificationTypeDefinition BuiltinDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Function)]
        [BaseDefinition(PredefinedClassificationTypeNames.MarkupNode)]
        public static ClassificationTypeDefinition FunctionDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(UiProperty)]
        [BaseDefinition(PredefinedClassificationTypeNames.MarkupAttribute)]
        public static ClassificationTypeDefinition UiPropertyDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(String)]
        [BaseDefinition(PredefinedClassificationTypeNames.String)]
        public static ClassificationTypeDefinition StringDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Escape)]
        [BaseDefinition(PredefinedClassificationTypeNames.Literal)]
        public static ClassificationTypeDefinition EscapeDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(HexColor)]
        [BaseDefinition(PredefinedClassificationTypeNames.MarkupAttributeValue)]
        public static ClassificationTypeDefinition HexColorDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Number)]
        [BaseDefinition(PredefinedClassificationTypeNames.Number)]
        public static ClassificationTypeDefinition NumberDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Comment)]
        [BaseDefinition(PredefinedClassificationTypeNames.Comment)]
        public static ClassificationTypeDefinition CommentDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Operator)]
        [BaseDefinition(PredefinedClassificationTypeNames.Operator)]
        public static ClassificationTypeDefinition OperatorDefinition = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Constant)]
        [BaseDefinition(PredefinedClassificationTypeNames.Literal)]
        public static ClassificationTypeDefinition ConstantDefinition = null;
    }
}

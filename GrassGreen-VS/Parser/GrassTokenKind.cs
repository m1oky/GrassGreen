namespace GrassGreen.VisualStudio.Parser
{
    public enum GrassTokenKind
    {
        EndOfFile,
        Identifier,
        Number,
        String,
        Directive,
        CommentLine,
        CommentBlock,
        Keyword,
        Type,
        StreamIo,
        Constant,
        BuiltinFunction,
        Operator,
        Punctuation,
        Unknown
    }
}

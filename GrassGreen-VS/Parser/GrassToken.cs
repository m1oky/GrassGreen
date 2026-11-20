namespace GrassGreen.VisualStudio.Parser
{
    public struct GrassToken
    {
        public GrassTokenKind Kind { get; }
        public string Text { get; }
        public int Start { get; }
        public int Length { get; }
        public int Line { get; }
        public int Column { get; }
        public bool IsUnterminated { get; }

        public int End => Start + Length;

        public GrassToken(GrassTokenKind kind, string text, int start, int length, int line, int column, bool isUnterminated = false)
        {
            Kind = kind;
            Text = text;
            Start = start;
            Length = length;
            Line = line;
            Column = column;
            IsUnterminated = isUnterminated;
        }

        public override string ToString()
        {
            return $"{Kind}: '{Text}' at ({Line}:{Column}, {Start}..{End})";
        }
    }
}

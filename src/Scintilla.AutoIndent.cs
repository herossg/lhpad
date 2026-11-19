using System.Runtime.InteropServices;

namespace LHPad;

internal sealed partial class Sci
{
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public bool AutoIndent { get; set; } = true;

    bool newlineCommandActive, newlineObserved, newlineMacroCaptured, insertingIndentation;

    sealed record IndentPrefix(List<byte[]> Chunks);
    sealed record CaretState(long Caret, long Anchor, long CaretSpace, long AnchorSpace);
    sealed record IndentInsertion(long Position, IndentPrefix Prefix)
    {
        public long Inserted { get; set; }
    }

    bool IsNewlineCommand(Message message) => !newlineCommandActive &&
        (message.Msg == S.SCI_NEWLINE || message.Msg == 0x0100 && (int)message.WParam == (int)Keys.Return &&
        (ModifierKeys & (Keys.Control | Keys.Alt)) == 0);

    void NewlineWithIndentation(ref Message message)
    {
        // The native NewLine command owns selection replacement, EOL choice, rectangular
        // selection rules and all caret creation. The container only adds literal prefixes.
        List<IndentPrefix> prefixes = AutoIndent && Call(S.SCI_GETREADONLY) == 0 ? SnapshotIndentation() : [];
        newlineCommandActive = true;
        newlineObserved = newlineMacroCaptured = false;
        Call(S.SCI_BEGINUNDOACTION);
        bool captureNewline;
        try
        {
            base.WndProc(ref message);
            if (newlineObserved && prefixes.Count > 0 && Call(S.SCI_GETREADONLY) == 0)
            {
                insertingIndentation = true;
                ContinueIndentation(prefixes);
            }
            captureNewline = newlineMacroCaptured;
        }
        finally
        {
            insertingIndentation = false;
            Call(S.SCI_ENDUNDOACTION);
            newlineCommandActive = false;
            newlineObserved = newlineMacroCaptured = false;
        }
        // Scintilla emits ReplaceSel once for EACH CR/LF and EACH caret, but playback
        // executes NewLine for all carets at once. Store one semantic command instead.
        if (captureNewline)
            MacroRecorded?.Invoke(S.SCI_NEWLINE, 0, 0);
    }

    bool FilterIndentationMacro(MacroNotification notification)
    {
        if (insertingIndentation)
            return true;
        if (!newlineCommandActive || !newlineObserved || notification.Message != S.SCI_REPLACESEL || notification.LParam == 0)
            return false;
        byte first = Marshal.ReadByte(notification.LParam);
        if (first is not (13 or 10))
            return false;
        byte second = Marshal.ReadByte(notification.LParam, 1);
        if (second != 0 && !(first == 13 && second == 10 && Marshal.ReadByte(notification.LParam, 2) == 0))
            return false;
        newlineMacroCaptured = true;
        return true;
    }

    List<IndentPrefix> SnapshotIndentation()
    {
        int count = (int)Call(S.SCI_GETSELECTIONS);
        int mode = (int)Call(S.SCI_GETSELECTIONMODE);
        bool onlyMain = mode is S.SC_SEL_RECTANGLE or S.SC_SEL_THIN || Call(S.SCI_GETADDITIONALSELECTIONTYPING) == 0;
        int main = (int)Call(S.SCI_GETMAINSELECTION);
        var prefixes = new List<IndentPrefix>(onlyMain ? 1 : count);
        for (int i = 0; i < (onlyMain ? 1 : count); i++)
        {
            int selection = onlyMain ? main : i;
            long start = (long)Call(S.SCI_GETSELECTIONNSTART, selection);
            long line = (long)Call(S.SCI_LINEFROMPOSITION, start);
            long position = (long)Call(S.SCI_POSITIONFROMLINE, line);
            var chunks = new List<byte[]>();
            // Read the leading prefix only, even on very long lines. Bounding it at the
            // selection start preserves a whitespace suffix when Enter splits the indent.
            while (position < start)
            {
                var bytes = Range(position, Math.Min(64 * 1024, start - position));
                int whitespace = 0;
                while (whitespace < bytes.Length && bytes[whitespace] is 9 or 32)
                    whitespace++;
                if (whitespace > 0)
                    chunks.Add(whitespace == bytes.Length ? bytes : bytes[..whitespace]);
                position += whitespace;
                if (whitespace < bytes.Length)
                    break;
            }
            prefixes.Add(new(chunks));
        }
        return prefixes;
    }

    void ContinueIndentation(List<IndentPrefix> prefixes)
    {
        int count = (int)Call(S.SCI_GETSELECTIONS);
        // Native overlapping-range normalization may change the selection set. Never
        // guess another caret's indentation if those indices no longer correspond.
        if (count != prefixes.Count)
            return;
        var carets = new CaretState[count];
        var insertions = new List<IndentInsertion>();
        var seen = new HashSet<long>();
        for (int i = 0; i < count; i++)
        {
            long caret = (long)Call(S.SCI_GETSELECTIONNCARET, i);
            carets[i] = new(caret, (long)Call(S.SCI_GETSELECTIONNANCHOR, i),
                (long)Call(S.SCI_GETSELECTIONNCARETVIRTUALSPACE, i), (long)Call(S.SCI_GETSELECTIONNANCHORVIRTUALSPACE, i));
            if (prefixes[i].Chunks.Count > 0 && seen.Add(caret))
                insertions.Add(new(caret, prefixes[i]));
        }
        if (insertions.Count == 0)
            return;
        long targetStart = (long)Call(S.SCI_GETTARGETSTART), targetEnd = (long)Call(S.SCI_GETTARGETEND);
        long targetStartSpace = (long)Call(S.SCI_GETTARGETSTARTVIRTUALSPACE), targetEndSpace = (long)Call(S.SCI_GETTARGETENDVIRTUALSPACE);
        insertions.Sort((a, b) => a.Position.CompareTo(b.Position));
        for (int i = insertions.Count - 1; i >= 0; i--)
        {
            var insertion = insertions[i];
            foreach (var chunk in insertion.Prefix.Chunks)
            {
                long position = insertion.Position + insertion.Inserted;
                Call(S.SCI_SETTARGETSTART, position);
                Call(S.SCI_SETTARGETEND, position);
                long inserted = (long)Bytes(S.SCI_REPLACETARGET, chunk, chunk.Length);
                if (inserted <= 0)
                    break;
                insertion.Inserted += inserted;
            }
        }
        var shifts = new long[insertions.Count];
        long total = 0;
        for (int i = 0; i < insertions.Count; i++)
            shifts[i] = total += insertions[i].Inserted;
        long Shift(long position)
        {
            int low = 0, high = insertions.Count;
            while (low < high)
            {
                int middle = low + (high - low) / 2;
                if (insertions[middle].Position <= position) low = middle + 1;
                else high = middle;
            }
            return position + (low > 0 ? shifts[low - 1] : 0);
        }
        // Target replacement preserves all ranges; restore each final native caret and
        // anchor explicitly so equality-at-insertion behavior cannot lose an indent.
        for (int i = 0; i < count; i++)
        {
            Call(S.SCI_SETSELECTIONNCARET, i, Shift(carets[i].Caret));
            Call(S.SCI_SETSELECTIONNANCHOR, i, Shift(carets[i].Anchor));
            Call(S.SCI_SETSELECTIONNCARETVIRTUALSPACE, i, carets[i].CaretSpace);
            Call(S.SCI_SETSELECTIONNANCHORVIRTUALSPACE, i, carets[i].AnchorSpace);
        }
        Call(S.SCI_SETTARGETSTART, Shift(targetStart));
        Call(S.SCI_SETTARGETEND, Shift(targetEnd));
        Call(S.SCI_SETTARGETSTARTVIRTUALSPACE, targetStartSpace);
        Call(S.SCI_SETTARGETENDVIRTUALSPACE, targetEndSpace);
        Call(S.SCI_CHOOSECARETX);
        Call(S.SCI_SCROLLCARET);
    }
}

using System.Text;

namespace LHPad;

internal static class MacroEditing
{
    public static async Task FormatAsync(Sci s, string language)
    {
        long a = s.SelectionStart, b = s.SelectionEnd;
        if (a == b)
        {
            a = 0;
            b = s.Length;
        }
        if (b - a > 32L * 1024 * 1024)
            throw new IOException("32 MiB 이하의 영역을 선택해 정렬해 주세요.");
        string text = s.TextRange(a, b - a);
        long revision = s.Revision;
        string eol = Eol(s);
        string output = await Task.Run(() => Formatting.Format(text, language, eol));
        if (s.IsDisposed || s.Revision != revision)
            throw new IOException("정렬 중 문서가 변경되었습니다. 다시 시도해 주세요.");
        s.Call(S.SCI_BEGINUNDOACTION);
        try
        {
            s.Replace(a, b, output);
        }
        finally { s.Call(S.SCI_ENDUNDOACTION); }
        s.Select(a, a + Encoding.UTF8.GetByteCount(output));
    }
    static string Eol(Sci s) => s.Call(S.SCI_GETEOLMODE) == 0 ? "\r\n" : s.Call(S.SCI_GETEOLMODE) == 1 ? "\r" : "\n";
    public static void Sort(Sci s, bool descending)
    {
        long a = s.SelectionStart, b = s.SelectionEnd;
        if (a == b)
            throw new IOException("정렬할 줄을 선택해 주세요.");
        long first = (long)s.Call(S.SCI_LINEFROMPOSITION, a), last = (long)s.Call(S.SCI_LINEFROMPOSITION, b);
        a = (long)s.Call(S.SCI_POSITIONFROMLINE, first);
        if (b == (long)s.Call(S.SCI_POSITIONFROMLINE, last))
            last--;
        b = (long)s.Call(S.SCI_GETLINEENDPOSITION, last);
        if (b - a > 32L * 1024 * 1024)
            throw new IOException("32 MiB 이하를 선택해 주세요.");
        var lines = LineDiff.Lines(s.TextRange(a, b - a));
        Array.Sort(lines, StringComparer.Ordinal);
        if (descending)
            Array.Reverse(lines);
        s.Replace(a, b, string.Join(Eol(s), lines));
    }
    public static void Column(Sci s, bool number, string text, long start, long increment)
    {
        int count = (int)s.Call(S.SCI_GETSELECTIONS);
        if (count < 2)
            throw new IOException("Alt+드래그로 세로 선택하거나 Ctrl+클릭으로 커서를 여러 개 만들어 주세요.");
        var selections = Enumerable.Range(0, count).Select(i => (A: Math.Min((long)s.Call(S.SCI_GETSELECTIONNANCHOR, i), (long)s.Call(S.SCI_GETSELECTIONNCARET, i)), B: Math.Max((long)s.Call(S.SCI_GETSELECTIONNANCHOR, i), (long)s.Call(S.SCI_GETSELECTIONNCARET, i)))).OrderBy(x => x.A).ToArray();
        s.Call(S.SCI_BEGINUNDOACTION);
        try
        {
            for (int i = selections.Length - 1; i >= 0; i--)
                s.Replace(selections[i].A, selections[i].B, number ? checked(start + i * increment).ToString() : text);
        }
        finally { s.Call(S.SCI_ENDUNDOACTION); }
    }
}

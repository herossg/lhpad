using System.Text;

namespace LHPad;

internal static class SearchCountTests
{
    public static async Task RunAsync(Form host, string folder, string output, Action<bool, string> check)
    {
        using (var s = new Sci())
        {
            _ = s.Handle;
            s.SetText("program Program programer program\n한글 한글");
            s.Saved();
            s.Select(2, 5);
            long revision = s.Revision;
            check(await Search.CountAsync(s, "program", 0, CancellationToken.None) == 4, "Text count honors case-insensitive matching");
            check(await Search.CountAsync(s, "program", S.SCFIND_MATCHCASE, CancellationToken.None) == 3, "Text count honors case sensitivity");
            check(await Search.CountAsync(s, "program", S.SCFIND_MATCHCASE | S.SCFIND_WHOLEWORD, CancellationToken.None) == 2, "Text count honors whole-word matching");
            check(await Search.CountAsync(s, "한글", 0, CancellationToken.None) == 2, "Text count handles Korean UTF-8 positions");
            check(await Search.CountAsync(s, "(?=한)", S.SCFIND_REGEXP | S.SCFIND_CXX11REGEX, CancellationToken.None) == 2, "Zero-width regular expression counting makes Unicode-safe progress");
            check(s.Anchor == 2 && s.Caret == 5 && s.Revision == revision && !s.Modified, "Counting leaves selection, cursor, content and saved state intact");
            check(await Search.CountAsync(s, "missing", 0, CancellationToken.None) == 0, "Text count reports zero when no matches exist");
            check(await Search.CountAsync(s, "", 0, CancellationToken.None) == 0, "An empty text count terminates without matching every position");
            s.SetText("aaaaa");
            check(await Search.CountAsync(s, "aa", 0, CancellationToken.None) == 2, "Text count uses non-overlapping matches");
            using var cancel = new CancellationTokenSource();
            cancel.Cancel();
            bool cancelled = false;
            try { await Search.CountAsync(s, "a", 0, cancel.Token); }
            catch (OperationCanceledException) { cancelled = true; }
            check(cancelled, "Text count supports cancellation");
        }
        string path = Path.Combine(folder, "count-search.bin");
        var data = new byte[2 * 1024 * 1024 + 512];
        long[] positions = [0, 1024 * 1024 - 3, 1024 * 1024 + 25, 2 * 1024 * 1024 + 9];
        foreach (int position in positions) Encoding.UTF8.GetBytes("program").CopyTo(data, position);
        foreach (int position in new[] { 128, 256 }) Encoding.Unicode.GetBytes("테스트").CopyTo(data, position);
        foreach (int position in new[] { 384, 512 }) Encoding.UTF8.GetBytes("한글").CopyTo(data, position);
        await File.WriteAllBytesAsync(path, data);
        using (var buffer = new BinaryBuffer(path))
        {
            check(await buffer.CountAsync("program"u8.ToArray(), CancellationToken.None) == 4, "Hex streaming count includes offset zero and matches crossing the 1 MiB read boundary");
            buffer.Replace(32, 0, "program"u8.ToArray());
            check(await buffer.CountAsync("program"u8.ToArray(), CancellationToken.None) == 5, "Hex count includes unsaved piece-table insertions");
            check(await buffer.FindAsync("program"u8.ToArray(), 1, buffer.Length, CancellationToken.None) == 32, "Forward Hex search still sees inserted bytes after the streaming scan change");
            using var cancel = new CancellationTokenSource();
            cancel.Cancel();
            bool cancelled = false;
            try { await buffer.CountAsync([0], cancel.Token); }
            catch (OperationCanceledException) { cancelled = true; }
            check(cancelled, "Hex count supports cancellation");
        }
        using var app = new MainForm(Path.Combine(folder, "search-count-profile"), []);
        app.Show(host);
        await app.Ready;
        await app.OpenAsync(path, true);
        var h = app.Current!.Hex!;
        using (var find = new FindForm(app))
        {
            find.Show(app);
            find.SetQuery("program");
            await find.FindNextAsync(false);
            check(Math.Min(h.Anchor, h.Caret) == 0 && Math.Abs(h.Caret - h.Anchor) == 6, "Default Hex search accepts the visible program string and includes the current first byte");
            await find.FindNextAsync(false);
            check(Math.Min(h.Anchor, h.Caret) == positions[1], "Repeating a Hex text search advances to the next occurrence");
            long anchor = h.Anchor, caret = h.Caret, revision = h.Revision;
            await find.CountMatchesAsync();
            check(find.ResultText.Contains("총 4개"), "Find dialog displays only the total number of Hex matches");
            check(h.Anchor == anchor && h.Caret == caret && h.Revision == revision && !h.Modified, "Hex count preserves the current byte selection and file contents");
            find.SetHexSearchFormat(0);
            find.SetQuery("program");
            await find.FindNextAsync(false);
            check(find.ResultText.Contains("텍스트 UTF-8"), "Invalid raw Hex input gives an inline Korean hint instead of an English exception dialog");
            find.SetQuery("70 72 6F 67 72 61 6D");
            await find.CountMatchesAsync();
            check(find.ResultText.Contains("총 4개"), "Explicit raw Hex byte count remains available");
            find.SetHexSearchFormat(2);
            find.SetQuery("테스트");
            await find.CountMatchesAsync();
            check(find.ResultText.Contains("총 2개"), "UTF-16 LE strings can be counted inside binary data");
            find.SetHexSearchFormat(3);
            find.SetQuery("한글");
            await find.CountMatchesAsync();
            check(find.ResultText.Contains("표현할 수 없는"), "ASCII search rejects unrepresentable Korean instead of silently searching question marks");
            find.SetHexSearchFormat(1);
            await find.CountMatchesAsync();
            check(find.ResultText.Contains("총 2개"), "UTF-8 Korean strings can be counted inside binary data");
            find.SetQuery("Program");
            await find.CountMatchesAsync();
            check(find.ResultText.Contains("총 0개"), "Hex text count uses explicit case-sensitive byte matching");
            find.SetQuery("program");
            await find.CountMatchesAsync();
            await Task.Delay(80);
            SelfTests.Screenshot(find, Path.Combine(output, "hex-search-count-preview.png"));
            find.Close();
        }
        var text = app.NewDocument("count.txt");
        text.Editor!.SetText("program program");
        text.Editor.Select(3, 4);
        using (var find = new FindForm(app))
        {
            find.SetQuery("program");
            await find.CountMatchesAsync();
            check(find.ResultText.Contains("총 2개") && text.Editor.Anchor == 3 && text.Editor.Caret == 4, "The same count button works in text mode without moving its cursor");
        }
        var closed = new TaskCompletionSource();
        app.FormClosed += (_, _) => closed.TrySetResult();
        app.Close();
        await closed.Task;
    }
}

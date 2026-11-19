using System.Text;

namespace LHPad;

internal static class FileMonitoringTests
{
    static readonly Encoding Utf8 = new UTF8Encoding(false, true);
    static async Task AppendAsync(string path, byte[] bytes)
    {
        await using var writer = new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.ReadWrite | FileShare.Delete);
        await writer.WriteAsync(bytes);
        await writer.FlushAsync();
    }
    public static async Task RunAsync(Form host, string folder, string output, Action<bool, string> check)
    {
        string path = Path.Combine(folder, "live.log");
        await File.WriteAllTextAsync(path, "start\n", Utf8);
        var tail = await LogTailer.CreateAsync(path, Utf8, FileState.Read(path));
        var unchanged = await tail.PollAsync();
        check(unchanged.Text == "" && !unchanged.Reset, "Idle log polling does not reload or duplicate existing content");
        byte[] korean = Utf8.GetBytes("한글🙂\r\n");
        var received = new StringBuilder();
        for (int i = 0; i < korean.Length; i++)
        {
            await AppendAsync(path, [korean[i]]);
            var update = await tail.PollAsync();
            check(!update.Reset, $"Partial UTF-8 append {i + 1} remains incremental");
            received.Append(update.Text);
            if (i == 0) check(tail.HasPendingInput, "An unfinished multibyte character is tracked without replacement glyphs");
        }
        check(received.ToString() == "한글🙂\r\n", "UTF-8 characters and surrogate pairs survive split writes across polling cycles");
        check(!tail.HasPendingInput, "A completed split character clears the pending-input state");
        foreach (var enc in new Encoding[] { new UnicodeEncoding(false, true, true), Encoding.GetEncoding(949, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback) })
        {
            string encoded = Path.Combine(folder, $"encoded-{enc.CodePage}.log");
            await File.WriteAllBytesAsync(encoded, [.. enc.GetPreamble(), .. enc.GetBytes("처음\n")]);
            var decoder = await LogTailer.CreateAsync(encoded, enc, FileState.Read(encoded));
            received.Clear();
            foreach (byte b in enc.GetBytes("다음\n"))
            {
                await AppendAsync(encoded, [b]);
                received.Append((await decoder.PollAsync()).Text);
            }
            check(received.ToString() == "다음\n", $"Code page {enc.CodePage} preserves decoder state across split writes");
            await File.WriteAllBytesAsync(encoded, [.. enc.GetPreamble(), .. enc.GetBytes("교체\n")]);
            var reset = await decoder.PollAsync();
            check(reset.Reset && reset.Text == "교체\n", $"Code page {enc.CodePage} resets decoder and skips BOM on file replacement");
        }
        await File.WriteAllTextAsync(path, "new\n", Utf8);
        var truncated = await tail.PollAsync();
        check(truncated.Reset && truncated.Text == "new\n", "Truncated log replaces the displayed content");
        await File.WriteAllTextAsync(path, "truncate and regrow beyond the old offset\n", Utf8);
        var regrown = await tail.PollAsync();
        check(regrown.Reset && regrown.Text.StartsWith("truncate"), "Truncate-and-regrow is recognized by its original boundary bytes");
        string replacement = Path.Combine(folder, "replacement.log");
        await File.WriteAllTextAsync(replacement, "rotated\n", Utf8);
        File.Move(replacement, path, true);
        var rotated = await tail.PollAsync();
        check(rotated.Reset && rotated.Text == "rotated\n", "Log rotation is recognized by Windows file identity");
        File.Delete(path);
        check((await tail.PollAsync()).Missing, "Deleted logs retain the current view while waiting for recreation");
        await File.WriteAllTextAsync(path, "recreated\n", Utf8);
        var recreated = await tail.PollAsync();
        check(recreated.Reset && recreated.Text == "recreated\n", "Recreated logs resume without manual reopening");
        await AppendAsync(path, new byte[LogTailer.BlockSize + 100].Select(_ => (byte)'a').ToArray());
        var block = await tail.PollAsync();
        check(block.Text.Length == LogTailer.BlockSize && block.Offset < block.State.Length, "Large log bursts read a bounded 256 KiB block per polling cycle");
        check((await tail.PollAsync()).Text.Length == 100, "The next polling cycle drains the remaining log backlog");

        string sharedPath = Path.Combine(folder, "writer-open.log");
        await File.WriteAllTextAsync(sharedPath, "writer remains open\n", Utf8);
        await using (var writer = new FileStream(sharedPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite | FileShare.Delete))
        using (var reader = new Sci())
        {
            _ = reader.Handle;
            check(!FileTypes.Detect(sharedPath).Binary, "Text detection accepts a file held open by a log writer");
            await reader.LoadStreamAsync(sharedPath, Utf8);
            check(reader.AllText() == "writer remains open\n", "Bounded document loading allows a persistent writer handle");
            await writer.WriteAsync(Utf8.GetBytes("next\n"));
            await writer.FlushAsync();
            check(reader.LoadedState != FileState.Read(sharedPath), "The initial load retains its observed disk stamp for later append detection");
        }

        string normal = Path.Combine(folder, "external.txt");
        await File.WriteAllTextAsync(normal, "original\nsecond line\n", Utf8);
        using var app = new MainForm(Path.Combine(folder, "file-monitor-profile"), [normal]);
        int prompts = 0;
        bool accept = false;
        app.ExternalReloadPrompt = (_, _) => { prompts++; return accept; };
        app.Show(host);
        await app.Ready;
        await Task.Delay(80);
        var doc = app.Current!;
        var s = doc.Editor!;
        await File.WriteAllTextAsync(normal, "external change\n", Utf8);
        await app.CheckExternalChangesAsync();
        check(prompts == 1 && s.AllText().StartsWith("original"), "External-change confirmation can keep the existing document");
        await app.CheckExternalChangesAsync();
        check(prompts == 1 && doc.ExternallyChanged(), "Declining reload suppresses duplicate prompts without clearing the overwrite conflict");
        await app.CheckExternalChangesAsync(true);
        check(prompts == 2 && s.AllText().StartsWith("original"), "A manual change check can ask again after an earlier declined reload");
        await File.WriteAllTextAsync(normal, "latest disk version\n", Utf8);
        accept = true;
        long revision = s.Revision;
        await app.CheckExternalChangesAsync();
        check(prompts == 3 && s.AllText() == "latest disk version\n" && !doc.Dirty, "Accepting external reload updates the same tab and resets the saved state");
        check(s.Revision > revision, "Replacing the native document invalidates cached preview and recovery revisions");
        s.Str(S.SCI_REPLACESEL, "my unsaved edits ");
        string unsaved = s.AllText();
        accept = false;
        await File.WriteAllTextAsync(normal, "disk again\n", Utf8);
        await app.CheckExternalChangesAsync();
        check(s.AllText() == unsaved && doc.Dirty, "Declining reload preserves unsaved edits");
        await File.WriteAllBytesAsync(normal, [0xff, 0xff]);
        bool failed = false;
        try { await app.ReloadFromDiskAsync(doc); }
        catch (DecoderFallbackException) { failed = true; }
        check(failed && s.AllText() == unsaved && doc.Dirty, "Failed reload leaves the original native document and unsaved edits intact");
        await File.WriteAllTextAsync(normal, "log initial\n", Utf8);
        await app.ReloadFromDiskAsync(doc);
        await app.StartMonitoringAsync(doc);
        check(doc.Monitoring && s.Call(S.SCI_GETREADONLY) == 1, "Log monitoring switches the existing editor to read-only");
        s.Str(S.SCI_REPLACESEL, "blocked");
        check(s.AllText() == "log initial\n", "Typing into a monitored document cannot change its content");
        await AppendAsync(normal, Utf8.GetBytes("new 한글 entry\n"));
        await app.PollMonitorsAsync();
        check(s.AllText() == "log initial\nnew 한글 entry\n" && !doc.Dirty && s.Caret == s.Length, "Live app appends Unicode log entries, follows EOF and keeps the tab clean");
        check(Search.In(s, "한글", 0, s.Length, 0) != null, "Live log contents remain searchable for analysis");
        doc.FollowTail = false;
        s.Select(0, 3);
        await AppendAsync(normal, Utf8.GetBytes("another line\n"));
        await app.PollMonitorsAsync();
        check(s.Anchor == 0 && s.Caret == 3 && s.AllText().EndsWith("another line\n"), "Disabling follow preserves the selected analysis position while appending new data");
        bool protectedSave = false;
        try { await app.SaveAsync(doc); }
        catch (InvalidOperationException) { protectedSave = true; }
        check(protectedSave, "Monitoring prevents overwriting the live log through Save");
        await File.WriteAllTextAsync(normal, "reset log\n", Utf8);
        await app.PollMonitorsAsync();
        check(s.AllText() == "reset log\n" && !doc.Dirty, "The live editor replaces a truncated log instead of appending stale data");
        await Task.Delay(80);
        SelfTests.Screenshot(app, Path.Combine(output, "log-monitor-preview.png"));
        app.StopMonitoring(doc);
        s.Str(S.SCI_REPLACESEL, "editable");
        check(!doc.Monitoring && s.Call(S.SCI_GETREADONLY) == 0 && doc.Dirty, "Stopping monitoring restores normal editing");
        accept = false;
        File.Delete(normal);
        int before = prompts;
        await app.CheckExternalChangesAsync();
        await app.CheckExternalChangesAsync();
        check(prompts == before + 1 && s.AllText().Contains("editable"), "External deletion is reported once and preserves the editor content");
        var closed = new TaskCompletionSource();
        app.FormClosed += (_, _) => closed.TrySetResult();
        app.Close();
        await closed.Task;
    }
}

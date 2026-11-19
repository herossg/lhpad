using System.Text;
using System.Text.Json;

namespace LHPad;

internal static class MacroTests
{
    static MacroStep Native(int message, string? text = null, long wp = 0) => new() { Message = message, WParam = wp, Data = text == null ? null : Encoding.UTF8.GetBytes(text) };
    public static async Task RunAsync(Form host, string folder, string output, Action<bool, string> check)
    {
        using var s = new Sci();
        host.Controls.Add(s);
        _ = s.Handle;
        string profile = Path.Combine(folder, "macro-profile");
        using var macros = new MacroController(profile);
        macros.Attach(s);
        s.SetText("one\ntwo\nthree");
        s.Select(0, 0);
        s.Call(S.SCI_EMPTYUNDOBUFFER);
        macros.Start();
        s.Call(S.SCI_HOME);
        s.Str(S.SCI_REPLACESEL, "// ");
        s.Call(S.SCI_LINEDOWN);
        macros.Stop();
        check(macros.Last.Select(step => step.Message).SequenceEqual(new[] { S.SCI_HOME, S.SCI_REPLACESEL, S.SCI_LINEDOWN }), "Native SCN_MACRORECORD captures text insertion and cursor movements");
        check(Encoding.UTF8.GetString(macros.Last[1].Data!) == "// ", "Macro text is copied out of the temporary native pointer");
        s.SetText("one\ntwo\nthree");
        s.Select(0, 0);
        s.Call(S.SCI_EMPTYUNDOBUFFER);
        var repeat = await macros.RunAsync(s, times: 2);
        check(repeat.Completed == 2 && s.AllText() == "// one\n// two\nthree", "Fixed repeat count applies exactly twice");
        s.Call(S.SCI_UNDO);
        check(s.AllText() == "one\ntwo\nthree", "One undo reverses the entire repeated macro");
        s.SetText("one\ntwo\nthree");
        s.Select(0, 0);
        var end = await macros.RunAsync(s, untilEnd: true);
        check(s.AllText() == "// one\n// two\n// three" && end.Completed == 3, "Run until EOF edits every original line including the last line once");
        s.SetText("a\nb\nc\nd");
        s.Select(2, 2);
        end = await macros.RunAsync(s, untilEnd: true);
        check(s.AllText() == "a\n// b\n// c\n// d", "Run until EOF begins at current cursor position");
        var rng = new Random(72);
        for (int trial = 0; trial < 60; trial++)
        {
            var lines = Enumerable.Range(0, rng.Next(1, 20)).Select(_ => new string('x', rng.Next(0, 10))).ToArray();
            s.SetText(string.Join("\n", lines));
            s.Select(0, 0);
            await macros.RunAsync(s, untilEnd: true);
            check(s.AllText() == string.Join("\n", lines.Select(line => "// " + line)), "EOF repetition covers short/empty lines " + trial);
        }
        var saved = macros.SaveLast("줄 앞 주석");
        using (var restored = new MacroController(profile))
        {
            check(restored.Last.Count == 3 && restored.Saved.Single().Name == "줄 앞 주석", "Last recording and named macros persist in profile");
            s.SetText("abc");
            s.Select(0, 0);
            await restored.RunAsync(s, restored.Saved[0].Steps);
            check(s.AllText() == "// abc", "Persisted named macro replays without native pointers");
            restored.Rename(saved.Id, "주석 추가");
            check(restored.Saved[0].Name == "주석 추가", "Saved macro can be renamed");
            restored.Delete(saved.Id);
            check(restored.Saved.Count == 0, "Saved macro can be deleted");
        }
        s.SetText("");
        s.Select(0, 0);
        macros.Start();
        foreach (char c in "한글")
            s.Call(0x0102, c);
        macros.Stop();
        check(s.AllText() == "한글" && macros.Last.Count > 0, "Actual WM_CHAR Korean typing is recorded");
        s.SetText("");
        await macros.RunAsync(s);
        check(s.AllText() == "한글", "Recorded Korean typing replays as UTF-8");
        s.SetText("");
        s.Call(S.SCI_SETEOLMODE, S.SC_EOL_CRLF);
        macros.Start();
        s.Call(S.SCI_NEWLINE);
        macros.Stop();
        check(macros.Last.Count == 1 && macros.Last[0].Message == S.SCI_NEWLINE, "CRLF recording normalizes to one newline command");
        s.SetText("");
        s.Call(S.SCI_SETEOLMODE, S.SC_EOL_LF);
        await macros.RunAsync(s);
        check(s.AllText() == "\n", "Recorded newline follows target document EOL mode");
        s.SetText("abc");
        s.Select(0, 0);
        var insertion = new[] { Native(S.SCI_REPLACESEL, "X") };
        var nonprogress = await macros.RunAsync(s, insertion, untilEnd: true);
        check(nonprogress.Completed == 1 && s.AllText() == "Xabc", "Until EOF stops insertion-only macros rather than growing indefinitely");
        s.SetText("a\nb\nc");
        s.Select(0, 0);
        var deletion = new[] { Native(S.SCI_LINEDELETE) };
        end = await macros.RunAsync(s, deletion, untilEnd: true);
        check(s.AllText() == "" && end.Completed <= 4, "Until EOF handles shrinking documents and line deletion");
        s.SetText("a\nb\nc");
        s.Select(4, 4);
        end = await macros.RunAsync(s, new[] { Native(S.SCI_LINEUP) }, untilEnd: true);
        check(end.Completed == 1, "Until EOF stops a macro that moves away from document end");
        s.SetText("abc");
        s.Select(0, 0);
        end = await macros.RunAsync(s, new[] { Native(S.SCI_NEWLINE) }, untilEnd: true);
        check(end.Completed == 1, "Until EOF stops a macro that only creates new lines");
        s.SetText("猫 dog 猫 dog");
        s.Select(0, 0);
        var find = new MacroStep { Kind = MacroStepKind.Find, Query = "dog", Wrap = false };
        await macros.RunAsync(s, [find]);
        check(s.TextRange(s.SelectionStart, s.SelectionEnd - s.SelectionStart) == "dog", "Recorded semantic search finds UTF-8 byte offsets");
        var replace = new MacroStep { Kind = MacroStepKind.Replace, Query = "dog", Replacement = "개", Wrap = false };
        await macros.RunAsync(s, [replace], untilEnd: true);
        check(s.AllText() == "猫 개 猫 개", "Recorded semantic replace repeats until no matches remain");
        s.SetText("a1 b2");
        s.Select(0, 0);
        await macros.RunAsync(s, [new() { Kind = MacroStepKind.ReplaceAll, Query = @"([a-z])(\d)", Replacement = @"\2\1", Flags = S.SCFIND_REGEXP | S.SCFIND_CXX11REGEX }]);
        check(s.AllText() == "1a 2b", "Recorded regex replace-all retains captures and options");
        s.SetText("{\"한글\":true}");
        s.Select(0, 0);
        await macros.RunAsync(s, [new() { Kind = MacroStepKind.Format, Query = "json" }]);
        check(s.AllText().Contains('\n') && JsonDocument.Parse(s.AllText()).RootElement.GetProperty("한글").GetBoolean(), "Formatting command replays semantically instead of storing absolute offsets");
        s.SetText("z\na\nb");
        s.Select(0, s.Length);
        await macros.RunAsync(s, [new() { Kind = MacroStepKind.SortLines }]);
        check(s.AllText() == "a\r\nb\r\nz" || s.AllText() == "a\nb\nz", "Selected-line sort is replayed");
        s.SetText("a\nb\nc");
        s.Select(0, 0);
        s.Call(S.SCI_ADDSELECTION, 2, 2);
        s.Call(S.SCI_ADDSELECTION, 4, 4);
        await macros.RunAsync(s, [new() { Kind = MacroStepKind.Column, Number = true, Start = 10, Increment = 5 }]);
        check(s.AllText() == "10a\n15b\n20c", "Column-numbering macro uses current multiple cursors");
        s.SetText("dog dog");
        s.Select(0, 0);
        end = await macros.RunAsync(s, [find], untilEnd: true);
        check(end.Completed <= 3, "Until EOF never wraps recorded searches back to document start");
        s.SetText("x\ny");
        await macros.RunAsync(s, [new() { Kind = MacroStepKind.ConvertEol, WParam = S.SC_EOL_CRLF }]);
        check(s.AllText() == "x\r\ny", "Recorded EOL conversion replays");
        bool rejected = false;
        try
        {
            await macros.RunAsync(s, [new() { Message = S.SCI_SETDOCPOINTER, LParam = 123 }]);
        }
        catch (InvalidDataException) { rejected = true; }
        check(rejected, "Macro replay rejects commands that could treat persisted values as pointers");
        s.SetText("");
        s.Select(0, 0);
        using (var timer = new System.Windows.Forms.Timer { Interval = 15 })
        {
            timer.Tick += (_, _) => macros.Cancel();
            timer.Start();
            var cancelled = await macros.RunAsync(s, insertion, 1000000);
            timer.Stop();
            check(cancelled.Cancelled && cancelled.Completed < 1000000, "Long repeat remains responsive and can be cancelled");
            s.Call(S.SCI_UNDO);
            check(s.AllText() == "", "Cancelled repetition remains one undo action");
        }
        bool captureAllowed = false;
        using (var loading = new MacroController(Path.Combine(folder, "loading-macro")))
        {
            loading.Attach(s, () => captureAllowed);
            loading.Start();
            s.SetText("loaded file");
            captureAllowed = true;
            s.Str(S.SCI_REPLACESEL, "edited");
            loading.Stop();
            check(loading.Last.Count == 1, "File loading is excluded from native recording");
        }
        string badProfile = Path.Combine(folder, "bad-macro");
        Directory.CreateDirectory(badProfile);
        File.WriteAllText(Path.Combine(badProfile, "macros.json"), JsonSerializer.Serialize(new MacroFile(1, [new() { Message = S.SCI_SETDOCPOINTER, LParam = 1 }], [])));
        using (var bad = new MacroController(badProfile))
            check(bad.Last.Count == 0 && bad.Error != null, "Invalid macro files are preserved and not executed");
        using (var stream = File.OpenRead(Path.Combine(AppContext.BaseDirectory, "data", "LHPad.ico")))
        using (var reader = new BinaryReader(stream))
        {
            check(reader.ReadUInt16() == 0 && reader.ReadUInt16() == 1 && reader.ReadUInt16() == 9, "Windows icon contains nine size variants from 16 to 256 pixels");
        }
        using (var app = new MainForm(Path.Combine(folder, "macro-app"), []))
        {
            app.Show(host);
            await app.Ready;
            app.Current!.Editor!.SetText("첫 번째 줄\r\n두 번째 줄\r\n세 번째 줄");
            app.Current.Editor.Select(0, 0);
            app.Macros.Start();
            app.Current.Editor.Call(S.SCI_HOME);
            app.Current.Editor.Str(S.SCI_REPLACESEL, "// ");
            app.Current.Editor.Call(S.SCI_LINEDOWN);
            app.Macros.Stop();
            check(app.Macros.Last.Count == 3, "Main window connects the recorder to its text tabs");
            SelfTests.Screenshot(app, Path.Combine(output, "macro-preview.png"));
            var savedTab = app.NewDocument("저장된 문서.txt");
            savedTab.Editor!.SetText("저장된 문서의 아이콘은 배경이 투명합니다.");
            savedTab.Editor.Saved();
            var editedTab = app.NewDocument("편집 중.txt");
            editedTab.Editor!.Str(S.SCI_REPLACESEL, "저장하지 않은 문서는 파스텔 빨강으로 표시됩니다.");
            await Task.Delay(80);
            SelfTests.Screenshot(app, Path.Combine(output, "tab-status-idle.png"));
            app.Macros.Start();
            await Task.Delay(80);
            SelfTests.Screenshot(app, Path.Combine(output, "tab-status-recording.png"));
            app.Macros.Stop();
            int originalWidth = app.Width;
            app.Width = app.MinimumSize.Width;
            await Task.Delay(80);
            SelfTests.Screenshot(app, Path.Combine(output, "toolbar-compact-preview.png"));
            app.Width = originalWidth;
            using (var dialog = new MacroRunDialog(app.Macros))
            {
                dialog.Show(app);
                await Task.Delay(80);
                SelfTests.Screenshot(dialog, Path.Combine(output, "macro-repeat-preview.png"));
                dialog.Close();
            }
            var closed = new TaskCompletionSource();
            app.FormClosed += (_, _) => closed.TrySetResult();
            app.Close();
            await closed.Task;
        }
    }
}

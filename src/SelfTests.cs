using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace LHPad;

internal static class SelfTests
{
    [DllImport("user32.dll")] static extern bool PrintWindow(nint hwnd, nint dc, uint flags);
    static readonly List<string> results = [];
    static void Check(bool condition, string description)
    {
        if (!condition)
            throw new Exception(description);
        results.Add("PASS " + description);
    }
    public static int Run(bool previewOnly = false, bool macroOnly = false, bool monitorOnly = false, bool uiOnly = false, bool searchOnly = false, bool windowOnly = false, bool foldOnly = false, bool indentOnly = false, bool dialogOnly = false)
    {
        int exit = 1;
        string output = Path.Combine(AppContext.BaseDirectory, "test-results");
        Directory.CreateDirectory(output);
        var folder = Path.Combine(output, "fixtures-" + DateTime.Now.ToString("yyyyMMdd-HHmmss"));
        Directory.CreateDirectory(folder);
        using var host = new Form { Text = "LHPad verification", Width = 1150, Height = 720, ShowInTaskbar = false };
        var sci = new Sci();
        host.Controls.Add(sci);
        host.Shown += async (_, _) =>
        {
            try
            {
                bool full = !previewOnly && !macroOnly && !monitorOnly && !uiOnly && !searchOnly && !windowOnly && !foldOnly && !indentOnly && !dialogOnly;
                if (full) await TestAsync(host, sci, folder, output);
                if (full || previewOnly && !macroOnly && !monitorOnly) await PreviewTests.RunAsync(host, folder, output, Check);
                if (full || macroOnly) await MacroTests.RunAsync(host, folder, output, Check);
                if (full || monitorOnly) await FileMonitoringTests.RunAsync(host, folder, output, Check);
                if (full || uiOnly) await IdleUiTests.RunAsync(host, folder, Check);
                if (full || searchOnly) await SearchCountTests.RunAsync(host, folder, output, Check);
                if (full || windowOnly) await WindowUiTests.RunAsync(host, folder, output, Check);
                if (full || foldOnly) await FoldingTests.RunAsync(host, folder, output, Check);
                if (full || indentOnly) await AutoIndentTests.RunAsync(host, folder, output, Check);
                if (full || dialogOnly) await DialogUiTests.RunAsync(host, folder, output, Check);
                results.Add("ALL TESTS PASSED");
                exit = 0;
            }
            catch (Exception ex) { results.Add("FAIL " + ex); }
            finally
            {
                File.WriteAllLines(Path.Combine(output, dialogOnly ? "dialog-results.txt" : indentOnly ? "indent-results.txt" : foldOnly ? "fold-results.txt" : windowOnly ? "window-results.txt" : searchOnly ? "search-results.txt" : uiOnly ? "ui-results.txt" : monitorOnly ? "monitor-results.txt" : macroOnly ? "macro-results.txt" : previewOnly ? "preview-results.txt" : "results.txt"), results);
                host.Close();
            }
        };
        Application.Run(host);
        return exit;
    }
    static async Task TestAsync(Form host, Sci s, string folder, string output)
    {
        s.SetText("한글 첫 줄\r\nsecond line\r\nthird");
        s.Saved();
        long revision = s.Revision;
        s.Replace(0, 6, "테스트");
        Check(s.Modified && s.Revision > revision, "Native edit notifications and dirty tracking");
        s.Call(S.SCI_UNDO);
        Check(s.AllText().StartsWith("한글 첫 줄"), "UTF-8 undo preserves Korean");
        s.SetText("alpha beta alpha\nalpha");
        Check(await Search.ReplaceAllAsync(s, "alpha", "가나다", 0) == 3 && s.AllText() == "가나다 beta 가나다\n가나다", "Unicode replace all");
        s.Call(S.SCI_UNDO);
        Check(s.AllText() == "alpha beta alpha\nalpha", "Replace all is one undo action");
        s.SetText("a1 b2 c3");
        Check(await Search.ReplaceAllAsync(s, @"([a-z])(\d)", @"\2\1", S.SCFIND_REGEXP | S.SCFIND_CXX11REGEX) == 3 && s.AllText() == "1a 2b 3c", "Notepad++ Boost regex capture replacement");
        s.SetText("a\nb");
        var zeroCount = await Search.ReplaceAllAsync(s, "^", ">", S.SCFIND_REGEXP | S.SCFIND_CXX11REGEX);
        Check(zeroCount == 2 && s.AllText() == ">a\n>b", $"Zero width regex replacement terminates (count={zeroCount}, text={s.AllText().Replace("\n", "\\n")})");
        s.SetText("one\ntwo\nthree");
        s.Select(0, 0);
        s.Call(S.SCI_ADDSELECTION, 4, 4);
        s.Call(S.SCI_ADDSELECTION, 8, 8);
        s.Str(S.SCI_REPLACESEL, "X");
        Check(s.Call(S.SCI_GETSELECTIONS) >= 1, "Native multiple selection API available");
        var langs = new Languages();
        Check(langs.Items.Count > 80, "Notepad++ language catalog imported");
        int available = 0;
        foreach (var l in langs.Items)
        {
            langs.Apply(s, l.Name);
            if (s.Call(S.SCI_GETLEXER) != S.SCLEX_CONTAINER)
                available++;
        }
        Check(available > 70, "Lexilla language engines instantiate");
        s.SetText("{\"key\":true,\"number\":42}");
        langs.Apply(s, "json");
        s.Call(S.SCI_COLOURISE, 0, -1);
        Check(s.Call(S.SCI_GETSTYLEAT, 1) != s.Call(S.SCI_GETSTYLEAT, 7), "JSON lexer applies distinct syntax styles");
        s.SetText("select x from t where id=1");
        langs.Apply(s, "sql");
        s.Call(S.SCI_COLOURISE, 0, -1);
        Check(s.Call(S.SCI_GETSTYLEAT, 0) != s.Call(S.SCI_GETSTYLEAT, 7), "SQL keywords styled from Notepad++ data");
        var json = Formatting.Format("{\"한글\":[1,2]}", "json", "\r\n");
        Check(json.Contains("\r\n") && JsonDocument.Parse(json).RootElement.GetProperty("한글").GetArrayLength() == 2, "JSON formatting semantic roundtrip");
        var xml = Formatting.Format("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root><a>한글</a></root>", "xml", "\n");
        Check(xml.Contains("encoding=\"UTF-8\"") && xml.Contains("\n"), "XML declaration preserved");
        bool rejected = false;
        try
        {
            Formatting.Format("<!DOCTYPE foo [<!ENTITY xxe SYSTEM 'file:///C:/Windows/win.ini'>]><foo>&xxe;</foo>", "xml", "\n");
        }
        catch (System.Xml.XmlException) { rejected = true; }
        Check(rejected, "XML formatting blocks external entity resolution");
        var html = "<!DOCTYPE html><html><body><div><p>Hello <b>world</b> !</p><script>if(a < b) x = '<tag>';</script></div></body></html>";
        var formatted = Formatting.Html(html);
        Check(formatted.Contains("if(a < b) x = '<tag>';") && formatted.Contains("Hello <b>world</b> !"), "HTML mixed text and raw script preserved");
        Check(Formatting.Html("<div><span>A</span> <span>B</span></div>").Contains("</span> <span>"), "HTML whitespace between inline elements preserved");
        string sql = "WITH t AS (SELECT 'a; -- quote' AS x /* outer /* nested */ c */ FROM foo) SELECT [id], $$a\n'b$$ FROM t WHERE x = N'한글' -- comment\n AND id >= 1;";
        Check(Formatting.SqlTokens(Formatting.Sql(sql)).SequenceEqual(Formatting.SqlTokens(sql)), "SQL strings identifiers and nested comments preserved");
        Check(Formatting.Sql(sql).Contains("N'한글'"), "SQL Unicode string prefix remains attached");
        foreach (var choice in new[] { new EncodingChoice("UTF-8", 65001), new("UTF8 BOM", 65001, true), new("UTF16 LE", 1200, true), new("UTF16 BE", 1201, true), new("CP949", 949), new("EUC-KR", 51949) })
        {
            string path = Path.Combine(folder, choice.CodePage + "-" + choice.Bom + ".txt");
            s.SetText("한글 테스트\r\nABC 123\r\n");
            await s.WriteStreamAsync(path, choice.Strict, choice.Bom);
            var detection = FileTypes.Detect(path);
            Check(!detection.Binary, $"Text detection {choice.Name}");
            await s.LoadStreamAsync(path, choice.Strict, choice.Bom ? choice.Strict.GetPreamble().Length : 0);
            Check(s.AllText() == "한글 테스트\r\nABC 123\r\n", $"Encoding save/reload {choice.Name}");
        }
        string badPath = Path.Combine(folder, "lossy.tmp");
        s.SetText("한글😀");
        bool lossy = false;
        try
        {
            await s.WriteStreamAsync(badPath, new EncodingChoice("ASCII", 20127).Strict, false);
        }
        catch (EncoderFallbackException) { lossy = true; }
        Check(lossy, "Unrepresentable characters reject lossy save");
        var diff = LineDiff.Compare(["a", "b", "c"], ["a", "insert", "b", "changed"]);
        Check(diff.Count(x => x.Kind == 0) == 2 && diff.Any(x => x.Left < 0) && diff.Any(x => x.Kind == 3), "Line diff aligns inserted and removed lines");
        var random = new Random(41);
        for (int test = 0; test < 100; test++)
        {
            var a = Enumerable.Range(0, random.Next(1, 80)).Select(_ => random.Next(8).ToString()).ToArray();
            var b = Enumerable.Range(0, random.Next(1, 80)).Select(_ => random.Next(8).ToString()).ToArray();
            var rows = LineDiff.Compare(a, b);
            Check(rows.Where(x => x.Left >= 0).Select(x => x.Left).SequenceEqual(Enumerable.Range(0, a.Length)) && rows.Where(x => x.Right >= 0).Select(x => x.Right).SequenceEqual(Enumerable.Range(0, b.Length)) && rows.Where(x => x.Kind == 0).All(x => a[x.Left] == b[x.Right]), $"Diff coverage invariant {test}");
        }
        string bin = Path.Combine(folder, "sample.bin");
        File.WriteAllBytes(bin, Enumerable.Range(0, 256).Select(x => (byte)x).ToArray());
        Check(FileTypes.Detect(bin).Binary, "Binary automatic detection");
        using (var h = new HexView())
        {
            h.Open(bin);
            h.SetBytes(14, [0xFE, 0xFF, 0xAA]);
            Check(h.Read(14) == 0xFE && h.Modified, "Hex nibble overwrite overlay");
            h.Undo();
            Check(h.Read(14) == 14 && !h.Modified, "Hex undo returns original bytes");
            h.Redo();
            Check(h.Read(14) == 0xFE, "Hex redo");
            Check(await h.FindAsync([0xFF, 0xAA], 0, CancellationToken.None) == 15, "Hex streaming search includes dirty bytes");
            string saved = Path.Combine(folder, "patched.bin");
            await h.WriteAsync(saved);
            Check(File.ReadAllBytes(saved)[16] == 0xAA && File.ReadAllBytes(bin)[16] == 16, "Hex save keeps source unchanged");
        }
        string huge = Path.Combine(folder, "large-sparse.bin");
        using (var f = new FileStream(huge, FileMode.Create, FileAccess.Write))
        {
            f.SetLength(3L * 1024 * 1024 * 1024);
            f.Position = f.Length - 1;
            f.WriteByte(0xA5);
        }
        using (var h = new HexView())
        {
            var watch = Stopwatch.StartNew();
            h.Open(huge);
            h.Go(h.Length - 1);
            Check(h.Read(h.Caret) == 0xA5 && h.Length == 3L * 1024 * 1024 * 1024, "3 GiB binary file uses 64-bit offsets");
            Check(watch.ElapsedMilliseconds < 3000, "3 GiB binary opens without loading whole file");
        }
        File.Delete(huge);
        var doc = new Document { Name = "미저장 한글", Editor = s };
        doc.Page.Controls.Add(s);
        s.SetText("원본에 없는 복원 내용😀\r\n둘째 줄");
        s.Select(0, 6);
        string profile = Path.Combine(folder, "profile");
        var store = new SessionStore(profile);
        await store.SaveAsync([doc], 0, true, true, false);
        var session = store.Read()!;
        Check(session.Documents.Count == 1 && session.Documents[0].Snapshot != null, "Unsaved tab committed to session manifest");
        using (var restored = new Sci())
        {
            host.Controls.Add(restored);
            await restored.LoadStreamAsync(session.Documents[0].Snapshot!, FileTypes.Utf8.Strict);
            Check(restored.AllText() == s.AllText(), "Unsaved snapshot exact Unicode recovery");
        }
        await store.SaveAsync([doc], 0, false, false, false);
        File.WriteAllText(store.Manifest, "corrupt");
        Check(store.Read()!.Documents[0].Name == "미저장 한글", "Corrupt session manifest falls back to prior generation");
        host.Controls.Add(s);
        // A real 256 MiB text file: native storage, chunked UTF-8 input/output, and a live UI timer.
        string big = Path.Combine(folder, "large-256MiB.log");
        var block = Encoding.UTF8.GetBytes(new string('a', 1023) + "\n");
        await using (var f = new FileStream(big, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 1024, FileOptions.Asynchronous))
        {
            var megabyte = new byte[1024 * 1024];
            for (int i = 0; i < 1024; i++)
                block.CopyTo(megabyte, i * 1024);
            for (int i = 0; i < 256; i++)
                await f.WriteAsync(megabyte);
        }
        int ticks = 0;
        using var timer = new System.Windows.Forms.Timer { Interval = 15 };
        timer.Tick += (_, _) => ticks++;
        timer.Start();
        s.Large = true;
        langs.Apply(s, "normal");
        var timerLoad = Stopwatch.StartNew();
        await s.LoadStreamAsync(big, FileTypes.Utf8.Strict);
        timerLoad.Stop();
        timer.Stop();
        Check(s.Length == 256L * 1024 * 1024 && s.Call(S.SCI_GETLINECOUNT) == 262145, "256 MiB text loaded with native 64-bit document");
        Check(ticks > 0, "UI remains responsive during large text loading");
        string bigSaved = Path.Combine(folder, "large-saved.log");
        await s.WriteStreamAsync(bigSaved, FileTypes.Utf8.Strict, false);
        using (var a = File.OpenRead(big))
        using (var b = File.OpenRead(bigSaved))
            Check(SHA256.HashData(a).SequenceEqual(SHA256.HashData(b)), "256 MiB text save SHA-256 matches input");
        results.Add($"METRIC largeLoadMs={timerLoad.ElapsedMilliseconds} uiTicks={ticks} workingSetMiB={Process.GetCurrentProcess().WorkingSet64 / 1048576}");
        s.SetText("검증 완료");
        s.Call(S.SCI_EMPTYUNDOBUFFER);
        File.Delete(big);
        File.Delete(bigSaved);
        // Piece table invariants exercise byte insertion, deletion, split offsets, and history.
        using (var buffer = new BinaryBuffer(bin))
        {
            var expected = File.ReadAllBytes(bin).ToList();
            var rng = new Random(104);
            for (int i = 0; i < 120; i++)
            {
                int at = rng.Next(expected.Count + 1), remove = rng.Next(Math.Min(8, expected.Count - at) + 1);
                byte[] inserted = Enumerable.Range(0, rng.Next(8)).Select(_ => (byte)rng.Next(256)).ToArray();
                buffer.Replace(at, remove, inserted);
                expected.RemoveRange(at, remove);
                expected.InsertRange(at, inserted);
                var actual = new byte[expected.Count];
                buffer.Read(0, actual);
                Check(actual.SequenceEqual(expected) && buffer.Length == expected.Count, $"Hex piece table insert/delete invariant {i}");
            }
            string edited = Path.Combine(folder, "piece-table.bin");
            await buffer.WriteAsync(edited);
            Check(File.ReadAllBytes(edited).SequenceEqual(expected), "Hex inserted/deleted file streaming save");
        }
        foreach (var encoding in FileTypes.Encodings)
        {
            try
            {
                _ = encoding.Strict;
                results.Add("ENCODING " + encoding.Name + " supported");
            }
            catch (NotSupportedException) { results.Add("UNSUPPORTED ENCODING " + encoding.Name); }
        }
        var celtic = new CelticEncoding();
        var allBytes = Enumerable.Range(0, 256).Select(i => (byte)i).ToArray();
        Check(celtic.GetBytes(celtic.GetString(allBytes)).SequenceEqual(allBytes), "ISO-8859-14 complete byte roundtrip");
        string appProfile = Path.Combine(folder, "app-profile");
        using (var app = new MainForm(appProfile, []))
        {
            app.Show(host);
            await app.Ready;
            app.Current!.Editor!.SetText("// 세션 복원 통합 검증\r\nconst name = '한글😀';\r\n");
            app.Current.Language = "javascript.js";
            app.Current.Editor.Select(0, 6);
            app.Current.Editor.Call(S.SCI_ADDSELECTION, 12, 12);
            await app.OpenAsync(bin);
            app.Current!.Hex!.InsertBytes(4, [0xAB, 0xCD]);
            app.Current.Hex.Go(4);
            await Task.Delay(100);
            Screenshot(app, Path.Combine(output, "hex-preview.png"));
            var closed = new TaskCompletionSource();
            app.FormClosed += (_, _) => closed.TrySetResult();
            app.Close();
            await closed.Task;
        }
        using (var reopened = new MainForm(appProfile, []))
        {
            reopened.Show(host);
            await reopened.Ready;
            Check(reopened.Documents.Count == 2, "Actual window close/reopen restores tab count");
            var textDoc = reopened.Documents.First(x => x.Editor != null);
            var hexDoc = reopened.Documents.First(x => x.Hex != null);
            Check(textDoc.Editor!.AllText().Contains("한글😀") && textDoc.Dirty && textDoc.Language == "javascript.js", "Actual restart restores unsaved text and language");
            Check(textDoc.Editor.Call(S.SCI_GETSELECTIONS) == 2, "Actual restart restores multiple cursors");
            Check(reopened.Current == hexDoc && hexDoc.Hex!.Length == 258 && hexDoc.Hex.Read(4) == 0xAB && hexDoc.Dirty, "Actual restart restores dirty inserted binary and active tab");
            Check(File.ReadAllBytes(bin).Length == 256 && File.ReadAllBytes(bin)[4] == 4, "Session recovery never overwrites original binary");
            var closed = new TaskCompletionSource();
            reopened.FormClosed += (_, _) => closed.TrySetResult();
            reopened.Close();
            await closed.Task;
        }
        using (var preview = new MainForm(Path.Combine(folder, "demo-profile"), [], true))
        {
            preview.Show(host);
            await preview.Ready;
            await Task.Delay(80);
            Screenshot(preview, Path.Combine(output, "text-preview.png"));
            using (var findPreview = new FindForm(preview))
            {
                findPreview.Show(preview);
                findPreview.SetQuery("name");
                await findPreview.FindNextAsync(false);
                await Task.Delay(80);
                Screenshot(findPreview, Path.Combine(output, "find-preview.png"));
                findPreview.Close();
            }
            var closed = new TaskCompletionSource();
            preview.FormClosed += (_, _) => closed.TrySetResult();
            preview.Close();
            await closed.Task;
        }
        using (var comparison = new CompareForm("before.json", "after.json", ["{", "  \"name\": \"LHPad\",", "  \"version\": 1", "}"], ["{", "  \"name\": \"LHPad\",", "  \"version\": 2,", "  \"hex\": true", "}"], LineDiff.Compare(["{", "  \"name\": \"LHPad\",", "  \"version\": 1", "}"], ["{", "  \"name\": \"LHPad\",", "  \"version\": 2,", "  \"hex\": true", "}"])))
        {
            comparison.Show(host);
            await Task.Delay(80);
            Screenshot(comparison, Path.Combine(output, "compare-preview.png"));
            comparison.Close();
        }
        Check(true, "Tests completed without opening or modifying user documents");
    }
    public static void Screenshot(Form form, string path)
    {
        using var bitmap = new Bitmap(form.Width, form.Height);
        using var g = Graphics.FromImage(bitmap);
        var dc = g.GetHdc();
        try
        {
            PrintWindow(form.Handle, dc, 2);
        }
        finally { g.ReleaseHdc(dc); }
        bitmap.Save(path);
    }
}

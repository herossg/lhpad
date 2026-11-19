using System.Text;

namespace LHPad;

internal static class AutoIndentTests
{
    static void Reset(Sci s, string text, int mode = S.SC_EOL_LF, long? position = null)
    {
        s.Call(S.SCI_SETREADONLY, 0);
        s.AutoIndent = true;
        s.Call(S.SCI_CLEARSELECTIONS);
        s.SetText(text);
        s.Call(S.SCI_SETEOLMODE, mode);
        s.Call(S.SCI_EMPTYUNDOBUFFER);
        s.Saved();
        s.Select(position ?? s.Length, position ?? s.Length);
    }
    static void PressEnter(Sci s)
    {
        s.Call(0x0100, (int)Keys.Return, 0x001C0001);
        s.Call(0x0102, '\r', 0x001C0001);
        s.Call(0x0101, (int)Keys.Return, 0xC01C0001);
    }
    static bool AtEnd(Sci s, string expected) => s.AllText() == expected && s.Caret == Encoding.UTF8.GetByteCount(expected) && s.Anchor == s.Caret;
    static async Task CloseAsync(Form app)
    {
        var closed = new TaskCompletionSource();
        app.FormClosed += (_, _) => closed.TrySetResult();
        app.Close();
        await closed.Task.WaitAsync(TimeSpan.FromSeconds(15));
    }
    static ToolStripMenuItem Option(MainForm app)
    {
        var edit = app.MainMenuStrip!.Items.OfType<ToolStripMenuItem>().Single(item => item.Text?.StartsWith("편집") == true);
        return edit.DropDownItems.OfType<ToolStripMenuItem>().Single(item => item.Text == "자동 들여쓰기");
    }
    public static async Task RunAsync(Form host, string folder, string output, Action<bool, string> check)
    {
        using (var s = new Sci())
        {
            host.Controls.Add(s);
            s.BringToFront();
            _ = s.Handle;
            check(s.AutoIndent, "New text editors enable indentation continuation by default");
            foreach (var (mode, eol, name) in new[] { (S.SC_EOL_CRLF, "\r\n", "CRLF"), (S.SC_EOL_LF, "\n", "LF"), (S.SC_EOL_CR, "\r", "CR") })
            {
                Reset(s, "\t\talpha", mode);
                long revision = s.Revision;
                PressEnter(s);
                string expected = "\t\talpha" + eol + "\t\t";
                check(AtEnd(s, expected) && s.Modified && s.Revision > revision,
                    $"Native Enter preserves exactly two tab bytes and positions the caret after them with {name} endings");
                s.Call(S.SCI_UNDO);
                check(AtEnd(s, "\t\talpha") && !s.Modified, $"One undo removes both the {name} newline and its automatic indentation");
                s.Call(S.SCI_REDO);
                check(AtEnd(s, expected) && s.Modified, $"One redo restores both the {name} newline and its automatic indentation");
            }
            foreach (string prefix in new[] { "  ", "    ", " \t  " })
            {
                Reset(s, prefix + "text");
                s.Call(S.SCI_NEWLINE);
                check(AtEnd(s, prefix + "text\n" + prefix), $"SCI_NEWLINE preserves the exact leading whitespace bytes {Convert.ToHexString(Encoding.UTF8.GetBytes(prefix))}");
            }
            Reset(s, "    if (ready) {");
            new Languages().Apply(s, "cpp");
            s.Call(S.SCI_NEWLINE);
            check(AtEnd(s, "    if (ready) {\n    "), "A brace does not add an extra indentation level beyond the previous line's prefix");
            new Languages().Apply(s, "normal");
            Reset(s, "plain text");
            PressEnter(s);
            check(AtEnd(s, "plain text\n"), "Enter on an unindented plain-text line retains normal newline behavior");
            Reset(s, "");
            PressEnter(s);
            check(AtEnd(s, "\n"), "Enter in an empty document adds only a newline");
            Reset(s, "  가나다", position: Encoding.UTF8.GetByteCount("  가"));
            PressEnter(s);
            check(s.AllText() == "  가\n  나다" && s.Caret == Encoding.UTF8.GetByteCount("  가\n  "),
                "Splitting a Unicode line carries its indentation and leaves the right-hand text after the caret");
            Reset(s, " \t  value", position: 2);
            s.Call(S.SCI_NEWLINE);
            check(s.AllText() == " \t\n \t  value" && s.Caret == 5,
                "Enter inside leading whitespace copies only the prefix to the left of the caret");
            Reset(s, "\t\t");
            PressEnter(s);
            check(AtEnd(s, "\t\t\n\t\t"), "An indentation-only line continues its exact tab prefix");
            Reset(s, "    text");
            s.AutoIndent = false;
            s.Call(S.SCI_NEWLINE);
            check(AtEnd(s, "    text\n"), "Disabling automatic indentation leaves the native newline unindented");
            Reset(s, "\t\tlocked");
            long lockedRevision = s.Revision;
            s.Call(S.SCI_SETREADONLY, 1);
            PressEnter(s);
            s.Call(S.SCI_NEWLINE);
            check(AtEnd(s, "\t\tlocked") && !s.Modified && s.Revision == lockedRevision,
                "Neither keyboard Enter nor SCI_NEWLINE changes a read-only document");
            s.Call(S.SCI_SETREADONLY, 0);

            Reset(s, "\t\talpha");
            s.Str(S.SCI_REPLACESEL, "\nbeta\n  gamma");
            check(AtEnd(s, "\t\talpha\nbeta\n  gamma"), "Multiline replacement/paste text is inserted verbatim without automatic indentation");
            Reset(s, "\t\talpha");
            byte[] added = "\nbeta\nend"u8.ToArray();
            s.Bytes(S.SCI_ADDTEXT, added, added.Length);
            check(AtEnd(s, "\t\talpha\nbeta\nend"), "Native bulk ADDTEXT insertion does not indent imported lines");
            Reset(s, "    alpha");
            s.Bytes(S.SCI_APPENDTEXT, added, added.Length);
            check(s.AllText() == "    alpha\nbeta\nend", "Native APPENDTEXT log/import insertion preserves its exact source bytes");
            const string loaded = "\t\talpha\nbeta\n  gamma";
            s.SetText(loaded);
            check(s.AllText() == loaded, "SetText never inserts extra whitespace into multiline content");
            string path = Path.Combine(folder, "auto-indent-load.txt");
            await File.WriteAllTextAsync(path, loaded, new UTF8Encoding(false));
            await s.LoadStreamAsync(path, FileTypes.Utf8.Strict);
            check(s.AllText() == loaded && !s.Modified && s.Call(S.SCI_CANUNDO) == 0, "Loading a text file preserves its indentation and saved state exactly");

            Reset(s, "\t\tone\n  two");
            s.Select(5, 5);
            s.Call(S.SCI_ADDSELECTION, s.Length, s.Length);
            s.Call(S.SCI_NEWLINE);
            check(s.AllText() == "\t\tone\n\t\t\n  two\n  " && s.Call(S.SCI_GETSELECTIONS) == 2,
                "One newline command continues the distinct tab and space prefixes at multiple carets");
            s.Call(S.SCI_UNDO);
            check(s.AllText() == "\t\tone\n  two" && !s.Modified, "A single undo reverses every caret's newline and continued indentation");

            using var macros = new MacroController(Path.Combine(folder, "auto-indent-macros"));
            macros.Attach(s);
            Reset(s, "\t\talpha");
            macros.Start();
            PressEnter(s);
            macros.Stop();
            check(macros.Last.Count == 1 && macros.Last[0].Message == S.SCI_NEWLINE,
                "Recording Enter stores one semantic newline without recording its generated tab prefix separately");
            Reset(s, "\t\talpha");
            await macros.RunAsync(s);
            check(AtEnd(s, "\t\talpha\n\t\t"), "Replaying a recorded Enter adds exactly one copy of the tab indentation");
            s.Call(S.SCI_UNDO);
            check(AtEnd(s, "\t\talpha") && !s.Modified, "Recorded Enter and its generated indentation remain one undo action during playback");
            Reset(s, "    beta");
            await macros.RunAsync(s);
            check(AtEnd(s, "    beta\n    "), "Recorded Enter follows the destination line's indentation instead of the original recording's tabs");
            Reset(s, "\t\tone\n  two");
            s.Select(5, 5);
            s.Call(S.SCI_ADDSELECTION, s.Length, s.Length);
            macros.Start();
            PressEnter(s);
            macros.Stop();
            bool oneNewline = macros.Last.Count == 1 && macros.Last[0].Message == S.SCI_NEWLINE;
            Reset(s, "\t\tone\n  two");
            s.Select(5, 5);
            s.Call(S.SCI_ADDSELECTION, s.Length, s.Length);
            await macros.RunAsync(s);
            check(oneNewline && s.AllText() == "\t\tone\n\t\t\n  two\n  ",
                "A multiple-caret Enter records one newline and replays each caret's indentation once");
            host.Controls.Remove(s);
        }

        string profile = Path.Combine(folder, "auto-indent-ui-profile");
        using (var app = new MainForm(profile, []))
        {
            app.Show(host);
            await app.Ready;
            var first = app.Current!.Editor!;
            check(Option(app).Checked && first.AutoIndent, "The Edit menu and first text tab expose automatic indentation as enabled by default");
            Reset(first, "\t\tfirst line", S.SC_EOL_CRLF);
            PressEnter(first);
            first.Str(S.SCI_REPLACESEL, "second line");
            PressEnter(first);
            first.Call(S.SCI_SETVIEWWS, S.SCWS_VISIBLEALWAYS);
            app.UpdateStatus();
            await Task.Delay(80);
            SelfTests.Screenshot(app, Path.Combine(output, "auto-indent-preview.png"));
            Option(app).PerformClick();
            var second = app.NewDocument("auto-indent-off.txt").Editor!;
            check(!Option(app).Checked && !first.AutoIndent && !second.AutoIndent, "Toggling automatic indentation off updates existing text tabs and the next new tab");
            second.SetText("\t\ttext");
            second.Select(second.Length, second.Length);
            second.Call(S.SCI_NEWLINE);
            check(AtEnd(second, "\t\ttext\r\n"), "The disabled global option produces an unindented newline in a newly created tab");
            await CloseAsync(app);
        }
        using (var app = new MainForm(profile, []))
        {
            app.Show(host);
            await app.Ready;
            check(!Option(app).Checked && app.Documents.Where(d => d.Editor != null).All(d => !d.Editor!.AutoIndent),
                "A real application restart restores the disabled indentation preference on every recovered text tab");
            Option(app).PerformClick();
            check(Option(app).Checked && app.Documents.Where(d => d.Editor != null).All(d => d.Editor!.AutoIndent),
                "Re-enabling the menu option applies immediately to all restored text tabs");
            await CloseAsync(app);
        }
    }
}

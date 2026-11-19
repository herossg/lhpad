namespace LHPad;

internal static class FoldingTests
{
    const string CSource = "int main() {\n    const char *literal = \"{\";\n    // } is not a closing brace\n    if (ready) {\n        hiddenMatch();\n    }\n}\nint other() {\n    return 1;\n}";
    const string HtmlSource = "<div id=\"outer\">\n    <img src=\"image.png\">\n    <br>\n    <section>\n        <div>\n            nested content\n        </div>\n    </section>\n</div>\n<p>outside</p>";

    static bool Header(Sci s, int line) => ((int)s.Call(S.SCI_GETFOLDLEVEL, line) & S.SC_FOLDLEVELHEADERFLAG) != 0;
    static bool Expanded(Sci s, int line) => s.Call(S.SCI_GETFOLDEXPANDED, line) != 0;
    static bool Visible(Sci s, int line) => s.Call(S.SCI_GETLINEVISIBLE, line) != 0;
    static long Position(Sci s, int line) => (long)s.Call(S.SCI_POSITIONFROMLINE, line);
    static void Load(Sci s, Languages languages, string language, string content)
    {
        s.SetAllFolds(true);
        s.SetText(content);
        languages.Apply(s, language);
        s.Saved();
        s.Call(S.SCI_EMPTYUNDOBUFFER);
        s.Select(0, 0);
    }
    static void ClickMargin(Sci s, int margin, int line)
    {
        int left = Enumerable.Range(0, margin).Sum(i => (int)s.Call(S.SCI_GETMARGINWIDTHN, i));
        int x = left + (int)s.Call(S.SCI_GETMARGINWIDTHN, margin) / 2;
        int y = (int)s.Call(S.SCI_POINTYFROMPOSITION, 0, Position(s, line)) + (int)s.Call(S.SCI_TEXTHEIGHT, line) / 2;
        long point = (ushort)x | ((long)(ushort)y << 16);
        s.Call(0x0201, 1, point); // Real native WM_LBUTTONDOWN / WM_LBUTTONUP routing.
        s.Call(0x0202, 0, point);
    }
    static bool DispatchShortcut(MainForm app, Keys key)
    {
        // Exercise the application's real protected command-key route without changing
        // global keyboard state or sending keystrokes to another foreground window.
        var message = Message.Create(app.Handle, 0x0104, (nint)(int)(key & Keys.KeyCode), (nint)0x20000000);
        object[] arguments = [message, key];
        return (bool)typeof(MainForm).GetMethod("ProcessCmdKey", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.Invoke(app, arguments)!;
    }
    public static async Task RunAsync(Form host, string folder, string output, Action<bool, string> check)
    {
        var languages = new Languages();
        using (var s = new Sci())
        {
            host.Controls.Add(s);
            s.BringToFront();
            _ = s.Handle;
            foreach (string language in new[] { "c", "cpp" })
            {
                Load(s, languages, language, CSource);
                check(Header(s, 0) && Header(s, 3) && Header(s, 7) && s.Call(S.SCI_GETFOLDPARENT, 4) == 3,
                    $"{language} lexer identifies nested brace headers and the native parent of their body");
                check(!Header(s, 1) && !Header(s, 2) && (int)s.Call(S.SCI_GETLASTCHILD, 0, -1) >= 5,
                    $"{language} string and comment braces do not create or prematurely close folds");
            }
            check(s.Call(S.SCI_GETMARGINWIDTHN, 1) > 0 && s.Call(S.SCI_GETMARGINMASKN, 1) == S.SC_MASK_FOLDERS
                && s.Call(S.SCI_MARKERSYMBOLDEFINED, S.SC_MARKNUM_FOLDERSUB) == S.SC_MARK_VLINE
                && s.Call(S.SCI_MARKERSYMBOLDEFINED, S.SC_MARKNUM_FOLDERMIDTAIL) == S.SC_MARK_TCORNER
                && s.Call(S.SCI_MARKERSYMBOLDEFINED, S.SC_MARKNUM_FOLDERTAIL) == S.SC_MARK_LCORNER
                && Visible(s, 4) && Expanded(s, 0),
                "Expanded native fold regions retain connected vertical and corner guides next to line numbers");

            long revision = s.Revision;
            ClickMargin(s, 1, 0);
            await Task.Delay(30);
            check(!Expanded(s, 0) && !Visible(s, 4) && Visible(s, 7), "One real folding-margin click collapses exactly once and preserves the following sibling block");
            ClickMargin(s, 1, 0);
            await Task.Delay(30);
            check(Expanded(s, 0) && Visible(s, 4), "A second real folding-margin click expands the same block exactly once");
            ClickMargin(s, 0, 0);
            check(Expanded(s, 0) && Visible(s, 4), "Clicking the line-number margin does not toggle a fold");
            s.Select(Position(s, 4), Position(s, 4));
            s.ToggleCurrentFold();
            check(Expanded(s, 0) && !Expanded(s, 3) && !Visible(s, 4)
                && Visible(s, (int)s.Call(S.SCI_LINEFROMPOSITION, s.Caret)), "Current-block folding from a body line targets its nearest enclosing header and keeps the caret visible");
            s.SetAllFolds(true);
            check(s.Call(S.SCI_GETALLLINESVISIBLE) != 0 && new[] { 0, 3, 7 }.All(line => Expanded(s, line)), "Expand all restores every nested header and line");
            s.Select(0, 0);
            s.SetAllFolds(false);
            check(new[] { 0, 3, 7 }.All(line => !Expanded(s, line)) && !Visible(s, 4) && !Visible(s, 8), "Collapse all also contracts nested children in every sibling block");
            s.SetAllFolds(true);
            s.SetFoldLevel(2, false);
            check(Expanded(s, 0) && Expanded(s, 7) && !Expanded(s, 3) && !Visible(s, 4) && Visible(s, 8), "Folding level two leaves outer blocks expanded while hiding the nested block body");
            s.SetFoldLevel(2, true);
            check(Expanded(s, 3) && Visible(s, 4), "Unfolding level two reveals the nested body again");
            check(s.AllText() == CSource && !s.Modified && s.Revision == revision && s.Call(S.SCI_CANUNDO) == 0,
                "Folding changes no text, saved-state flag, revision or undo history");
            s.Replace(s.Length, s.Length, " // edited");
            string edited = s.AllText();
            revision = s.Revision;
            s.SetAllFolds(false);
            s.SetAllFolds(true);
            check(s.Modified && s.Revision == revision && s.AllText() == edited && s.Call(S.SCI_CANUNDO) != 0, "Fold commands preserve an already dirty document and its pending undo entry");
            s.Call(S.SCI_UNDO);
            check(s.AllText() == CSource && !s.Modified, "Undo after fold operations reverses the text edit without extra view-only undo steps");
            s.SetAllFolds(false);
            languages.Apply(s, "normal");
            check(s.Call(S.SCI_GETALLLINESVISIBLE) != 0 && Enumerable.Range(0, (int)s.Call(S.SCI_GETLINECOUNT)).All(line => !Header(s, line)),
                "Switching a collapsed source file to plain text reveals every line and removes stale fold headers");
            s.ToggleCurrentFold();
            s.SetAllFolds(false);
            check(s.Call(S.SCI_GETALLLINESVISIBLE) != 0 && s.AllText() == CSource, "Plain-text braces cannot create bogus folds or hide content");
            Load(s, languages, "cpp", CSource);
            s.SetAllFolds(false);
            s.Large = true;
            languages.Apply(s, "cpp");
            s.SetAllFolds(false);
            check(!s.FoldingAvailable && s.Call(S.SCI_GETALLLINESVISIBLE) != 0 && s.AllText() == CSource,
                "Entering large-file plain-view mode reveals previously hidden lines and disables source folding");
            s.Large = false;

            Load(s, languages, "html", HtmlSource);
            check(Header(s, 0) && Header(s, 3) && Header(s, 4) && s.Call(S.SCI_GETFOLDPARENT, 5) == 4,
                "HTML lexer folds nested div and section tags using their actual native hierarchy");
            check(!Header(s, 1) && !Header(s, 2) && !Header(s, 9), "HTML img and br void elements and one-line tags do not create false fold regions");
            s.Select(Position(s, 4), Position(s, 4));
            s.ToggleCurrentFold();
            check(!Visible(s, 5) && Visible(s, 1) && Visible(s, 9), "Collapsing an inner HTML div preserves its containing section and outside content");

            Load(s, languages, "xml", "<?xml version=\"1.0\"?>\n<root>\n    <empty />\n    <group>\n        <item>text</item>\n    </group>\n</root>");
            check(Header(s, 1) && Header(s, 3) && !Header(s, 2) && !Header(s, 4) && s.Call(S.SCI_GETFOLDPARENT, 4) == 3,
                "XML folds nested elements without treating self-closing or one-line elements as headers");
            s.SetAllFolds(false);
            check(!Visible(s, 4) && Visible(s, 0), "XML collapse all hides nested content while retaining the declaration");

            Load(s, languages, "json", "{\n    \"literal\": \"{ not a block }\",\n    \"items\": [\n        {\n            \"name\": \"value\"\n        }\n    ]\n}");
            check(Header(s, 0) && Header(s, 2) && Header(s, 3) && !Header(s, 1) && s.Call(S.SCI_GETFOLDPARENT, 4) == 3,
                "JSON folds objects and arrays while ignoring braces inside strings");
            s.SetAllFolds(false);
            s.SetAllFolds(true);
            check(s.Call(S.SCI_GETALLLINESVISIBLE) != 0 && !s.Modified, "JSON expand all restores the complete saved document");
            host.Controls.Remove(s);
        }

        using var app = new MainForm(Path.Combine(folder, "fold-ui-profile"), []);
        app.Show(host);
        await app.Ready;
        var document = app.NewDocument("folding-example.cpp");
        var editor = document.Editor!;
        Load(editor, languages, "cpp", CSource);
        document.Language = "cpp";
        app.UpdateStatus();
        var viewMenu = app.MainMenuStrip!.Items.OfType<ToolStripMenuItem>().Single(item => item.Text?.StartsWith("보기") == true);
        var foldMenu = viewMenu.DropDownItems.OfType<ToolStripMenuItem>().Single(item => item.Text == "코드 접기");
        var toggle = foldMenu.DropDownItems.OfType<ToolStripMenuItem>().Single(item => item.Text == "현재 구간 접기 / 펼치기");
        var collapseAll = foldMenu.DropDownItems.OfType<ToolStripMenuItem>().Single(item => item.Text == "모두 접기");
        var expandAll = foldMenu.DropDownItems.OfType<ToolStripMenuItem>().Single(item => item.Text == "모두 펼치기");
        check(toggle.ShortcutKeys == (Keys.Control | Keys.Alt | Keys.B) && collapseAll.ShortcutKeys == (Keys.Alt | Keys.D0)
            && expandAll.ShortcutKeys == (Keys.Alt | Keys.Shift | Keys.D0), "Fold menus expose current-block and Notepad++-style collapse/expand-all shortcuts");
        viewMenu.ShowDropDown();
        check(foldMenu.Enabled, "The folding submenu is enabled for a supported source document");
        viewMenu.HideDropDown();
        await Task.Delay(80);
        SelfTests.Screenshot(app, Path.Combine(output, "folding-expanded-preview.png"));
        collapseAll.PerformClick();
        check(!Visible(editor, 4), "The actual application collapse-all menu hides nested source content");
        await Task.Delay(80);
        SelfTests.Screenshot(app, Path.Combine(output, "folding-collapsed-preview.png"));
        using (var find = new FindForm(app))
        {
            find.Show(app);
            find.SetQuery("hiddenMatch");
            await find.FindNextAsync(false);
            check(editor.TextRange(editor.SelectionStart, editor.SelectionEnd - editor.SelectionStart) == "hiddenMatch" && Visible(editor, 4)
                && Expanded(editor, 0) && Expanded(editor, 3), "Find navigates to a match inside collapsed blocks and automatically reveals every enclosing fold");
            find.Close();
        }
        expandAll.PerformClick();
        check(editor.Call(S.SCI_GETALLLINESVISIBLE) != 0 && !document.Dirty, "The actual expand-all menu reveals the complete source file without marking its tab dirty");
        editor.Call(S.SCI_SETREADONLY, 1);
        collapseAll.PerformClick();
        check(!Visible(editor, 4) && !editor.Modified, "Read-only source documents still allow view-only folding");
        editor.Call(S.SCI_SETREADONLY, 0);
        var htmlDocument = app.NewDocument("folding-example.html");
        Load(htmlDocument.Editor!, languages, "html", HtmlSource);
        htmlDocument.Language = "html";
        app.UpdateStatus();
        await Task.Delay(80);
        SelfTests.Screenshot(app, Path.Combine(output, "folding-html-expanded-preview.png"));
        var plain = app.NewDocument("plain.txt");
        plain.Editor!.SetText("{\n    ordinary text\n}");
        viewMenu.ShowDropDown();
        check(!foldMenu.Enabled, "The folding submenu is disabled for plain text even when it contains brace characters");
        viewMenu.HideDropDown();
        string binaryPath = Path.Combine(folder, "folding-guard.bin");
        await File.WriteAllBytesAsync(binaryPath, [0x4D, 0x5A, 0, 1, 2, 3]);
        await app.OpenAsync(binaryPath, true);
        viewMenu.ShowDropDown();
        check(!foldMenu.Enabled, "The folding submenu is disabled for a Hex document");
        viewMenu.HideDropDown();
        app.Controls.OfType<TabControl>().Single().SelectedTab = document.Page;
        editor.SetAllFolds(true);
        bool collapsedByShortcut = DispatchShortcut(app, Keys.Alt | Keys.D0) && !Visible(editor, 4);
        bool expandedByShortcut = DispatchShortcut(app, Keys.Alt | Keys.Shift | Keys.D0) && editor.Call(S.SCI_GETALLLINESVISIBLE) != 0;
        check(collapsedByShortcut && expandedByShortcut,
            "Actual command-key dispatch collapses and expands after switching from Hex to source without reopening the View menu");
        var closed = new TaskCompletionSource();
        app.FormClosed += (_, _) => closed.TrySetResult();
        app.Close();
        await closed.Task.WaitAsync(TimeSpan.FromSeconds(15));
    }
}

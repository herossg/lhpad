namespace LHPad;

internal sealed partial class MainForm
{
    readonly ToolStripMenuItem foldingMenu = new("코드 접기") { Enabled = false };

    bool CanFoldCurrent => !busy && !macros.IsPlaying && Current is { Loading: false, Editor: { FoldingAvailable: true } }
        && Current.Surface?.Mode != DocumentViewMode.Preview;

    void BuildFoldingMenu(ToolStripMenuItem view)
    {
        view.DropDownItems.Add(new ToolStripSeparator());
        view.DropDownItems.Add(foldingMenu);
        ToolStripMenuItem Command(ToolStripMenuItem parent, string title, Keys keys, Action<Sci> action)
        {
            var item = new ToolStripMenuItem(title, null, (_, _) => Try(() => ApplyFold(action))) { ShortcutKeys = keys };
            parent.DropDownItems.Add(item);
            return item;
        }
        Command(foldingMenu, "현재 구간 접기 / 펼치기", Keys.Control | Keys.Alt | Keys.B, s => s.ToggleCurrentFold());
        foldingMenu.DropDownItems.Add(new ToolStripSeparator());
        Command(foldingMenu, "모두 접기", Keys.Alt | Keys.D0, s => s.SetAllFolds(false));
        Command(foldingMenu, "모두 펼치기", Keys.Alt | Keys.Shift | Keys.D0, s => s.SetAllFolds(true));
        foldingMenu.DropDownItems.Add(new ToolStripSeparator());
        var collapseLevel = new ToolStripMenuItem("단계별 접기");
        var expandLevel = new ToolStripMenuItem("단계별 펼치기");
        foldingMenu.DropDownItems.AddRange([collapseLevel, expandLevel]);
        for (int depth = 1; depth <= 8; depth++)
        {
            int level = depth;
            var digit = (Keys)((int)Keys.D0 + depth);
            Command(collapseLevel, $"{depth}단계", Keys.Alt | digit, s => s.SetFoldLevel(level, false));
            Command(expandLevel, $"{depth}단계", Keys.Alt | Keys.Shift | digit, s => s.SetFoldLevel(level, true));
        }
        view.DropDownOpening += (_, _) => UpdateFoldingUi();
    }

    void UpdateFoldingUi()
    {
        bool enabled = CanFoldCurrent;
        if (foldingMenu.Enabled != enabled) foldingMenu.Enabled = enabled;
    }

    void ApplyFold(Action<Sci> action)
    {
        if (!CanFoldCurrent) return;
        var editor = Current!.Editor!;
        action(editor);
        editor.Focus();
    }
}

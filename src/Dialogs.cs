namespace LHPad;

internal static class Dialogs
{
    public static string? Input(IWin32Window owner, string title, string label, string initial = "")
    {
        using var dialog = new InputDialog(title, label, initial);
        return dialog.ShowDialog(owner) == DialogResult.OK ? dialog.Value : null;
    }
    public static (bool Number, string Text, long Start, long Step)? Column(IWin32Window owner)
    {
        using var dialog = new ColumnDialog();
        return dialog.ShowDialog(owner) == DialogResult.OK ? dialog.Value : null;
    }
    public static void About(IWin32Window owner)
    {
        using var dialog = new AboutDialog();
        dialog.ShowDialog(owner);
    }
    public static DialogResult Message(string text, string title = "LHPad", MessageBoxButtons buttons = MessageBoxButtons.OK, MessageBoxIcon icon = MessageBoxIcon.None)
        => Message(Form.ActiveForm, text, title, buttons, icon);
    public static DialogResult Message(IWin32Window? owner, string text, string title = "LHPad", MessageBoxButtons buttons = MessageBoxButtons.OK, MessageBoxIcon icon = MessageBoxIcon.None)
    {
        using var dialog = new MessageDialog(text, title, buttons, icon);
        return owner == null ? dialog.ShowDialog() : dialog.ShowDialog(owner);
    }
}

internal sealed class InputDialog : StyledDialog
{
    readonly TextBox input;
    public string Value => input.Text;
    public InputDialog(string title, string label, string initial = "") : base(title, 448)
    {
        DialogStyle.Add(Body, DialogStyle.Label(label, ContentWidth));
        input = new() { Text = initial, Dock = DockStyle.Fill, Margin = new(0, 0, 0, 8) };
        DialogStyle.Add(Body, input);
        var ok = DialogStyle.Button("확인", DialogResult.OK);
        var cancel = DialogStyle.Button("취소", DialogResult.Cancel);
        DialogStyle.Add(Body, DialogStyle.Footer(true, cancel, ok));
        AcceptButton = ok;
        CancelButton = cancel;
        Shown += (_, _) => { input.Focus(); input.SelectAll(); };
    }
}

internal sealed class ColumnDialog : StyledDialog
{
    readonly CheckBox number = new() { Text = "숫자 연속 입력", AutoSize = true, Margin = new(0, 0, 0, 8) };
    readonly TextBox text = new() { PlaceholderText = "선택한 모든 행에 넣을 문자열", Dock = DockStyle.Fill, Margin = new(0, 0, 0, 12) };
    readonly NumericUpDown start = new() { Minimum = -1000000000, Maximum = 1000000000, Dock = DockStyle.Fill, Margin = Padding.Empty, Enabled = false };
    readonly NumericUpDown step = new() { Minimum = -1000000000, Maximum = 1000000000, Value = 1, Dock = DockStyle.Fill, Margin = Padding.Empty, Enabled = false };
    public (bool Number, string Text, long Start, long Step) Value => (number.Checked, text.Text, (long)start.Value, (long)step.Value);
    public ColumnDialog() : base("열 편집기", 448)
    {
        DialogStyle.Add(Body, number);
        DialogStyle.Add(Body, text);
        var values = new TableLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 2, Margin = new(0, 0, 0, 8) };
        values.ColumnStyles.Add(new(SizeType.Percent, 50));
        values.ColumnStyles.Add(new(SizeType.Percent, 50));
        values.RowStyles.Add(new(SizeType.AutoSize));
        values.RowStyles.Add(new(SizeType.AutoSize));
        values.Controls.Add(DialogStyle.Label("시작 값"), 0, 0);
        var incrementLabel = DialogStyle.Label("증가 값");
        incrementLabel.Margin = new(8, 0, 0, 8);
        values.Controls.Add(incrementLabel, 1, 0);
        start.Margin = new(0, 0, 8, 0);
        step.Margin = new(8, 0, 0, 0);
        values.Controls.Add(start, 0, 1);
        values.Controls.Add(step, 1, 1);
        DialogStyle.Add(Body, values);
        number.CheckedChanged += (_, _) => { text.Enabled = !number.Checked; start.Enabled = step.Enabled = number.Checked; };
        var apply = DialogStyle.Button("적용", DialogResult.OK);
        var cancel = DialogStyle.Button("취소", DialogResult.Cancel);
        DialogStyle.Add(Body, DialogStyle.Footer(true, cancel, apply));
        AcceptButton = apply;
        CancelButton = cancel;
    }
}

internal sealed class AboutDialog : StyledDialog
{
    readonly Bitmap artwork;
    readonly Font titleFont = new("맑은 고딕", 13, FontStyle.Bold);
    public AboutDialog() : base("LHPad 정보", 480)
    {
        using (var icon = new Icon(Path.Combine(AppContext.BaseDirectory, "data", "LHPad.ico"), 256, 256)) artwork = icon.ToBitmap();
        var picture = new PictureBox { Image = artwork, Size = new(128, 128), SizeMode = PictureBoxSizeMode.Zoom, Anchor = AnchorStyles.None, Margin = new(0, 0, 0, 12), AccessibleName = "LHPad 타자기 아이콘" };
        DialogStyle.Add(Body, picture);
        string version = typeof(MainForm).Assembly.GetName().Version?.ToString(3) ?? "";
        var heading = DialogStyle.Label("LHPad " + version);
        heading.Font = titleFont;
        heading.TextAlign = ContentAlignment.MiddleCenter;
        DialogStyle.Add(Body, heading);
        var subtitle = DialogStyle.Label("Windows 텍스트 · Hex 편집기", ContentWidth, true);
        subtitle.TextAlign = ContentAlignment.MiddleCenter;
        subtitle.Margin = new(0, 0, 0, 16);
        DialogStyle.Add(Body, subtitle);
        var info = DialogStyle.Label("Scintilla + Lexilla 기반\nNotepad++의 언어·스타일 데이터를 사용합니다.\nHex 화면은 HxD의 구성을 참고했습니다.\n\nGPL-3.0-or-later\nLICENSE · THIRD-PARTY-NOTICES.txt 참조", ContentWidth);
        info.TextAlign = ContentAlignment.MiddleCenter;
        DialogStyle.Add(Body, info);
        var shortcuts = DialogStyle.Label("Alt+드래그: 세로 선택 · Ctrl+클릭: 다중 커서\nCtrl+탭 클릭: 비교할 탭 2개 선택\nAlt+0 / Alt+Shift+0: 코드 모두 접기 / 펼치기", ContentWidth, true);
        shortcuts.TextAlign = ContentAlignment.MiddleCenter;
        DialogStyle.Add(Body, shortcuts);
        var close = DialogStyle.Button("확인", DialogResult.OK);
        DialogStyle.Add(Body, DialogStyle.Footer(true, close));
        AcceptButton = CancelButton = close;
    }
    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);
        if (disposing) { artwork.Dispose(); titleFont.Dispose(); }
    }
}

internal sealed class MessageDialog : StyledDialog
{
    readonly Bitmap? symbol;
    readonly DialogResult closeResult;
    public MessageDialog(string text, string title, MessageBoxButtons buttons = MessageBoxButtons.OK, MessageBoxIcon icon = MessageBoxIcon.None) : base(title, 480)
    {
        Icon? systemIcon = icon switch { MessageBoxIcon.Error => SystemIcons.Error, MessageBoxIcon.Warning => SystemIcons.Warning, MessageBoxIcon.Question => SystemIcons.Question, MessageBoxIcon.Information => SystemIcons.Information, _ => null };
        int symbolWidth = systemIcon == null ? 0 : 48;
        var content = new TableLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, ColumnCount = symbolWidth == 0 ? 1 : 2, RowCount = 1, Margin = new(0, 0, 0, 8) };
        if (systemIcon != null)
        {
            symbol = systemIcon.ToBitmap();
            content.ColumnStyles.Add(new(SizeType.Absolute, symbolWidth));
            content.Controls.Add(new PictureBox { Image = symbol, Size = new(32, 32), SizeMode = PictureBoxSizeMode.Zoom, Margin = new(0, 0, 16, 0) }, 0, 0);
        }
        content.ColumnStyles.Add(new(SizeType.Percent, 100));
        content.RowStyles.Add(new(SizeType.AutoSize));
        int textWidth = ContentWidth - symbolWidth - 20;
        var label = DialogStyle.Label(text, textWidth);
        label.Dock = DockStyle.Top;
        label.Margin = Padding.Empty;
        label.Font = DialogStyle.Font;
        int height = Math.Max(32, label.GetPreferredSize(new(textWidth, 0)).Height);
        var scroll = new Panel { AutoScroll = true, Dock = DockStyle.Fill, Height = Math.Min(360, height), Margin = Padding.Empty };
        scroll.Controls.Add(label);
        content.Controls.Add(scroll, symbolWidth == 0 ? 0 : 1, 0);
        DialogStyle.Add(Body, content);
        (string, DialogResult)[] choices = buttons switch
        {
            MessageBoxButtons.YesNoCancel => [("예", DialogResult.Yes), ("아니요", DialogResult.No), ("취소", DialogResult.Cancel)],
            MessageBoxButtons.YesNo => [("예", DialogResult.Yes), ("아니요", DialogResult.No)],
            MessageBoxButtons.OKCancel => [("확인", DialogResult.OK), ("취소", DialogResult.Cancel)],
            MessageBoxButtons.RetryCancel => [("다시 시도", DialogResult.Retry), ("취소", DialogResult.Cancel)],
            MessageBoxButtons.AbortRetryIgnore => [("중단", DialogResult.Abort), ("다시 시도", DialogResult.Retry), ("무시", DialogResult.Ignore)],
            _ => [("확인", DialogResult.OK)]
        };
        var actions = choices.Select(choice => DialogStyle.Button(choice.Item1, choice.Item2)).ToArray();
        DialogStyle.Add(Body, DialogStyle.Footer(true, actions.Reverse().ToArray()));
        AcceptButton = actions[0];
        CancelButton = actions[^1];
        closeResult = choices[^1].Item2;
        Shown += (_, _) => actions[0].Focus();
    }
    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        // Form.Close assigns Cancel before raising FormClosing. For dialogs without
        // a Cancel action, map that window-close result to their negative/close action.
        if (DialogResult == DialogResult.None || DialogResult == DialogResult.Cancel && closeResult != DialogResult.Cancel)
            DialogResult = closeResult;
        base.OnFormClosing(e);
    }
    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);
        if (disposing) symbol?.Dispose();
    }
}

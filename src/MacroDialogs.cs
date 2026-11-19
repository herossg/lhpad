namespace LHPad;

internal sealed class MacroRunDialog : StyledDialog
{
    readonly ComboBox selection = new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
    readonly NumericUpDown count = new() { Minimum = 1, Maximum = 1000000, Value = 10, Width = 160 };
    readonly RadioButton numbered = new() { Text = "지정 횟수만큼 실행", Checked = true, AutoSize = true };
    readonly RadioButton end = new() { Text = "현재 위치부터 문서 끝까지", AutoSize = true };
    readonly List<(string Label, IReadOnlyList<MacroStep> Steps)> choices = [];
    public IReadOnlyList<MacroStep> Steps => choices[selection.SelectedIndex].Steps;
    public int Times => (int)count.Value;
    public bool UntilEnd => end.Checked;
    public MacroRunDialog(MacroController macros) : base("매크로 여러 번 실행", 480)
    {
        DialogStyle.Add(Body, DialogStyle.Label("실행할 매크로"));
        if (macros.Last.Count > 0)
            choices.Add(($"현재 녹화 ({macros.Last.Count:N0}단계)", macros.Last));
        choices.AddRange(macros.Saved.Select(m => (m.Name, (IReadOnlyList<MacroStep>)m.Steps)));
        selection.Items.AddRange(choices.Select(c => (object)c.Label).ToArray());
        if (choices.Count > 0)
            selection.SelectedIndex = 0;
        selection.Margin = new Padding(0, 0, 0, 12);
        DialogStyle.Add(Body, selection);
        var repeat = new TableLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 2, Margin = new Padding(0, 0, 0, 12) };
        repeat.ColumnStyles.Add(new(SizeType.Percent, 100));
        repeat.ColumnStyles.Add(new(SizeType.Absolute, 132));
        repeat.RowStyles.Add(new(SizeType.AutoSize));
        repeat.RowStyles.Add(new(SizeType.AutoSize));
        numbered.Anchor = AnchorStyles.Left;
        numbered.Margin = new Padding(0, 0, 8, 8);
        count.Dock = DockStyle.Fill;
        count.Margin = new Padding(0, 0, 0, 8);
        repeat.Controls.Add(numbered, 0, 0);
        repeat.Controls.Add(count, 1, 0);
        end.Margin = Padding.Empty;
        repeat.Controls.Add(end, 0, 1);
        repeat.SetColumnSpan(end, 2);
        DialogStyle.Add(Body, repeat);
        numbered.CheckedChanged += (_, _) => count.Enabled = numbered.Checked;
        DialogStyle.Add(Body, DialogStyle.Label("문서 끝까지 실행하려면 녹화에 다음 줄 이동이나 검색을 포함하세요. 진행이 없거나 검색 결과가 없으면 중지합니다. 실행 중 Esc로 중지할 수 있습니다.", ContentWidth, true));
        var cancel = DialogStyle.Button("취소", DialogResult.Cancel);
        var run = DialogStyle.Button("실행", DialogResult.OK);
        run.Enabled = choices.Count > 0;
        DialogStyle.Add(Body, DialogStyle.Footer(true, cancel, run));
        AcceptButton = run;
        CancelButton = cancel;
    }
}

internal sealed class MacroManagerDialog : StyledDialog
{
    readonly ListBox list = new() { Dock = DockStyle.Fill, Height = 184, IntegralHeight = false, Margin = new Padding(0, 0, 0, 8) };
    public MacroManagerDialog(MacroController macros) : base("저장된 매크로 관리", 448)
    {
        DialogStyle.Add(Body, DialogStyle.Label("저장된 매크로"));
        DialogStyle.Add(Body, list);
        var close = DialogStyle.Button("닫기", DialogResult.OK);
        var remove = DialogStyle.Button("삭제");
        var rename = DialogStyle.Button("이름 변경");
        DialogStyle.Add(Body, DialogStyle.Footer(true, close, remove, rename));
        AcceptButton = close;
        CancelButton = close;
        void Populate()
        {
            list.Items.Clear();
            list.Items.AddRange(macros.Saved.Cast<object>().ToArray());
            int listHeight = Math.Clamp(list.Items.Count * 20 + 8, 80, 184);
            list.Height = IsHandleCreated ? LogicalToDeviceUnits(listHeight) : listHeight;
            if (list.Items.Count > 0)
                list.SelectedIndex = 0;
            rename.Enabled = remove.Enabled = list.Items.Count > 0;
        }
        rename.Click += (_, _) => { if (list.SelectedItem is not SavedMacro macro) return; string? name = Dialogs.Input(this, "매크로 이름 변경", "새 이름", macro.Name); if (name == null) return; try { macros.Rename(macro.Id, name); Populate(); } catch (Exception ex) { Dialogs.Message(this, ex.Message, "매크로"); } };
        remove.Click += (_, _) => { if (list.SelectedItem is SavedMacro macro) { macros.Delete(macro.Id); Populate(); } };
        Populate();
    }
}

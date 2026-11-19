namespace LHPad;

internal static class DialogUiTests
{
    static IEnumerable<Control> Descendants(Control root)
    {
        foreach (Control child in root.Controls)
        {
            yield return child;
            foreach (var nested in Descendants(child)) yield return nested;
        }
    }
    static async Task Inspect(Form host, StyledDialog dialog, string output, string name, Action<bool, string> check)
    {
        dialog.Show(host);
        await Task.Delay(100);
        var buttons = Descendants(dialog).OfType<Button>().Where(b => b.Visible).ToArray();
        SelfTests.Screenshot(dialog, Path.Combine(output, name + ".png"));
        check(buttons.Length > 0 && buttons.All(b => b.Height == dialog.LogicalToDeviceUnits(DialogStyle.ButtonHeight)), $"{name}: every action button has the shared DPI-scaled height (DPI {dialog.DeviceDpi}, expected {dialog.LogicalToDeviceUnits(DialogStyle.ButtonHeight)}, actual {string.Join(", ", buttons.Select(b => b.Text + ":" + b.Height))})");
        check(buttons.All(b => dialog.ClientRectangle.Contains(dialog.RectangleToClient(b.RectangleToScreen(b.ClientRectangle))))
            && buttons.All(b => b.Parent!.ClientRectangle.Contains(b.Bounds)), $"{name}: action buttons fit inside their rows and the dialog without clipping");
        var rows = dialog.Body.Controls.Cast<Control>().Where(c => c.Visible).ToArray();
        int bottom = rows.Max(c => c.Bottom + c.Margin.Bottom);
        check(Math.Abs(dialog.ClientSize.Height - bottom - dialog.Body.Padding.Bottom) <= 3, $"{name}: dialog height follows the content without an unused bottom area");
        dialog.Hide();
    }
    public static async Task RunAsync(Form host, string folder, string output, Action<bool, string> check)
    {
        using var app = new MainForm(Path.Combine(folder, "dialogs-profile"), []);
        app.Show(host);
        await app.Ready;
        app.Current!.Editor!.SetText("alpha beta alpha");
        using (var find = new FindForm(app))
        {
            await Inspect(app, find, output, "dialog-find-text", check);
            int shortHeight = find.Height;
            find.SetQuery("alpha");
            await find.CountMatchesAsync();
            find.Show(app);
            await Task.Delay(60);
            check(find.ResultText.Contains("2개") && find.Height <= shortHeight + find.LogicalToDeviceUnits(40), "Find count remains visible in the compact text dialog");
            find.Hide();
            string binary = Path.Combine(folder, "dialogs.bin");
            await File.WriteAllBytesAsync(binary, "MZ alpha alpha"u8.ToArray());
            await app.OpenAsync(binary, true);
            find.UpdateSearchContext();
            await Inspect(app, find, output, "dialog-find-hex", check);
            check(find.Height > shortHeight && find.Height < shortHeight + 150, "Hex search adds only the format and hint rows to the compact dialog");
            find.SetHexSearchFormat(0);
            find.SetQuery("not hex");
            await find.CountMatchesAsync();
            await Inspect(app, find, output, "dialog-find-validation", check);
            check(find.ResultText.Contains("16진수"), "Long Hex validation feedback stays available after the layout change");
        }
        using (var input = new InputDialog("줄 이동", "이동할 줄 번호", "1"))
        {
            await Inspect(app, input, output, "dialog-go-to", check);
            check(input.Value == "1" && input.AcceptButton is Button { DialogResult: DialogResult.OK } && input.CancelButton is Button { DialogResult: DialogResult.Cancel }, "Input dialog retains initial value and Enter/Escape actions");
        }
        using (var column = new ColumnDialog())
        {
            await Inspect(app, column, output, "dialog-column", check);
            Descendants(column).OfType<CheckBox>().Single().Checked = true;
            check(column.Value.Number && Descendants(column).OfType<NumericUpDown>().All(n => n.Enabled), "Column editor still switches between string and numeric entry");
        }
        app.Macros.Start();
        app.NewDocument().Editor!.Str(S.SCI_REPLACESEL, "example");
        app.Macros.Stop();
        app.Macros.SaveLast("예제 매크로");
        using (var repeat = new MacroRunDialog(app.Macros))
        {
            await Inspect(app, repeat, output, "dialog-macro-repeat", check);
            Descendants(repeat).OfType<RadioButton>().Single(r => r.Text.Contains("문서 끝")).Checked = true;
            check(repeat.UntilEnd && !Descendants(repeat).OfType<NumericUpDown>().Single().Enabled && repeat.Steps.Count > 0, "Macro repeat keeps EOF mode, count disabling and selected steps");
        }
        using (var manager = new MacroManagerDialog(app.Macros)) await Inspect(app, manager, output, "dialog-macro-manager", check);
        using (var about = new AboutDialog())
        {
            await Inspect(app, about, output, "dialog-about", check);
            var picture = Descendants(about).OfType<PictureBox>().Single();
            check(picture.Image != null && picture.Width == about.LogicalToDeviceUnits(128) && picture.Height == picture.Width
                && picture.Bottom < Descendants(about).OfType<Label>().Min(l => l.Top), "About dialog shows a large centered app icon above the information");
        }
        using (var message = new MessageDialog("문서가 외부에서 변경되었습니다.\n디스크의 최신 내용으로 다시 불러올까요?", "외부 변경", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question))
        {
            await Inspect(app, message, output, "dialog-confirm", check);
            check(message.AcceptButton is Button { DialogResult: DialogResult.Yes } && message.CancelButton is Button { DialogResult: DialogResult.Cancel }, "Confirmation dialog preserves its explicit Yes and Cancel results");
        }
        using (var message = new MessageDialog(string.Join("\n", Enumerable.Repeat("복원 파일을 읽지 못했습니다. 원본 파일 경로와 인코딩을 확인해 주세요.", 35)), "복원 오류", MessageBoxButtons.OK, MessageBoxIcon.Warning))
        {
            await Inspect(app, message, output, "dialog-long-message", check);
            check(message.Height <= message.LogicalToDeviceUnits(500) && Descendants(message).OfType<Panel>().Any(p => p.AutoScroll && p.VerticalScroll.Visible), "Lengthy messages use scrolling instead of an oversized window");
        }
        foreach (var expected in new[] { DialogResult.Yes, DialogResult.No, DialogResult.Cancel })
        {
            using var message = new MessageDialog("변경 내용을 저장할까요?", "탭 닫기", MessageBoxButtons.YesNoCancel);
            bool primaryFocused = false;
            message.Shown += (_, _) => message.BeginInvoke(() =>
            {
                primaryFocused = Descendants(message).OfType<Button>().Single(b => b.DialogResult == DialogResult.Yes).Focused;
                Descendants(message).OfType<Button>().Single(b => b.DialogResult == expected).PerformClick();
            });
            var response = message.ShowDialog(app);
            check(primaryFocused && response == expected, $"Modal confirmation focuses its primary action and returns the selected {expected} result");
        }
        using (var message = new MessageDialog("외부 변경을 다시 불러올까요?", "외부 변경", MessageBoxButtons.YesNo))
        {
            message.Shown += (_, _) => message.BeginInvoke(message.Close);
            check(message.ShowDialog(app) == DialogResult.No, "Closing a Yes/No confirmation does not approve the action");
        }
        var closed = new TaskCompletionSource();
        app.FormClosed += (_, _) => closed.TrySetResult();
        app.Close();
        await closed.Task.WaitAsync(TimeSpan.FromSeconds(15));
    }
}

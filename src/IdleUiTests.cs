namespace LHPad;

internal static class IdleUiTests
{
    public static async Task RunAsync(Form host, string folder, Action<bool, string> check)
    {
        using var app = new MainForm(Path.Combine(folder, "idle-ui-profile"), []);
        app.Show(host);
        await app.Ready;
        var editor = app.Current!.Editor!;
        editor.Focus();
        await Task.Delay(250);
        var tabs = app.Controls.OfType<TabControl>().Single();
        var toolbar = app.Controls.OfType<ToolStrip>().Single(c => c is not MenuStrip && c is not StatusStrip);
        int invalidations = 0, enabledChanges = 0;
        tabs.Invalidated += (_, _) => invalidations++;
        foreach (ToolStripItem item in toolbar.Items) item.EnabledChanged += (_, _) => enabledChanges++;
        await Task.Delay(2300); // Two real monitor timer ticks, with the native caret still blinking.
        check(invalidations == 0, $"Idle editor leaves tab headers unchanged across timer ticks (invalidations={invalidations})");
        check(enabledChanges == 0, $"Idle toolbar has no transient enable/disable changes (changes={enabledChanges})");
        editor.Str(S.SCI_REPLACESEL, "first line\nsecond line");
        await Task.Delay(80);
        check(app.Current.Dirty && invalidations > 0, "Editing still refreshes the document's unsaved tab indicator");
        editor.Saved();
        app.UpdateStatus();
        await Task.Delay(80);
        check(!app.Current.Dirty, "Saved state still clears the unsaved tab indicator");
        invalidations = enabledChanges = 0;
        editor.Select(2, 2);
        await Task.Delay(120);
        check(invalidations == 0 && enabledChanges == 0, "Caret movement updates position without repainting tab headers or toggling toolbar controls");
        app.Macros.Start();
        var record = toolbar.Items.OfType<ToolStripButton>().Single(b => b.Text == "녹화");
        var activeImage = record.Image;
        app.Macros.Stop();
        check(!ReferenceEquals(activeImage, record.Image), "Recording still changes the button's status color");
        string logPath = Path.Combine(folder, "idle.log");
        await File.WriteAllTextAsync(logPath, "initial log\n", new System.Text.UTF8Encoding(false));
        await app.OpenAsync(logPath);
        var log = app.Current!;
        await app.StartMonitoringAsync(log);
        await Task.Delay(200);
        invalidations = enabledChanges = 0;
        await Task.Delay(2300);
        check(invalidations == 0 && enabledChanges == 0, "An unchanged monitored log leaves tab headers and toolbar state stable");
        await File.AppendAllTextAsync(logPath, "appended entry\n");
        await app.PollMonitorsAsync();
        await Task.Delay(80);
        check(log.Editor!.AllText().EndsWith("appended entry\n") && !log.Dirty && invalidations == 0, "Live log append updates content without flashing an unsaved tab icon");
        app.StopMonitoring(log);
        var closed = new TaskCompletionSource();
        app.FormClosed += (_, _) => closed.TrySetResult();
        app.Close();
        await closed.Task;
    }
}

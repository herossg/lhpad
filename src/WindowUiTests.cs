namespace LHPad;

internal static class WindowUiTests
{
    static async Task CloseAsync(Form app)
    {
        var closed = new TaskCompletionSource();
        app.FormClosed += (_, _) => closed.TrySetResult();
        app.Close();
        await closed.Task.WaitAsync(TimeSpan.FromSeconds(15));
    }
    public static async Task RunAsync(Form host, string folder, string output, Action<bool, string> check)
    {
        using (var images = new UiImages())
        {
            foreach (int size in new[] { 16, 20, 24, 30, 32, 40 })
            {
                foreach (bool dirty in new[] { false, true })
                {
                    var icon = images.DocumentIcon(dirty, size);
                    bool margin = true;
                    for (int i = 0; i < size; i++)
                        margin &= icon.GetPixel(i, 0).A == 0 && icon.GetPixel(i, size - 1).A == 0 && icon.GetPixel(0, i).A == 0 && icon.GetPixel(size - 1, i).A == 0;
                    int opaque = 0, mint = 0, rose = 0, ivory = 0, dark = 0;
                    int left = size, top = size, right = -1, bottom = -1;
                    int cornerSize = Math.Max(2, size / 5), cornerPixels = 0, emptyCornerPixels = 0;
                    for (int y = 0; y < size; y++)
                    {
                        for (int x = 0; x < size; x++)
                        {
                            var pixel = icon.GetPixel(x, y);
                            if (y < cornerSize && (x < cornerSize || x >= size - cornerSize))
                            {
                                cornerPixels++;
                                if (pixel.A < 32) emptyCornerPixels++;
                            }
                            if (pixel.A < 128) continue;
                            opaque++;
                            left = Math.Min(left, x); top = Math.Min(top, y);
                            right = Math.Max(right, x); bottom = Math.Max(bottom, y);
                            if (pixel.G >= 130 && pixel.B >= 140 && pixel.G - pixel.R >= 12 && pixel.B - pixel.R >= 12) mint++;
                            if (pixel.R >= 170 && pixel.G >= 90 && pixel.B >= 90 && pixel.R - pixel.G >= 18 && pixel.R - pixel.B >= 8) rose++;
                            int brightest = Math.Max(pixel.R, Math.Max(pixel.G, pixel.B));
                            int darkest = Math.Min(pixel.R, Math.Min(pixel.G, pixel.B));
                            if (pixel.R >= 180 && pixel.G >= 175 && pixel.B >= 155 && brightest - darkest <= 65) ivory++;
                            if (brightest < 135) dark++;
                        }
                    }
                    int area = size * size;
                    bool visibleSilhouette = opaque >= area / 6 && left > 0 && top > 0 && right < size - 1 && bottom < size - 1
                        && right - left + 1 >= size * .55 && bottom - top + 1 >= size * .55;
                    check(margin && emptyCornerPixels >= cornerPixels * .9 && visibleSilhouette,
                        $"{size}px {(dirty ? "unsaved" : "saved")} typewriter has transparent outer edges and upper corners with a large unclipped silhouette");
                    int bodyColor = dirty ? rose : mint, otherBodyColor = dirty ? mint : rose;
                    check(bodyColor >= Math.Max(3, area / 40) && bodyColor > otherBodyColor * 2 && ivory >= Math.Max(3, area / 40) && dark >= Math.Max(2, area / 80),
                        $"{size}px {(dirty ? "unsaved" : "saved")} typewriter keeps its {(dirty ? "pastel rose" : "pastel mint-blue")} body, light paper/keys and dark contours");
                }
            }
        }
        var primary = new Rectangle(0, 0, 1920, 1040);
        var offscreen = WindowPlacementStore.VisibleBounds(new(50000, 60000, 4000, 3000, false), [primary], new(800, 500));
        check(primary.Contains(offscreen) && offscreen.Size == primary.Size, "A removed or smaller monitor brings the saved window fully into the current working area");
        var secondary = new Rectangle(-1600, 0, 1600, 900);
        var negative = WindowPlacementStore.VisibleBounds(new(-1500, 50, 1200, 800, false), [primary, secondary], new(800, 500));
        check(negative == new Rectangle(-1500, 50, 1200, 800), "A still-connected secondary monitor preserves valid negative screen coordinates");
        string backupProfile = Path.Combine(folder, "window-backup-profile");
        var settings = new WindowPlacementStore(backupProfile);
        var first = new WindowPlacement(30, 40, 1100, 700, false);
        settings.Save(first);
        settings.Save(first with { Width = 1250, Maximized = true });
        File.WriteAllText(Path.Combine(backupProfile, "window.json"), "invalid JSON");
        check(settings.Read() == first, "Damaged window settings recover from the previous atomic backup");

        string profile = Path.Combine(folder, "window-ui-profile");
        Rectangle normal;
        using (var app = new MainForm(profile, []))
        {
            app.Show(host);
            await app.Ready;
            var area = Screen.FromControl(app).WorkingArea;
            app.WindowState = FormWindowState.Normal;
            app.Bounds = new(area.Left + 30, area.Top + 30, Math.Min(1380, area.Width - 60), Math.Min(820, area.Height - 60));
            await Task.Delay(100);
            normal = app.Bounds;
            app.Current!.Editor!.Str(S.SCI_REPLACESEL, "저장 전 문서");
            var saved = app.NewDocument("README.md");
            saved.Editor!.SetText("# LHPad\r\n");
            saved.Editor.Saved();
            app.UpdateStatus();
            await Task.Delay(100);
            SelfTests.Screenshot(app, Path.Combine(output, "window-tabs-preview.png"));
            await CloseAsync(app);
        }
        check(new WindowPlacementStore(profile).Read() is { Maximized: false } stored && new Rectangle(stored.X, stored.Y, stored.Width, stored.Height) == normal, "Normal window dimensions and position are written on actual application exit");
        using (var app = new MainForm(profile, []))
        {
            app.Show(host);
            await app.Ready;
            check(app.WindowState == FormWindowState.Normal && app.Bounds == normal, "A real application restart restores the last normal window size and position");
            check(app.Documents.Any(d => d.Dirty && d.Editor!.AllText() == "저장 전 문서"), "Window restoration keeps existing unsaved-tab session recovery working");
            app.WindowState = FormWindowState.Maximized;
            await Task.Delay(120);
            check(app.RestoreBounds == normal, "Maximizing retains the original normal restore bounds");
            await CloseAsync(app);
        }
        using (var app = new MainForm(profile, []))
        {
            app.Show(host);
            await app.Ready;
            await Task.Delay(100);
            check(app.WindowState == FormWindowState.Maximized && app.RestoreBounds == normal, "A real application restart restores maximized state and its normal restore bounds");
            app.WindowState = FormWindowState.Minimized;
            await Task.Delay(80);
            await CloseAsync(app);
        }
        check(new WindowPlacementStore(profile).Read()?.Maximized == true, "Closing while minimized preserves the previous maximized state");
        using (var app = new MainForm(profile, []))
        {
            app.Show(host);
            await app.Ready;
            check(app.WindowState == FormWindowState.Maximized, "A window closed from the taskbar reopens visibly instead of starting minimized");
            app.WindowState = FormWindowState.Normal;
            await Task.Delay(80);
            check(app.Bounds == normal, "Restoring a maximized window returns to the user's previous normal dimensions");
            await CloseAsync(app);
        }
    }
}

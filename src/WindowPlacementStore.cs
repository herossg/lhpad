using System.Text.Json;

namespace LHPad;

internal sealed record WindowPlacement(int X, int Y, int Width, int Height, bool Maximized, int Version = 1);

internal sealed class WindowPlacementStore(string profile)
{
    readonly string path = Path.Combine(profile, "window.json");
    public WindowPlacement? Read()
    {
        foreach (var file in new[] { path, path + ".bak" })
        {
            try
            {
                if (!File.Exists(file)) continue;
                var saved = JsonSerializer.Deserialize<WindowPlacement>(File.ReadAllText(file));
                if (saved is { Version: 1, Width: >= 200 and <= 100000, Height: >= 100 and <= 100000 }) return saved;
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            catch (JsonException) { }
        }
        return null;
    }
    public void Save(WindowPlacement placement)
    {
        Directory.CreateDirectory(profile);
        string temporary = path + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(placement));
        using (var file = new FileStream(temporary, FileMode.Open, FileAccess.Write, FileShare.None)) file.Flush(true);
        if (File.Exists(path)) File.Replace(temporary, path, path + ".bak", true);
        else File.Move(temporary, path);
    }
    public static WindowPlacement Capture(Form form, FormWindowState lastVisibleState)
    {
        var bounds = form.WindowState == FormWindowState.Normal ? form.Bounds : form.RestoreBounds;
        bool maximized = form.WindowState == FormWindowState.Maximized || form.WindowState == FormWindowState.Minimized && lastVisibleState == FormWindowState.Maximized;
        return new(bounds.X, bounds.Y, bounds.Width, bounds.Height, maximized);
    }
    public static Rectangle VisibleBounds(WindowPlacement saved, IReadOnlyList<Rectangle> workingAreas, Size minimum)
    {
        if (workingAreas.Count == 0) return new(0, 0, Math.Max(minimum.Width, saved.Width), Math.Max(minimum.Height, saved.Height));
        var target = workingAreas[0];
        long best = 0;
        foreach (var area in workingAreas)
        {
            long width = Math.Max(0, Math.Min((long)saved.X + saved.Width, area.Right) - Math.Max((long)saved.X, area.Left));
            long height = Math.Max(0, Math.Min((long)saved.Y + saved.Height, area.Bottom) - Math.Max((long)saved.Y, area.Top));
            if (width * height <= best) continue;
            best = width * height;
            target = area;
        }
        int w = Math.Clamp(saved.Width, Math.Min(minimum.Width, target.Width), target.Width);
        int h = Math.Clamp(saved.Height, Math.Min(minimum.Height, target.Height), target.Height);
        return new(Math.Clamp(saved.X, target.Left, target.Right - w), Math.Clamp(saved.Y, target.Top, target.Bottom - h), w, h);
    }
    public void Restore(Form form)
    {
        if (Read() is not { } saved) return;
        // Primary comes first so a removed monitor falls back to a visible primary work area.
        var areas = Screen.AllScreens.OrderByDescending(s => s.Primary).Select(s => s.WorkingArea).ToArray();
        var bounds = VisibleBounds(saved, areas, form.MinimumSize);
        form.StartPosition = FormStartPosition.Manual;
        form.Bounds = bounds;
        form.WindowState = saved.Maximized ? FormWindowState.Maximized : FormWindowState.Normal;
    }
}

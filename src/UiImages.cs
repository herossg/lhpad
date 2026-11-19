using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace LHPad;

internal sealed class UiImages : IDisposable
{
    public Bitmap SavedDocument { get; } = Load("tab-saved.png");
    public Bitmap UnsavedDocument { get; } = Load("tab-unsaved.png");
    public Bitmap RecordIdle { get; } = Circle(Color.FromArgb(169, 211, 245));
    public Bitmap RecordActive { get; } = Circle(Color.FromArgb(255, 179, 179));
    readonly Dictionary<(bool Dirty, int Size), Bitmap> tabIcons = [];

    public Bitmap DocumentIcon(bool dirty, int size)
    {
        if (size < 8) throw new ArgumentOutOfRangeException(nameof(size));
        if (tabIcons.TryGetValue((dirty, size), out var cached)) return cached;
        var source = dirty ? UnsavedDocument : SavedDocument;
        // Preserve the entire transparent typewriter artwork. A device-pixel gutter
        // keeps its contours clear of the tab boundary at every DPI.
        var destination = new Rectangle(1, 1, size - 2, size - 2);
        var icon = new Bitmap(size, size, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(icon))
        using (var attributes = new ImageAttributes())
        {
            g.Clear(Color.Transparent);
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.SetClip(destination);
            attributes.SetWrapMode(WrapMode.TileFlipXY);
            g.DrawImage(source, destination, 0, 0, source.Width, source.Height, GraphicsUnit.Pixel, attributes);
        }
        tabIcons.Add((dirty, size), icon);
        return icon;
    }

    static Bitmap Load(string name)
    {
        using var image = Image.FromFile(Path.Combine(AppContext.BaseDirectory, "data", name));
        return new Bitmap(image);
    }

    static Bitmap Circle(Color color)
    {
        var image = new Bitmap(64, 64);
        using var g = Graphics.FromImage(image);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using var brush = new SolidBrush(color);
        g.FillEllipse(brush, 6, 6, 52, 52);
        return image;
    }

    public void Dispose()
    {
        foreach (var icon in tabIcons.Values) icon.Dispose();
        tabIcons.Clear();
        SavedDocument.Dispose();
        UnsavedDocument.Dispose();
        RecordIdle.Dispose();
        RecordActive.Dispose();
    }
}

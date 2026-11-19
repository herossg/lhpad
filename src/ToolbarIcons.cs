using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace LHPad;

internal enum ToolbarIcon
{
    NewFile, Open, Save, Find, Replace, Format, Compare, Source,
    Preview, Split, Monitor, FollowTail, Stop, Play, Repeat, Help
}

/// <summary>Small, DPI-aware toolbar artwork. Returned images remain owned by this cache.</summary>
internal sealed class ToolbarIcons : IDisposable
{
    readonly Dictionary<(ToolbarIcon Icon, int Size), Bitmap> images = [];
    bool disposed;

    static readonly Color Slate = Color.FromArgb(91, 111, 130);
    static readonly Color Blue = Color.FromArgb(177, 211, 240);
    static readonly Color Mint = Color.FromArgb(181, 222, 205);
    static readonly Color Lavender = Color.FromArgb(208, 195, 234);
    static readonly Color Rose = Color.FromArgb(243, 186, 192);
    static readonly Color Yellow = Color.FromArgb(245, 221, 161);
    static readonly Color Paper = Color.FromArgb(248, 251, 253);

    public Bitmap Get(ToolbarIcon icon, int pixelSize)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (pixelSize is < 8 or > 512) throw new ArgumentOutOfRangeException(nameof(pixelSize));
        if (!Enum.IsDefined(icon)) throw new ArgumentOutOfRangeException(nameof(icon));
        if (images.TryGetValue((icon, pixelSize), out var cached)) return cached;

        var bitmap = new Bitmap(pixelSize, pixelSize, PixelFormat.Format32bppArgb);
        try
        {
            using var g = Graphics.FromImage(bitmap);
            g.Clear(Color.Transparent);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.CompositingQuality = CompositingQuality.HighQuality;
            g.ScaleTransform(pixelSize / 24f, pixelSize / 24f);
            Draw(g, icon);
            images.Add((icon, pixelSize), bitmap);
            return bitmap;
        }
        catch { bitmap.Dispose(); throw; }
    }

    static void Draw(Graphics g, ToolbarIcon icon)
    {
        switch (icon)
        {
            case ToolbarIcon.NewFile:
                Polygon(g, Paper, [new(5, 3), new(13, 3), new(18, 8), new(18, 20), new(5, 20)]);
                Polygon(g, Blue, [new(13, 3), new(13, 8), new(18, 8)]);
                Line(g, 8, 11, 13, 11);
                Line(g, 8, 14, 12, 14);
                Ellipse(g, Mint, 13.5f, 13.5f, 8, 8);
                Line(g, 17.5f, 15.5f, 17.5f, 19.5f);
                Line(g, 15.5f, 17.5f, 19.5f, 17.5f);
                break;
            case ToolbarIcon.Open:
                Polygon(g, Yellow, [new(3, 6), new(9, 6), new(11, 8), new(20, 8), new(20, 20), new(3, 20)]);
                Polygon(g, Color.FromArgb(250, 232, 188), [new(3, 10), new(21, 10), new(18.5f, 20), new(4.5f, 20)]);
                break;
            case ToolbarIcon.Save:
                Polygon(g, Blue, [new(4, 3.5f), new(17, 3.5f), new(20, 6.5f), new(20, 20.5f), new(4, 20.5f)]);
                RoundRect(g, Paper, 7, 3.5f, 9, 6.5f, 0.7f);
                RoundRect(g, Paper, 7, 13, 10, 7.5f, 0.7f);
                Line(g, 13.5f, 5.5f, 13.5f, 8);
                Line(g, 9.5f, 16, 14.5f, 16);
                Line(g, 9.5f, 18, 14.5f, 18);
                break;
            case ToolbarIcon.Find:
                Ellipse(g, Blue, 3.5f, 3.5f, 12, 12);
                Line(g, 14, 14, 20, 20, 3.1f, Slate);
                Line(g, 14.7f, 14.7f, 19.2f, 19.2f, 1.6f, Lavender);
                Arc(g, 6, 6, 7, 7, 205, 85, 1.3f, Paper);
                break;
            case ToolbarIcon.Replace:
                RoundRect(g, Rose, 3.5f, 6, 6, 7, 1);
                RoundRect(g, Mint, 14.5f, 11, 6, 7, 1);
                Line(g, 10, 5, 18, 5);
                ArrowHead(g, 18, 5, 1, 0, 2.1f);
                Line(g, 14, 20, 6, 20);
                ArrowHead(g, 6, 20, -1, 0, 2.1f);
                break;
            case ToolbarIcon.Format:
                RoundRect(g, Lavender, 3, 3.5f, 18, 17, 2);
                Line(g, 7, 7, 17, 7);
                Line(g, 10, 10.5f, 17, 10.5f);
                Line(g, 10, 14, 17, 14);
                Line(g, 7, 17.5f, 17, 17.5f);
                break;
            case ToolbarIcon.Compare:
                RoundRect(g, Blue, 3, 4, 7, 16, 1.2f);
                RoundRect(g, Rose, 14, 4, 7, 16, 1.2f);
                Line(g, 5, 8, 8, 8);
                Line(g, 5, 11, 8, 11);
                Line(g, 16, 8, 19, 8);
                Line(g, 16, 14, 19, 14);
                Line(g, 10.5f, 10, 13.5f, 10, 1.2f);
                ArrowHead(g, 13.5f, 10, 1, 0, 1.3f);
                Line(g, 13.5f, 15, 10.5f, 15, 1.2f);
                ArrowHead(g, 10.5f, 15, -1, 0, 1.3f);
                break;
            case ToolbarIcon.Source:
                RoundRect(g, Blue, 3, 4, 18, 16, 2);
                Polyline(g, [new(8, 8), new(5.5f, 12), new(8, 16)]);
                Polyline(g, [new(16, 8), new(18.5f, 12), new(16, 16)]);
                Line(g, 13.2f, 7.5f, 10.8f, 16.5f);
                break;
            case ToolbarIcon.Preview:
                using (var eye = new GraphicsPath())
                {
                    eye.AddBezier(2.8f, 12, 7, 3.8f, 17, 3.8f, 21.2f, 12);
                    eye.AddBezier(21.2f, 12, 17, 20.2f, 7, 20.2f, 2.8f, 12);
                    eye.CloseFigure();
                    FillStroke(g, eye, Mint);
                }
                Ellipse(g, Lavender, 8, 8, 8, 8);
                Ellipse(g, Slate, 10.5f, 10.5f, 3, 3, false);
                break;
            case ToolbarIcon.Split:
                RoundRect(g, Paper, 3, 4, 18, 16, 2);
                using (var fill = new SolidBrush(Mint)) g.FillRectangle(fill, 13, 5, 7, 14);
                Line(g, 12, 4, 12, 20);
                Polyline(g, [new(7.5f, 8), new(5.5f, 12), new(7.5f, 16)], 1.25f);
                Line(g, 15, 8, 18, 8, 1.25f);
                Line(g, 15, 11, 18, 11, 1.25f);
                Line(g, 15, 14, 17, 14, 1.25f);
                break;
            case ToolbarIcon.Monitor:
                RoundRect(g, Lavender, 3, 4, 18, 14, 1.5f);
                Polyline(g, [new(5.5f, 14), new(8.5f, 10), new(11.5f, 13), new(15, 8), new(18.5f, 10)], 1.6f);
                Line(g, 12, 18, 12, 21);
                Line(g, 8, 21, 16, 21);
                break;
            case ToolbarIcon.FollowTail:
                RoundRect(g, Paper, 4, 3, 16, 18, 1.5f);
                Line(g, 7, 6, 17, 6, 1.2f);
                Line(g, 7, 9, 14, 9, 1.2f);
                Polygon(g, Blue, [new(10, 11), new(14, 11), new(14, 15), new(17, 15), new(12, 19), new(7, 15), new(10, 15)]);
                break;
            case ToolbarIcon.Stop:
                RoundRect(g, Rose, 5, 5, 14, 14, 2);
                break;
            case ToolbarIcon.Play:
                Polygon(g, Mint, [new(7, 3.5f), new(20, 12), new(7, 20.5f)]);
                break;
            case ToolbarIcon.Repeat:
                using (var arrow = new GraphicsPath())
                {
                    arrow.AddArc(4, 4.5f, 16, 15, 205, 130);
                    Stroke(g, arrow, Blue, 3.3f);
                    arrow.Reset();
                    arrow.AddArc(4, 4.5f, 16, 15, 25, 130);
                    Stroke(g, arrow, Lavender, 3.3f);
                }
                Polygon(g, Blue, [new(20.3f, 5.7f), new(20.3f, 11), new(15.2f, 9.1f)], 1.1f);
                Polygon(g, Lavender, [new(3.7f, 18.3f), new(3.7f, 13), new(8.8f, 14.9f)], 1.1f);
                break;
            case ToolbarIcon.Help:
                Ellipse(g, Blue, 3, 3, 18, 18);
                using (var question = new GraphicsPath())
                {
                    question.AddBezier(9, 8.5f, 9, 5.2f, 15.5f, 5.2f, 15.5f, 9);
                    question.AddBezier(15.5f, 9, 15.5f, 11.2f, 12, 11.4f, 12, 14);
                    Stroke(g, question, Slate, 1.8f);
                }
                Ellipse(g, Slate, 11, 16, 2, 2, false);
                break;
        }
    }

    static void Line(Graphics g, float x1, float y1, float x2, float y2, float width = 1.35f, Color? color = null)
    {
        using var pen = Pen(color ?? Slate, width);
        g.DrawLine(pen, x1, y1, x2, y2);
    }

    static void Polyline(Graphics g, PointF[] points, float width = 1.35f)
    {
        using var pen = Pen(Slate, width);
        g.DrawLines(pen, points);
    }

    static void ArrowHead(Graphics g, float x, float y, float dx, float dy, float size)
    {
        Polyline(g, [new(x - dx * size - dy * size, y - dy * size + dx * size),
            new(x, y), new(x - dx * size + dy * size, y - dy * size - dx * size)], 1.25f);
    }

    static void Polygon(Graphics g, Color fill, PointF[] points, float width = 1.35f)
    {
        using var path = new GraphicsPath();
        path.AddPolygon(points);
        FillStroke(g, path, fill, width);
    }

    static void RoundRect(Graphics g, Color fill, float x, float y, float width, float height, float radius)
    {
        using var path = new GraphicsPath();
        float diameter = radius * 2;
        path.AddArc(x, y, diameter, diameter, 180, 90);
        path.AddArc(x + width - diameter, y, diameter, diameter, 270, 90);
        path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0, 90);
        path.AddArc(x, y + height - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
        FillStroke(g, path, fill);
    }

    static void Ellipse(Graphics g, Color fill, float x, float y, float width, float height, bool outline = true)
    {
        using var brush = new SolidBrush(fill);
        g.FillEllipse(brush, x, y, width, height);
        if (outline)
        {
            using var pen = Pen(Slate, 1.35f);
            g.DrawEllipse(pen, x, y, width, height);
        }
    }

    static void Arc(Graphics g, float x, float y, float width, float height, float start, float sweep, float lineWidth, Color color)
    {
        using var pen = Pen(color, lineWidth);
        g.DrawArc(pen, x, y, width, height, start, sweep);
    }

    static void FillStroke(Graphics g, GraphicsPath path, Color fill, float width = 1.35f)
    {
        using var brush = new SolidBrush(fill);
        g.FillPath(brush, path);
        Stroke(g, path, Slate, width);
    }

    static void Stroke(Graphics g, GraphicsPath path, Color color, float width)
    {
        using var pen = Pen(color, width);
        g.DrawPath(pen, path);
    }

    static Pen Pen(Color color, float width) => new(color, width)
    {
        StartCap = LineCap.Round,
        EndCap = LineCap.Round,
        LineJoin = LineJoin.Round
    };

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        foreach (var image in images.Values) image.Dispose();
        images.Clear();
    }
}

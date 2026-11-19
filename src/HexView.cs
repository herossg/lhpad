using System.Globalization;
using System.Text;

namespace LHPad;

internal sealed class HexView : Control
{
    BinaryBuffer? buffer;
    readonly VScrollBar scroll = new() { Dock = DockStyle.Right };
    public event Action? Changed;
    public string SourcePath => buffer?.SourcePath ?? "";
    public long Length => buffer?.Length ?? 0;
    public long Caret
    {
        get; private set;
    }
    public new long Anchor
    {
        get; private set;
    }
    public bool Modified => buffer?.Modified ?? false;
    public long Revision
    {
        get; private set;
    }
    public long TopRow
    {
        get; private set;
    }
    public bool InsertMode
    {
        get; private set;
    }
    public bool AsciiMode
    {
        get; private set;
    }
    bool lowNibble, syncing, dragging;
    int RowHeight => Math.Max(20, Font.Height + 5);
    int Rows => Math.Max(1, (ClientSize.Height - 40) / RowHeight);
    float Cell => TextRenderer.MeasureText("0000000000", Font, Size.Empty, TextFormatFlags.NoPadding).Width / 10f;
    float HexX => Cell * 19;
    float AsciiX => HexX + Cell * 50;
    public HexView()
    {
        Dock = DockStyle.Fill;
        Font = new Font("Consolas", 11);
        BackColor = Color.FromArgb(253, 253, 252);
        ForeColor = Color.FromArgb(39, 48, 60);
        TabStop = true;
        DoubleBuffered = true;
        SetStyle(ControlStyles.Selectable, true);
        Controls.Add(scroll);
        scroll.ValueChanged += (_, _) => { if (syncing) return; TopRow = (long)Math.Round(scroll.Value / (double)ScrollMax() * MaxTop()); Invalidate(); };
        ContextMenuStrip = new();
        ContextMenuStrip.Items.Add("복사 (Hex)", null, (_, _) => Safe(Copy));
        ContextMenuStrip.Items.Add("복사 (문자)", null, (_, _) => Safe(CopyText));
        ContextMenuStrip.Items.Add("붙여넣기", null, (_, _) => Safe(Paste));
        ContextMenuStrip.Items.Add("선택 바이트 삭제", null, (_, _) => Safe(Delete));
        ContextMenuStrip.Items.Add("삽입 / 덮어쓰기 전환  Insert", null, (_, _) => ToggleInsert());
    }
    void Safe(Action action)
    {
        try
        {
            action();
        }
        catch (Exception ex) { Dialogs.Message(this, ex.Message, "Hex 편집"); }
    }
    public void Open(string path)
    {
        var replacement = new BinaryBuffer(path);
        buffer?.Dispose();
        buffer = replacement;
        Caret = Anchor = TopRow = 0;
        UpdateScroll();
        Invalidate();
    }
    public void MarkSaved(string path)
    {
        long caret = Caret;
        Open(path);
        Go(caret);
    }
    public byte Read(long offset) => buffer!.Read(offset);
    public void SetBytes(long offset, byte[] data)
    {
        if (offset < 0 || offset + data.LongLength > Length)
            throw new IOException("덮어쓸 바이트가 파일 범위를 벗어납니다.");
        buffer!.Replace(offset, data.Length, data);
        Touch();
    }
    public void InsertBytes(long offset, byte[] data)
    {
        buffer!.Replace(offset, 0, data);
        Touch();
    }
    public void DeleteRange(long offset, long length)
    {
        buffer!.Replace(offset, length, []);
        Touch();
        Go(Math.Min(offset, Math.Max(0, Length - 1)));
    }
    void Touch()
    {
        Revision++;
        UpdateScroll();
        Changed?.Invoke();
        Invalidate();
    }
    public void Undo()
    {
        if (buffer?.Undo() == true)
        {
            Go(Math.Min(Caret, Math.Max(0, Length - 1)));
            Touch();
        }
    }
    public void Redo()
    {
        if (buffer?.Redo() == true)
        {
            Go(Math.Min(Caret, Math.Max(0, Length - 1)));
            Touch();
        }
    }
    public async Task WriteAsync(string path)
    {
        long version = Revision;
        await buffer!.WriteAsync(path);
        if (version != Revision)
            throw new IOException("문서가 변경되어 백업을 다시 시도합니다.");
    }
    public void ReleaseSource() => buffer?.Release();
    public void ReopenSource() => buffer?.Reopen();
    public Task<long> FindAsync(byte[] pattern, long start, CancellationToken ct) => buffer!.FindAsync(pattern, start, Length, ct);
    public Task<long> CountAsync(byte[] pattern, CancellationToken ct) => buffer!.CountAsync(pattern, ct);
    public byte[] SelectedBytes()
    {
        long a = Math.Min(Caret, Anchor), b = Math.Min(Length - 1, Math.Max(Caret, Anchor));
        if (b < a)
            return [];
        if (b - a >= 1024 * 1024)
            throw new IOException("한 번에 1 MiB 이하를 복사해 주세요.");
        var bytes = new byte[(int)(b - a + 1)];
        buffer!.Read(a, bytes);
        return bytes;
    }
    public void Copy()
    {
        var bytes = SelectedBytes();
        if (bytes.Length > 0)
            Clipboard.SetText(string.Join(" ", bytes.Select(x => x.ToString("X2"))));
    }
    public void CopyText()
    {
        var bytes = SelectedBytes();
        if (bytes.Length > 0)
            Clipboard.SetText(Encoding.GetEncoding(1252).GetString(bytes));
    }
    public static byte[] ParseHex(string text) => Convert.FromHexString(string.Concat(text.Where(c => !char.IsWhiteSpace(c))));
    public void Paste()
    {
        if (!Clipboard.ContainsText())
            return;
        byte[] bytes = AsciiMode ? Encoding.GetEncoding(1252, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback).GetBytes(Clipboard.GetText()) : ParseHex(Clipboard.GetText());
        long a = Math.Min(Caret, Anchor);
        if (InsertMode)
            InsertBytes(a, bytes);
        else
            SetBytes(a, bytes);
        Go(a);
        if (bytes.Length > 0)
            Go(a + bytes.Length - 1, true);
    }
    public void Cut()
    {
        Copy();
        Delete();
    }
    public void SelectAll()
    {
        Go(0);
        Go(Math.Max(0, Length - 1), true);
    }
    public void Delete()
    {
        long a = Math.Min(Caret, Anchor);
        if (a >= Length)
            return;
        DeleteRange(a, Math.Min(Length - a, Math.Abs(Caret - Anchor) + 1));
    }
    public void ToggleInsert()
    {
        InsertMode = !InsertMode;
        lowNibble = false;
        Changed?.Invoke();
        Invalidate();
    }
    public void Go(long offset, bool extend = false)
    {
        Caret = Math.Clamp(offset, 0, Math.Max(0, InsertMode ? Length : Length - 1));
        if (!extend)
            Anchor = Caret;
        lowNibble = false;
        if (Caret / 16 < TopRow)
            TopRow = Caret / 16;
        if (Caret / 16 >= TopRow + Rows)
            TopRow = Caret / 16 - Rows + 1;
        UpdateScroll();
        Invalidate();
        Changed?.Invoke();
    }
    long MaxTop() => Math.Max(0, (Length + 15) / 16 - Rows + 1);
    int ScrollMax() => Math.Max(1, (int)Math.Min(1000000, MaxTop()));
    void UpdateScroll()
    {
        syncing = true;
        TopRow = Math.Clamp(TopRow, 0, MaxTop());
        scroll.LargeChange = 1;
        scroll.Maximum = ScrollMax();
        scroll.Value = MaxTop() == 0 ? 0 : (int)(TopRow / (double)MaxTop() * ScrollMax());
        syncing = false;
    }
    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        UpdateScroll();
        Invalidate();
    }
    protected override void OnMouseWheel(MouseEventArgs e)
    {
        TopRow = Math.Clamp(TopRow - e.Delta / 120 * 3, 0, MaxTop());
        UpdateScroll();
        Invalidate();
    }
    void SelectMouse(MouseEventArgs e, bool extend)
    {
        long row = TopRow + Math.Max(0, (e.Y - 38) / RowHeight);
        int col = AsciiMode ? Math.Clamp((int)((e.X - AsciiX) / Cell), 0, 15) : Math.Clamp((int)((e.X - HexX) / (Cell * 3)), 0, 15);
        Go(row * 16 + col, extend);
    }
    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button != MouseButtons.Left)
            return;
        Focus();
        AsciiMode = e.X >= AsciiX;
        dragging = true;
        Capture = true;
        SelectMouse(e, (ModifierKeys & Keys.Shift) != 0);
    }
    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (!dragging)
            return;
        if (e.Y < 38 && TopRow > 0)
            TopRow--;
        else if (e.Y > Height - RowHeight)
            TopRow = Math.Min(MaxTop(), TopRow + 1);
        SelectMouse(e, true);
    }
    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        dragging = false;
        Capture = false;
    }
    protected override bool IsInputKey(Keys keyData) => true;
    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        Safe(() =>
        {
            if (e.Control)
            {
                switch (e.KeyCode)
                {
                    case Keys.C:
                        Copy();
                        break;
                    case Keys.X:
                        Cut();
                        break;
                    case Keys.V:
                        Paste();
                        break;
                    case Keys.Z:
                        Undo();
                        break;
                    case Keys.Y:
                        Redo();
                        break;
                    case Keys.A:
                        SelectAll();
                        break;
                    case Keys.Home:
                        Go(0, e.Shift);
                        break;
                    case Keys.End:
                        Go(Length - 1, e.Shift);
                        break;
                    default:
                        return;
                }
                e.Handled = true;
                e.SuppressKeyPress = true;
                return;
            }
            if (e.KeyCode == Keys.Insert)
            {
                ToggleInsert();
                e.Handled = true;
                return;
            }
            if (e.KeyCode == Keys.Delete)
            {
                Delete();
                e.Handled = true;
                return;
            }
            if (e.KeyCode == Keys.Tab)
            {
                AsciiMode = !AsciiMode;
                Invalidate();
                e.Handled = true;
                e.SuppressKeyPress = true;
                return;
            }
            long target = e.KeyCode switch
            {
                Keys.Left => Math.Max(0, Caret - 1),
                Keys.Right => Caret + 1,
                Keys.Up => Math.Max(0, Caret - 16),
                Keys.Down => Caret + 16,
                Keys.PageUp => Math.Max(0, Caret - Rows * 16),
                Keys.PageDown => Caret + Rows * 16,
                Keys.Home => Caret / 16 * 16,
                Keys.End => Math.Min(Length - 1, Caret / 16 * 16 + 15),
                _ => -1
            };
            if (target >= 0)
            {
                Go(target, e.Shift);
                e.Handled = true;
            }
        });
    }
    protected override void OnKeyPress(KeyPressEventArgs e)
    {
        base.OnKeyPress(e);
        if (char.IsControl(e.KeyChar))
            return;
        Safe(() =>
        {
            if (AsciiMode)
            {
                var bytes = Encoding.GetEncoding(1252, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback).GetBytes(e.KeyChar.ToString());
                if (InsertMode || Length == 0)
                    InsertBytes(Caret, bytes);
                else
                    SetBytes(Caret, bytes);
                Go(Caret + bytes.Length);
                e.Handled = true;
                return;
            }
            if (!Uri.IsHexDigit(e.KeyChar))
                return;
            int value = int.Parse(e.KeyChar.ToString(), NumberStyles.HexNumber);
            if (!lowNibble && (InsertMode || Length == 0 || Caret == Length))
            {
                InsertBytes(Caret, [(byte)(value << 4)]);
                lowNibble = true;
            }
            else
            {
                byte old = Read(Caret);
                SetBytes(Caret, [(byte)(lowNibble ? (old & 0xF0) | value : (value << 4) | (old & 15))]);
                if (lowNibble)
                    Go(Caret + 1);
                else
                    lowNibble = true;
            }
            e.Handled = true;
        });
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        var g = e.Graphics;
        using var header = new SolidBrush(Color.FromArgb(237, 242, 242));
        g.FillRectangle(header, 0, 0, Width, 31);
        void Draw(string text, float x, float y, Color color) => TextRenderer.DrawText(g, text, Font, new Point((int)x, (int)y), color, TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix);
        Draw("OFFSET (h)", 12, 7, Color.DimGray);
        Draw(string.Join(" ", Enumerable.Range(0, 16).Select(x => x.ToString("X2"))), HexX, 7, Color.DimGray);
        Draw("Decoded text", AsciiX, 7, Color.DimGray);
        if (buffer == null)
            return;
        int amount = (int)Math.Min((Rows + 1) * 16, Math.Max(0, Length - TopRow * 16));
        var bytes = new byte[amount];
        buffer.Read(TopRow * 16, bytes);
        long lo = Math.Min(Anchor, Caret), hi = Math.Max(Anchor, Caret);
        var ansi = Encoding.GetEncoding(1252);
        for (int row = 0; row <= Rows; row++)
        {
            long offset = (TopRow + row) * 16;
            if (offset >= Length)
                break;
            float y = 38 + row * RowHeight;
            Draw(offset.ToString("X16"), 12, y, Color.FromArgb(128, 137, 148));
            for (int col = 0; col < 16 && offset + col < Length; col++)
            {
                long p = offset + col;
                byte b = bytes[row * 16 + col];
                if (p >= lo && p <= hi)
                {
                    using var selected = new SolidBrush(Color.FromArgb(191, 226, 219));
                    g.FillRectangle(selected, HexX + col * 3 * Cell - 1, y - 1, Cell * 2 + 3, RowHeight);
                    g.FillRectangle(selected, AsciiX + col * Cell, y - 1, Cell + 1, RowHeight);
                }
                var color = buffer.IsEdited(p) ? Color.FromArgb(193, 87, 24) : ForeColor;
                Draw(b.ToString("X2"), HexX + col * 3 * Cell, y, color);
                Draw(b >= 32 && b != 127 ? ansi.GetString([b]) : "·", AsciiX + col * Cell, y, color);
                if (Focused && p == Caret)
                {
                    using var pen = new Pen(Color.FromArgb(0, 118, 105));
                    g.DrawRectangle(pen, AsciiMode ? AsciiX + col * Cell : HexX + col * 3 * Cell, y - 1, AsciiMode ? Cell : Cell * 2, RowHeight - 1);
                }
            }
        }
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing)
            buffer?.Dispose();
        base.Dispose(disposing);
    }
}

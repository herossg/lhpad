namespace LHPad;

internal static class DialogStyle
{
    public const int ButtonWidth = 88, ButtonHeight = 32, Gap = 8, Inset = 20;
    public static readonly Font Font = new("맑은 고딕", 9);
    public static readonly Icon AppIcon = new(Path.Combine(AppContext.BaseDirectory, "data", "LHPad.ico"), 32, 32);

    public static Button Button(string text, DialogResult result = DialogResult.None) => new()
    {
        Text = text, DialogResult = result, Size = new(ButtonWidth, ButtonHeight),
        AutoSize = false, Margin = Padding.Empty, UseVisualStyleBackColor = true
    };

    public static Label Label(string text, int width = 0, bool muted = false) => new()
    {
        Text = text, AutoSize = true, Dock = DockStyle.Fill,
        MaximumSize = new(width, 0), Margin = new(0, 0, 0, Gap),
        ForeColor = muted ? Color.FromArgb(104, 115, 124) : Color.FromArgb(40, 49, 59)
    };

    public static void Add(TableLayoutPanel layout, Control control)
    {
        int row = layout.RowCount++;
        layout.RowStyles.Add(new(SizeType.AutoSize));
        layout.Controls.Add(control, 0, row);
    }

    public static FlowLayoutPanel Row(params Control[] controls)
    {
        var row = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill, Margin = new(0, 0, 0, Gap) };
        for (int i = 0; i < controls.Length; i++)
        {
            controls[i].Margin = new(0, 0, i == controls.Length - 1 ? 0 : 12, 0);
            row.Controls.Add(controls[i]);
        }
        return row;
    }

    public static TableLayoutPanel Field(string title, Control editor)
    {
        var row = new TableLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new(0, 0, 0, Gap) };
        row.ColumnStyles.Add(new(SizeType.Absolute, 88));
        row.ColumnStyles.Add(new(SizeType.Percent, 100));
        row.RowStyles.Add(new(SizeType.AutoSize));
        var label = Label(title);
        label.TextAlign = ContentAlignment.MiddleLeft;
        label.Margin = new(0, 0, 12, 0);
        editor.Dock = DockStyle.Fill;
        editor.Margin = Padding.Empty;
        row.Controls.Add(label, 0, 0);
        row.Controls.Add(editor, 1, 0);
        return row;
    }

    // Right-to-left order is cancel, then primary; primary stays left of cancel.
    public static FlowLayoutPanel Footer(bool rightAligned, params Button[] buttons)
    {
        var row = new FlowLayoutPanel
        {
            AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill,
            FlowDirection = rightAligned ? FlowDirection.RightToLeft : FlowDirection.LeftToRight,
            WrapContents = false, Margin = new(0, Gap, 0, 0)
        };
        for (int i = 0; i < buttons.Length; i++)
        {
            buttons[i].TabIndex = rightAligned ? buttons.Length - 1 - i : i;
            buttons[i].Margin = rightAligned ? new(Gap, 0, 0, 0) : new(0, 0, i == buttons.Length - 1 ? 0 : Gap, 0);
            row.Controls.Add(buttons[i]);
        }
        return row;
    }
}

internal class StyledDialog : Form
{
    bool initialScaleApplied;
    protected internal TableLayoutPanel Body { get; }
    protected int ContentWidth { get; }
    public StyledDialog(string title, int width)
    {
        Text = title;
        AutoScaleDimensions = new(96, 96);
        AutoScaleMode = AutoScaleMode.Dpi;
        Font = DialogStyle.Font;
        Icon = DialogStyle.AppIcon;
        BackColor = Color.White;
        ForeColor = Color.FromArgb(40, 49, 59);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterParent;
        MaximizeBox = MinimizeBox = ShowInTaskbar = false;
        ContentWidth = width - 2 * DialogStyle.Inset;
        Body = new()
        {
            AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Dock = DockStyle.Fill,
            ColumnCount = 1, RowCount = 0, Margin = Padding.Empty, Padding = new(DialogStyle.Inset),
            MinimumSize = new(width, 0), MaximumSize = new(width, 0)
        };
        Body.ColumnStyles.Add(new(SizeType.Percent, 100));
        ClientSize = new(width, 1);
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Controls.Add(Body);
    }
    protected override void OnLoad(EventArgs e)
    {
        // Derived constructors add their controls after this base constructor. Apply
        // the initial DPI transform once the complete dialog has been assembled.
        if (!initialScaleApplied)
        {
            initialScaleApplied = true;
            AutoScaleDimensions = new(96, 96);
            PerformAutoScale();
        }
        base.OnLoad(e);
    }
}

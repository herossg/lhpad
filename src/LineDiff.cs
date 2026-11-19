namespace LHPad;

internal record DiffRow(int Left, int Right, int Kind); // 0 equal, 1 deletion, 2 insertion, 3 replacement
internal static class LineDiff
{
    public static List<DiffRow> Compare(string[] left, string[] right)
    {
        var rows = new List<DiffRow>();
        void Region(int a, int aEnd, int b, int bEnd, int depth)
        {
            while (a < aEnd && b < bEnd && left[a] == right[b])
            {
                rows.Add(new(a++, b++, 0));
            }
            int suffix = 0;
            while (a < aEnd - suffix && b < bEnd - suffix && left[aEnd - 1 - suffix] == right[bEnd - 1 - suffix])
                suffix++;
            int ae = aEnd - suffix, be = bEnd - suffix;
            if (a == ae)
            {
                while (b < be)
                    rows.Add(new(-1, b++, 2));
            }
            else if (b == be)
            {
                while (a < ae)
                    rows.Add(new(a++, -1, 1));
            }
            else if ((long)(ae - a) * (be - b) <= 1000000)
            {
                int n = ae - a, m = be - b;
                var table = new int[n + 1, m + 1];
                for (int i = n - 1; i >= 0; i--)
                    for (int j = m - 1; j >= 0; j--)
                        table[i, j] = left[a + i] == right[b + j] ? table[i + 1, j + 1] + 1 : Math.Max(table[i + 1, j], table[i, j + 1]);
                int x = 0, y = 0;
                while (x < n || y < m)
                {
                    if (x < n && y < m && left[a + x] == right[b + y])
                    {
                        rows.Add(new(a + x++, b + y++, 0));
                    }
                    else if (y >= m || x < n && table[x + 1, y] >= table[x, y + 1])
                        rows.Add(new(a + x++, -1, 1));
                    else
                        rows.Add(new(-1, b + y++, 2));
                }
            }
            else
            {
                var la = new Dictionary<string, int>(StringComparer.Ordinal);
                var rb = new Dictionary<string, int>(StringComparer.Ordinal);
                for (int i = a; i < ae; i++)
                    la[left[i]] = la.ContainsKey(left[i]) ? -1 : i;
                for (int j = b; j < be; j++)
                    rb[right[j]] = rb.ContainsKey(right[j]) ? -1 : j;
                var pairs = la.Where(x => x.Value >= 0 && rb.TryGetValue(x.Key, out int v) && v >= 0).Select(x => (A: x.Value, B: rb[x.Key])).OrderBy(x => x.A).ToArray();
                var tails = new List<int>();
                var prev = new int[pairs.Length];
                for (int i = 0; i < pairs.Length; i++)
                {
                    int lo = 0, hi = tails.Count;
                    while (lo < hi)
                    {
                        int mid = (lo + hi) / 2;
                        if (pairs[tails[mid]].B < pairs[i].B)
                            lo = mid + 1;
                        else
                            hi = mid;
                    }
                    prev[i] = lo > 0 ? tails[lo - 1] : -1;
                    if (lo == tails.Count)
                        tails.Add(i);
                    else
                        tails[lo] = i;
                }
                if (tails.Count == 0 || depth > 32)
                {
                    while (a < ae || b < be)
                        rows.Add(new(a < ae ? a++ : -1, b < be ? b++ : -1, 3));
                }
                else
                {
                    var anchors = new List<(int A, int B)>();
                    for (int i = tails[^1]; i >= 0; i = prev[i])
                        anchors.Add(pairs[i]);
                    anchors.Reverse();
                    foreach (var p in anchors)
                    {
                        Region(a, p.A, b, p.B, depth + 1);
                        rows.Add(new(p.A, p.B, 0));
                        a = p.A + 1;
                        b = p.B + 1;
                    }
                    Region(a, ae, b, be, depth + 1);
                }
            }
            for (int i = 0; i < suffix; i++)
                rows.Add(new(ae + i, be + i, 0));
        }
        Region(0, left.Length, 0, right.Length, 0);
        var aligned = new List<DiffRow>();
        for (int i = 0; i < rows.Count;)
        {
            if (rows[i].Kind == 0)
            {
                aligned.Add(rows[i++]);
                continue;
            }
            var a = new List<int>();
            var b = new List<int>();
            while (i < rows.Count && rows[i].Kind != 0)
            {
                if (rows[i].Left >= 0)
                    a.Add(rows[i].Left);
                if (rows[i].Right >= 0)
                    b.Add(rows[i].Right);
                i++;
            }
            for (int j = 0; j < Math.Max(a.Count, b.Count); j++)
                aligned.Add(new(j < a.Count ? a[j] : -1, j < b.Count ? b[j] : -1, j < a.Count && j < b.Count ? 3 : j < a.Count ? 1 : 2));
        }
        return aligned;
    }
    public static string[] Lines(string text) => text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
}
internal sealed class CompareForm : Form
{
    readonly Sci left = new(), right = new(); readonly List<int> changes = []; int index = -1; bool sync;
    public CompareForm(string leftName, string rightName, string[] a, string[] b, List<DiffRow> rows)
    {
        Text = $"줄 비교 · {leftName} ↔ {rightName}";
        Font = DialogStyle.Font;
        Icon = DialogStyle.AppIcon;
        Width = 1350;
        Height = 820;
        StartPosition = FormStartPosition.CenterParent;
        var bar = new ToolStrip { GripStyle = ToolStripGripStyle.Hidden, Padding = new Padding(10) };
        bar.Items.Add("이전 차이  Shift+F7", null, (_, _) => Navigate(-1));
        bar.Items.Add("다음 차이  F7", null, (_, _) => Navigate(1));
        bar.Items.Add(new ToolStripLabel($"  {leftName}  ↔  {rightName}   |   변경 {rows.Count(x => x.Kind != 0):N0}줄 · 비교 시점의 스냅샷"));
        var split = new SplitContainer { Dock = DockStyle.Fill, SplitterWidth = 5 };
        Controls.Add(split);
        Controls.Add(bar);
        bar.Dock = DockStyle.Top;
        split.Panel1.Controls.Add(left);
        split.Panel2.Controls.Add(right);
        Shown += (_, _) => { split.SplitterDistance = split.Width / 2; left.SetText(string.Join("\n", rows.Select(x => x.Left < 0 ? "" : a[x.Left]))); right.SetText(string.Join("\n", rows.Select(x => x.Right < 0 ? "" : b[x.Right]))); for (int i = 0; i < rows.Count; i++) { var row = rows[i]; if (row.Kind != 0) { changes.Add(i); left.Call(S.SCI_MARKERADD, i, row.Left < 0 ? 22 : 20); right.Call(S.SCI_MARKERADD, i, row.Right < 0 ? 22 : 21); } left.Str(S.SCI_MARGINSETTEXT, row.Left < 0 ? "" : (row.Left + 1).ToString(), i); right.Str(S.SCI_MARGINSETTEXT, row.Right < 0 ? "" : (row.Right + 1).ToString(), i); } foreach (var s in new[] { left, right }) { s.Call(S.SCI_SETMARGINTYPEN, 0, S.SC_MARGIN_RTEXT); s.Call(S.SCI_SETREADONLY, 1); s.Call(S.SCI_SETCARETLINEVISIBLE, 0); s.Call(S.SCI_SETMARGINWIDTHN, 1, 0); s.Call(S.SCI_EMPTYUNDOBUFFER); s.Call(S.SCI_MARKERDEFINE, 20, S.SC_MARK_BACKGROUND); s.Call(S.SCI_MARKERDEFINE, 21, S.SC_MARK_BACKGROUND); s.Call(S.SCI_MARKERDEFINE, 22, S.SC_MARK_BACKGROUND); s.Call(S.SCI_MARKERSETBACK, 20, 0xD9DBFF); s.Call(S.SCI_MARKERSETBACK, 21, 0xDCEEDD); s.Call(S.SCI_MARKERSETBACK, 22, 0xEEEEEE); } Navigate(1); };
        left.PositionChanged += () => Sync(left, right);
        right.PositionChanged += () => Sync(right, left);
    }
    void Sync(Sci a, Sci b)
    {
        if (sync)
            return;
        sync = true;
        b.Call(S.SCI_SETFIRSTVISIBLELINE, a.Call(S.SCI_GETFIRSTVISIBLELINE));
        sync = false;
    }
    void Navigate(int delta)
    {
        if (changes.Count == 0)
            return;
        index = (index + delta + changes.Count) % changes.Count;
        foreach (var s in new[] { left, right })
        {
            s.Call(S.SCI_GOTOLINE, changes[index]);
            s.Call(S.SCI_SETFIRSTVISIBLELINE, Math.Max(0, changes[index] - 4));
        }
    }
    protected override bool ProcessCmdKey(ref Message msg, Keys key)
    {
        if (key == Keys.F7)
        {
            Navigate(1);
            return true;
        }
        if (key == (Keys.Shift | Keys.F7))
        {
            Navigate(-1);
            return true;
        }
        return base.ProcessCmdKey(ref msg, key);
    }
}

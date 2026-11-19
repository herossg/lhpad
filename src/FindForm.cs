using System.Text;

namespace LHPad;

internal static class Search
{
    public static async Task<long> CountAsync(Sci s, string query, int flags, CancellationToken ct)
    {
        if (query.Length == 0) return 0;
        long count = 0, position = 0, revision = s.Revision, length = s.Length;
        var yield = System.Diagnostics.Stopwatch.StartNew();
        while (position <= length)
        {
            ct.ThrowIfCancellationRequested();
            if (s.IsDisposed || s.Revision != revision)
                throw new IOException("검색 중 문서가 변경되었습니다. 일치 개수를 다시 확인하세요.");
            var hit = In(s, query, position, length, flags);
            if (hit == null) break;
            count++;
            if (hit.Value.Start == hit.Value.End)
            {
                if (hit.Value.End == length) break;
                position = (long)s.Call(S.SCI_POSITIONAFTER, hit.Value.End);
            }
            else position = hit.Value.End;
            if (yield.ElapsedMilliseconds >= 12)
            {
                await Task.Delay(1, ct);
                yield.Restart();
            }
        }
        return count;
    }
    public static (long Start, long End)? In(Sci s, string query, long start, long end, int flags)
    {
        if (query.Length == 0)
            return null;
        if ((flags & S.SCFIND_REGEXP) != 0)
            flags |= unchecked((int)0xC8000000); // Notepad++ empty-match + CRLF handling
        s.Call(S.SCI_SETSTATUS, 0);
        s.Call(S.SCI_SETSEARCHFLAGS, flags);
        s.Call(S.SCI_SETTARGETSTART, start);
        s.Call(S.SCI_SETTARGETEND, end);
        var bytes = Encoding.UTF8.GetBytes(query);
        long result = (long)s.Bytes(S.SCI_SEARCHINTARGET, bytes, bytes.Length);
        if (result == -2 || s.Call(S.SCI_GETSTATUS) == S.SC_STATUS_WARN_REGEX)
            throw new FormatException("올바르지 않은 정규식입니다.");
        if (result == -3)
            throw new FormatException("정규식이 너무 복잡하거나 실행 한도를 초과했습니다.");
        return result < 0 ? null : ((long)s.Call(S.SCI_GETTARGETSTART), (long)s.Call(S.SCI_GETTARGETEND));
    }
    public static int Replace(Sci s, string replacement, bool regex)
    {
        var bytes = Encoding.UTF8.GetBytes(replacement);
        return (int)s.Bytes(regex ? S.SCI_REPLACETARGETRE : S.SCI_REPLACETARGET, bytes, bytes.Length);
    }
    public static async Task<int> ReplaceAllAsync(Sci s, string query, string replacement, int flags)
    {
        int count = 0;
        long position = 0;
        s.Call(S.SCI_BEGINUNDOACTION);
        bool enabled = s.Enabled;
        s.Enabled = false;
        try
        {
            while (position <= s.Length)
            {
                var hit = In(s, query, position, s.Length, flags);
                if (hit == null)
                    break;
                bool empty = hit.Value.Start == hit.Value.End;
                bool atEnd = hit.Value.End == s.Length;
                int n = Replace(s, replacement, (flags & S.SCFIND_REGEXP) != 0);
                count++;
                position = hit.Value.Start + n;
                if (empty)
                {
                    if (atEnd)
                        break;
                    position = (long)s.Call(S.SCI_POSITIONAFTER, position);
                }
                if (count % 1000 == 0)
                    await Task.Delay(1);
            }
            return count;
        }
        finally { s.Call(S.SCI_ENDUNDOACTION); s.Enabled = enabled; }
    }
}
internal sealed class FindForm : StyledDialog
{
    readonly MainForm main; readonly TextBox query = new() { Width = 410 }, replacement = new() { Width = 410 };
    readonly CheckBox regex = new() { Text = "정규식", AutoSize = true }, matchCase = new() { Text = "대소문자 구분", AutoSize = true }, word = new() { Text = "단어 단위", AutoSize = true }, wrap = new() { Text = "끝에서 처음으로", Checked = true, AutoSize = true }, allTabs = new() { Text = "모든 텍스트 탭에서 바꾸기", AutoSize = true };
    readonly ComboBox hexType = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 215 };
    readonly Label hexHint = new() { AutoSize = true, MaximumSize = new Size(550, 0), ForeColor = Color.DimGray };
    readonly FlowLayoutPanel binary = new() { AutoSize = true, Dock = DockStyle.Fill };
    Button previousButton = null!, replaceAllButton = null!;
    readonly Label result = new() { AutoSize = true, ForeColor = Color.FromArgb(0, 116, 106), MaximumSize = new Size(550, 0) };
    CancellationTokenSource? cancel;
    Document? contextDocument;
    Sci? lastEditor; string lastQuery = ""; long? lastEmpty;
    HexView? lastHex; string lastHexQuery = ""; int lastHexType; long lastHexPosition = -1;
    int Flags => (matchCase.Checked ? S.SCFIND_MATCHCASE : 0) | (word.Checked ? S.SCFIND_WHOLEWORD : 0) | (regex.Checked ? S.SCFIND_REGEXP | S.SCFIND_CXX11REGEX : 0);
    public FindForm(MainForm main) : base("찾기 / 바꾸기", 608)
    {
        this.main = main;
        hexHint.MaximumSize = result.MaximumSize = new Size(ContentWidth, 0);
        result.Visible = false;
        result.Margin = new Padding(0, 8, 0, 0);
        result.TextChanged += (_, _) => result.Visible = result.Text.Length > 0;
        DialogStyle.Add(Body, DialogStyle.Field("찾을 내용", query));
        DialogStyle.Add(Body, DialogStyle.Field("바꿀 내용", replacement));
        var options = DialogStyle.Row(matchCase, word, regex, wrap);
        DialogStyle.Add(Body, options);
        hexType.Items.AddRange(["Hex 바이트 (70 72 6F)", "텍스트 UTF-8", "텍스트 UTF-16 LE", "텍스트 ASCII"]);
        hexType.SelectedIndex = 1;
        binary.Margin = new Padding(0, 0, 0, 8);
        binary.Controls.Add(DialogStyle.Field("검색 형식", hexType));
        DialogStyle.Add(Body, binary);
        hexHint.Margin = new Padding(0, 0, 0, 8);
        DialogStyle.Add(Body, hexHint);
        allTabs.Dock = DockStyle.Fill;
        allTabs.Margin = new Padding(0, 0, 0, 8);
        DialogStyle.Add(Body, allTabs);
        var actions = new List<Button>();
        Button Add(string title, Func<Task> action)
        {
            var b = DialogStyle.Button(title);
            b.Click += async (_, _) => { b.Enabled = false; try { await main.Guard(action); } finally { b.Enabled = true; UpdateSearchContext(); } };
            actions.Add(b);
            return b;
        }
        var next = Add("다음 찾기", () => FindNextAsync(false));
        previousButton = Add("이전 찾기", () => FindNextAsync(true));
        Add("일치 개수", CountMatchesAsync);
        Add("바꾸기", ReplaceOneAsync);
        replaceAllButton = Add("모두 바꾸기", ReplaceAllAsync);
        Add("중지", () => { cancel?.Cancel(); return Task.CompletedTask; });
        DialogStyle.Add(Body, DialogStyle.Footer(false, actions.ToArray()));
        DialogStyle.Add(Body, result);
        AcceptButton = next;
        FormClosing += (_, _) => cancel?.Cancel();
        void InvalidateResult() { cancel?.Cancel(); result.Text = ""; }
        query.TextChanged += (_, _) => InvalidateResult();
        foreach (var option in new[] { matchCase, word, regex }) option.CheckedChanged += (_, _) => InvalidateResult();
        hexType.SelectedIndexChanged += (_, _) => { InvalidateResult(); UpdateSearchContext(); };
        Activated += (_, _) => UpdateSearchContext();
        UpdateSearchContext();
    }
    public void UpdateSearchContext()
    {
        if (contextDocument != main.Current)
        {
            contextDocument = main.Current;
            cancel?.Cancel();
            result.Text = "";
        }
        bool hex = main.Current?.Hex != null;
        binary.Visible = hexHint.Visible = hex;
        matchCase.Enabled = word.Enabled = regex.Enabled = allTabs.Enabled = !hex;
        previousButton.Enabled = replaceAllButton.Enabled = !hex;
        query.PlaceholderText = hex && hexType.SelectedIndex == 0 ? "예: 70 72 6F 67 72 61 6D" : "예: program";
        hexHint.Text = hexType.SelectedIndex == 0
            ? "16진수 바이트를 입력하세요. 문자열은 위에서 ‘텍스트 UTF-8’을 선택하세요."
            : "문자열을 그대로 입력하세요. Hex 검색은 대소문자를 구분합니다.";
    }
    internal void SetHexSearchFormat(int index) => hexType.SelectedIndex = index;
    internal string ResultText => result.Text;
    public void SetQuery(string text) => query.Text = text;
    public void FocusQuery(bool replace)
    {
        if (replace)
            replacement.Focus();
        else
        {
            query.Focus();
            query.SelectAll();
        }
    }
    byte[] HexBytes(string text) => hexType.SelectedIndex switch
    {
        0 => HexView.ParseHex(text),
        1 => new UTF8Encoding(false, true).GetBytes(text),
        2 => new UnicodeEncoding(false, false, true).GetBytes(text),
        _ => Encoding.GetEncoding(20127, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback).GetBytes(text)
    };
    bool TryHexBytes(string text, out byte[] bytes)
    {
        try { bytes = HexBytes(text); return true; }
        catch (FormatException)
        {
            result.ForeColor = Color.Firebrick;
            result.Text = "Hex 바이트는 두 자리 16진수로 입력하세요(예: 70 72 6F). program 같은 문자열은 ‘텍스트 UTF-8’을 선택하세요.";
        }
        catch (EncoderFallbackException)
        {
            result.ForeColor = Color.Firebrick;
            result.Text = "선택한 형식으로 표현할 수 없는 문자입니다. 한글은 텍스트 UTF-8 또는 UTF-16 LE로 검색하세요.";
        }
        bytes = [];
        return false;
    }
    public async Task CountMatchesAsync()
    {
        var d = main.Current;
        if (d == null) return;
        if (query.Text.Length == 0) { result.Text = "찾을 내용을 입력하세요."; return; }
        byte[] pattern = [];
        if (d.Hex != null && !TryHexBytes(query.Text, out pattern)) return;
        cancel?.Cancel();
        var operation = cancel = new CancellationTokenSource();
        string requested = query.Text;
        int requestedType = hexType.SelectedIndex;
        int requestedFlags = Flags;
        long revision = d.Revision;
        result.ForeColor = Color.FromArgb(0, 116, 106);
        result.Text = "현재 문서의 일치 개수를 세는 중…";
        try
        {
            long count = d.Editor is { } s
                ? await Search.CountAsync(s, requested, requestedFlags, operation.Token)
                : await d.Hex!.CountAsync(pattern, operation.Token);
            operation.Token.ThrowIfCancellationRequested();
            if (IsDisposed || cancel != operation || main.Current != d || query.Text != requested || d.Hex != null && hexType.SelectedIndex != requestedType || d.Editor != null && Flags != requestedFlags) return;
            if (d.Revision != revision) throw new IOException("검색 중 문서가 변경되었습니다. 일치 개수를 다시 확인하세요.");
            result.Text = $"현재 문서의 일치 항목: 총 {count:N0}개";
        }
        catch (OperationCanceledException) { if (!IsDisposed && cancel == operation) result.Text = "일치 개수 확인을 중지했습니다."; }
        catch (IOException ex) { if (!IsDisposed && cancel == operation) { result.ForeColor = Color.Firebrick; result.Text = ex.Message; } }
        finally { if (cancel == operation) cancel = null; operation.Dispose(); }
    }
    public async Task FindNextAsync(bool backwards, bool record = true)
    {
        var d = main.Current;
        if (d == null || query.Text.Length == 0)
            return;
        cancel?.Cancel();
        result.ForeColor = Color.FromArgb(0, 116, 106);
        if (d.Editor is { } s)
        {
            if (record)
                main.Macros.RecordAction(new()
                {
                    Kind = MacroStepKind.Find,
                    Query = query.Text,
                    Flags = Flags,
                    Backwards = backwards,
                    Wrap = wrap.Checked
                });
            long start = backwards ? s.SelectionStart : s.SelectionEnd;
            if (lastEditor == s && lastQuery == query.Text && lastEmpty == start && regex.Checked)
                start = (long)s.Call(backwards ? S.SCI_POSITIONBEFORE : S.SCI_POSITIONAFTER, start);
            var hit = Search.In(s, query.Text, start, backwards ? 0 : s.Length, Flags);
            bool wrapped = false;
            if (hit == null && wrap.Checked)
            {
                hit = Search.In(s, query.Text, backwards ? s.Length : 0, start, Flags);
                wrapped = true;
            }
            if (hit == null)
            {
                result.Text = "찾는 내용이 없습니다.";
                return;
            }
            s.Select(hit.Value.Start, hit.Value.End);
            lastEditor = s;
            lastQuery = query.Text;
            lastEmpty = hit.Value.Start == hit.Value.End ? hit.Value.Start : null;
            result.Text = wrapped ? "문서 경계에서 이어서 찾았습니다." : "일치하는 항목을 찾았습니다.";
        }
        else if (d.Hex is { } h)
        {
            if (backwards)
                throw new InvalidOperationException("Hex 검색은 앞으로 찾기를 지원합니다.");
            if (!TryHexBytes(query.Text, out var pattern)) return;
            if (pattern.Length == 0)
                return;
            cancel?.Cancel();
            var operation = cancel = new CancellationTokenSource();
            string requested = query.Text;
            int requestedType = hexType.SelectedIndex;
            long revision = h.Revision;
            try
            {
                result.Text = "Hex 검색 중…";
                bool sameHit = lastHex == h && lastHexQuery == query.Text && lastHexType == hexType.SelectedIndex
                    && Math.Min(h.Caret, h.Anchor) == lastHexPosition && Math.Abs(h.Caret - h.Anchor) + 1 == pattern.Length;
                long start = Math.Min(h.Length, sameHit ? lastHexPosition + 1 : h.Caret), pos = await h.FindAsync(pattern, start, operation.Token);
                if (pos < 0 && wrap.Checked)
                    pos = await h.FindAsync(pattern, 0, operation.Token);
                operation.Token.ThrowIfCancellationRequested();
                if (IsDisposed || cancel != operation || main.Current != d || query.Text != requested || hexType.SelectedIndex != requestedType) return;
                if (h.Revision != revision) { result.Text = "검색 중 문서가 변경되었습니다. 다시 검색하세요."; return; }
                if (pos < 0)
                {
                    result.Text = "찾는 바이트가 없습니다.";
                    return;
                }
                h.Go(pos);
                h.Go(pos + pattern.Length - 1, true);
                lastHex = h; lastHexQuery = query.Text; lastHexType = hexType.SelectedIndex; lastHexPosition = pos;
                result.Text = $"0x{pos:X} ({pos:N0})에서 {pattern.Length}바이트";
            }
            catch (OperationCanceledException) { if (!IsDisposed && cancel == operation) result.Text = "검색을 중지했습니다."; }
            finally { if (cancel == operation) cancel = null; operation.Dispose(); }
        }
    }
    async Task ReplaceOneAsync()
    {
        var d = main.Current;
        MainForm.RequireEditable(d);
        if (d == null || query.Text.Length == 0)
            return;
        if (d.Editor is { } s)
        {
            main.Macros.RecordAction(new()
            {
                Kind = MacroStepKind.Replace,
                Query = query.Text,
                Replacement = replacement.Text,
                Flags = Flags,
                Wrap = wrap.Checked
            });
            var hit = Search.In(s, query.Text, s.SelectionStart, s.SelectionEnd, Flags);
            if (hit is { } x && x.Start == s.SelectionStart && x.End == s.SelectionEnd)
            {
                long start = x.Start;
                int n = Search.Replace(s, replacement.Text, regex.Checked);
                s.Select(start, start + n);
            }
            await FindNextAsync(false, false);
        }
        else if (d.Hex is { } h)
        {
            if (!TryHexBytes(query.Text, out var pattern) || !TryHexBytes(replacement.Text, out var bytes)) return;
            if (pattern.Length != bytes.Length)
                throw new IOException("Hex 바꾸기는 같은 길이의 바이트로 덮어씁니다.");
            long pos = Math.Min(h.Caret, h.Anchor);
            if (pos + pattern.Length <= h.Length && Enumerable.Range(0, pattern.Length).All(i => h.Read(pos + i) == pattern[i]))
                h.SetBytes(pos, bytes);
            await FindNextAsync(false);
        }
    }
    async Task ReplaceAllAsync()
    {
        if (!allTabs.Checked) MainForm.RequireEditable(main.Current);
        if (main.Macros.IsRecording && allTabs.Checked)
            throw new InvalidOperationException("매크로 녹화 중에는 '모든 텍스트 탭에서 바꾸기'를 해제하고 현재 문서에 실행하세요.");
        if (main.Current?.Hex != null)
            throw new IOException("Hex 탭은 현재 항목 바꾸기를 지원합니다.");
        var targets = allTabs.Checked ? main.Documents.Where(d => d.Editor != null && !d.Monitoring).Select(d => d.Editor!).ToArray() : main.Current?.Editor is { } s ? new[] { s } : [];
        int total = 0;
        main.Macros.RecordAction(new()
        {
            Kind = MacroStepKind.ReplaceAll,
            Query = query.Text,
            Replacement = replacement.Text,
            Flags = Flags
        });
        foreach (var editor in targets)
            total += await Search.ReplaceAllAsync(editor, query.Text, replacement.Text, Flags);
        result.Text = $"{targets.Length}개 문서에서 {total:N0}개 항목을 바꿨습니다.";
    }
}

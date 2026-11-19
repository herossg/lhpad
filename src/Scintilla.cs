using System.Runtime.InteropServices;
using System.Text;

namespace LHPad;

internal sealed partial class Sci : Control
{
    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    static extern nint SendMessage(nint hwnd, int msg, nint wp, nint lp);
    [DllImport("Lexilla.dll", CallingConvention = CallingConvention.StdCall)]
    internal static extern nint CreateLexer([MarshalAs(UnmanagedType.LPStr)] string name);
    [StructLayout(LayoutKind.Sequential)]
    struct Header
    {
        public nint Window; public nuint Id; public uint Code;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct MacroNotification
    {
        public Header Header;
        public nint Position;
        public int Character, Modifiers, ModificationType;
        public nint Text, Length, LinesAdded;
        public int Message;
        public nuint WParam;
        public nint LParam;
    }
    public event Action<int, long, long>? MacroRecorded;
    protected override CreateParams CreateParams
    {
        get
        {
            var p = base.CreateParams;
            p.ClassName = "Scintilla";
            return p;
        }
    }
    public event Action? Changed;
    public event Action? PositionChanged;
    public event Action<int>? MarginClicked;
    public long Revision
    {
        get; private set;
    }
    public bool FoldingAvailable { get; private set; }
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public bool Large
    {
        get; set;
    }
    public Sci()
    {
        Dock = DockStyle.Fill;
        TabStop = true;
        SetStyle(ControlStyles.UserPaint, false);
    }
    public nint Call(int msg, long wp = 0, long lp = 0) => SendMessage(Handle, msg, (nint)wp, (nint)lp);
    public unsafe nint Bytes(int msg, byte[] bytes, long wp = 0)
    {
        fixed (byte* p = bytes)
            return Call(msg, wp, (long)p);
    }
    public nint Str(int msg, string text, long wp = 0) => Bytes(msg, Encoding.UTF8.GetBytes(text + "\0"), wp);
    public unsafe void Property(string key, string value)
    {
        var k = Encoding.UTF8.GetBytes(key + "\0");
        var v = Encoding.UTF8.GetBytes(value + "\0");
        fixed (byte* a = k, b = v)
            Call(S.SCI_SETPROPERTY, (long)a, (long)b);
    }
    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        // Scintilla's large document option uses 64-bit positions throughout the document.
        var doc = Call(S.SCI_CREATEDOCUMENT, 0, S.SC_DOCUMENTOPTION_TEXT_LARGE);
        Call(S.SCI_SETDOCPOINTER, 0, doc);
        Call(S.SCI_RELEASEDOCUMENT, 0, doc);
        Call(S.SCI_SETCODEPAGE, 65001);
        Call(S.SCI_STYLESETFORE, S.STYLE_DEFAULT, ColorTranslator.ToWin32(Color.FromArgb(34, 42, 53)));
        Call(S.SCI_STYLESETBACK, S.STYLE_DEFAULT, ColorTranslator.ToWin32(Color.FromArgb(253, 253, 252)));
        Str(S.SCI_STYLESETFONT, "Consolas", S.STYLE_DEFAULT);
        Call(S.SCI_STYLESETSIZE, S.STYLE_DEFAULT, 11);
        Call(S.SCI_STYLECLEARALL);
        Call(S.SCI_SETCARETFORE, ColorTranslator.ToWin32(Color.FromArgb(0, 117, 111)));
        Call(S.SCI_SETCARETLINEVISIBLE, 1);
        Call(S.SCI_SETCARETLINEBACK, ColorTranslator.ToWin32(Color.FromArgb(242, 247, 246)));
        Call(S.SCI_SETSELFORE, 1, ColorTranslator.ToWin32(Color.FromArgb(20, 47, 57)));
        Call(S.SCI_SETSELBACK, 1, ColorTranslator.ToWin32(Color.FromArgb(192, 226, 220)));
        Call(S.SCI_SETMARGINTYPEN, 0, S.SC_MARGIN_NUMBER);
        Call(S.SCI_SETMARGINWIDTHN, 0, 62);
        Call(S.SCI_STYLESETFORE, S.STYLE_LINENUMBER, ColorTranslator.ToWin32(Color.FromArgb(136, 145, 149)));
        Call(S.SCI_SETMARGINTYPEN, 1, S.SC_MARGIN_SYMBOL);
        Call(S.SCI_SETMARGINWIDTHN, 1, 0);
        Call(S.SCI_SETMARGINMASKN, 1, S.SC_MASK_FOLDERS);
        Call(S.SCI_SETMARGINSENSITIVEN, 1, 0);
        Call(S.SCI_SETMARGINCURSORN, 1, S.SC_CURSORARROW);
        var margin = ColorTranslator.ToWin32(Color.FromArgb(242, 245, 245));
        var guide = ColorTranslator.ToWin32(Color.FromArgb(127, 148, 151));
        Call(S.SCI_SETFOLDMARGINCOLOUR, 1, margin);
        Call(S.SCI_SETFOLDMARGINHICOLOUR, 1, margin);
        int[] folds = [S.SC_MARK_BOXPLUSCONNECTED, S.SC_MARK_BOXMINUSCONNECTED, S.SC_MARK_TCORNER, S.SC_MARK_LCORNER, S.SC_MARK_VLINE, S.SC_MARK_BOXPLUS, S.SC_MARK_BOXMINUS];
        for (int i = 25; i <= 31; i++)
        {
            Call(S.SCI_MARKERDEFINE, i, folds[i - 25]);
            // Native folder markers use BACK for the box outline and the vertical/branch strokes.
            Call(S.SCI_MARKERSETFORE, i, margin);
            Call(S.SCI_MARKERSETBACK, i, guide);
            Call(S.SCI_MARKERSETBACKSELECTED, i, ColorTranslator.ToWin32(Color.FromArgb(67, 126, 134)));
        }
        Call(S.SCI_MARKERENABLEHIGHLIGHT, 1);
        Call(S.SCI_SETFOLDFLAGS, S.SC_FOLDFLAG_LINEAFTER_CONTRACTED);
        // Scintilla owns margin clicks. A second container click handler would toggle twice.
        Call(S.SCI_SETAUTOMATICFOLD, S.SC_AUTOMATICFOLD_SHOW | S.SC_AUTOMATICFOLD_CLICK | S.SC_AUTOMATICFOLD_CHANGE);
        UpdateFoldMarginSize();
        Call(S.SCI_SETMULTIPLESELECTION, 1);
        Call(S.SCI_SETADDITIONALSELECTIONTYPING, 1);
        Call(S.SCI_SETMULTIPASTE, S.SC_MULTIPASTE_EACH);
        Call(S.SCI_SETRECTANGULARSELECTIONMODIFIER, S.SCMOD_ALT);
        Call(S.SCI_SETVIRTUALSPACEOPTIONS, S.SCVS_RECTANGULARSELECTION);
        Call(S.SCI_SETTABWIDTH, 4);
        Call(S.SCI_SETUSETABS, 0);
        Call(S.SCI_SETINDENTATIONGUIDES, S.SC_IV_LOOKBOTH);
        Call(S.SCI_SETEOLMODE, S.SC_EOL_CRLF);
        Call(S.SCI_SETSCROLLWIDTH, 1);
        Call(S.SCI_SETSCROLLWIDTHTRACKING, 1);
        Call(S.SCI_SETMODEVENTMASK, S.SC_MOD_INSERTTEXT | S.SC_MOD_DELETETEXT);
        ContextMenuStrip = BuildContext();
    }
    ContextMenuStrip BuildContext()
    {
        var m = new ContextMenuStrip();
        foreach (var x in new[] { ("실행 취소", S.SCI_UNDO), ("다시 실행", S.SCI_REDO), ("잘라내기", S.SCI_CUT), ("복사", S.SCI_COPY), ("붙여넣기", S.SCI_PASTE), ("모두 선택", S.SCI_SELECTALL) })
            m.Items.Add(x.Item1, null, (_, _) => Call(x.Item2));
        return m;
    }
    protected override void OnDpiChangedAfterParent(EventArgs e)
    {
        base.OnDpiChangedAfterParent(e);
        if (IsHandleCreated)
            UpdateFoldMarginSize();
    }
    void UpdateFoldMarginSize()
    {
        Call(S.SCI_SETMARGINWIDTHN, 1, FoldingAvailable ? Math.Max(18, (int)Math.Round(20d * DeviceDpi / 96)) : 0);
        int stroke = Math.Max(100, (int)Math.Round(100d * DeviceDpi / 96));
        for (int marker = S.SC_MARKNUM_FOLDEREND; marker <= S.SC_MARKNUM_FOLDEROPEN; marker++)
            Call(S.SCI_MARKERSETSTROKEWIDTH, marker, stroke);
    }
    public void ConfigureFolding(bool enabled)
    {
        if (FoldingAvailable && !enabled)
        {
            // Switching to another lexer must never leave lines hidden or stale fold headers behind.
            // ClearDocumentStyle is a native style/fold reset; it leaves content, undo and the save point intact.
            // ClearDocumentStyle calls the native contraction state's ShowAll, which clears
            // both hidden-line and expanded-flag state without asking the lexer to run.
            Call(S.SCI_CLEARDOCUMENTSTYLE);
        }
        FoldingAvailable = enabled && !Large;
        Call(S.SCI_SETMARGINSENSITIVEN, 1, FoldingAvailable ? 1 : 0);
        UpdateFoldMarginSize();
    }
    public void ToggleCurrentFold()
    {
        if (!FoldingAvailable)
            return;
        long line = (long)Call(S.SCI_LINEFROMPOSITION, Caret);
        // Finish only the styling needed to identify the caret's containing block. The native
        // folding engine finds the end of that block incrementally when it is contracted.
        long end = line + 1 < (long)Call(S.SCI_GETLINECOUNT) ? (long)Call(S.SCI_POSITIONFROMLINE, line + 1) : Length;
        long styled = (long)Call(S.SCI_GETENDSTYLED);
        if (styled < end)
            Call(S.SCI_COLOURISE, styled, end);
        if (((int)Call(S.SCI_GETFOLDLEVEL, line) & S.SC_FOLDLEVELHEADERFLAG) == 0)
            line = (long)Call(S.SCI_GETFOLDPARENT, line);
        if (line < 0)
            return;
        Call(S.SCI_FOLDLINE, line, S.SC_FOLDACTION_TOGGLE);
        MoveCaretToVisibleFold();
    }
    public void SetAllFolds(bool expanded)
    {
        if (!FoldingAvailable)
            return;
        // ContractEveryLevel also sets nested fold flags. Native FoldAll finishes lexing only
        // for this explicit document-wide operation, never on idle/caret/timer updates.
        Call(S.SCI_FOLDALL, expanded ? S.SC_FOLDACTION_EXPAND : S.SC_FOLDACTION_CONTRACT | S.SC_FOLDACTION_CONTRACT_EVERY_LEVEL);
        MoveCaretToVisibleFold();
    }
    public void SetFoldLevel(int depth, bool expanded)
    {
        if (!FoldingAvailable || depth < 1 || depth > 8)
            return;
        long styled = (long)Call(S.SCI_GETENDSTYLED);
        if (styled < Length)
            Call(S.SCI_COLOURISE, styled, -1);
        // Count structural nesting rather than raw numeric levels: indentation lexers may
        // increase levels by an entire tab width, while brace/tag lexers increase by one.
        var parents = new List<int>();
        long lines = (long)Call(S.SCI_GETLINECOUNT);
        for (long line = 0; line < lines; line++)
        {
            int level = (int)Call(S.SCI_GETFOLDLEVEL, line);
            if ((level & S.SC_FOLDLEVELHEADERFLAG) == 0)
                continue;
            int number = level & S.SC_FOLDLEVELNUMBERMASK;
            while (parents.Count > 0 && parents[^1] >= number)
                parents.RemoveAt(parents.Count - 1);
            parents.Add(number);
            if (parents.Count != depth)
                continue;
            if (Call(S.SCI_GETLINEVISIBLE, line) != 0)
                Call(S.SCI_FOLDLINE, line, expanded ? S.SC_FOLDACTION_EXPAND : S.SC_FOLDACTION_CONTRACT);
            else
                // Expanding a hidden header with FoldLine would also open its ancestors.
                // Keep exact-depth commands local; native ExpandLine honours this flag later.
                Call(S.SCI_SETFOLDEXPANDED, line, expanded ? 1 : 0);
        }
        MoveCaretToVisibleFold();
    }
    void MoveCaretToVisibleFold()
    {
        long line = (long)Call(S.SCI_LINEFROMPOSITION, Caret);
        if (Call(S.SCI_GETLINEVISIBLE, line) != 0)
            return;
        do { line = (long)Call(S.SCI_GETFOLDPARENT, line); }
        while (line >= 0 && Call(S.SCI_GETLINEVISIBLE, line) == 0);
        if (line >= 0)
            Call(S.SCI_GOTOLINE, line);
    }
    protected override void WndProc(ref Message m)
    {
        if (IsNewlineCommand(m))
        {
            NewlineWithIndentation(ref m);
            return;
        }
        if (m.Msg == 0x204E && m.LParam != 0)
        { // OCM_NOTIFY reflected by WinForms
            var h = Marshal.PtrToStructure<Header>(m.LParam);
            if (h.Code == S.SCN_CHARADDED && newlineCommandActive)
            {
                var notification = Marshal.PtrToStructure<MacroNotification>(m.LParam);
                if (notification.Character is '\r' or '\n')
                    newlineObserved = true;
            }
            if (h.Code == S.SCN_MODIFIED)
            {
                var notification = Marshal.PtrToStructure<MacroNotification>(m.LParam);
                if ((notification.ModificationType & (S.SC_MOD_INSERTTEXT | S.SC_MOD_DELETETEXT)) != 0)
                {
                    Revision++;
                    Changed?.Invoke();
                }
            }
            if (h.Code == S.SCN_UPDATEUI)
                PositionChanged?.Invoke();
            if (h.Code == S.SCN_MACRORECORD)
            {
                var notification = Marshal.PtrToStructure<MacroNotification>(m.LParam);
                if (!FilterIndentationMacro(notification))
                    MacroRecorded?.Invoke(notification.Message, (long)notification.WParam, (long)notification.LParam);
            }
            if (h.Code == S.SCN_MARGINCLICK)
            {
                var pos = Marshal.ReadInt64(m.LParam, 24);
                MarginClicked?.Invoke((int)Call(S.SCI_LINEFROMPOSITION, pos));
            }
        }
        base.WndProc(ref m);
    }
    public long Length => (long)Call(S.SCI_GETLENGTH);
    public long Caret => (long)Call(S.SCI_GETCURRENTPOS);
    public new long Anchor => (long)Call(S.SCI_GETANCHOR);
    public long SelectionStart => (long)Call(S.SCI_GETSELECTIONSTART);
    public long SelectionEnd => (long)Call(S.SCI_GETSELECTIONEND);
    public bool Modified => Call(S.SCI_GETMODIFY) != 0;
    public FileState? LoadedState { get; private set; }
    public void UseDocumentFrom(Sci source)
    {
        Call(S.SCI_SETDOCPOINTER, 0, (long)source.Call(S.SCI_GETDOCPOINTER));
        LoadedState = source.LoadedState;
        // Replacing the document pointer doesn't send SCN_MODIFIED; invalidate preview and recovery revisions explicitly.
        Revision++;
        Changed?.Invoke();
    }
    public unsafe byte[] Range(long start, long count)
    {
        var data = new byte[checked((int)count)];
        if (count > 0)
            Marshal.Copy(Call(S.SCI_GETRANGEPOINTER, start, count), data, 0, data.Length);
        return data;
    }
    public string TextRange(long start, long count) => Encoding.UTF8.GetString(Range(start, count));
    public string AllText()
    {
        if (Length > 32L * 1024 * 1024)
            throw new InvalidOperationException("이 작업은 32 MiB 이하 문서에서 지원됩니다. 작은 영역을 선택해 주세요.");
        return TextRange(0, Length);
    }
    public void SetText(string text)
    {
        var data = Encoding.UTF8.GetBytes(text);
        Call(S.SCI_CLEARALL);
        Bytes(S.SCI_ADDTEXT, data, data.Length);
    }
    public void Replace(long a, long b, string text)
    {
        Call(S.SCI_SETTARGETSTART, a);
        Call(S.SCI_SETTARGETEND, b);
        var bytes = Encoding.UTF8.GetBytes(text);
        Bytes(S.SCI_REPLACETARGET, bytes, bytes.Length);
    }
    public void Select(long anchor, long caret)
    {
        if (FoldingAvailable)
        {
            // SETSEL/SCROLLCARET do not open contracted parents on their own. Search and
            // macro navigation use this path, so reveal each destination before moving.
            Call(S.SCI_ENSUREVISIBLE, (long)Call(S.SCI_LINEFROMPOSITION, Math.Clamp(anchor, 0, Length)));
            Call(S.SCI_ENSUREVISIBLE, (long)Call(S.SCI_LINEFROMPOSITION, Math.Clamp(caret, 0, Length)));
        }
        Call(S.SCI_SETSEL, anchor, caret);
        Call(S.SCI_SCROLLCARET);
    }
    public void Saved() => Call(S.SCI_SETSAVEPOINT);
    public async Task LoadStreamAsync(string path, Encoding encoding, int skip = 0, Action<long>? progress = null)
    {
        Call(S.SCI_SETSTATUS, 0);
        Call(S.SCI_SETREADONLY, 0);
        Call(S.SCI_SETUNDOCOLLECTION, 0);
        Call(S.SCI_CLEARALL);
        try
        {
            using var file = FileState.Open(path);
            var state = FileState.Read(file);
            file.Position = skip;
            var input = new byte[256 * 1024];
            var chars = new char[encoding.GetMaxCharCount(input.Length)];
            var bytes = new byte[Encoding.UTF8.GetMaxByteCount(chars.Length)];
            var decoder = encoding.GetDecoder();
            var encoder = Encoding.UTF8.GetEncoder();
            long remaining = Math.Max(0, state.Length - skip);
            var yield = System.Diagnostics.Stopwatch.StartNew();
            while (remaining > 0)
            {
                int n = await file.ReadAsync(input.AsMemory(0, (int)Math.Min(input.Length, remaining)));
                if (n == 0) throw new IOException("읽는 도중 파일이 비워졌습니다. 다시 불러와 주세요.");
                remaining -= n;
                int decoded = decoder.GetChars(input, 0, n, chars, 0, remaining == 0);
                int count = encoder.GetBytes(chars, 0, decoded, bytes, 0, false);
                Bytes(S.SCI_ADDTEXT, bytes, count);
                if (Call(S.SCI_GETSTATUS) != 0)
                    throw new IOException("편집 엔진이 문서를 적재하지 못했습니다. 사용 가능한 메모리를 확인해 주세요.");
                progress?.Invoke(file.Position);
                if (yield.ElapsedMilliseconds > 12)
                {
                    await Task.Delay(1);
                    yield.Restart();
                }
            }
            int tail = encoder.GetBytes([], 0, 0, bytes, 0, true);
            if (tail > 0)
                Bytes(S.SCI_ADDTEXT, bytes, tail);
            LoadedState = state;
        }
        finally { Call(S.SCI_SETUNDOCOLLECTION, 1); Call(S.SCI_EMPTYUNDOBUFFER); Saved(); }
        var first = TextRange(0, Math.Min(Length, 65536));
        Call(S.SCI_SETEOLMODE, first.Contains("\r\n") ? S.SC_EOL_CRLF : first.Contains('\n') ? S.SC_EOL_LF : first.Contains('\r') ? S.SC_EOL_CR : S.SC_EOL_CRLF);
    }
    public async Task WriteStreamAsync(string path, Encoding encoding, bool bom, bool consistent = true)
    {
        var version = Revision;
        long size = Length;
        await using var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 1024, FileOptions.Asynchronous);
        var pre = bom ? encoding.GetPreamble() : [];
        if (pre.Length > 0)
            await stream.WriteAsync(pre);
        var decoder = Encoding.UTF8.GetDecoder();
        var encoder = encoding.GetEncoder();
        var chars = new char[1024 * 1024 + 2];
        var output = new byte[encoding.GetMaxByteCount(chars.Length)];
        for (long offset = 0; offset < size;)
        {
            if (IsDisposed || consistent && Revision != version)
                throw new IOException("문서가 변경되어 백업을 다시 시도합니다.");
            var bytes = Range(offset, Math.Min(1024 * 1024, size - offset));
            offset += bytes.Length;
            int c = decoder.GetChars(bytes, 0, bytes.Length, chars, 0, offset == size);
            int b = encoder.GetBytes(chars, 0, c, output, 0, offset == size);
            await stream.WriteAsync(output.AsMemory(0, b));
            await Task.Delay(1);
        }
        if (IsDisposed || consistent && Revision != version)
            throw new IOException("문서가 변경되어 백업을 다시 시도합니다.");
        await stream.FlushAsync();
        stream.Flush(true);
    }
}

using System.Diagnostics;
using System.Globalization;
using System.Text;

namespace LHPad;

internal sealed partial class MainForm : Form
{
    readonly TabControl tabs = new() { Dock = DockStyle.Fill, DrawMode = TabDrawMode.OwnerDrawFixed, Padding = new Point(32, 7) };
    readonly UiImages uiImages = new();
    readonly ToolbarIcons toolbarArtwork = new();
    readonly List<Document> docs = []; readonly List<SessionDocument> unavailable = []; readonly HashSet<Document> compare = [];
    readonly Languages languages = new(); SessionStore store;
    readonly WindowPlacementStore windowPlacement;
    FormWindowState lastVisibleWindowState = FormWindowState.Normal;
    readonly StatusStrip status = new() { BackColor = Color.FromArgb(240, 245, 244) };
    readonly ToolStripStatusLabel statusFile = new() { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
    readonly ToolStripStatusLabel statusPosition = new(), statusEncoding = new(), statusLanguage = new(), statusMode = new();
    readonly ToolStripStatusLabel statusRecovery = new() { Text = "세션 자동 복원", ForeColor = Color.FromArgb(0, 119, 103) };
    readonly System.Windows.Forms.Timer backupTimer = new() { Interval = 3000 };
    readonly MenuStrip menu = new() { Padding = new Padding(8, 3, 8, 3) };
    readonly ToolStrip toolbar = new() { GripStyle = ToolStripGripStyle.Hidden, Padding = new Padding(9, 6, 9, 6), BackColor = Color.FromArgb(248, 250, 249) };
    readonly ToolStripButton sourceButton = new("원문"), previewButton = new("미리보기"), splitButton = new("분할 보기");
    readonly MacroController macros;
    readonly ToolStripMenuItem autoIndentItem = new("자동 들여쓰기") { Checked = true, CheckOnClick = true };
    bool autoIndent = true;
    readonly ToolStripButton macroRecordButton = new("녹화"), macroStopButton = new("■ 중지"), macroPlayButton = new("▶ 매크로");
    readonly ToolStripButton macroRepeatButton = new("매크로 반복 실행")
    {
        AutoSize = true
    };
    readonly ToolStripMenuItem macroMenu = new("매크로(&M)"), savedMacroMenu = new("저장된 매크로 실행");
    readonly ToolStripStatusLabel macroStatus = new() { ForeColor = Color.FromArgb(0, 120, 105) };
    ToolStripMenuItem? macroRecordItem, macroStopItem, macroPlayItem, macroRepeatItem, macroSaveItem, macroManageItem;
    Task<MacroRunResult>? macroPlayback;
    string savedMacroSignature = "";
    bool? controlsInPlayback;
    readonly record struct TabHeaderState(string Name, bool Dirty);
    public MacroController Macros => macros;
    FindForm? find; Task? backup; bool starting = true, closing, allowClose, whiteSpace, eolVisible, controlVisible, busy;
    int untitled; readonly string[] initialFiles; readonly bool demo;
    public Document? Current => tabs.SelectedTab == null ? null : docs.FirstOrDefault(x => x.Page == tabs.SelectedTab);
    readonly TaskCompletionSource ready = new();
    public Task Ready => ready.Task;
    public IReadOnlyList<Document> Documents => docs;
    public MainForm(string profile, string[] files, bool demo = false)
    {
        store = new(profile);
        windowPlacement = new(profile);
        macros = new MacroController(profile);
        initialFiles = files;
        this.demo = demo;
        Text = "LHPad";
        Icon = new Icon(Path.Combine(AppContext.BaseDirectory, "data", "LHPad.ico"));
        Width = 1320;
        Height = 850;
        MinimumSize = new Size(800, 500);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("맑은 고딕", 9);
        BackColor = Color.White;
        AllowDrop = true;
        KeyPreview = true;
        BuildMenu();
        BuildToolbar();
        status.Items.Add(monitorStatusLabel);
        status.Items.AddRange([statusFile, statusPosition, statusEncoding, statusLanguage, statusMode, statusRecovery]);
        status.Items.Add(macroStatus);
        macros.StateChanged += UpdateMacroUi;
        UpdateMacroUi();
        Controls.Add(tabs);
        Controls.Add(toolbar);
        Controls.Add(menu);
        Controls.Add(status);
        MainMenuStrip = menu;
        menu.Dock = toolbar.Dock = DockStyle.Top;
        status.Dock = DockStyle.Bottom;
        tabs.DrawItem += DrawTab;
        tabs.SelectedIndexChanged += (_, _) => { UpdateStatus(); find?.UpdateSearchContext(); Current?.Editor?.Focus(); Current?.Hex?.Focus(); };
        tabs.MouseDown += (_, e) => { for (int i = 0; i < tabs.TabCount; i++) { var r = tabs.GetTabRect(i); if (!r.Contains(e.Location)) continue; var d = docs.First(x => x.Page == tabs.TabPages[i]); if (e.Button == MouseButtons.Middle || e.Button == MouseButtons.Left && e.X > r.Right - LogicalToDeviceUnits(24)) { _ = CloseTabAsync(d); return; } if (e.Button == MouseButtons.Left && (ModifierKeys & Keys.Control) != 0) { if (!compare.Add(d)) compare.Remove(d); tabs.Invalidate(); } if (e.Button == MouseButtons.Right) { tabs.SelectedTab = d.Page; TabMenu(d).Show(tabs, e.Location); } break; } };
        DragEnter += (_, e) => { if (e.Data?.GetDataPresent(DataFormats.FileDrop) == true) e.Effect = DragDropEffects.Copy; };
        DragDrop += async (_, e) => { if (e.Data?.GetData(DataFormats.FileDrop) is string[] paths) foreach (var p in paths) await Guard(() => OpenAsync(p)); };
        backupTimer.Tick += async (_, _) => { if (starting || busy || closing || macros.IsPlaying || backup is { IsCompleted: false }) return; try { backup = BackupAsync(); await backup; } catch (Exception) { statusRecovery.Text = "백업 재시도 대기"; statusRecovery.ForeColor = Color.DarkOrange; } };
        Shown += async (_, _) => await StartAsync();
        FormClosing += OnClosing;
        Activated += async (_, _) => await CheckExternalChangesAsync();
        fileMonitorTimer.Tick += async (_, _) => await PollMonitorsAsync();
        statusEncoding.IsLink = true;
        statusEncoding.Click += (_, _) => ShowEncodingMenu();
        statusLanguage.IsLink = true;
        statusLanguage.Click += (_, _) => menu.Items[6].Select();
    }
    void BuildMenu()
    {
        ToolStripMenuItem Root(string title)
        {
            var m = new ToolStripMenuItem(title);
            menu.Items.Add(m);
            return m;
        }
        ToolStripMenuItem Item(ToolStripMenuItem parent, string label, Action action, Keys keys = Keys.None)
        {
            var i = new ToolStripMenuItem(label, null, (_, _) => { if (!busy) Try(action); });
            if (keys != Keys.None)
                i.ShortcutKeys = keys;
            parent.DropDownItems.Add(i);
            return i;
        }
        var file = Root("파일(&F)");
        Item(file, "새 문서", () => NewDocument(), Keys.Control | Keys.N);
        Item(file, "열기…", () => _ = ChooseOpenAsync(), Keys.Control | Keys.O);
        Item(file, "저장", () => _ = Guard(() => SaveAsync(Current)), Keys.Control | Keys.S);
        Item(file, "다른 이름으로 저장…", () => _ = Guard(() => SaveAsync(Current, true)), Keys.Control | Keys.Alt | Keys.S);
        Item(file, "모두 저장", () => _ = Guard(SaveAllAsync), Keys.Control | Keys.Shift | Keys.S);
        file.DropDownItems.Add(new ToolStripSeparator());
        Item(file, "현재 탭 닫기", () => _ = CloseTabAsync(Current), Keys.Control | Keys.W);
        Item(file, "종료", Close, Keys.Alt | Keys.F4);
        var edit = Root("편집(&E)");
        Item(edit, "실행 취소", () => Edit(S.SCI_UNDO), Keys.Control | Keys.Z);
        Item(edit, "다시 실행", () => Edit(S.SCI_REDO), Keys.Control | Keys.Y);
        edit.DropDownItems.Add(new ToolStripSeparator());
        Item(edit, "잘라내기", () => Edit(S.SCI_CUT), Keys.Control | Keys.X);
        Item(edit, "복사", () => Edit(S.SCI_COPY), Keys.Control | Keys.C);
        Item(edit, "붙여넣기", () => Edit(S.SCI_PASTE), Keys.Control | Keys.V);
        Item(edit, "모두 선택", () => Edit(S.SCI_SELECTALL), Keys.Control | Keys.A);
        edit.DropDownItems.Add(new ToolStripSeparator());
        Item(edit, "선택 / 현재 줄 복제", () => Edit(S.SCI_SELECTIONDUPLICATE), Keys.Control | Keys.D);
        Item(edit, "현재 줄 삭제", () => Edit(S.SCI_LINEDELETE), Keys.Control | Keys.L);
        Item(edit, "줄 위로 이동", () => Edit(S.SCI_MOVESELECTEDLINESUP), Keys.Control | Keys.Shift | Keys.Up);
        Item(edit, "줄 아래로 이동", () => Edit(S.SCI_MOVESELECTEDLINESDOWN), Keys.Control | Keys.Shift | Keys.Down);
        Item(edit, "선택 줄 오름차순 정렬", () => SortLines(false));
        Item(edit, "선택 줄 내림차순 정렬", () => SortLines(true));
        edit.DropDownItems.Add(new ToolStripSeparator());
        autoIndentItem.ToolTipText = "Enter를 누르면 윗줄의 탭·공백 들여쓰기를 다음 줄에 이어 줍니다.";
        autoIndentItem.Click += (_, _) =>
        {
            if (busy || macros.IsPlaying) { autoIndentItem.Checked = autoIndent; return; }
            autoIndent = autoIndentItem.Checked;
            ApplyViews();
        };
        edit.DropDownItems.Add(autoIndentItem);
        var search = Root("검색(&S)");
        Item(search, "찾기…", () => ShowFind(false), Keys.Control | Keys.F);
        Item(search, "바꾸기…", () => ShowFind(true), Keys.Control | Keys.H);
        Item(search, "다음 찾기", () => { ShowFind(false, false); _ = Guard(() => find!.FindNextAsync(false)); }, Keys.F3);
        Item(search, "이전 찾기", () => { ShowFind(false, false); _ = Guard(() => find!.FindNextAsync(true)); }, Keys.Shift | Keys.F3);
        Item(search, "줄 / 오프셋 이동…", GoTo, Keys.Control | Keys.G);
        var view = Root("보기(&V)");
        Item(view, "원문 보기", () => _ = Guard(() => ShowDocumentViewAsync(DocumentViewMode.Source)));
        Item(view, "미리보기 전환", () => _ = Guard(() => ShowDocumentViewAsync(Current?.Surface?.Mode == DocumentViewMode.Preview ? DocumentViewMode.Source : DocumentViewMode.Preview)), Keys.Control | Keys.Shift | Keys.V);
        Item(view, "좌우 분할 보기", () => _ = Guard(() => ShowDocumentViewAsync(DocumentViewMode.Split)), Keys.Control | Keys.Alt | Keys.V);
        Item(view, "미리보기 새로고침", () => _ = Guard(() => Current?.Surface?.RefreshAsync(true) ?? Task.CompletedTask), Keys.F5);
        view.DropDownItems.Add(new ToolStripSeparator());
        var ws = Item(view, "공백·탭 표시", () => { whiteSpace = !whiteSpace; ApplyViews(); });
        ws.CheckOnClick = true;
        var eol = Item(view, "줄바꿈 문자 표시", () => { eolVisible = !eolVisible; ApplyViews(); });
        eol.CheckOnClick = true;
        var control = Item(view, "제어 문자 표시", () => { controlVisible = !controlVisible; ApplyViews(); });
        control.CheckOnClick = true;
        Item(view, "모든 기호 표시", () => { whiteSpace = eolVisible = controlVisible = !(whiteSpace && eolVisible && controlVisible); ApplyViews(); });
        Item(view, "자동 줄바꿈", () => { if (Current?.Editor is { } s) s.Call(S.SCI_SETWRAPMODE, s.Call(S.SCI_GETWRAPMODE) == 0 ? 1 : 0); });
        Item(view, "확대", () => Edit(S.SCI_ZOOMIN), Keys.Control | Keys.Add);
        Item(view, "축소", () => Edit(S.SCI_ZOOMOUT), Keys.Control | Keys.Subtract);
        Item(view, "기본 배율", () => Current?.Editor?.Call(S.SCI_SETZOOM, 0), Keys.Control | Keys.NumPad0);
        BuildFoldingMenu(view);
        view.DropDownItems.Add(new ToolStripSeparator());
        Item(view, "Hex로 다시 열기", () => _ = Guard(() => ReopenAsync(true)));
        Item(view, "텍스트로 다시 열기", () => _ = Guard(() => ReopenAsync(false)));
        BuildFileWatchingMenu(view);
        var multi = Root("다중 편집(&U)");
        Item(multi, "세로 선택: Alt + 마우스 드래그", () => Dialogs.Message("Alt + 드래그: 세로 선택\nAlt + Shift + 방향키: 키보드 세로 선택\nCtrl + 클릭: 커서 추가\n여러 커서를 만든 뒤 입력하면 함께 편집됩니다.", "다중 편집"));
        Item(multi, "같은 단어 다음 항목 추가", () => Edit(S.SCI_MULTIPLESELECTADDNEXT), Keys.Control | Keys.Alt | Keys.Down);
        Item(multi, "같은 단어 모두 선택", () => Edit(S.SCI_MULTIPLESELECTADDEACH), Keys.Control | Keys.Shift | Keys.L);
        Item(multi, "열 편집기…", ColumnEdit, Keys.Alt | Keys.C);
        var enc = Root("인코딩(&C)");
        var interpret = new ToolStripMenuItem("이 인코딩으로 다시 열기");
        var convert = new ToolStripMenuItem("저장 인코딩 변환");
        enc.DropDownItems.AddRange([interpret, convert]);
        foreach (var choice in FileTypes.Encodings)
        {
            Item(interpret, choice.Name, () => _ = Guard(() => ReloadEncodingAsync(choice)));
            Item(convert, choice.Name, () => { if (Current?.Editor == null) return; _ = choice.Strict; Current.Encoding = choice; Current.EncodingChanged = true; UpdateStatus(); });
        }
        var endings = new ToolStripMenuItem("줄바꿈 변환");
        enc.DropDownItems.Add(endings);
        foreach (var x in new[] { ("Windows CRLF", S.SC_EOL_CRLF), ("Unix LF", S.SC_EOL_LF), ("Mac CR", S.SC_EOL_CR) })
            Item(endings, x.Item1, () => { macros.RecordAction(new() { Kind = MacroStepKind.ConvertEol, WParam = x.Item2 }); Current?.Editor?.Call(S.SCI_CONVERTEOLS, x.Item2); Current?.Editor?.Call(S.SCI_SETEOLMODE, x.Item2); UpdateStatus(); });
        var language = Root("언어(&L)");
        foreach (var group in languages.Items.OrderBy(x => x.Display).GroupBy(x => char.ToUpperInvariant(x.Display[0])))
        {
            var sub = new ToolStripMenuItem(group.Key.ToString());
            language.DropDownItems.Add(sub);
            foreach (var l in group)
                Item(sub, l.Display, () => { if (Current?.Editor == null) return; Current.Language = l.Name; languages.Apply(Current.Editor, l.Name); UpdateStatus(); });
        }
        menu.Items.Add(macroMenu);
        macroRecordItem = Item(macroMenu, "녹화 시작 / 중지", () => _ = Guard(ToggleMacroRecordingAsync), Keys.Control | Keys.Shift | Keys.R);
        macroStopItem = Item(macroMenu, "녹화 / 실행 중지", () => { if (macros.IsRecording) Try(macros.Stop); else macros.Cancel(); });
        macroPlayItem = Item(macroMenu, "현재 녹화 실행", () => _ = Guard(() => PlayMacroAsync()), Keys.Control | Keys.Shift | Keys.P);
        macroRepeatItem = Item(macroMenu, "여러 번 실행…", () => _ = Guard(RepeatMacroAsync), Keys.Control | Keys.Alt | Keys.Shift | Keys.P);
        macroSaveItem = Item(macroMenu, "현재 녹화 저장…", SaveRecordedMacro);
        macroMenu.DropDownItems.Add(new ToolStripSeparator());
        macroMenu.DropDownItems.Add(savedMacroMenu);
        macroManageItem = Item(macroMenu, "저장된 매크로 관리…", () => { using var dialog = new MacroManagerDialog(macros); dialog.ShowDialog(this); });
        var tools = Root("도구(&T)");
        Item(tools, "문서 / 선택 영역 정렬", () => _ = Guard(FormatAsync), Keys.Control | Keys.Alt | Keys.F);
        Item(tools, "이 탭을 비교 대상으로 선택", () => { if (Current == null) return; if (!compare.Add(Current)) compare.Remove(Current); tabs.Invalidate(); });
        Item(tools, "선택한 두 탭 줄 비교", () => _ = Guard(CompareAsync), Keys.Control | Keys.Alt | Keys.C);
        Item(tools, "Hex SHA-256…", () => _ = Guard(HashAsync));
        tools.DropDownItems.Add(new ToolStripSeparator());
        Item(tools, "복원 파일 폴더 열기", () => Process.Start(new ProcessStartInfo("explorer.exe", store.DirectoryPath) { UseShellExecute = true }));
        Item(tools, "LHPad 정보", () => Dialogs.About(this));
    }
    void BuildToolbar()
    {
        toolbar.ImageScalingSize = new Size(24, 24);
        toolbar.ShowItemToolTips = true;
        void Configure(ToolStripButton button, string name, ToolbarIcon? icon, string tooltip)
        {
            button.Text = name;
            button.DisplayStyle = ToolStripItemDisplayStyle.Image;
            button.ImageScaling = ToolStripItemImageScaling.SizeToFit;
            button.AutoSize = true;
            button.Padding = new Padding(5);
            button.Margin = new Padding(2, 0, 2, 0);
            button.AccessibleName = name;
            button.ToolTipText = tooltip;
            if (icon is { } kind) button.Image = toolbarArtwork.Get(kind, 64);
        }
        void Button(string name, ToolbarIcon icon, string tooltip, Action action)
        {
            var button = new ToolStripButton();
            Configure(button, name, icon, tooltip);
            button.Click += (_, _) => { if (!busy) Try(action); };
            toolbar.Items.Add(button);
        }
        Button("새 문서", ToolbarIcon.NewFile, "새 문서 (Ctrl+N)", () => NewDocument());
        Button("열기", ToolbarIcon.Open, "파일 열기 (Ctrl+O)", () => _ = ChooseOpenAsync());
        Button("저장", ToolbarIcon.Save, "저장 (Ctrl+S)", () => _ = Guard(() => SaveAsync(Current)));
        toolbar.Items.Add(new ToolStripSeparator());
        Button("찾기", ToolbarIcon.Find, "찾기 (Ctrl+F)", () => ShowFind(false));
        Button("바꾸기", ToolbarIcon.Replace, "바꾸기 (Ctrl+H)", () => ShowFind(true));
        toolbar.Items.Add(new ToolStripSeparator());
        Button("문서 정렬", ToolbarIcon.Format, "문서 / 선택 영역 정렬 (Ctrl+Alt+F)", () => _ = Guard(FormatAsync));
        Button("탭 비교", ToolbarIcon.Compare, "선택한 두 탭 줄 비교 (Ctrl+Alt+C)", () => _ = Guard(CompareAsync));
        toolbar.Items.Add(new ToolStripSeparator());
        sourceButton.Click += (_, _) => _ = Guard(() => ShowDocumentViewAsync(DocumentViewMode.Source));
        previewButton.Click += (_, _) => _ = Guard(() => ShowDocumentViewAsync(DocumentViewMode.Preview));
        splitButton.Click += (_, _) => _ = Guard(() => ShowDocumentViewAsync(DocumentViewMode.Split));
        Configure(sourceButton, "원문", ToolbarIcon.Source, "원문 보기");
        Configure(previewButton, "미리보기", ToolbarIcon.Preview, "Markdown / HTML / SVG 미리보기 (Ctrl+Shift+V)");
        Configure(splitButton, "분할 보기", ToolbarIcon.Split, "원문과 미리보기 나란히 (Ctrl+Alt+V)");
        toolbar.Items.AddRange([sourceButton, previewButton, splitButton]);
        monitorButton.Click += (_, _) => _ = Guard(ToggleCurrentMonitoringAsync);
        followTailButton.Click += (_, _) => ToggleFollowTail();
        Configure(monitorButton, "로그 모니터링", ToolbarIcon.Monitor, "로그 모니터링 — 현재 텍스트 파일을 읽기 전용으로 모니터링");
        Configure(followTailButton, "끝 따라가기", ToolbarIcon.FollowTail, "끝 따라가기 — 새 로그가 추가되면 끝으로 이동. 끄면 현재 분석 위치 유지");
        toolbar.Items.AddRange([monitorButton, followTailButton]);
        toolbar.Items.Add(new ToolStripSeparator());
        macroRecordButton.Click += (_, _) => _ = Guard(ToggleMacroRecordingAsync);
        macroStopButton.Click += (_, _) => { if (macros.IsRecording) Try(macros.Stop); else macros.Cancel(); };
        macroPlayButton.Click += (_, _) => _ = Guard(() => PlayMacroAsync());
        macroRepeatButton.Click += (_, _) => _ = Guard(RepeatMacroAsync);
        Configure(macroRecordButton, "녹화", null, "매크로 녹화 시작 / 중지 (Ctrl+Shift+R)");
        Configure(macroStopButton, "중지", ToolbarIcon.Stop, "매크로 녹화 / 실행 중지 (실행 중 Esc)");
        Configure(macroPlayButton, "매크로 실행", ToolbarIcon.Play, "현재 매크로 실행 (Ctrl+Shift+P)");
        Configure(macroRepeatButton, "매크로 반복 실행", ToolbarIcon.Repeat, "매크로 반복 실행: 횟수 지정 / 문서 끝까지 (Ctrl+Alt+Shift+P)");
        toolbar.Items.AddRange([macroRecordButton, macroStopButton, macroPlayButton, macroRepeatButton]);
        toolbar.Items.Add(new ToolStripSeparator());
        const string help = "세로 선택: Alt+드래그\n다중 커서: Ctrl+클릭\n비교할 탭 선택: Ctrl+탭 클릭";
        Button("편집 단축키 도움말", ToolbarIcon.Help, help, () => Dialogs.Message(this, help, "편집 단축키"));
    }
    ContextMenuStrip TabMenu(Document d)
    {
        var m = new ContextMenuStrip();
        m.Items.Add(compare.Contains(d) ? "비교 선택 해제" : "비교 대상으로 선택", null, (_, _) => { if (!compare.Add(d)) compare.Remove(d); tabs.Invalidate(); });
        m.Items.Add("선택한 두 탭 비교", null, (_, _) => _ = Guard(CompareAsync));
        m.Items.Add("전체 경로 복사", null, (_, _) => { if (d.Path != null) Clipboard.SetText(d.Path); });
        m.Items.Add("닫기", null, (_, _) => _ = CloseTabAsync(d));
        return m;
    }
    void DrawTab(object? sender, DrawItemEventArgs e)
    {
        var d = docs.FirstOrDefault(x => x.Page == tabs.TabPages[e.Index]);
        if (d == null)
            return;
        var r = tabs.GetTabRect(e.Index);
        using var bg = new SolidBrush(e.Index == tabs.SelectedIndex ? Color.White : Color.FromArgb(237, 241, 241));
        e.Graphics.FillRectangle(bg, r);
        if (e.Index == tabs.SelectedIndex)
        {
            using var accent = new SolidBrush(Color.FromArgb(0, 129, 116));
            e.Graphics.FillRectangle(accent, r.X, r.Y, r.Width, 3);
        }
        int iconSize = Math.Min(LogicalToDeviceUnits(20), r.Height - LogicalToDeviceUnits(8)), inset = LogicalToDeviceUnits(8), gap = LogicalToDeviceUnits(5), closeWidth = LogicalToDeviceUnits(24);
        e.Graphics.DrawImageUnscaled(uiImages.DocumentIcon(d.Dirty, iconSize), r.X + inset, r.Y + (r.Height - iconSize) / 2);
        int textX = r.X + inset + iconSize + gap;
        var text = $"{(compare.Contains(d) ? "✓ " : "")}{d.Name}";
        TextRenderer.DrawText(e.Graphics, text, Font, new Rectangle(textX, r.Y + 3, Math.Max(0, r.Right - closeWidth - textX), r.Height - 4), Color.FromArgb(43, 52, 63), TextFormatFlags.EndEllipsis | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
        TextRenderer.DrawText(e.Graphics, "×", Font, new Rectangle(r.Right - closeWidth, r.Y, closeWidth, r.Height), Color.Gray, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
    }
    public Document NewDocument(string? name = null, bool binary = false)
    {
        var d = new Document { Name = name ?? $"새 문서 {++untitled}" };
        docs.Add(d);
        tabs.TabPages.Add(d.Page);
        if (binary)
        {
            d.Hex = new();
            d.Page.Controls.Add(HexPanel(d.Hex));
            d.Hex.Changed += () => { if (!d.Loading) UpdateStatus(); };
        }
        else
        {
            d.Editor = new() { AutoIndent = autoIndent };
            d.Surface = new DocumentSurface(d, store.DirectoryPath);
            d.Page.Controls.Add(d.Surface);
            d.Surface.LocalLinkClicked += path => _ = Guard(async () => { await OpenAsync(path); if (Current?.Surface?.CanPreview == true) await ShowDocumentViewAsync(DocumentViewMode.Preview); });
            _ = d.Editor.Handle;
            d.Editor.Changed += () => { if (!d.Loading) UpdateStatus(); };
            d.Editor.PositionChanged += () => { if (!d.Loading) UpdateStatus(); };
            languages.Apply(d.Editor, "normal");
            macros.Attach(d.Editor, () => !d.Loading && !d.Monitoring && Current == d);
        }
        d.Page.Text = d.Name + "     ";
        tabs.SelectedTab = d.Page;
        ApplyViews();
        UpdateStatus();
        return d;
    }
    Control HexPanel(HexView hex)
    {
        var split = new SplitContainer { Size = new Size(1200, 600), Dock = DockStyle.Fill, FixedPanel = FixedPanel.Panel2, SplitterDistance = 900, Panel2MinSize = 210 };
        var inspector = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, BorderStyle = BorderStyle.None, BackColor = Color.FromArgb(245, 248, 247), Font = new Font("Consolas", 10) };
        var endian = new CheckBox { Text = "Big endian", Dock = DockStyle.Top, Height = 30, Padding = new Padding(5) };
        var label = new Label { Text = "데이터 검사", Dock = DockStyle.Top, Height = 37, Padding = new Padding(8), Font = new Font(Font, FontStyle.Bold) };
        split.Panel1.Controls.Add(hex);
        split.Panel2.Controls.Add(inspector);
        split.Panel2.Controls.Add(endian);
        split.Panel2.Controls.Add(label);
        split.SizeChanged += (_, _) => { if (split.Width > 500) split.SplitterDistance = split.Width - 230; };
        void Refresh()
        {
            if (hex.Length == 0 || hex.Caret >= hex.Length)
            {
                inspector.Text = "파일 끝";
                return;
            }
            int n = (int)Math.Min(8, hex.Length - hex.Caret);
            var bytes = Enumerable.Range(0, n).Select(i => hex.Read(hex.Caret + i)).ToArray();
            string Read(int count, Func<byte[], string> f)
            {
                if (n < count)
                    return "—";
                var b = bytes.Take(count).ToArray();
                if (endian.Checked)
                    Array.Reverse(b);
                return f(b);
            }
            inspector.Text = $"\r\n Offset\r\n 0x{hex.Caret:X}\r\n {hex.Caret:N0} (dec)\r\n\r\n UInt8    {bytes[0]}\r\n Int8     {unchecked((sbyte)bytes[0])}\r\n\r\n UInt16\r\n {Read(2, b => BitConverter.ToUInt16(b).ToString())}\r\n Int16\r\n {Read(2, b => BitConverter.ToInt16(b).ToString())}\r\n\r\n UInt32\r\n {Read(4, b => BitConverter.ToUInt32(b).ToString())}\r\n Int32\r\n {Read(4, b => BitConverter.ToInt32(b).ToString())}\r\n\r\n UInt64\r\n {Read(8, b => BitConverter.ToUInt64(b).ToString())}\r\n\r\n Float32\r\n {Read(4, b => BitConverter.ToSingle(b).ToString("G9"))}\r\n Float64\r\n {Read(8, b => BitConverter.ToDouble(b).ToString("G17"))}\r\n\r\n {(hex.InsertMode ? "삽입" : "덮어쓰기")} 모드\r\n 수정 바이트: 주황색\r\n\r\n Ctrl+G  오프셋 이동\r\n Ctrl+F  Hex 검색";
        }
        hex.Changed += Refresh;
        endian.CheckedChanged += (_, _) => Refresh();
        return split;
    }
    async Task StartAsync()
    {
        try
        {
            if (!demo)
                await RestoreAsync();
            foreach (var path in initialFiles)
                await Guard(() => OpenAsync(path));
            if (docs.Count == 0)
                NewDocument();
            if (demo)
            {
                Current!.Editor!.SetText("// LHPad — 가볍게 쓰는 나만의 편집기\r\n// Alt + 드래그: 세로 선택 · Ctrl + 클릭: 다중 커서\r\n\r\nconst editor = {\r\n    name: \"LHPad\",\r\n    engine: \"Scintilla + Lexilla\",\r\n    features: [\"Text\", \"Hex\", \"Compare\", \"Recovery\"],\r\n    ready: true\r\n};\r\n\r\nfunction welcome(name) {\r\n    return `안녕하세요, ${name}!`;\r\n}\r\n");
                Current.Language = "javascript.js";
                Current.Name = "welcome.js";
                Current.Page.Text = "welcome.js     ";
                languages.Apply(Current.Editor, Current.Language);
                UpdateStatus();
            }
        }
        catch (Exception ex) { Dialogs.Message(ex.Message, "복원 오류 — 기존 백업은 보존됩니다."); statusRecovery.Text = "일부 복원 보류"; if (docs.Count == 0) NewDocument(); }
        finally { starting = false; ready.TrySetResult(); }
        backupTimer.Start();
        fileMonitorTimer.Start();
        await CheckExternalChangesAsync();
    }
    public async Task OpenAsync(string path, bool? forceBinary = null, EncodingChoice? forceEncoding = null)
    {
        path = Path.GetFullPath(path);
        if (!File.Exists(path))
            throw new FileNotFoundException("파일을 찾을 수 없습니다.", path);
        var existing = docs.FirstOrDefault(d => string.Equals(d.Path, path, StringComparison.OrdinalIgnoreCase));
        if (existing != null)
        {
            tabs.SelectedTab = existing.Page;
            return;
        }
        var detected = FileTypes.Detect(path);
        var d = NewDocument(Path.GetFileName(path), forceBinary ?? detected.Binary);
        d.Path = path;
        d.Encoding = forceEncoding ?? detected.Encoding;
        d.Language = languages.Detect(path);
        d.Loading = true;
        d.Page.Enabled = false;
        busy = true;
        try
        {
            if (d.Hex != null)
            {
                d.Hex.Open(path);
                d.Hex.Go(0);
            }
            else
            {
                d.Editor!.Large = new FileInfo(path).Length > 200L * 1024 * 1024;
                await d.Editor.LoadStreamAsync(path, d.Encoding.Strict, forceEncoding == null ? detected.Skip : MatchingBom(path, d.Encoding), p => statusFile.Text = $"여는 중: {p / 1048576d:N1} MiB");
                languages.Apply(d.Editor, d.Language);
            }
            if (d.Editor?.LoadedState is { } loaded) d.SetDiskState(loaded);
            else d.RememberStamp();
        }
        catch { docs.Remove(d); tabs.TabPages.Remove(d.Page); d.Dispose(); throw; }
        finally { d.Loading = false; d.Page.Enabled = true; busy = false; UpdateStatus(); }
    }
    static int MatchingBom(string path, EncodingChoice encoding)
    {
        var pre = encoding.Strict.GetPreamble();
        if (pre.Length == 0)
            return 0;
        using var f = FileState.Open(path);
        var bytes = new byte[pre.Length];
        return f.Read(bytes) == bytes.Length && bytes.SequenceEqual(pre) ? pre.Length : 0;
    }
    async Task RestoreAsync()
    {
        Session? session;
        try
        {
            session = store.Read();
        }
        catch (Exception ex)
        {
            var original = store.DirectoryPath;
            store = new SessionStore(Path.Combine(original, "new-session-" + DateTime.Now.ToString("yyyyMMdd-HHmmss")));
            throw new IOException($"{ex.Message}\n기존 복원 폴더: {original}\n새 작업은 별도 폴더에 보관합니다: {store.DirectoryPath}", ex);
        }
        if (session == null)
            return;
        whiteSpace = session.WhiteSpace;
        eolVisible = session.EolVisible;
        controlVisible = session.ControlsVisible;
        autoIndent = session.AutoIndent;
        autoIndentItem.Checked = autoIndent;
        var failed = new List<string>();
        foreach (var s in session.Documents)
        {
            Document? d = null;
            try
            {
                string? source = s.Snapshot ?? s.Path;
                if (source == null || !File.Exists(source))
                    throw new IOException($"파일 없음: {source}");
                d = NewDocument(s.Name, s.Binary);
                d.Id = s.Id;
                d.Path = s.Path;
                d.Encoding = s.Encoding;
                d.Language = s.Language;
                d.Loading = true;
                d.Page.Enabled = false;
                if (d.Hex != null)
                {
                    d.Hex.Open(source);
                    d.Hex.Go(s.Anchor);
                    d.Hex.Go(s.Caret, true);
                }
                else
                {
                    d.Editor!.Large = new FileInfo(source).Length > 200L * 1024 * 1024;
                    await d.Editor.LoadStreamAsync(source, s.Snapshot != null ? FileTypes.Utf8.Strict : s.Encoding.Strict, s.Snapshot != null ? 0 : MatchingBom(source, s.Encoding));
                    languages.Apply(d.Editor, s.Language);
                    d.Editor.Call(S.SCI_SETWRAPMODE, s.Wrap ? 1 : 0);
                    d.Editor.Call(S.SCI_SETEOLMODE, s.Eol);
                    d.Editor.Call(S.SCI_SETZOOM, s.Zoom);
                    d.Editor.Select(s.Anchor, s.Caret);
                    if (s.Selections.Count > 1)
                    {
                        d.Editor.Call(S.SCI_CLEARSELECTIONS);
                        for (int i = 0; i < s.Selections.Count; i++)
                        {
                            var sel = s.Selections[i];
                            if (i == 0)
                                d.Editor.Select(sel.Anchor, sel.Caret);
                            else
                                d.Editor.Call(S.SCI_ADDSELECTION, sel.Caret, sel.Anchor);
                        }
                    }
                    d.Editor.Call(S.SCI_SETFIRSTVISIBLELINE, s.FirstLine);
                }
                d.Recovered = s.Dirty;
                d.Snapshot = s.Snapshot;
                d.SnapshotRevision = d.Revision;
                if (d.Surface?.CanPreview == true)
                    await d.Surface.SetModeAsync(s.ViewMode);
                d.RememberStamp();
                if (s.Dirty && s.Stamp != default)
                {
                    d.Stamp = s.Stamp;
                    d.OriginalLength = s.OriginalLength;
                    if (d.LoadedState is { } loaded)
                        d.LoadedState = loaded with { WriteTicks = s.Stamp.Ticks, Length = s.OriginalLength };
                }
            }
            catch (Exception ex) { unavailable.Add(s); failed.Add($"{s.Name}: {ex.Message}"); if (d != null) { docs.Remove(d); tabs.TabPages.Remove(d.Page); d.Dispose(); } }
            finally { if (d != null && !d.Page.IsDisposed) { d.Loading = false; d.Page.Enabled = true; d.Surface?.RequestRefresh(); } }
        }
        if (docs.Count > 0)
            tabs.SelectedIndex = Math.Clamp(session.Active, 0, docs.Count - 1);
        ApplyViews();
        if (failed.Count > 0)
            throw new IOException(string.Join("\n", failed));
    }
    async Task BackupAsync()
    {
        await store.SaveAsync(docs, tabs.SelectedIndex, whiteSpace, eolVisible, controlVisible, unavailable, autoIndent);
        statusRecovery.Text = unavailable.Count == 0 ? "세션 자동 복원" : "일부 복원 보류";
        statusRecovery.ForeColor = Color.FromArgb(0, 119, 103);
    }
    public void UpdateStatus()
    {
        if (IsDisposed)
            return;
        var d = Current;
        foreach (var document in docs)
        {
            var header = new TabHeaderState(document.Name, document.Dirty);
            if (document.Page.Tag is TabHeaderState previous && previous == header)
                continue;
            document.Page.Tag = header;
            int index = tabs.TabPages.IndexOf(document.Page);
            if (index >= 0) tabs.Invalidate(tabs.GetTabRect(index));
        }
        UpdateFoldingUi();
        if (d == null)
            return;
        d.Surface?.UpdateContext();
        sourceButton.Checked = d.Surface?.Mode == DocumentViewMode.Source;
        previewButton.Checked = d.Surface?.Mode == DocumentViewMode.Preview;
        splitButton.Checked = d.Surface?.Mode == DocumentViewMode.Split;
        UpdateMacroUi();
        Text = $"{(d.Dirty ? "● " : "")}{d.Name} — LHPad";
        statusFile.Text = d.Path ?? "저장하지 않은 문서";
        statusEncoding.Text = d.Hex != null ? "Hex / ASCII" : d.Encoding.Name;
        statusLanguage.Text = d.Hex != null ? "Binary" : languages.Items.FirstOrDefault(x => x.Name == d.Language)?.Display ?? d.Language;
        if (d.Editor is { } s)
        {
            statusPosition.Text = $"줄 {s.Call(S.SCI_LINEFROMPOSITION, s.Caret) + 1:N0}  열 {s.Call(S.SCI_GETCOLUMN, s.Caret) + 1:N0}   |   {s.Length:N0} bytes";
            statusMode.Text = $"{(s.Call(S.SCI_GETEOLMODE) == 0 ? "CRLF" : s.Call(S.SCI_GETEOLMODE) == 1 ? "CR" : "LF")}  {(s.Call(S.SCI_GETOVERTYPE) == 0 ? "INS" : "OVR")}{(s.Large ? " · 대용량" : "")}";
        }
        else if (d.Hex is { } h)
        {
            statusPosition.Text = $"0x{h.Caret:X}   |   {h.Length:N0} bytes";
            statusMode.Text = h.InsertMode ? "INS" : "OVR";
        }
    }
    public async Task ShowDocumentViewAsync(DocumentViewMode mode)
    {
        if (Current?.Surface == null)
            return;
        Current.Surface.UpdateContext();
        await Current.Surface.SetModeAsync(mode);
        UpdateStatus();
    }
    void ApplyViews()
    {
        foreach (var d in docs)
            if (d.Editor is { } s)
            {
                s.AutoIndent = autoIndent;
                s.Call(S.SCI_SETVIEWWS, whiteSpace ? S.SCWS_VISIBLEALWAYS : S.SCWS_INVISIBLE);
                s.Call(S.SCI_SETVIEWEOL, eolVisible ? 1 : 0);
                s.Call(S.SCI_SETCONTROLCHARSYMBOL, controlVisible ? 0 : 32);
            }
        UpdateStatus();
    }
    async Task ChooseOpenAsync()
    {
        using var dialog = new OpenFileDialog { Multiselect = true, Filter = "모든 파일|*.*|텍스트 파일|*.txt;*.log;*.json;*.xml;*.html;*.sql" };
        if (dialog.ShowDialog(this) == DialogResult.OK)
            foreach (var p in dialog.FileNames)
                await Guard(() => OpenAsync(p));
    }
    public async Task<bool> SaveAsync(Document? d, bool saveAs = false)
    {
        if (d == null)
            return false;
        if (d.Loading)
            return false;
        RequireEditable(d);
        var target = d.Path;
        if (saveAs || target == null)
        {
            using var dialog = new SaveFileDialog { FileName = d.Name, Filter = "모든 파일|*.*", OverwritePrompt = true };
            if (dialog.ShowDialog(this) != DialogResult.OK)
                return false;
            target = dialog.FileName;
        }
        if (string.Equals(target, d.Path, StringComparison.OrdinalIgnoreCase) && d.ExternallyChanged() && Dialogs.Message(this, "원본 파일이 외부에서 변경되었거나 삭제되었습니다. 현재 편집 내용으로 저장할까요?", "외부 변경", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
            return false;
        target = Path.GetFullPath(target);
        if (docs.Any(x => x != d && string.Equals(x.Path, target, StringComparison.OrdinalIgnoreCase)))
            throw new IOException("다른 탭에 열려 있는 파일입니다.");
        string temp = Path.Combine(Path.GetDirectoryName(target)!, "." + Path.GetFileName(target) + "." + Guid.NewGuid().ToString("N") + ".tmp");
        busy = true;
        d.Page.Enabled = false;
        try
        {
            if (d.Editor != null)
                await d.Editor.WriteStreamAsync(temp, d.Encoding.Strict, d.Encoding.Bom);
            else
                await d.Hex!.WriteAsync(temp);
            bool sameSource = d.Hex != null && string.Equals(d.Hex.SourcePath, target, StringComparison.OrdinalIgnoreCase);
            if (sameSource)
                d.Hex!.ReleaseSource();
            try
            {
                if (File.Exists(target))
                    File.Replace(temp, target, null, true);
                else
                    File.Move(temp, target);
            }
            catch { if (sameSource) d.Hex!.ReopenSource(); throw; }
            d.Path = target;
            d.Name = Path.GetFileName(target);
            d.Page.Text = d.Name + "     ";
            if (d.Editor != null)
                d.Editor.Saved();
            else
                d.Hex!.MarkSaved(target);
            d.EncodingChanged = d.Recovered = false;
            d.RememberStamp();
            return true;
        }
        finally { if (File.Exists(temp)) File.Delete(temp); busy = false; d.Page.Enabled = true; UpdateStatus(); }
    }
    async Task SaveAllAsync()
    {
        foreach (var d in docs.ToArray())
            if (d.Dirty || d.Path == null)
                if (!await SaveAsync(d))
                    break;
    }
    async Task CloseTabAsync(Document? d)
    {
        if (d == null || busy || closing || macros.IsPlaying)
            return;
        if (backup is { IsCompleted: false })
            await backup;
        if (d.Dirty)
        {
            var result = Dialogs.Message(this, $"'{d.Name}' 변경 내용을 저장할까요?\n'아니요'를 선택하면 이 탭의 변경 내용을 버립니다.", "탭 닫기", MessageBoxButtons.YesNoCancel);
            if (result == DialogResult.Cancel)
                return;
            if (result == DialogResult.Yes)
            {
                try
                {
                    if (!await SaveAsync(d))
                        return;
                }
                catch (Exception ex) { Dialogs.Message(ex.Message); return; }
            }
        }
        docs.Remove(d);
        compare.Remove(d);
        tabs.TabPages.Remove(d.Page);
        if (d.Editor != null)
            macros.Detach(d.Editor);
        d.Dispose();
        if (docs.Count == 0)
            NewDocument();
        UpdateStatus();
    }
    async void OnClosing(object? sender, FormClosingEventArgs e)
    {
        if (allowClose)
            return;
        e.Cancel = true;
        if (closing || busy)
            return;
        if (macros.IsPlaying)
        {
            macros.Cancel();
            if (macroPlayback != null)
                try
                {
                    await macroPlayback;
                }
                catch { }
        }
        if (macros.IsRecording)
            macros.Stop();
        closing = true;
        backupTimer.Stop();
        fileMonitorTimer.Stop();
        Enabled = false;
        try
        {
            if (backup is { IsCompleted: false })
                try
                {
                    await backup;
                }
                catch { }
            await BackupAsync();
            windowPlacement.Save(WindowPlacementStore.Capture(this, lastVisibleWindowState));
            macros.Dispose();
            allowClose = true;
            Close();
        }
        catch (Exception ex) { closing = false; Enabled = true; backupTimer.Start(); fileMonitorTimer.Start(); Dialogs.Message(this, "세션 또는 창 설정 저장에 실패해 창을 유지합니다.\n" + ex.Message, "종료 보류", MessageBoxButtons.OK, MessageBoxIcon.Error); }
    }
    void Edit(int message)
    {
        if (Current?.Surface?.PreviewHasFocus == true)
        {
            _ = Guard(() => Current.Surface.EditAsync(message));
            return;
        }
        if (Current?.Editor is { } s)
            s.Call(message);
        else if (Current?.Hex is { } h)
        {
            if (message == S.SCI_UNDO)
                h.Undo();
            if (message == S.SCI_REDO)
                h.Redo();
            if (message == S.SCI_COPY)
                h.Copy();
            if (message == S.SCI_PASTE)
                h.Paste();
            if (message == S.SCI_CUT)
                h.Cut();
            if (message == S.SCI_SELECTALL)
                h.SelectAll();
        }
        UpdateStatus();
    }
    void GoTo()
    {
        if (Current == null)
            return;
        bool hex = Current.Hex != null;
        var value = Dialogs.Input(this, hex ? "오프셋 이동" : "줄 이동", hex ? "오프셋 (0x 접두사: 16진수, 그 외: 10진수)" : "이동할 줄 번호", hex ? "0x0" : "1");
        if (value == null)
            return;
        long n = value.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? long.Parse(value[2..], NumberStyles.HexNumber) : long.Parse(value);
        if (hex)
            Current.Hex!.Go(n);
        else
            Current.Editor!.Call(S.SCI_GOTOLINE, Math.Max(0, n - 1));
    }
    public void ShowFind(bool replace, bool focus = true)
    {
        if (focus && Current?.Surface?.Mode == DocumentViewMode.Preview)
            _ = Guard(() => ShowDocumentViewAsync(DocumentViewMode.Split));
        if (find == null || find.IsDisposed)
            find = new FindForm(this);
        if (focus)
        {
            var s = Current?.Editor;
            if (s != null && s.SelectionEnd - s.SelectionStart is > 0 and < 4096)
                find.SetQuery(s.TextRange(s.SelectionStart, s.SelectionEnd - s.SelectionStart));
            find.Show(this);
            find.Activate();
            find.FocusQuery(replace);
        }
    }
    void ShowEncodingMenu()
    {
        if (menu.Items[5] is ToolStripMenuItem m)
            m.ShowDropDown();
    }
    async Task ReloadEncodingAsync(EncodingChoice choice)
    {
        RequireEditable(Current);
        if (Current?.Path == null || Current.Editor == null)
            throw new IOException("저장된 텍스트 파일에서 사용할 수 있습니다.");
        _ = choice.Strict;
        if (Current.Dirty && Dialogs.Message("저장하지 않은 내용을 버리고 다시 열까요?", "인코딩 다시 해석", MessageBoxButtons.YesNo) != DialogResult.Yes)
            return;
        var d = Current;
        var path = d.Path;
        busy = true;
        d.Page.Enabled = false;
        try
        {
            await d.Editor.LoadStreamAsync(path, choice.Strict, MatchingBom(path, choice));
            d.Encoding = choice;
            d.EncodingChanged = d.Recovered = false;
            d.RememberStamp();
            languages.Apply(d.Editor, d.Language);
        }
        finally { busy = false; d.Page.Enabled = true; UpdateStatus(); }
    }
    async Task ReopenAsync(bool hex)
    {
        RequireEditable(Current);
        var d = Current;
        if (d?.Path == null)
            throw new IOException("저장된 파일에서 사용할 수 있습니다.");
        if (d.Dirty)
            throw new IOException("변경 내용을 저장한 뒤 다시 열어 주세요.");
        var path = d.Path;
        docs.Remove(d);
        tabs.TabPages.Remove(d.Page);
        d.Dispose();
        await OpenAsync(path, hex);
    }
    async Task FormatAsync()
    {
        var d = Current;
        RequireEditable(d);
        if (d?.Editor == null)
            throw new IOException("텍스트 문서에서 사용할 수 있습니다.");
        macros.RecordAction(new()
        {
            Kind = MacroStepKind.Format,
            Query = d.Language
        });
        statusFile.Text = "정렬 중…";
        await MacroEditing.FormatAsync(d.Editor, d.Language);
        UpdateStatus();
    }
    void SortLines(bool descending)
    {
        RequireEditable(Current);
        var s = Current?.Editor;
        if (s == null)
            return;
        macros.RecordAction(new()
        {
            Kind = MacroStepKind.SortLines,
            Descending = descending
        });
        MacroEditing.Sort(s, descending);
    }
    void ColumnEdit()
    {
        RequireEditable(Current);
        var s = Current?.Editor;
        if (s == null)
            return;
        var input = Dialogs.Column(this);
        if (input == null)
            return;
        macros.RecordAction(new()
        {
            Kind = MacroStepKind.Column,
            Number = input.Value.Number,
            Query = input.Value.Text,
            Start = input.Value.Start,
            Increment = input.Value.Step
        });
        MacroEditing.Column(s, input.Value.Number, input.Value.Text, input.Value.Start, input.Value.Step);
    }
    async Task CompareAsync()
    {
        if (compare.Count != 2)
            throw new IOException("Ctrl을 누른 채 탭 두 개를 클릭해 비교 대상을 선택하세요. 탭 우클릭 메뉴에서도 선택할 수 있습니다.");
        var pair = docs.Where(compare.Contains).ToArray();
        if (pair.Any(x => x.Editor == null))
            throw new IOException("줄 비교는 텍스트 탭 두 개에서 지원됩니다.");
        var a = LineDiff.Lines(pair[0].Editor!.AllText());
        var b = LineDiff.Lines(pair[1].Editor!.AllText());
        if (a.Length + b.Length > 400000)
            throw new IOException("비교는 두 문서 합계 400,000줄 이하에서 지원됩니다.");
        statusFile.Text = "줄 비교 중…";
        var result = await Task.Run(() => LineDiff.Compare(a, b));
        new CompareForm(pair[0].Name, pair[1].Name, a, b, result).Show(this);
        UpdateStatus();
    }
    async Task HashAsync()
    {
        var h = Current?.Hex;
        if (h == null)
            throw new IOException("Hex 탭에서 사용할 수 있습니다.");
        string path = Path.Combine(store.DirectoryPath, Guid.NewGuid() + ".hash.tmp");
        try
        {
            statusFile.Text = "SHA-256 계산 중…";
            await h.WriteAsync(path);
            string hash = await Task.Run(() => { using var f = File.OpenRead(path); return Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(f)); });
            Dialogs.Message(this, hash, "SHA-256 — 미저장 편집 내용 포함");
        }
        finally { if (File.Exists(path)) File.Delete(path); UpdateStatus(); }
    }
    async Task ToggleMacroRecordingAsync()
    {
        RequireEditable(Current);
        if (macros.IsRecording)
        {
            macros.Stop();
            return;
        }
        if (Current?.Editor == null || Current.Loading)
            throw new InvalidOperationException("텍스트 탭에서 매크로를 녹화하세요.");
        if (Current.Surface?.Mode == DocumentViewMode.Preview)
            await ShowDocumentViewAsync(DocumentViewMode.Source);
        macros.Start();
        Current.Editor.Focus();
    }
    async Task PlayMacroAsync(IReadOnlyList<MacroStep>? steps = null, int times = 1, bool untilEnd = false)
    {
        var d = Current;
        RequireEditable(d);
        if (d?.Editor == null || d.Loading)
            throw new InvalidOperationException("편집 가능한 텍스트 탭에서 실행하세요.");
        if (backup is { IsCompleted: false })
            try
            {
                await backup;
            }
            catch { }
        if (d.Surface?.Mode == DocumentViewMode.Preview)
            await ShowDocumentViewAsync(DocumentViewMode.Source);
        find?.Hide();
        macroPlayback = macros.RunAsync(d.Editor, steps, times, untilEnd);
        var result = await macroPlayback;
        UpdateStatus();
        macroStatus.Text = $"매크로 {result.Completed:N0}회 · {result.Reason}";
    }
    async Task RepeatMacroAsync()
    {
        using var dialog = new MacroRunDialog(macros);
        if (dialog.ShowDialog(this) == DialogResult.OK)
            await PlayMacroAsync(dialog.Steps, dialog.Times, dialog.UntilEnd);
    }
    void SaveRecordedMacro()
    {
        string? name = Dialogs.Input(this, "매크로 저장", "매크로 이름");
        if (name != null)
            Try(() => macros.SaveLast(name));
    }
    void UpdateMacroUi()
    {
        if (IsDisposed)
            return;
        bool playing = macros.IsPlaying, recording = macros.IsRecording, text = Current?.Editor != null && Current.Loading == false && Current.Monitoring == false;
        if (controlsInPlayback != playing)
        {
            controlsInPlayback = playing;
            foreach (ToolStripItem root in menu.Items)
                root.Enabled = !playing || ReferenceEquals(root, macroMenu);
            foreach (ToolStripItem item in toolbar.Items)
                item.Enabled = !playing;
        }
        tabs.Enabled = !playing;
        UpdateFileWatchingUi();
        sourceButton.Enabled = !playing && text;
        previewButton.Enabled = splitButton.Enabled = !playing && Current?.Surface?.CanPreview == true;
        macroRecordButton.Enabled = !playing && (text || recording);
        var recordImage = recording ? uiImages.RecordActive : uiImages.RecordIdle;
        if (!ReferenceEquals(macroRecordButton.Image, recordImage)) macroRecordButton.Image = recordImage;
        macroRecordButton.DisplayStyle = ToolStripItemDisplayStyle.Image;
        macroRecordButton.AccessibleDescription = recording ? "매크로 녹화가 켜져 있습니다." : "매크로 녹화 대기 상태입니다.";
        macroStopButton.Enabled = playing || recording;
        macroPlayButton.Enabled = !playing && !recording && text && macros.Last.Count > 0;
        macroRepeatButton.Enabled = !playing && !recording && text && (macros.Last.Count > 0 || macros.Saved.Count > 0);
        if (macroRecordItem != null)
            macroRecordItem.Enabled = macroRecordButton.Enabled;
        if (macroStopItem != null)
            macroStopItem.Enabled = macroStopButton.Enabled;
        if (macroPlayItem != null)
            macroPlayItem.Enabled = macroPlayButton.Enabled;
        if (macroRepeatItem != null)
            macroRepeatItem.Enabled = macroRepeatButton.Enabled;
        if (macroSaveItem != null)
            macroSaveItem.Enabled = !playing && !recording && macros.Last.Count > 0;
        if (macroManageItem != null)
            macroManageItem.Enabled = !playing && !recording && macros.Saved.Count > 0;
        savedMacroMenu.Enabled = !playing && !recording && text && macros.Saved.Count > 0;
        string signature = string.Join('|', macros.Saved.Select(m => m.Id + ":" + m.Name));
        if (signature != savedMacroSignature)
        {
            savedMacroSignature = signature;
            savedMacroMenu.DropDownItems.Clear();
            foreach (var macro in macros.Saved)
                savedMacroMenu.DropDownItems.Add(macro.Name, null, (_, _) => _ = Guard(() => PlayMacroAsync(macro.Steps)));
        }
        if (playing)
            macroStatus.Text = $"매크로 실행 {macros.Completed:N0}회 · Esc 중지";
        else if (macros.Error != null)
            macroStatus.Text = macros.Error;
        else
            macroStatus.Text = $"{(recording ? macros.RecordedCount : macros.Last.Count):N0}단계";
        macroStatus.ForeColor = Color.FromArgb(0, 120, 105);
    }
    protected override void OnLoad(EventArgs e)
    {
        base.OnLoad(e);
        windowPlacement.Restore(this);
    }
    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        if (WindowState != FormWindowState.Minimized) lastVisibleWindowState = WindowState;
    }
    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);
        if (disposing)
        {
            fileMonitorTimer.Dispose();
            toolbarArtwork.Dispose();
            uiImages.Dispose();
        }
    }
    public async Task Guard(Func<Task> operation)
    {
        try
        {
            await operation();
        }
        catch (Exception ex) { Dialogs.Message(this, ex.Message, "LHPad", MessageBoxButtons.OK, MessageBoxIcon.Warning); UpdateStatus(); }
    }
    void Try(Action operation)
    {
        try
        {
            operation();
        }
        catch (Exception ex) { Dialogs.Message(this, ex.Message, "LHPad", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
    }
    protected override bool ProcessCmdKey(ref Message msg, Keys key)
    {
        if (key == Keys.Escape && macros.IsPlaying)
        {
            macros.Cancel();
            return true;
        }
        if (key == (Keys.Control | Keys.Tab) || key == (Keys.Control | Keys.Shift | Keys.Tab))
        {
            if (tabs.TabCount > 0)
                tabs.SelectedIndex = (tabs.SelectedIndex + (key.HasFlag(Keys.Shift) ? -1 : 1) + tabs.TabCount) % tabs.TabCount;
            return true;
        }
        return base.ProcessCmdKey(ref msg, key);
    }
}

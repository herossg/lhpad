using System.Text;

namespace LHPad;

internal sealed partial class MainForm
{
    readonly System.Windows.Forms.Timer fileMonitorTimer = new() { Interval = 1000 };
    readonly ToolStripButton monitorButton = new("로그 모니터링"), followTailButton = new("끝 따라가기");
    readonly ToolStripStatusLabel monitorStatusLabel = new();
    ToolStripMenuItem? monitorMenuItem, followTailMenuItem;
    bool checkingFiles, pollingFiles;
    // The native dialog is replaced only by the integration-test harness.
    internal Func<Document, FileState, bool>? ExternalReloadPrompt;

    void BuildFileWatchingMenu(ToolStripMenuItem view)
    {
        view.DropDownItems.Add(new ToolStripSeparator());
        view.DropDownItems.Add("외부 파일 변경 확인", null, async (_, _) => await CheckExternalChangesAsync(true));
        monitorMenuItem = new("로그 모니터링", null, (_, _) => _ = Guard(ToggleCurrentMonitoringAsync));
        followTailMenuItem = new("끝 따라가기", null, (_, _) => ToggleFollowTail());
        view.DropDownItems.AddRange([monitorMenuItem, followTailMenuItem]);
    }
    internal static void RequireEditable(Document? d)
    {
        if (d?.Monitoring == true)
            throw new InvalidOperationException("로그 모니터링 중에는 읽기 전용입니다. 모니터링을 끈 후 편집하거나 저장하세요.");
    }
    void UpdateFileWatchingUi()
    {
        var d = Current;
        bool available = !busy && !closing && !macros.IsPlaying && !macros.IsRecording;
        bool monitoring = d?.Monitoring == true;
        monitorButton.Enabled = available && d?.Editor != null && d.Path != null;
        monitorButton.Checked = monitoring;
        followTailButton.Enabled = available && monitoring;
        followTailButton.Checked = monitoring && d!.FollowTail;
        if (monitorMenuItem != null) { monitorMenuItem.Enabled = monitorButton.Enabled; monitorMenuItem.Checked = monitoring; }
        if (followTailMenuItem != null) { followTailMenuItem.Enabled = followTailButton.Enabled; followTailMenuItem.Checked = followTailButton.Checked; }
        // Encoding changes cannot be applied to a live decoder.
        menu.Items[5].Enabled = !macros.IsPlaying && !monitoring;
        monitorStatusLabel.Text = monitoring ? $"로그 · {(d!.FollowTail ? "끝 따라가기" : "위치 유지")} · {d.MonitorStatus}" : "";
        monitorStatusLabel.ForeColor = Color.FromArgb(0, 120, 105);
    }
    public async Task CheckExternalChangesAsync(bool forcePrompt = false)
    {
        if (starting || closing || busy || checkingFiles || pollingFiles || macros.IsPlaying || macros.IsRecording || IsDisposed)
            return;
        checkingFiles = true;
        try
        {
            foreach (var d in docs.ToArray())
            {
                if (d.Path == null || d.Loading || d.Monitoring || d.Page.IsDisposed) continue;
                FileState state;
                try { state = await Task.Run(() => FileState.Read(d.Path)); }
                catch (IOException) { continue; } // A writer can briefly hold an exclusive lock.
                catch (UnauthorizedAccessException) { continue; }
                if (closing || busy || IsDisposed || d.Page.IsDisposed) break;
                if (state == d.LoadedState || !forcePrompt && state == d.NotifiedState) continue;
                bool reload;
                if (ExternalReloadPrompt != null)
                    reload = ExternalReloadPrompt(d, state);
                else if (!state.Exists)
                {
                    Dialogs.Message(this, $"'{d.Name}' 파일이 외부에서 삭제되었거나 이동되었습니다.\n현재 문서 내용은 유지합니다. 필요하면 다른 이름으로 저장하세요.", "외부 파일 변경", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    reload = false;
                }
                else
                {
                    string warning = d.Dirty ? "\n\n저장하지 않은 편집 내용이 있습니다. 다시 불러오면 현재 편집 내용이 사라집니다." : "";
                    reload = Dialogs.Message(this, $"'{d.Name}' 파일이 다른 프로그램에서 변경되었습니다.\n디스크의 최신 내용으로 다시 불러올까요?{warning}\n\n아니요: 현재 내용 유지", "외부 파일 변경", MessageBoxButtons.YesNo, d.Dirty ? MessageBoxIcon.Warning : MessageBoxIcon.Question) == DialogResult.Yes;
                }
                d.NotifiedState = state;
                if (reload && state.Exists)
                    try { await ReloadFromDiskAsync(d); }
                    catch (Exception ex) { Dialogs.Message(this, ex.Message + "\n현재 문서 내용은 유지됩니다.", "다시 불러오기 실패", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
            }
        }
        finally { checkingFiles = false; }
    }
    public async Task ReloadFromDiskAsync(Document d)
    {
        if (d.Path == null || !docs.Contains(d) || d.Loading || d.Monitoring)
            throw new IOException("다시 불러올 수 없는 문서입니다.");
        if (backup is { IsCompleted: false }) await backup;
        busy = d.Loading = true;
        d.Page.Enabled = false;
        try
        {
            if (d.Editor is { } s)
            {
                long anchor = s.Anchor, caret = s.Caret, line = (long)s.Call(S.SCI_GETFIRSTVISIBLELINE);
                using var staged = new Sci();
                _ = staged.Handle;
                staged.Large = new FileInfo(d.Path).Length > 200L * 1024 * 1024;
                await staged.LoadStreamAsync(d.Path, d.Encoding.Strict, MatchingBom(d.Path, d.Encoding));
                if (closing || d.Page.IsDisposed) return;
                s.UseDocumentFrom(staged);
                s.Large = staged.Large;
                s.Call(S.SCI_SETEOLMODE, (long)staged.Call(S.SCI_GETEOLMODE));
                s.Call(S.SCI_EMPTYUNDOBUFFER);
                s.Saved();
                s.Select(Math.Min(anchor, s.Length), Math.Min(caret, s.Length));
                s.Call(S.SCI_SETFIRSTVISIBLELINE, line);
                d.SetDiskState(staged.LoadedState!.Value);
                languages.Apply(s, d.Language);
            }
            else
            {
                long anchor = d.Hex!.Anchor, caret = d.Hex.Caret;
                var state = FileState.Read(d.Path);
                d.Hex.Open(d.Path);
                d.Hex.Go(anchor);
                d.Hex.Go(caret, true);
                d.SetDiskState(state);
            }
            d.EncodingChanged = d.Recovered = false;
            d.Snapshot = null;
            d.SnapshotRevision = -1;
        }
        finally
        {
            busy = d.Loading = false;
            if (!d.Page.IsDisposed) d.Page.Enabled = true;
            d.Surface?.RequestRefresh();
            UpdateStatus();
        }
    }
    async Task ToggleCurrentMonitoringAsync()
    {
        if (Current is not { } d) return;
        if (d.Monitoring) StopMonitoring(d);
        else await StartMonitoringAsync(d);
    }
    public async Task StartMonitoringAsync(Document d)
    {
        if (d.Monitoring) return;
        if (busy || closing || macros.IsPlaying || macros.IsRecording || d.Loading)
            throw new IOException("현재 작업이 끝난 뒤 모니터링을 시작하세요.");
        if (d.Editor == null || d.Path == null)
            throw new IOException("저장된 텍스트 파일에서 로그 모니터링을 사용할 수 있습니다.");
        if (d.Dirty)
            throw new IOException("저장하지 않은 편집 내용이 있습니다. 먼저 저장한 뒤 모니터링을 시작하세요.");
        if (d.ExternallyChanged())
        {
            await CheckExternalChangesAsync(true);
            if (d.ExternallyChanged()) throw new IOException("디스크의 최신 내용을 다시 불러온 후 모니터링을 시작하세요.");
        }
        if (backup is { IsCompleted: false }) await backup;
        busy = true;
        try
        {
            var tailer = await LogTailer.CreateAsync(d.Path, d.Encoding.Strict, d.LoadedState!.Value);
            if (closing || d.Page.IsDisposed) return;
            d.Monitor = tailer;
            d.MonitorStatus = "읽기 전용";
            d.Editor.Call(S.SCI_SETREADONLY, 1);
            if (d.Surface?.Mode != DocumentViewMode.Source) await d.Surface!.SetModeAsync(DocumentViewMode.Source);
            if (d.FollowTail) d.Editor.Select(d.Editor.Length, d.Editor.Length);
        }
        finally { busy = false; UpdateStatus(); }
    }
    public void StopMonitoring(Document d)
    {
        if (d.Monitor == null) return;
        // An explicitly stopped backlog is a partial view, so never present it as a complete saved file.
        if (d.Monitor.HasPendingInput || d.LoadedState is { } state && d.Monitor.Offset < state.Length) d.Recovered = true;
        d.Monitor = null;
        d.Editor?.Call(S.SCI_SETREADONLY, 0);
        d.MonitorStatus = "";
        UpdateStatus();
    }
    void ToggleFollowTail()
    {
        if (Current is not { Monitoring: true, Editor: { } s } d) return;
        d.FollowTail = !d.FollowTail;
        if (d.FollowTail) s.Select(s.Length, s.Length);
        UpdateStatus();
    }
    public async Task PollMonitorsAsync()
    {
        if (pollingFiles || checkingFiles || starting || busy || closing || macros.IsPlaying || IsDisposed || backup is { IsCompleted: false }) return;
        if (!docs.Any(d => d.Monitoring)) return;
        pollingFiles = true;
        bool uiChanged = false;
        void SetMonitorStatus(Document d, string value)
        {
            if (d.MonitorStatus == value) return;
            d.MonitorStatus = value;
            if (Current == d) uiChanged = true;
        }
        try
        {
            foreach (var d in docs.ToArray())
            {
                if (d.Monitor is not { } monitor || d.Editor is not { } s || d.Page.IsDisposed) continue;
                try
                {
                    var update = await monitor.PollAsync();
                    if (closing || d.Page.IsDisposed || d.Monitor != monitor) continue;
                    if (update.Missing) { SetMonitorStatus(d, "파일 재생성 대기"); continue; }
                    if (update.Reset || update.Text.Length > 0)
                    {
                        if (Current == d) uiChanged = true;
                        d.Loading = true;
                        s.Call(S.SCI_SETREADONLY, 0);
                        s.Call(S.SCI_SETUNDOCOLLECTION, 0);
                        long first = (long)s.Call(S.SCI_GETFIRSTVISIBLELINE), anchor = s.Anchor, caret = s.Caret;
                        try
                        {
                            if (update.Reset) s.Call(S.SCI_CLEARALL);
                            var bytes = Encoding.UTF8.GetBytes(update.Text);
                            if (bytes.Length > 0) s.Bytes(S.SCI_APPENDTEXT, bytes, bytes.Length);
                            s.Saved();
                            if (d.FollowTail) s.Select(s.Length, s.Length);
                            else
                            {
                                s.Select(Math.Min(anchor, s.Length), Math.Min(caret, s.Length));
                                s.Call(S.SCI_SETFIRSTVISIBLELINE, first);
                            }
                            if (!s.Large && s.Length > 200L * 1024 * 1024) { s.Large = true; languages.Apply(s, d.Language); }
                        }
                        finally
                        {
                            s.Call(S.SCI_EMPTYUNDOBUFFER);
                            s.Call(S.SCI_SETUNDOCOLLECTION, 1);
                            s.Call(S.SCI_SETREADONLY, 1);
                            d.Loading = false;
                            d.Surface?.RequestRefresh();
                        }
                    }
                    d.SetDiskState(update.State);
                    SetMonitorStatus(d, update.Offset < update.State.Length ? $"읽는 중 {update.Offset / 1048576d:N1} MiB" : "읽기 전용");
                }
                catch (DecoderFallbackException)
                {
                    StopMonitoring(d);
                    statusRecovery.Text = "로그 인코딩 불일치: 인코딩 확인 후 다시 시작";
                    statusRecovery.ForeColor = Color.DarkOrange;
                }
                catch (IOException) { monitor.RetryFromStart(); SetMonitorStatus(d, "파일 읽기 재시도 대기"); }
                catch (UnauthorizedAccessException) { SetMonitorStatus(d, "파일 접근 대기"); }
            }
        }
        finally { pollingFiles = false; if (uiChanged) UpdateStatus(); }
    }
}

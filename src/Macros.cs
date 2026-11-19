using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace LHPad;

internal enum MacroStepKind
{
    Native, Find, Replace, ReplaceAll, ConvertEol, Format, SortLines, Column
}
internal sealed record MacroStep
{
    public MacroStepKind Kind
    {
        get; init;
    }
    public int Message
    {
        get; init;
    }
    public long WParam
    {
        get; init;
    }
    public long LParam
    {
        get; init;
    }
    public byte[]? Data
    {
        get; init;
    }
    public string Query { get; init; } = "";
    public string Replacement { get; init; } = "";
    public int Flags
    {
        get; init;
    }
    public bool Backwards
    {
        get; init;
    }
    public bool Wrap
    {
        get; init;
    }
    public bool Descending
    {
        get; init;
    }
    public bool Number
    {
        get; init;
    }
    public long Start
    {
        get; init;
    }
    public long Increment
    {
        get; init;
    }
}
internal sealed record SavedMacro(string Id, string Name, List<MacroStep> Steps)
{
    public override string ToString() => Name;
}
internal sealed record MacroFile(int Version, List<MacroStep> Last, List<SavedMacro> Saved);
internal sealed record MacroRunResult(long Completed, string Reason, bool Cancelled);

internal sealed class MacroController : IDisposable
{
    const int MaxSteps = 100000, MaxStringBytes = 4 * 1024 * 1024;
    readonly string path;
    readonly Dictionary<Sci, (Func<bool> CanCapture, Action<int, long, long> Handler)> editors = [];
    readonly List<MacroStep> recorded = [];
    List<MacroStep> last = [];
    readonly List<SavedMacro> saved = [];
    CancellationTokenSource? cancellation;
    int suppressed;
    bool precedingCr;
    public bool IsRecording
    {
        get; private set;
    }
    public bool IsPlaying
    {
        get; private set;
    }
    public IReadOnlyList<MacroStep> Last => last;
    public IReadOnlyList<SavedMacro> Saved => saved;
    public int RecordedCount => recorded.Count;
    public long Completed
    {
        get; private set;
    }
    public string? Error
    {
        get; private set;
    }
    public event Action? StateChanged;

    public MacroController(string profile)
    {
        path = Path.Combine(profile, "macros.json");
        foreach (var candidate in new[] { path, path + ".bak" })
        {
            if (!File.Exists(candidate))
                continue;
            try
            {
                var file = JsonSerializer.Deserialize<MacroFile>(File.ReadAllText(candidate)) ?? throw new IOException("빈 매크로 파일입니다.");
                if (file.Version != 1)
                    throw new IOException("지원하지 않는 매크로 버전입니다.");
                Validate(file.Last);
                if (file.Saved == null)
                    throw new InvalidDataException("저장된 매크로 목록이 없습니다.");
                foreach (var item in file.Saved)
                {
                    if (item == null || string.IsNullOrWhiteSpace(item.Name) || item.Name.Length > 100 || string.IsNullOrEmpty(item.Id))
                        throw new InvalidDataException("저장된 매크로 정보가 올바르지 않습니다.");
                    Validate(item.Steps);
                }
                last = file.Last;
                saved.AddRange(file.Saved);
                Error = null;
                break;
            }
            catch (Exception ex) when (ex is IOException or JsonException or InvalidDataException or ArgumentException)
            {
                Error = "매크로 파일을 읽지 못했습니다: " + ex.Message;
            }
        }
        if (Error != null && File.Exists(path))
            File.Copy(path, path + ".damaged-" + Guid.NewGuid().ToString("N"), false);
    }

    public void Attach(Sci editor, Func<bool>? canCapture = null)
    {
        if (editors.ContainsKey(editor))
            return;
        Action<int, long, long> handler = (message, wp, lp) => Capture(editor, message, wp, lp);
        editors.Add(editor, (canCapture ?? (() => true), handler));
        editor.MacroRecorded += handler;
        if (IsRecording)
            editor.Call(S.SCI_STARTRECORD);
    }
    public void Detach(Sci editor)
    {
        if (!editors.Remove(editor, out var entry))
            return;
        if (!editor.IsDisposed)
            editor.Call(S.SCI_STOPRECORD);
        editor.MacroRecorded -= entry.Handler;
    }
    public void Start()
    {
        if (IsPlaying)
            throw new InvalidOperationException("실행 중인 매크로를 먼저 중지하세요.");
        recorded.Clear();
        precedingCr = false;
        Error = null;
        IsRecording = true;
        foreach (var editor in editors.Keys.Where(e => !e.IsDisposed))
            editor.Call(S.SCI_STARTRECORD);
        StateChanged?.Invoke();
    }
    public void Stop()
    {
        if (!IsRecording)
            return;
        IsRecording = false;
        foreach (var editor in editors.Keys.Where(e => !e.IsDisposed))
            editor.Call(S.SCI_STOPRECORD);
        last = recorded.ToList();
        Save();
        StateChanged?.Invoke();
    }
    public IDisposable SuppressCapture()
    {
        suppressed++;
        return new CaptureScope(() => suppressed--);
    }
    sealed class CaptureScope(Action release) : IDisposable
    {
        bool disposed; public void Dispose()
        {
            if (!disposed)
            {
                disposed = true;
                release();
            }
        }
    }
    public void RecordAction(MacroStep step)
    {
        if (!IsRecording || IsPlaying || suppressed > 0)
            return;
        if (recorded.Count >= MaxSteps)
        {
            Error = "매크로 녹화 한도(100,000단계)에 도달했습니다.";
            Stop();
            return;
        }
        Validate([step]);
        recorded.Add(step);
        precedingCr = false;
        StateChanged?.Invoke();
    }
    void Capture(Sci editor, int message, long wp, long lp)
    {
        if (!IsRecording || IsPlaying || suppressed > 0 || !editors[editor].CanCapture() || !MacroCommands.Allowed.Contains(message))
            return;
        try
        {
            byte[]? data = null;
            if (MacroCommands.StringCommands.Contains(message))
            {
                if (lp == 0)
                    data = [];
                else if (message is S.SCI_ADDTEXT or S.SCI_APPENDTEXT)
                {
                    if (wp < 0 || wp > MaxStringBytes)
                        throw new InvalidDataException("녹화할 텍스트가 4 MiB를 초과합니다.");
                    data = new byte[(int)wp];
                    Marshal.Copy((nint)lp, data, 0, data.Length);
                }
                else
                {
                    string text = Marshal.PtrToStringUTF8((nint)lp) ?? "";
                    if (Encoding.UTF8.GetByteCount(text) > MaxStringBytes)
                        throw new InvalidDataException("녹화할 텍스트가 4 MiB를 초과합니다.");
                    data = Encoding.UTF8.GetBytes(text);
                }
                lp = 0;
            }
            if (message == S.SCI_REPLACESEL && data is { Length: > 0 and <= 2 })
            {
                string text = Encoding.UTF8.GetString(data);
                if (text is "\r" or "\n" or "\r\n")
                {
                    if (precedingCr && text == "\n")
                    {
                        precedingCr = false;
                        return;
                    }
                    bool cr = text == "\r";
                    RecordAction(new()
                    {
                        Message = S.SCI_NEWLINE
                    });
                    precedingCr = cr;
                    return;
                }
            }
            RecordAction(new()
            {
                Message = message,
                WParam = wp,
                LParam = lp,
                Data = data
            });
        }
        catch (Exception ex) when (ex is InvalidDataException or ArgumentException)
        {
            Error = ex.Message;
            Stop();
        }
    }
    public SavedMacro SaveLast(string name)
    {
        if (IsRecording || IsPlaying)
            throw new InvalidOperationException("녹화·실행을 마친 뒤 저장하세요.");
        if (last.Count == 0)
            throw new InvalidOperationException("저장할 녹화 내용이 없습니다.");
        name = name.Trim();
        if (name.Length is 0 or > 100)
            throw new ArgumentException("이름은 1~100자로 입력하세요.");
        if (saved.Any(m => m.Name.Equals(name, StringComparison.OrdinalIgnoreCase)))
            throw new ArgumentException("같은 이름의 매크로가 있습니다. 다른 이름을 입력하세요.");
        var macro = new SavedMacro(Guid.NewGuid().ToString("N"), name, last.ToList());
        saved.Add(macro);
        Save();
        StateChanged?.Invoke();
        return macro;
    }
    public void Delete(string id)
    {
        saved.RemoveAll(m => m.Id == id);
        Save();
        StateChanged?.Invoke();
    }
    public void Rename(string id, string name)
    {
        int index = saved.FindIndex(m => m.Id == id);
        if (index < 0)
            return;
        name = name.Trim();
        if (name.Length is 0 or > 100 || saved.Any(m => m.Id != id && m.Name.Equals(name, StringComparison.OrdinalIgnoreCase)))
            throw new ArgumentException("중복되지 않는 1~100자의 이름을 입력하세요.");
        saved[index] = saved[index] with
        {
            Name = name
        };
        Save();
        StateChanged?.Invoke();
    }
    void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string tmp = path + ".tmp";
        File.WriteAllText(tmp, JsonSerializer.Serialize(new MacroFile(1, last, saved), new JsonSerializerOptions { WriteIndented = true }));
        using (var stream = new FileStream(tmp, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            stream.Flush(true);
        if (File.Exists(path))
            File.Replace(tmp, path, path + ".bak", true);
        else
            File.Move(tmp, path);
    }
    static void Validate(IReadOnlyList<MacroStep> steps)
    {
        if (steps == null || steps.Count > MaxSteps)
            throw new InvalidDataException("매크로 단계 목록이 올바르지 않습니다.");
        foreach (var step in steps)
        {
            if (step == null || !Enum.IsDefined(step.Kind))
                throw new InvalidDataException("지원하지 않는 매크로 동작입니다.");
            if (step.Data?.Length > MaxStringBytes || Encoding.UTF8.GetByteCount(step.Query) > MaxStringBytes || Encoding.UTF8.GetByteCount(step.Replacement) > MaxStringBytes)
                throw new InvalidDataException("매크로 텍스트가 너무 큽니다.");
            if (step.Kind == MacroStepKind.Native)
            {
                if (!MacroCommands.Allowed.Contains(step.Message))
                    throw new InvalidDataException("허용되지 않는 편집 명령입니다.");
                if (MacroCommands.StringCommands.Contains(step.Message))
                {
                    if (step.Data == null || step.LParam != 0)
                        throw new InvalidDataException("매크로 문자열이 올바르지 않습니다.");
                    if (step.Message is S.SCI_ADDTEXT or S.SCI_APPENDTEXT && step.WParam != step.Data.Length)
                        throw new InvalidDataException("매크로 문자열 길이가 일치하지 않습니다.");
                }
                else if (step.LParam != 0 || step.Data != null)
                    throw new InvalidDataException("허용되지 않는 명령 매개변수입니다.");
            }
            if (step.Kind == MacroStepKind.ConvertEol && step.WParam is < 0 or > 2)
                throw new InvalidDataException("올바르지 않은 줄바꿈 형식입니다.");
        }
    }
    public void Cancel() => cancellation?.Cancel();

    public async Task<MacroRunResult> RunAsync(Sci editor, IReadOnlyList<MacroStep>? steps = null, int times = 1, bool untilEnd = false)
    {
        if (IsRecording || IsPlaying)
            throw new InvalidOperationException("매크로 녹화·실행 중에는 다른 매크로를 실행할 수 없습니다.");
        var macro = (steps ?? last).ToArray();
        Validate(macro);
        if (macro.Length == 0)
            throw new InvalidOperationException("먼저 매크로를 녹화하거나 저장된 매크로를 선택하세요.");
        if (times < 1 || times > 1000000)
            throw new ArgumentOutOfRangeException(nameof(times));
        if (editor.IsDisposed || editor.Call(S.SCI_GETREADONLY) != 0)
            throw new InvalidOperationException("편집 가능한 텍스트 문서가 필요합니다.");
        IsPlaying = true;
        Completed = 0;
        cancellation = new();
        bool wasEnabled = editor.Enabled;
        editor.Enabled = false;
        editor.Call(S.SCI_BEGINUNDOACTION);
        StateChanged?.Invoke();
        string reason = "완료";
        bool cancelled = false;
        var yield = Stopwatch.StartNew();
        long? lastEmpty = null;
        try
        {
            while (untilEnd || Completed < times)
            {
                cancellation.Token.ThrowIfCancellationRequested();
                long beforeLength = editor.Length, beforeCaret = editor.Caret, beforeLine = (long)editor.Call(S.SCI_LINEFROMPOSITION, beforeCaret), beforeLines = (long)editor.Call(S.SCI_GETLINECOUNT), beforeRevision = editor.Revision;
                foreach (var step in macro)
                {
                    cancellation.Token.ThrowIfCancellationRequested();
                    if (editor.IsDisposed)
                        throw new OperationCanceledException();
                    if (!await ExecuteAsync(editor, step, untilEnd, lastEmpty))
                    {
                        if (editor.Revision != beforeRevision)
                            Completed++;
                        return new(Completed, "검색 결과가 없어 중지했습니다.", false);
                    }
                    if (step.Kind == MacroStepKind.Find || step.Kind == MacroStepKind.Replace)
                        lastEmpty = editor.SelectionStart == editor.SelectionEnd ? editor.Caret : null;
                    if (yield.ElapsedMilliseconds >= 12)
                    {
                        StateChanged?.Invoke();
                        await Task.Delay(1);
                        yield.Restart();
                    }
                }
                Completed++;
                StateChanged?.Invoke();
                if (untilEnd)
                {
                    long afterCaret = editor.Caret, afterLine = (long)editor.Call(S.SCI_LINEFROMPOSITION, afterCaret), afterLines = (long)editor.Call(S.SCI_GETLINECOUNT);
                    if (afterLine < beforeLine && afterLines >= beforeLines)
                    {
                        reason = "커서가 문서 끝 방향으로 이동하지 않아 중지했습니다.";
                        break;
                    }
                    if (afterLine == beforeLine && afterCaret == editor.Length && editor.SelectionStart == editor.SelectionEnd)
                    {
                        reason = "문서 끝에 도달했습니다.";
                        break;
                    }
                    bool progresses = editor.Length - afterCaret < beforeLength - beforeCaret || afterLines < beforeLines;
                    if (!progresses)
                    {
                        reason = "커서 이동이나 줄 삭제가 없어 반복을 중지했습니다.";
                        break;
                    }
                }
                if (yield.ElapsedMilliseconds >= 12)
                {
                    await Task.Delay(1);
                    yield.Restart();
                }
            }
        }
        catch (OperationCanceledException) { cancelled = true; reason = "사용자가 실행을 중지했습니다."; }
        finally
        {
            if (!editor.IsDisposed)
            {
                editor.Call(S.SCI_ENDUNDOACTION);
                editor.Enabled = wasEnabled;
                editor.Call(S.SCI_SCROLLCARET);
            }
            IsPlaying = false;
            cancellation.Dispose();
            cancellation = null;
            StateChanged?.Invoke();
        }
        return new(Completed, reason, cancelled);
    }
    static async Task<bool> ExecuteAsync(Sci s, MacroStep step, bool untilEnd, long? lastEmpty)
    {
        if (step.Kind == MacroStepKind.Native)
        {
            if (step.Data != null)
            {
                var data = new byte[step.Data.Length + 1];
                step.Data.CopyTo(data, 0);
                s.Bytes(step.Message, data, step.WParam);
            }
            else
                s.Call(step.Message, step.WParam, step.LParam);
            return true;
        }
        if (step.Kind == MacroStepKind.ConvertEol)
        {
            s.Call(S.SCI_CONVERTEOLS, step.WParam);
            s.Call(S.SCI_SETEOLMODE, step.WParam);
            return true;
        }
        if (step.Kind == MacroStepKind.Format)
        {
            await MacroEditing.FormatAsync(s, step.Query);
            return true;
        }
        if (step.Kind == MacroStepKind.SortLines)
        {
            MacroEditing.Sort(s, step.Descending);
            return true;
        }
        if (step.Kind == MacroStepKind.Column)
        {
            MacroEditing.Column(s, step.Number, step.Query, step.Start, step.Increment);
            return true;
        }
        if (step.Kind == MacroStepKind.ReplaceAll)
        {
            await Search.ReplaceAllAsync(s, step.Query, step.Replacement, step.Flags);
            return true;
        }
        if (step.Kind == MacroStepKind.Replace)
        {
            var hit = Search.In(s, step.Query, s.SelectionStart, s.SelectionEnd, step.Flags);
            if (hit is { } h && h.Start == s.SelectionStart && h.End == s.SelectionEnd)
            {
                int n = Search.Replace(s, step.Replacement, (step.Flags & S.SCFIND_REGEXP) != 0);
                s.Select(h.Start, h.Start + n);
            }
        }
        long start = step.Backwards ? s.SelectionStart : s.SelectionEnd;
        if (lastEmpty == start && (step.Flags & S.SCFIND_REGEXP) != 0)
            start = (long)s.Call(step.Backwards ? S.SCI_POSITIONBEFORE : S.SCI_POSITIONAFTER, start);
        var found = Search.In(s, step.Query, start, step.Backwards ? 0 : s.Length, step.Flags);
        if (found == null && step.Wrap && !untilEnd)
            found = Search.In(s, step.Query, step.Backwards ? s.Length : 0, start, step.Flags);
        if (found == null)
            return false;
        s.Select(found.Value.Start, found.Value.End);
        return true;
    }
    public void Dispose()
    {
        Cancel();
        if (IsRecording)
            Stop();
        foreach (var editor in editors.Keys.ToArray())
            Detach(editor);
    }
}

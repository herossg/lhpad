using System.Text;
using System.Text.Json;

namespace LHPad;

internal sealed class Document : IDisposable
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string? Path
    {
        get; set;
    }
    public string Name { get; set; } = "새 문서";
    public EncodingChoice Encoding { get; set; } = FileTypes.Utf8;
    public string Language { get; set; } = "normal";
    public TabPage Page { get; } = new() { Padding = Padding.Empty, BackColor = Color.White };
    public Sci? Editor
    {
        get; set;
    }
    public HexView? Hex
    {
        get; set;
    }
    public DocumentSurface? Surface
    {
        get; set;
    }
    public bool Loading
    {
        get; set;
    }
    public bool EncodingChanged
    {
        get; set;
    }
    public bool Recovered
    {
        get; set;
    }
    public bool Dirty => EncodingChanged || Recovered || (Editor?.Modified ?? Hex?.Modified ?? false);
    public long Revision => Editor?.Revision ?? Hex?.Revision ?? 0;
    public string? Snapshot
    {
        get; set;
    }
    public long SnapshotRevision { get; set; } = -1;
    public DateTime Stamp
    {
        get; set;
    }
    public long OriginalLength
    {
        get; set;
    }
    public FileState? LoadedState { get; set; }
    public FileState? NotifiedState { get; set; }
    public LogTailer? Monitor { get; set; }
    public bool FollowTail { get; set; } = true;
    public string MonitorStatus { get; set; } = "";
    public bool Monitoring => Monitor != null;
    public void SetDiskState(FileState state)
    {
        LoadedState = state;
        NotifiedState = null;
        Stamp = state.Exists ? new DateTime(state.WriteTicks, DateTimeKind.Utc) : default;
        OriginalLength = state.Length;
    }
    public void RememberStamp()
    {
        if (Path != null && File.Exists(Path))
        {
            SetDiskState(FileState.Read(Path));
        }
    }
    public bool ExternallyChanged()
    {
        if (Path == null)
            return false;
        var state = FileState.Read(Path);
        return LoadedState is { } loaded ? state != loaded : !state.Exists || state.WriteTicks != Stamp.Ticks || state.Length != OriginalLength;
    }
    public void Dispose() => Page.Dispose();
}
internal record Selection(long Anchor, long Caret);
internal sealed record SessionDocument
{
    public DocumentViewMode ViewMode
    {
        get; init;
    }
    public string Id { get; init; } = ""; public string? Path
    {
        get; init;
    }
    public string Name { get; init; } = "";
    public EncodingChoice Encoding { get; init; } = FileTypes.Utf8; public string Language { get; init; } = "normal";
    public bool Binary
    {
        get; init;
    }
    public bool Dirty
    {
        get; init;
    }
    public string? Snapshot
    {
        get; init;
    }
    public DateTime Stamp
    {
        get; init;
    }
    public long OriginalLength
    {
        get; init;
    }
    public long Caret
    {
        get; init;
    }
    public long Anchor
    {
        get; init;
    }
    public long FirstLine
    {
        get; init;
    }
    public List<Selection> Selections { get; init; } = [];
    public bool Wrap
    {
        get; init;
    }
    public int Eol
    {
        get; init;
    }
    public int Zoom
    {
        get; init;
    }
}
internal sealed record Session
{
    public bool AutoIndent { get; init; } = true;
    public int Version { get; init; } = 1; public int Active
    {
        get; init;
    }
    public List<SessionDocument> Documents { get; init; } = []; public bool WhiteSpace
    {
        get; init;
    }
    public bool EolVisible
    {
        get; init;
    }
    public bool ControlsVisible
    {
        get; init;
    }
}
internal sealed class SessionStore(string directory)
{
    public string DirectoryPath => directory;
    public string Manifest => System.IO.Path.Combine(directory, "session.json");
    public Session? Read()
    {
        Exception? failure = null;
        foreach (var path in new[] { Manifest, Manifest + ".bak" })
        {
            if (!File.Exists(path))
                continue;
            try
            {
                var s = JsonSerializer.Deserialize<Session>(File.ReadAllText(path));
                if (s == null || s.Version != 1)
                    throw new IOException("지원하지 않는 세션 버전입니다.");
                return s;
            }
            catch (Exception ex) { failure = ex; }
        }
        if (failure != null)
            throw new IOException("세션 파일을 읽지 못했습니다. 백업은 유지됩니다.", failure);
        return null;
    }
    public async Task SaveAsync(List<Document> documents, int active, bool whitespace, bool eol, bool controls, List<SessionDocument>? unavailable = null, bool autoIndent = true)
    {
        Directory.CreateDirectory(directory);
        var states = new List<SessionDocument>();
        foreach (var d in documents.ToArray())
        {
            if (d.Loading || d.Page.IsDisposed)
                throw new IOException("문서를 여는 중입니다.");
            if (d.Dirty || d.Path == null)
            {
                if (d.Snapshot == null || d.SnapshotRevision != d.Revision || !File.Exists(d.Snapshot))
                {
                    string final = System.IO.Path.Combine(directory, $"{d.Id}-{Guid.NewGuid():N}.snapshot");
                    string tmp = final + ".tmp";
                    var revision = d.Revision;
                    try
                    {
                        if (d.Editor != null)
                            await d.Editor.WriteStreamAsync(tmp, FileTypes.Utf8.Strict, false);
                        else
                            await d.Hex!.WriteAsync(tmp);
                        File.Move(tmp, final);
                        d.Snapshot = final;
                        d.SnapshotRevision = revision;
                    }
                    catch { if (File.Exists(tmp)) File.Delete(tmp); throw; }
                }
            }
            var s = d.Editor;
            var selections = new List<Selection>();
            if (s != null)
                for (int i = 0; i < (int)s.Call(S.SCI_GETSELECTIONS); i++)
                    selections.Add(new((long)s.Call(S.SCI_GETSELECTIONNANCHOR, i), (long)s.Call(S.SCI_GETSELECTIONNCARET, i)));
            states.Add(new()
            {
                Id = d.Id,
                Path = d.Path,
                Name = d.Name,
                Encoding = d.Encoding,
                Language = d.Language,
                ViewMode = d.Surface?.Mode ?? DocumentViewMode.Source,
                Binary = d.Hex != null,
                Dirty = d.Dirty,
                Snapshot = d.Dirty || d.Path == null ? d.Snapshot : null,
                Caret = s?.Caret ?? d.Hex!.Caret,
                Anchor = s?.Anchor ?? d.Hex!.Anchor,
                FirstLine = (long)(s?.Call(S.SCI_GETFIRSTVISIBLELINE) ?? 0),
                Selections = selections,
                Wrap = s?.Call(S.SCI_GETWRAPMODE) != 0,
                Eol = (int)(s?.Call(S.SCI_GETEOLMODE) ?? 0),
                Zoom = (int)(s?.Call(S.SCI_GETZOOM) ?? 0),
                Stamp = d.Stamp,
                OriginalLength = d.OriginalLength
            });
        }
        if (unavailable != null)
            states.AddRange(unavailable);
        string pending = Manifest + ".tmp";
        await File.WriteAllTextAsync(pending, JsonSerializer.Serialize(new Session { Active = active, Documents = states, WhiteSpace = whitespace, EolVisible = eol, ControlsVisible = controls, AutoIndent = autoIndent }, new JsonSerializerOptions { WriteIndented = true }));
        using (var f = new FileStream(pending, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            f.Flush(true);
        if (File.Exists(Manifest))
            File.Replace(pending, Manifest, Manifest + ".bak", true);
        else
            File.Move(pending, Manifest);
        // Keep snapshots referenced by either manifest generation. Never remove recovery files before commit.
        var keep = states.Select(x => x.Snapshot).OfType<string>().ToHashSet(StringComparer.OrdinalIgnoreCase);
        try
        {
            if (File.Exists(Manifest + ".bak"))
            {
                var old = JsonSerializer.Deserialize<Session>(File.ReadAllText(Manifest + ".bak"));
                foreach (var path in old?.Documents.Select(x => x.Snapshot).OfType<string>() ?? [])
                    keep.Add(path);
            }
            foreach (var path in Directory.GetFiles(directory, "*.snapshot"))
                if (!keep.Contains(path))
                    File.Delete(path);
        }
        catch (IOException) { }
    }
}

namespace LHPad;

// A piece table: unmodified bytes stay on disk; edits reference small immutable byte arrays.
// Positions and piece lengths are Int64, including files larger than 2 GiB.
internal sealed class BinaryBuffer : IDisposable
{
    internal record Piece(long Offset, long Length, byte[]? Data = null);
    FileStream source;
    List<Piece> pieces;
    readonly Stack<List<Piece>> undo = [], redo = [];
    public string SourcePath
    {
        get;
    }
    public long Length => pieces.Sum(x => x.Length);
    public bool Modified => !(pieces.Count == 0 && source.Length == 0 || pieces.Count == 1 && pieces[0].Data == null && pieces[0].Offset == 0 && pieces[0].Length == source.Length);
    public BinaryBuffer(string path)
    {
        SourcePath = path;
        source = OpenFile();
        pieces = source.Length == 0 ? [] : [new(0, source.Length)];
    }
    FileStream OpenFile() => new(SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
    public void Release() => source.Dispose();
    public void Reopen()
    {
        source.Dispose();
        source = OpenFile();
    }
    public byte Read(long offset)
    {
        Span<byte> b = stackalloc byte[1];
        Read(offset, b);
        return b[0];
    }
    public int Read(long offset, Span<byte> destination) => ReadParts(pieces, source, offset, destination);
    internal static int ReadParts(IReadOnlyList<Piece> parts, FileStream file, long offset, Span<byte> destination)
    {
        long start = 0;
        int written = 0;
        foreach (var p in parts)
        {
            long end = start + p.Length;
            if (offset >= end)
            {
                start = end;
                continue;
            }
            long within = Math.Max(0, offset - start);
            int n = (int)Math.Min(destination.Length - written, p.Length - within);
            if (n <= 0)
                break;
            var target = destination.Slice(written, n);
            if (p.Data != null)
                p.Data.AsSpan(checked((int)(p.Offset + within)), n).CopyTo(target);
            else
            {
                int done = 0;
                while (done < n)
                {
                    int count = RandomAccess.Read(file.SafeFileHandle, target[done..], p.Offset + within + done);
                    if (count == 0)
                        throw new EndOfStreamException("원본 바이너리 파일이 변경되었습니다.");
                    done += count;
                }
            }
            written += n;
            offset += n;
            start = end;
            if (written == destination.Length)
                break;
        }
        return written;
    }
    public void Replace(long offset, long remove, byte[] insert)
    {
        if (offset < 0 || remove < 0 || offset + remove > Length)
            throw new ArgumentOutOfRangeException(nameof(offset));
        if (remove == 0 && insert.Length == 0)
            return;
        if (remove == insert.LongLength && remove <= 1024 * 1024)
        {
            var before = new byte[(int)remove];
            Read(offset, before);
            if (before.SequenceEqual(insert))
                return;
        }
        var result = new List<Piece>();
        long position = 0, endRemove = offset + remove;
        bool added = false;
        foreach (var p in pieces)
        {
            long end = position + p.Length;
            if (end <= offset)
            {
                result.Add(p);
            }
            else if (position >= endRemove)
            {
                if (!added)
                {
                    if (insert.Length > 0)
                        result.Add(new(0, insert.Length, insert.ToArray()));
                    added = true;
                }
                result.Add(p);
            }
            else
            {
                if (position < offset)
                    result.Add(p with
                    {
                        Length = offset - position
                    });
                if (!added)
                {
                    if (insert.Length > 0)
                        result.Add(new(0, insert.Length, insert.ToArray()));
                    added = true;
                }
                if (end > endRemove)
                    result.Add(p with
                    {
                        Offset = p.Offset + (endRemove - position),
                        Length = end - endRemove
                    });
            }
            position = end;
        }
        if (!added && insert.Length > 0)
            result.Add(new(0, insert.Length, insert.ToArray()));
        var merged = new List<Piece>();
        foreach (var p in result)
        {
            if (p.Length == 0)
                continue;
            if (merged.Count > 0 && ReferenceEquals(merged[^1].Data, p.Data) && merged[^1].Offset + merged[^1].Length == p.Offset)
                merged[^1] = merged[^1] with
                {
                    Length = merged[^1].Length + p.Length
                };
            else
                merged.Add(p);
        }
        undo.Push(pieces);
        redo.Clear();
        pieces = merged;
    }
    public bool Undo()
    {
        if (!undo.TryPop(out var old))
            return false;
        redo.Push(pieces);
        pieces = old;
        return true;
    }
    public bool Redo()
    {
        if (!redo.TryPop(out var next))
            return false;
        undo.Push(pieces);
        pieces = next;
        return true;
    }
    public bool IsEdited(long offset)
    {
        long position = 0;
        foreach (var p in pieces)
        {
            if (offset < position + p.Length)
                return p.Data != null;
            position += p.Length;
        }
        return false;
    }
    public async Task WriteAsync(string path)
    {
        var parts = pieces.ToArray();
        var sourcePath = SourcePath;
        await Task.Run(() => { using var file = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024, FileOptions.SequentialScan); using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 1024); var buffer = new byte[1024 * 1024]; long length = parts.Sum(x => x.Length); for (long offset = 0; offset < length;) { int n = ReadParts(parts, file, offset, buffer.AsSpan(0, (int)Math.Min(buffer.Length, length - offset))); output.Write(buffer, 0, n); offset += n; } output.Flush(true); });
    }
    public async Task<long> FindAsync(byte[] pattern, long start, long limit, CancellationToken ct)
        => await ScanAsync(pattern, start, limit, false, ct);
    public async Task<long> CountAsync(byte[] pattern, CancellationToken ct)
        => await ScanAsync(pattern, 0, Length, true, ct);
    async Task<long> ScanAsync(byte[] pattern, long start, long limit, bool countMatches, CancellationToken ct)
    {
        var parts = pieces.ToArray();
        var path = SourcePath;
        return await Task.Run(() =>
        {
            if (pattern.Length == 0) return countMatches ? 0L : -1L;
            int[] lps = new int[pattern.Length];
            for (int i = 1, j = 0; i < pattern.Length;)
            {
                if (pattern[i] == pattern[j]) lps[i++] = ++j;
                else if (j > 0) j = lps[j - 1];
                else i++;
            }
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024, FileOptions.SequentialScan);
            long length = Math.Min(limit, parts.Sum(x => x.Length)), count = 0;
            var buffer = new byte[1024 * 1024];
            int matched = 0;
            for (long position = Math.Clamp(start, 0, length); position < length;)
            {
                ct.ThrowIfCancellationRequested();
                int n = ReadParts(parts, file, position, buffer.AsSpan(0, (int)Math.Min(buffer.Length, length - position)));
                for (int i = 0; i < n; i++, position++)
                {
                    if ((i & 16383) == 0) ct.ThrowIfCancellationRequested();
                    byte value = buffer[i];
                    while (matched > 0 && value != pattern[matched]) matched = lps[matched - 1];
                    if (value == pattern[matched]) matched++;
                    if (matched != pattern.Length) continue;
                    if (!countMatches) return position - pattern.Length + 1;
                    count++;
                    matched = 0; // Count non-overlapping matches, as in text search.
                }
            }
            return countMatches ? count : -1L;
        }, ct);
    }
    public void Dispose() => source.Dispose();
}

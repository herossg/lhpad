using Microsoft.Win32.SafeHandles;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace LHPad;

internal readonly record struct FileState(bool Exists, long Length, long WriteTicks, uint Volume, ulong Identity)
{
    [StructLayout(LayoutKind.Sequential)]
    struct Info
    {
        public uint Attributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME Created, Accessed, Written;
        public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetFileInformationByHandle(SafeFileHandle handle, out Info info);
    public static FileStream Open(string path) => new(path, FileMode.Open, FileAccess.Read,
        FileShare.ReadWrite | FileShare.Delete, 65536, FileOptions.Asynchronous);
    public static FileState Read(FileStream file)
    {
        if (!GetFileInformationByHandle(file.SafeFileHandle, out var info))
            throw new IOException("파일 상태를 읽지 못했습니다.", new Win32Exception(Marshal.GetLastWin32Error()));
        long time = ((long)(uint)info.Written.dwHighDateTime << 32) | (uint)info.Written.dwLowDateTime;
        return new(true, ((long)info.SizeHigh << 32) | info.SizeLow,
            DateTime.FromFileTimeUtc(time).Ticks, info.Volume, ((ulong)info.IndexHigh << 32) | info.IndexLow);
    }
    public static FileState Read(string path)
    {
        try { using var file = Open(path); return Read(file); }
        catch (FileNotFoundException) { return default; }
        catch (DirectoryNotFoundException) { return default; }
    }
}

internal sealed record TailUpdate(string Text, bool Reset, bool Missing, FileState State, long Offset);

// Poll one bounded block at a time. A decoder retains incomplete multibyte characters between polls.
internal sealed class LogTailer
{
    readonly string path;
    readonly Encoding encoding;
    Decoder decoder;
    FileState state;
    long offset;
    long pendingBytes;
    byte[] prefix = [], tail = [];
    bool missing;
    public const int BlockSize = 256 * 1024;
    public long Offset => offset;
    public bool HasPendingInput => pendingBytes > 0;
    public void RetryFromStart() => missing = true;
    LogTailer(string path, Encoding encoding) { this.path = path; this.encoding = encoding; decoder = encoding.GetDecoder(); }

    public static async Task<LogTailer> CreateAsync(string path, Encoding encoding, FileState expected)
    {
        var monitor = new LogTailer(path, encoding);
        await using var file = FileState.Open(path);
        monitor.state = FileState.Read(file);
        if (monitor.state != expected)
            throw new IOException("파일이 변경되었습니다. 다시 불러온 후 모니터링을 시작하세요.");
        monitor.offset = expected.Length;
        monitor.prefix = await SampleAsync(file, 0, (int)Math.Min(64, expected.Length));
        monitor.tail = await SampleAsync(file, Math.Max(0, expected.Length - 64), (int)Math.Min(64, expected.Length));
        // Stateful encodings such as ISO-2022 carry shift state from the existing file into later writes.
        if (encoding.CodePage is >= 50220 and <= 50229 or 52936 or 65000)
        {
            file.Position = 0;
            var bytes = new byte[BlockSize];
            var chars = new char[encoding.GetMaxCharCount(bytes.Length)];
            long remaining = expected.Length;
            while (remaining > 0)
            {
                int n = await file.ReadAsync(bytes.AsMemory(0, (int)Math.Min(bytes.Length, remaining)));
                if (n == 0) throw new IOException("모니터링 시작 중 파일이 변경되었습니다.");
                monitor.decoder.GetChars(bytes, 0, n, chars, 0, false);
                remaining -= n;
            }
        }
        return monitor;
    }
    static async Task<byte[]> SampleAsync(FileStream file, long position, int count)
    {
        var data = new byte[count];
        file.Position = position;
        int done = 0;
        while (done < count)
        {
            int n = await file.ReadAsync(data.AsMemory(done));
            if (n == 0) break;
            done += n;
        }
        return data[..done];
    }
    public async Task<TailUpdate> PollAsync()
    {
        FileStream file;
        try { file = FileState.Open(path); }
        catch (FileNotFoundException) { missing = true; return new("", false, true, default, offset); }
        catch (DirectoryNotFoundException) { missing = true; return new("", false, true, default, offset); }
        await using (file)
        {
            var current = FileState.Read(file);
            bool reset = missing || current.Volume != state.Volume || current.Identity != state.Identity || current.Length < offset;
            // Check the prefix and the old EOF boundary, including truncate-and-regrow with the same file ID.
            if (!reset && current != state)
            {
                var head = await SampleAsync(file, 0, prefix.Length);
                var boundary = await SampleAsync(file, Math.Max(0, offset - tail.Length), tail.Length);
                reset = !head.SequenceEqual(prefix) || !boundary.SequenceEqual(tail) || current.Length == offset;
            }
            long start = reset ? 0 : offset;
            var nextDecoder = reset ? encoding.GetDecoder() : decoder;
            if (reset)
            {
                var preamble = encoding.GetPreamble();
                // UTF-8's strict encoding may be configured without an output BOM; recognize its input BOM too.
                if (encoding.CodePage == 65001) preamble = [0xef, 0xbb, 0xbf];
                if (preamble.Length > 0 && (await SampleAsync(file, 0, preamble.Length)).SequenceEqual(preamble))
                    start = preamble.Length;
            }
            int count = (int)Math.Min(BlockSize, Math.Max(0, current.Length - start));
            var bytes = await SampleAsync(file, start, count);
            var chars = new char[encoding.GetMaxCharCount(bytes.Length)];
            int used = nextDecoder.GetChars(bytes, 0, bytes.Length, chars, 0, false);
            long nextPending = Math.Max(0, (reset ? 0 : pendingBytes) + bytes.Length - encoding.GetByteCount(chars, 0, used));
            long nextOffset = start + bytes.Length;
            var nextTail = await SampleAsync(file, Math.Max(0, nextOffset - 64), (int)Math.Min(64, nextOffset));
            // Don't commit an inconsistent sample while a writer truncates the file during this read.
            var after = FileState.Read(file);
            if (after.Length < nextOffset)
            {
                missing = true;
                throw new IOException("파일 교체 중입니다. 다음 주기에 다시 읽습니다.");
            }
            if (reset || prefix.Length == 0)
                prefix = await SampleAsync(file, 0, (int)Math.Min(64, nextOffset));
            tail = nextTail;
            decoder = nextDecoder;
            pendingBytes = nextPending;
            state = current;
            offset = nextOffset;
            missing = false;
            return new(new string(chars, 0, used), reset, false, current, offset);
        }
    }
}

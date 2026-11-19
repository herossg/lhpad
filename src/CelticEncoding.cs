using System.Text;

namespace LHPad;

// The Windows/.NET code-page providers do not consistently expose ISO-8859-14.
// Use the Unicode Consortium's original byte mapping, included with its license.
internal sealed class CelticEncoding : Encoding
{
    static readonly char[] forward = Build();
    static readonly Dictionary<char, byte> reverse = forward.Select((c, i) => (c, i)).ToDictionary(x => x.c, x => (byte)x.i);
    static char[] Build()
    {
        var map = new char[256];
        foreach (var line in File.ReadLines(Path.Combine(AppContext.BaseDirectory, "data", "iso8859-14.txt")))
        {
            if (!line.StartsWith("0x"))
                continue;
            var cols = line.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
            map[System.Convert.ToInt32(cols[0], 16)] = (char)System.Convert.ToInt32(cols[1], 16);
        }
        return map;
    }
    public CelticEncoding() : base(28604, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback) { }
    public override string WebName => "iso-8859-14";
    public override int GetByteCount(char[] chars, int index, int count)
    {
        for (int i = index; i < index + count; i++)
            if (!reverse.ContainsKey(chars[i]))
                throw new EncoderFallbackException("ISO-8859-14로 표현할 수 없는 문자입니다.");
        return count;
    }
    public override int GetBytes(char[] chars, int charIndex, int charCount, byte[] bytes, int byteIndex)
    {
        for (int i = 0; i < charCount; i++)
        {
            if (!reverse.TryGetValue(chars[charIndex + i], out byte b))
                throw new EncoderFallbackException("ISO-8859-14로 표현할 수 없는 문자입니다.");
            bytes[byteIndex + i] = b;
        }
        return charCount;
    }
    public override int GetCharCount(byte[] bytes, int index, int count) => count;
    public override int GetChars(byte[] bytes, int byteIndex, int byteCount, char[] chars, int charIndex)
    {
        for (int i = 0; i < byteCount; i++)
            chars[charIndex + i] = forward[bytes[byteIndex + i]];
        return byteCount;
    }
    public override int GetMaxByteCount(int charCount) => charCount;
    public override int GetMaxCharCount(int byteCount) => byteCount;
}

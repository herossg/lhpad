using System.Text;
using System.Text.Json;
using System.Xml.Linq;

namespace LHPad;

internal record EncodingChoice(string Name, int CodePage, bool Bom = false)
{
    [System.Text.Json.Serialization.JsonIgnore]
    public Encoding Strict => CodePage switch { 28604 => new CelticEncoding(), 65001 => new UTF8Encoding(Bom, true), 1200 => new UnicodeEncoding(false, Bom, true), 1201 => new UnicodeEncoding(true, Bom, true), 12000 => new UTF32Encoding(false, Bom, true), 12001 => new UTF32Encoding(true, Bom, true), _ => Encoding.GetEncoding(CodePage, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback) };
    public override string ToString() => Name;
}
internal record Detection(bool Binary, EncodingChoice Encoding, int Skip);
internal static class FileTypes
{
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern uint GetACP();
    public static int AnsiCodePage => (int)GetACP();
    public static readonly EncodingChoice Utf8 = new("UTF-8", 65001);
    public static readonly EncodingChoice[] Encodings = [new("UTF-8", 65001), new("UTF-8 BOM", 65001, true), new("UTF-16 LE BOM", 1200, true), new("UTF-16 BE BOM", 1201, true), new("UTF-16 LE", 1200), new("UTF-16 BE", 1201), new("UTF-32 LE BOM", 12000, true), new("UTF-32 BE BOM", 12001, true), new("ANSI (시스템)", AnsiCodePage), new("한국어 Windows-949 (CP949)", 949), new("한국어 EUC-KR", 51949), new("한국어 ISO-2022-KR", 50225), new("일본어 Shift-JIS", 932), new("일본어 EUC-JP", 51932), new("일본어 ISO-2022-JP", 50220), new("중국어 GB2312 / GBK", 936), new("중국어 GB18030", 54936), new("중국어 Big5", 950), new("중국어 ISO-2022-CN", 50227), new("아랍어 Windows-1256", 1256), new("발트 Windows-1257", 1257), new("중부 유럽 Windows-1250", 1250), new("키릴 Windows-1251", 1251), new("서유럽 Windows-1252", 1252), new("그리스 Windows-1253", 1253), new("터키 Windows-1254", 1254), new("히브리 Windows-1255", 1255), new("베트남 Windows-1258", 1258), new("태국 TIS-620", 874), new("OEM US 437", 437), new("OEM Latin 850", 850), new("OEM Multilingual 852", 852), new("OEM Cyrillic 855", 855), new("OEM Turkish 857", 857), new("OEM Russian 866", 866), new("KOI8-R", 20866), new("KOI8-U", 21866), new("Macintosh Roman", 10000), new("Macintosh Cyrillic", 10007), new("ISO-8859-1", 28591), new("ISO-8859-2", 28592), new("ISO-8859-3", 28593), new("ISO-8859-4", 28594), new("ISO-8859-5", 28595), new("ISO-8859-6", 28596), new("ISO-8859-7", 28597), new("ISO-8859-8", 28598), new("ISO-8859-9", 28599), new("ISO-8859-13", 28603), new("ISO-8859-15", 28605), new("ISO-8859-14 (Celtic)", 28604), .. new[] { 720, 737, 775, 858, 860, 861, 862, 863, 865, 869 }.Select(cp => new EncodingChoice($"OEM DOS {cp}", cp))];
    public static Detection Detect(string path)
    {
        using var stream = FileState.Open(path);
        var data = new byte[(int)Math.Min(65536, stream.Length)];
        stream.ReadExactly(data);
        if (data.AsSpan().StartsWith(new byte[] { 0xFF, 0xFE, 0, 0 }))
            return new(false, new("UTF-32 LE BOM", 12000, true), 4);
        if (data.AsSpan().StartsWith(new byte[] { 0, 0, 0xFE, 0xFF }))
            return new(false, new("UTF-32 BE BOM", 12001, true), 4);
        if (data.AsSpan().StartsWith(new byte[] { 0xEF, 0xBB, 0xBF }))
            return new(false, new("UTF-8 BOM", 65001, true), 3);
        if (data.AsSpan().StartsWith(new byte[] { 0xFF, 0xFE }))
            return new(false, new("UTF-16 LE BOM", 1200, true), 2);
        if (data.AsSpan().StartsWith(new byte[] { 0xFE, 0xFF }))
            return new(false, new("UTF-16 BE BOM", 1201, true), 2);
        if (data.Length >= 4 && (data.AsSpan(0, 2).SequenceEqual("MZ"u8) || data.AsSpan(0, 4).SequenceEqual("PK\x03\x04"u8) || data.AsSpan(0, 4).SequenceEqual(new byte[] { 0x89, 0x50, 0x4E, 0x47 }) || data.AsSpan(0, 4).SequenceEqual("%PDF"u8) || data.AsSpan(0, 4).SequenceEqual("\x7F"u8.ToArray().Concat("ELF"u8.ToArray()).ToArray())))
            return new(true, Utf8, 0);
        if (data.Length >= 8)
        {
            int even = 0, odd = 0;
            for (int i = 0; i < data.Length; i++)
                if (data[i] == 0)
                {
                    if ((i & 1) == 0)
                        even++;
                    else
                        odd++;
                }
            if (odd > data.Length * .25 && even < data.Length * .02)
                return new(false, new("UTF-16 LE", 1200), 0);
            if (even > data.Length * .25 && odd < data.Length * .02)
                return new(false, new("UTF-16 BE", 1201), 0);
        }
        int bad = data.Count(x => x == 0 || x < 9 || x > 13 && x < 32);
        if (bad > 0 && (data.Contains((byte)0) || bad > data.Length * .02))
            return new(true, Utf8, 0);
        try
        {
            Utf8.Strict.GetDecoder().Convert(data, new char[data.Length], stream.Length <= data.Length, out _, out _, out _);
            return new(false, Utf8, 0);
        }
        catch (DecoderFallbackException)
        {
            foreach (var cp in new[] { 949, AnsiCodePage, 1252 }.Distinct())
            {
                try
                {
                    var choice = new EncodingChoice(cp == 949 ? "한국어 Windows-949 (CP949)" : $"Windows-{cp}", cp);
                    choice.Strict.GetDecoder().Convert(data, new char[data.Length], stream.Length <= data.Length, out _, out _, out _);
                    return new(false, choice, 0);
                }
                catch (DecoderFallbackException) { }
            }
            return new(true, Utf8, 0);
        }
    }
}
internal record Language(string Name, string Display, string Lexer);
internal sealed class Languages
{
    public List<Language> Items
    {
        get;
    }
    readonly XDocument styles, words;
    public Languages()
    {
        var root = Path.Combine(AppContext.BaseDirectory, "data");
        Items = JsonSerializer.Deserialize<List<Language>>(File.ReadAllText(Path.Combine(root, "languages.json")))!;
        Items.RemoveAll(x => x.Name is "searchResult" or "user" or "external");
        if (!Items.Any(x => x.Name == "markdown"))
            Items.Add(new("markdown", "Markdown", "markdown"));
        styles = XDocument.Load(Path.Combine(root, "stylers.xml"));
        words = XDocument.Load(Path.Combine(root, "langs.xml"));
    }
    public string Detect(string? path)
    {
        if (path == null)
            return "normal";
        var ext = Path.GetExtension(path).TrimStart('.');
        var file = Path.GetFileName(path);
        if (new[] { "md", "markdown", "mdown", "mkd" }.Contains(ext.ToLowerInvariant()))
            return "markdown";
        if (file.Equals("Dockerfile", StringComparison.OrdinalIgnoreCase))
            return "dockerfile";
        if (file.Equals("CMakeLists.txt", StringComparison.OrdinalIgnoreCase))
            return "cmake";
        var lang = words.Descendants("Language").FirstOrDefault(x => ((string?)x.Attribute("ext") ?? "").Split(' ').Any(e => e.Equals(ext, StringComparison.OrdinalIgnoreCase) && ext != ""));
        var name = (string?)lang?.Attribute("name") ?? "normal";
        return name == "javascript" ? "javascript.js" : name;
    }
    public void Apply(Sci sci, string name)
    {
        var lang = Items.FirstOrDefault(x => x.Name == name) ?? Items[0];
        sci.ConfigureFolding(false);
        sci.Call(S.SCI_SETILEXER, 0, Sci.CreateLexer(sci.Large ? "null" : lang.Lexer));
        sci.Call(S.SCI_STYLECLEARALL);
        if (sci.Large)
            return;
        var styler = styles.Descendants("LexerType").FirstOrDefault(x => (string?)x.Attribute("name") == name);
        var keywords = words.Descendants("Language").FirstOrDefault(x => (string?)x.Attribute("name") == name);
        for (int i = 0; i < 9; i++)
            sci.Str(S.SCI_SETKEYWORDS, "", i);
        var classes = new[] { "instre1", "instre2", "type1", "type2", "type3", "type4", "type5", "type6", "type7" };
        if (styler != null)
            foreach (var style in styler.Elements("WordsStyle"))
            {
                if (!int.TryParse((string?)style.Attribute("styleID"), out int id))
                    continue;
                foreach (var attr in new[] { ("fgColor", S.SCI_STYLESETFORE), ("bgColor", S.SCI_STYLESETBACK) })
                {
                    var hex = (string?)style.Attribute(attr.Item1);
                    if (hex != null && int.TryParse(hex, System.Globalization.NumberStyles.HexNumber, null, out int rgb))
                        sci.Call(attr.Item2, id, ((rgb >> 16) & 255) | (rgb & 0xFF00) | ((rgb & 255) << 16));
                }
                int font = (int?)style.Attribute("fontStyle") ?? 0;
                sci.Call(S.SCI_STYLESETBOLD, id, font & 1);
                sci.Call(S.SCI_STYLESETITALIC, id, (font >> 1) & 1);
                var kw = (string?)style.Attribute("keywordClass");
                int index = Array.IndexOf(classes, kw);
                if (lang.Lexer == "cpp")
                    index = kw switch
                    {
                        "instre1" => 0,
                        "type1" => 1,
                        "type2" => 2,
                        "instre2" => 3,
                        _ => -1
                    };
                if (lang.Lexer == "hypertext")
                    index = kw switch
                    {
                        "instre1" => 0,
                        "instre2" => 5,
                        _ => index
                    };
                if (index >= 0)
                    sci.Str(S.SCI_SETKEYWORDS, string.Join(" ", keywords?.Elements("Keywords").Where(x => (string?)x.Attribute("name") == kw).Select(x => x.Value) ?? []), index);
            }
        // Lexilla's HTML and XML variants require BOTH fold and fold.html. These properties
        // mirror Notepad++'s setHtmlLexer and C-family defaults in the bundled primary source.
        sci.Property("fold", "1");
        sci.Property("fold.compact", "0");
        sci.Property("fold.html", "1");
        sci.Property("fold.html.preprocessor", "1");
        sci.Property("fold.hypertext.comment", "1");
        sci.Property("fold.xml.at.tag.open", "1");
        sci.Property("fold.comment", "1");
        sci.Property("fold.cpp.comment.explicit", "0");
        sci.Property("fold.preprocessor", "1");
        sci.Property("lexer.json.allow.comments", name == "json5" ? "1" : "0");
        sci.Property("lexer.cpp.track.preprocessor", "0");
        sci.ConfigureFolding(lang.Lexer is not ("null" or "markdown"));
        if (name == "markdown")
        {
            for (int id = 6; id <= 11; id++)
            {
                sci.Call(S.SCI_STYLESETFORE, id, ColorTranslator.ToWin32(Color.FromArgb(0, 115, 105)));
                sci.Call(S.SCI_STYLESETBOLD, id, 1);
            }
            foreach (int id in new[] { 2, 3 })
                sci.Call(S.SCI_STYLESETBOLD, id, 1);
            foreach (int id in new[] { 4, 5 })
                sci.Call(S.SCI_STYLESETITALIC, id, 1);
            sci.Call(S.SCI_STYLESETFORE, 18, ColorTranslator.ToWin32(Color.FromArgb(0, 110, 170)));
            foreach (int id in new[] { 19, 20, 21 })
                sci.Call(S.SCI_STYLESETFORE, id, ColorTranslator.ToWin32(Color.FromArgb(150, 70, 25)));
        }
        // Warm only the first chunk; Scintilla continues lexing as lines become visible.
        // Explicit fold-all/depth commands may finish native lexing on demand.
        sci.Call(S.SCI_COLOURISE, 0, Math.Min(sci.Length, 64 * 1024));
    }
}

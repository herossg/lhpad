using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace LHPad;

internal static partial class Formatting
{
    public static string Format(string input, string language, string eol)
    {
        var result = language switch
        {
            "json" => JsonSerializer.Serialize(JsonDocument.Parse(input).RootElement, new JsonSerializerOptions { WriteIndented = true, Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping }),
            "xml" => Xml(input),
            "html" => Html(input),
            "sql" => Sql(input),
            _ => throw new InvalidOperationException("정렬은 HTML, XML, JSON, SQL에서 지원됩니다. 언어 메뉴에서 문서 종류를 선택해 주세요.")
        };
        return result.Replace("\r\n", "\n").Replace('\r', '\n').Replace("\n", eol);
    }
    static string Xml(string input)
    {
        using var reader = XmlReader.Create(new StringReader(input), new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null });
        var doc = XDocument.Load(reader);
        var b = new StringBuilder();
        using (var writer = XmlWriter.Create(b, new XmlWriterSettings { Indent = true, IndentChars = "  ", OmitXmlDeclaration = doc.Declaration == null, NewLineChars = "\n" }))
            doc.Save(writer);
        // XmlWriter over a StringBuilder emits utf-16 in declarations; preserve the source declaration.
        var result = b.ToString();
        if (doc.Declaration != null)
            result = Regex.Replace(result, "^<\\?xml[^?]*\\?>", doc.Declaration.ToString());
        return result;
    }
    static readonly HashSet<string> Blocks = new("html head body div section article main nav header footer aside ul ol li table thead tbody tfoot tr td th form fieldset p h1 h2 h3 h4 h5 h6 dl dt dd figure figcaption script style pre textarea title meta link hr br input img".Split(' '), StringComparer.OrdinalIgnoreCase);
    static readonly HashSet<string> VoidTags = new("area base br col embed hr img input link meta param source track wbr".Split(' '), StringComparer.OrdinalIgnoreCase);
    // Tokens keep attributes, comments, raw text and entities verbatim. Only block boundaries are indented.
    public static string Html(string input)
    {
        var tokens = Regex.Matches(input, @"<!--[\s\S]*?-->|<(script|style|pre|textarea)\b[^>]*>[\s\S]*?</\1\s*>|<(?:(?:""[^""]*"")|(?:'[^']*')|[^'"">])*>|[^<]+|<", RegexOptions.IgnoreCase, TimeSpan.FromSeconds(3)).Select(x => x.Value).ToArray();
        var b = new StringBuilder();
        int depth = 0;
        bool lineStart = true;
        void NewLine()
        {
            while (b.Length > 0 && b[^1] == ' ')
                b.Length--;
            if (b.Length > 0 && b[^1] != '\n')
                b.Append('\n');
            lineStart = true;
        }
        void Add(string value)
        {
            if (lineStart)
                b.Append(' ', Math.Max(0, depth) * 2);
            b.Append(value);
            lineStart = false;
        }
        for (int i = 0; i < tokens.Length; i++)
        {
            var token = tokens[i];
            if (string.IsNullOrWhiteSpace(token))
            {
                bool Boundary(string value)
                {
                    var m = Regex.Match(value, @"^</?\s*([\w:-]+)");
                    return m.Success && Blocks.Contains(m.Groups[1].Value);
                }
                if (i > 0 && i + 1 < tokens.Length && !Boundary(tokens[i - 1]) && !Boundary(tokens[i + 1]))
                    Add(token);
                continue;
            }
            if (!token.StartsWith('<') || token == "<")
            {
                Add(token);
                continue;
            }
            var match = Regex.Match(token, @"^</?\s*([\w:-]+)");
            var name = match.Groups[1].Value;
            bool close = token.StartsWith("</"), raw = Regex.IsMatch(token, @"^<(script|style|pre|textarea)\b", RegexOptions.IgnoreCase);
            bool block = Blocks.Contains(name) || token.StartsWith("<!");
            bool self = VoidTags.Contains(name) || token.EndsWith("/>") || token.StartsWith("<!");
            if (close)
                depth = Math.Max(0, depth - 1);
            if (block)
                NewLine();
            Add(token);
            if (!close && !self && !raw)
                depth++;
            if (block && (close || self || raw || i + 1 < tokens.Length && tokens[i + 1].TrimStart().StartsWith('<')))
                NewLine();
        }
        return b.ToString().TrimEnd();
    }
    public static List<string> SqlTokens(string input)
    {
        // Strings, quoted identifiers, dollar quoted strings, and nested block comments are indivisible.
        var result = new List<string>();
        int i = 0;
        while (i < input.Length)
        {
            if (char.IsWhiteSpace(input[i]))
            {
                i++;
                continue;
            }
            int start = i;
            char c = input[i++];
            if (c is 'N' or 'n' or 'E' or 'e' or 'X' or 'x' or 'B' or 'b' && i < input.Length && input[i] == '\'')
            {
                c = input[i++];
            }
            if (c == '-' && i < input.Length && input[i] == '-')
            {
                while (i < input.Length && input[i] != '\n' && input[i] != '\r')
                    i++;
            }
            else if (c == '/' && i < input.Length && input[i] == '*')
            {
                i++;
                int depth = 1;
                while (i < input.Length && depth > 0)
                {
                    if (i + 1 < input.Length && input[i] == '/' && input[i + 1] == '*')
                    {
                        depth++;
                        i += 2;
                    }
                    else if (i + 1 < input.Length && input[i] == '*' && input[i + 1] == '/')
                    {
                        depth--;
                        i += 2;
                    }
                    else
                        i++;
                }
                if (depth != 0)
                    throw new FormatException("닫히지 않은 SQL 주석입니다.");
            }
            else if (c is '\'' or '"' or '`' or '[')
            {
                char end = c == '[' ? ']' : c;
                bool closed = false;
                while (i < input.Length)
                {
                    if (input[i++] != end)
                        continue;
                    if (i < input.Length && input[i] == end)
                    {
                        i++;
                        continue;
                    }
                    closed = true;
                    break;
                }
                if (!closed)
                    throw new FormatException("닫히지 않은 SQL 문자열입니다.");
            }
            else if (c == '$' && Regex.Match(input[start..], @"^\$[A-Za-z_0-9]*\$") is { Success: true } dollar)
            {
                string tag = dollar.Value;
                int end = input.IndexOf(tag, start + tag.Length, StringComparison.Ordinal);
                if (end < 0)
                    throw new FormatException("닫히지 않은 dollar 문자열입니다.");
                i = end + tag.Length;
            }
            else if (char.IsLetterOrDigit(c) || c is '_' or '@' or '#' or '$')
            {
                while (i < input.Length && (char.IsLetterOrDigit(input[i]) || input[i] is '_' or '@' or '#' or '$'))
                    i++;
            }
            else if (i < input.Length && new[] { "<=", ">=", "<>", "!=", "||", "::", "->", "=>", "!<", "!>" }.Contains(input.Substring(start, 2)))
                i++;
            result.Add(input[start..i]);
        }
        return result;
    }
    public static string Sql(string input)
    {
        var tokens = SqlTokens(input);
        var b = new StringBuilder();
        int depth = 0;
        string previous = "";
        bool lineStart = true;
        var clauses = new HashSet<string>("SELECT FROM WHERE JOIN LEFT RIGHT INNER OUTER FULL CROSS ON GROUP ORDER HAVING UNION EXCEPT INTERSECT LIMIT OFFSET FETCH WITH INSERT UPDATE DELETE VALUES SET RETURNING WHEN ELSE END BEGIN DECLARE".Split(' '));
        void NewLine()
        {
            while (b.Length > 0 && b[^1] == ' ')
                b.Length--;
            if (b.Length > 0 && b[^1] != '\n')
                b.Append('\n');
            lineStart = true;
        }
        foreach (var token in tokens)
        {
            var upper = token.ToUpperInvariant();
            if (token == ")")
                depth = Math.Max(0, depth - 1);
            if (clauses.Contains(upper))
                NewLine();
            if (lineStart)
            {
                b.Append(' ', depth * 2);
                lineStart = false;
            }
            else if (token is not "," and not ";" and not ")" and not "." && previous is not "(" and not ".")
                b.Append(' ');
            b.Append(token);
            if (token == "(")
                depth++;
            if (token is "," or ";" || token.StartsWith("--"))
                NewLine();
            previous = token;
        }
        var result = b.ToString().TrimEnd();
        if (!SqlTokens(result).SequenceEqual(tokens))
            throw new FormatException("SQL 토큰 보존 검사에 실패했습니다.");
        return result;
    }
}

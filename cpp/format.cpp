#include "core.h"
#include <regex>
namespace lp {
static std::string lower(std::string s) {
    for (auto &c : s)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}
static void trimEnd(std::string &s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
}
std::vector<std::string> sqlTokens(const std::string &s) {
    std::vector<std::string> out;
    size_t i = 0;
    auto word = [](unsigned char c) {
        return c >= 128 || isalnum(c) || c == '_' || c == '@' || c == '#' || c == '$';
    };
    while (i < s.size()) {
        if (isspace(static_cast<unsigned char>(s[i]))) {
            i++;
            continue;
        }
        size_t start = i;
        char c = s[i++];
        if (std::string("NnEeXxBb").find(c) != std::string::npos && i < s.size() && s[i] == '\'')
            c = s[i++];
        if (c == '-' && i < s.size() && s[i] == '-') {
            while (i < s.size() && s[i] != '\n' && s[i] != '\r')
                i++;
        } else if (c == '/' && i < s.size() && s[i] == '*') {
            i++;
            int depth = 1;
            while (i < s.size() && depth) {
                if (s.compare(i, 2, "/*") == 0) {
                    depth++;
                    i += 2;
                } else if (s.compare(i, 2, "*/") == 0) {
                    depth--;
                    i += 2;
                } else
                    i++;
            }
            if (depth)
                fail(L"닫히지 않은 SQL 주석입니다.");
        } else if (c == '\'' || c == '"' || c == '`' || c == '[') {
            char end = c == '[' ? ']' : c;
            bool closed = false;
            while (i < s.size()) {
                if (s[i++] != end)
                    continue;
                if (i < s.size() && s[i] == end) {
                    i++;
                    continue;
                }
                closed = true;
                break;
            }
            if (!closed)
                fail(L"닫히지 않은 SQL 문자열입니다.");
        } else if (c == '$') {
            size_t k = i;
            while (k < s.size() && (isalnum(static_cast<unsigned char>(s[k])) || s[k] == '_'))
                k++;
            if (k < s.size() && s[k] == '$') {
                auto tag = s.substr(start, k - start + 1);
                auto end = s.find(tag, k + 1);
                if (end == std::string::npos)
                    fail(L"닫히지 않은 dollar 문자열입니다.");
                i = end + tag.size();
            } else
                while (i < s.size() && word(s[i]))
                    i++;
        } else if (word(c)) {
            while (i < s.size() && word(s[i]))
                i++;
        } else if (i < s.size()) {
            static const std::set<std::string> pairs = {"<=", ">=", "<>", "!=", "||",
                                                        "::", "->", "=>", "!<", "!>"};
            if (pairs.contains(s.substr(start, 2)))
                i++;
        }
        out.push_back(s.substr(start, i - start));
    }
    return out;
}
static std::string sql(const std::string &input) {
    auto tokens = sqlTokens(input);
    std::string b, previous;
    int depth = 0;
    bool start = true;
    static const std::set<std::string> clauses = {
        "select",    "from",  "where",     "join",  "left",  "right",  "inner",  "outer",
        "full",      "cross", "on",        "group", "order", "having", "union",  "except",
        "intersect", "limit", "offset",    "fetch", "with",  "insert", "update", "delete",
        "values",    "set",   "returning", "when",  "else",  "end",    "begin",  "declare"};
    auto newline = [&] {
        while (!b.empty() && b.back() == ' ')
            b.pop_back();
        if (!b.empty() && b.back() != '\n')
            b += '\n';
        start = true;
    };
    for (auto &t : tokens) {
        if (t == ")")
            depth = std::max(0, depth - 1);
        if (clauses.contains(lower(t)))
            newline();
        if (start) {
            b.append(depth * 2, ' ');
            start = false;
        } else if (t != "," && t != ";" && t != ")" && t != "." && previous != "(" && previous != ".")
            b += ' ';
        b += t;
        if (t == "(")
            depth++;
        if (t == "," || t == ";" || t.starts_with("--"))
            newline();
        previous = t;
    }
    trimEnd(b);
    if (sqlTokens(b) != tokens)
        fail(L"SQL 토큰 보존 검사 실패");
    return b;
}
static std::string html(const std::string &s) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < s.size()) {
        size_t begin = i;
        if (s[i] != '<') {
            auto next = s.find('<', i);
            i = next == std::string::npos ? s.size() : next;
            tokens.push_back(s.substr(begin, i - begin));
            continue;
        }
        if (s.compare(i, 4, "<!--") == 0) {
            auto end = s.find("-->", i + 4);
            i = end == std::string::npos ? s.size() : end + 3;
            tokens.push_back(s.substr(begin, i - begin));
            continue;
        }
        char quote = 0;
        for (i++; i < s.size(); i++) {
            char c = s[i];
            if (quote) {
                if (c == quote)
                    quote = 0;
            } else if (c == '\'' || c == '"')
                quote = c;
            else if (c == '>') {
                i++;
                break;
            }
        }
        auto tag = s.substr(begin, i - begin);
        auto low = lower(tag);
        size_t k = low.starts_with("</") ? 2 : 1, end = k;
        while (end < low.size() && (isalnum(static_cast<unsigned char>(low[end])) || low[end] == '-'))
            end++;
        auto name = low.substr(k, end - k);
        if (!low.starts_with("</") &&
            (name == "script" || name == "style" || name == "pre" || name == "textarea")) {
            auto rest = lower(s.substr(i));
            auto close = rest.find("</" + name);
            if (close != std::string::npos) {
                auto fin = s.find('>', i + close);
                i = fin == std::string::npos ? s.size() : fin + 1;
                tag = s.substr(begin, i - begin);
            }
        }
        tokens.push_back(tag);
    }
    static const std::set<std::string> blocks = {
        "html",   "head",   "body",       "div",    "section", "article", "main",     "nav",
        "header", "footer", "aside",      "ul",     "ol",      "li",      "table",    "thead",
        "tbody",  "tfoot",  "tr",         "td",     "th",      "form",    "fieldset", "p",
        "h1",     "h2",     "h3",         "h4",     "h5",      "h6",      "dl",       "dt",
        "dd",     "figure", "figcaption", "script", "style",   "pre",     "textarea", "title",
        "meta",   "link",   "hr",         "br",     "input",   "img"};
    static const std::set<std::string> voids = {"area",  "base", "br",   "col",   "embed",  "hr",    "img",
                                                "input", "link", "meta", "param", "source", "track", "wbr"};
    std::string b;
    int depth = 0;
    bool start = true;
    auto newline = [&] {
        while (!b.empty() && b.back() == ' ')
            b.pop_back();
        if (!b.empty() && b.back() != '\n')
            b += '\n';
        start = true;
    };
    auto add = [&](const std::string &x) {
        if (start) {
            b.append(depth * 2, ' ');
            start = false;
        }
        b += x;
    };
    for (size_t n = 0; n < tokens.size(); n++) {
        auto &t = tokens[n];
        if (t[0] != '<') {
            if (std::all_of(t.begin(), t.end(), [](unsigned char c) { return isspace(c) != 0; })) {
                if (!start && n + 1 < tokens.size())
                    add(t.find('\n') == std::string::npos ? t : " ");
            } else
                add(t);
            continue;
        }
        bool close = t.starts_with("</");
        auto low = lower(t);
        size_t k = close ? 2 : 1, end = k;
        while (end < low.size() && (isalnum(static_cast<unsigned char>(low[end])) || low[end] == '-'))
            end++;
        auto name = low.substr(k, end - k);
        bool raw = name == "script" || name == "style" || name == "pre" || name == "textarea";
        bool block = blocks.contains(name) || t.starts_with("<!"),
             self = voids.contains(name) || t.ends_with("/>") || t.starts_with("<!");
        if (close)
            depth = std::max(0, depth - 1);
        if (block)
            newline();
        add(t);
        if (!close && !self && !raw)
            depth++;
        if (block && (close || self || raw || n + 1 < tokens.size() && tokens[n + 1].starts_with('<')))
            newline();
    }
    trimEnd(b);
    return b;
}
std::string formatText(const std::string &input, const std::string &lang, const std::string &eol) {
    std::string result;
    if (lang == "json")
        result = Json::parse(input).dump(2, ' ', false);
    else if (lang == "xml") {
        if (lower(input).find("<!doctype") != std::string::npos)
            fail(L"XML DTD와 외부 엔터티는 지원하지 않습니다.");
        pugi::xml_document doc;
        auto status = doc.load_buffer(input.data(), input.size(),
                                      pugi::parse_default | pugi::parse_declaration, pugi::encoding_utf8);
        if (!status)
            fail(L"XML 구문 오류: " + wide(status.description()));
        std::ostringstream stream;
        doc.save(stream, "  ",
                 pugi::format_default | (input.starts_with("<?xml") ? 0 : pugi::format_no_declaration),
                 pugi::encoding_utf8);
        result = stream.str();
    } else if (lang == "html")
        result = html(input);
    else if (lang == "sql")
        result = sql(input);
    else
        fail(L"정렬은 HTML, XML, JSON, SQL에서 지원합니다.");
    std::string out;
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i] == '\r' || result[i] == '\n') {
            if (result[i] == '\r' && i + 1 < result.size() && result[i + 1] == '\n')
                i++;
            out += eol;
        } else
            out += result[i];
    }
    return out;
}
} // namespace lp

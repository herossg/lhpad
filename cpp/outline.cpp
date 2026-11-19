// Navigation aids around the editor: outline (function list) panel, bookmarks and the
// same-document split view.
#include "app.h"
#include <cctype>
#include <regex>
namespace lp {
namespace {
enum class Rules { None, Markdown, CLike, Script, Python, Sql, Shell, PowerShell, Batch, Ini, Go, Rust, Ruby, Perl, Vb, Lua };
Rules rulesFor(const std::string &language) {
    static const std::map<std::string, Rules> table = {
        {"markdown", Rules::Markdown}, {"c", Rules::CLike},          {"cpp", Rules::CLike},
        {"cs", Rules::CLike},          {"objc", Rules::CLike},       {"java", Rules::CLike},
        {"d", Rules::CLike},           {"rc", Rules::CLike},         {"javascript", Rules::Script},
        {"javascript.js", Rules::Script}, {"typescript", Rules::Script}, {"actionscript", Rules::Script},
        {"php", Rules::Script},        {"jsp", Rules::CLike},        {"swift", Rules::Script},
        {"gdscript", Rules::Script},   {"python", Rules::Python},    {"sql", Rules::Sql},
        {"mssql", Rules::Sql},         {"bash", Rules::Shell},       {"powershell", Rules::PowerShell},
        {"batch", Rules::Batch},       {"ini", Rules::Ini},          {"props", Rules::Ini},
        {"toml", Rules::Ini},          {"registry", Rules::Ini},     {"go", Rules::Go},
        {"rust", Rules::Rust},         {"ruby", Rules::Ruby},        {"perl", Rules::Perl},
        {"vb", Rules::Vb},             {"lua", Rules::Lua}};
    auto it = table.find(language);
    return it == table.end() ? Rules::None : it->second;
}
bool identChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80;
}
std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
}
int indentDepth(const std::string &line) {
    int columns = 0;
    for (char c : line) {
        if (c == ' ')
            columns++;
        else if (c == '\t')
            columns += 4;
        else
            break;
    }
    return std::min(columns / 4, 6);
}
bool isKeyword(const std::string &word) {
    static const std::set<std::string> words = {
        "if",     "for",      "while", "switch", "return", "else",  "do",       "case",     "new",
        "delete", "throw",    "catch", "using",  "typedef", "sizeof", "goto",   "await",    "yield",
        "static_assert", "decltype", "alignof", "typeof", "lock", "foreach", "using", "fixed", "checked",
        "unchecked", "elif", "try", "defined", "function", "assert", "co_await", "co_return", "with"};
    return words.contains(word);
}
// C-family function definitions: `type name(args) qualifiers {`, with the brace optionally on the
// next line. Declarations (`;`), calls and control statements are rejected.
std::string clikeFunction(const std::string &line, const std::string &next, bool bare) {
    auto paren = line.find('(');
    if (paren == std::string::npos || paren == 0)
        return {};
    size_t end = paren;
    while (end > 0 && line[end - 1] == ' ')
        end--;
    size_t start = end;
    while (start > 0 && (identChar(line[start - 1]) || line[start - 1] == ':' || line[start - 1] == '~'))
        start--;
    auto name = line.substr(start, end - start);
    while (!name.empty() && name.front() == ':')
        name.erase(0, 1);
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])))
        return {};
    auto bareName = name.substr(name.rfind(':') == std::string::npos ? 0 : name.rfind(':') + 1);
    auto prefix = trim(line.substr(0, start));
    auto firstWord = line.substr(0, line.find_first_of(" \t(<"));
    if (isKeyword(bareName) || isKeyword(firstWord) || bareName == "operator")
        return {};
    if (prefix.find_first_of("=(.,\"'!|?+") != std::string::npos || prefix.find("->") != std::string::npos ||
        prefix.find("return ") != std::string::npos)
        return {};
    if (prefix.empty() && !bare && name.find("::") == std::string::npos)
        return {};
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = paren; i < line.size(); i++) {
        if (line[i] == '(')
            depth++;
        else if (line[i] == ')' && --depth == 0) {
            close = i;
            break;
        }
    }
    if (close == std::string::npos)
        // The signature continues on the next line. A trailing comma is a call unless a return type precedes.
        return line.back() == ';' || (line.back() == ',' && prefix.empty()) ? std::string{} : name;
    auto rest = trim(line.substr(close + 1));
    static const std::regex qualifiers(
        R"(^((const|noexcept|override|final|volatile|mutable|throws\s+[\w.,\s]+|->\s*[\w:<>,\s\*&]+|async)\s*)*(:.*)?(\{.*)?$)");
    if (!rest.empty() && rest.back() == ';')
        return {};
    if (rest.empty())
        return trim(next).starts_with("{") || (!bare && trim(next).starts_with(":")) ? name : std::string{};
    if (rest.size() < 300 && std::regex_match(rest, qualifiers) && (!bare || rest.find('{') != std::string::npos))
        return name;
    return {};
}
} // namespace
bool outlineSupported(const std::string &language) {
    return rulesFor(language) != Rules::None;
}
std::vector<OutlineItem> buildOutline(const std::string &text, const std::string &language) {
    std::vector<OutlineItem> items;
    auto rules = rulesFor(language);
    if (rules == Rules::None)
        return items;
    std::vector<std::string> lines;
    {
        size_t start = 0;
        while (start <= text.size()) {
            auto end = text.find('\n', start);
            auto line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(std::move(line));
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    auto add = [&](std::string name, size_t line, int depth) {
        if (items.size() < 20000 && !name.empty())
            items.push_back({wide(name, CP_UTF8, false), static_cast<Pos>(line), depth});
    };
    using std::regex;
    auto icase = regex::ECMAScript | regex::icase;
    static const regex classLike(
        R"(^\s*(?:(?:public|private|protected|internal|static|abstract|sealed|final|partial|export|default|data|open|inline)\s+)*(class|struct|interface|enum|namespace|union|record|trait|module|object)\s+([A-Za-z_$][\w$:.]*))");
    static const regex scriptFunction(
        R"(^\s*(?:(?:export|default|async|static|public|private|protected|abstract|final|override)\s+)*(?:function\*?|func|def)\s+&?([A-Za-z_$][\w$]*))");
    static const regex scriptAssigned(
        R"(^\s*(?:(?:export|const|let|var|static|public|private|readonly)\s+)*([A-Za-z_$][\w$.]*)\s*[:=]\s*(?:async\s+)?(?:function\b|\([^()]*\)\s*(?::\s*[\w<>\[\]|, ]+)?\s*=>|[A-Za-z_$][\w$]*\s*=>))");
    static const regex python(R"(^(\s*)(?:async\s+)?(def|class)\s+([A-Za-z_]\w*))");
    static const regex sql(
        R"(^\s*(?:create|alter)\s+(?:or\s+(?:replace|alter)\s+)?(?:definer\s*=\s*\S+\s+)?(procedure|proc|function|view|table|trigger|package(?:\s+body)?|type|index)\s+(?:if\s+not\s+exists\s+)?([\w.$\[\]"`]+))",
        icase);
    static const regex shell(R"(^\s*(?:function\s+([A-Za-z_][\w.:-]*)|([A-Za-z_][\w.:-]*)\s*\(\s*\)))");
    static const regex powershell(R"(^\s*(?:function|filter|workflow|class)\s+([\w-]+))", icase);
    static const regex batch(R"(^\s*:([^\s:]\S*))");
    static const regex ini(R"(^\s*\[([^\]]+)\])");
    static const regex go(R"(^\s*func\s+(?:\([^)]*\)\s*)?([A-Za-z_]\w*)|^\s*type\s+([A-Za-z_]\w*)\s+(?:struct|interface))");
    static const regex rust(R"(^\s*(?:pub(?:\([^)]*\))?\s+)?(?:async\s+|const\s+|unsafe\s+|extern\s+"[^"]*"\s+)*(fn|struct|enum|trait|impl|mod|union)\b\s*(?:<[^>]*>\s*)?([A-Za-z_][\w:<>, ]*))");
    static const regex ruby(R"(^\s*(def|class|module)\s+([\w.:?!=]+))");
    static const regex perl(R"(^\s*sub\s+([\w:]+))");
    static const regex vb(R"(^\s*(?:(?:public|private|friend|protected|shared|overrides|overridable|static)\s+)*(sub|function|property\s+(?:get|let|set)|class|module)\s+(\w+))", icase);
    static const regex lua(R"(^\s*(?:local\s+)?function\s+([\w.:]+))");
    bool fenced = false;
    std::smatch m;
    for (size_t i = 0; i < lines.size(); i++) {
        const auto &raw = lines[i];
        if (raw.size() > 400)
            continue;
        auto line = trim(raw);
        if (line.empty())
            continue;
        int depth = indentDepth(raw);
        switch (rules) {
        case Rules::Markdown: {
            if (line.starts_with("```") || line.starts_with("~~~")) {
                fenced = !fenced;
                break;
            }
            if (fenced || raw[0] != '#')
                break;
            size_t level = raw.find_first_not_of('#');
            if (level != std::string::npos && level <= 6 && (raw[level] == ' ' || raw[level] == '\t')) {
                auto title = trim(raw.substr(level));
                while (!title.empty() && title.back() == '#')
                    title.pop_back();
                add(trim(title), i, static_cast<int>(level) - 1);
            }
            break;
        }
        case Rules::CLike:
        case Rules::Script: {
            if (line.starts_with("#") || line.starts_with("//") || line.starts_with("/*") || line.starts_with("*"))
                break;
            if (std::regex_search(raw, m, classLike) && line.back() != ';') {
                add(m[1].str() + " " + m[2].str(), i, depth);
                break;
            }
            if (rules == Rules::Script) {
                if (std::regex_search(raw, m, scriptFunction) || std::regex_search(raw, m, scriptAssigned)) {
                    add(m[1].str() + "()", i, depth);
                    break;
                }
            }
            std::string next;
            for (size_t j = i + 1; j < lines.size() && j <= i + 2 && next.empty(); j++)
                next = trim(lines[j]);
            auto name = clikeFunction(line, next, rules == Rules::Script);
            if (!name.empty())
                add(name + "()", i, depth);
            break;
        }
        case Rules::Python:
            if (std::regex_search(raw, m, python))
                add((m[2] == "class" ? "class " : "") + m[3].str() + (m[2] == "def" ? "()" : ""), i, depth);
            break;
        case Rules::Sql:
            if (std::regex_search(raw, m, sql))
                add(m[1].str() + " " + m[2].str(), i, 0);
            break;
        case Rules::Shell:
            if (!line.starts_with("#") && std::regex_search(raw, m, shell))
                add((m[1].matched ? m[1].str() : m[2].str()) + "()", i, depth);
            break;
        case Rules::PowerShell:
            if (std::regex_search(raw, m, powershell))
                add(m[1].str(), i, depth);
            break;
        case Rules::Batch:
            if (std::regex_search(raw, m, batch))
                add(":" + m[1].str(), i, 0);
            break;
        case Rules::Ini:
            if (std::regex_search(raw, m, ini))
                add("[" + m[1].str() + "]", i, 0);
            break;
        case Rules::Go:
            if (std::regex_search(raw, m, go))
                add(m[1].matched ? m[1].str() + "()" : "type " + m[2].str(), i, depth);
            break;
        case Rules::Rust:
            if (std::regex_search(raw, m, rust))
                add(m[1] == "fn" ? trim(m[2].str()) + "()" : m[1].str() + " " + trim(m[2].str()), i, depth);
            break;
        case Rules::Ruby:
            if (std::regex_search(raw, m, ruby))
                add(m[1] == "def" ? m[2].str() + "()" : m[1].str() + " " + m[2].str(), i, depth);
            break;
        case Rules::Perl:
            if (std::regex_search(raw, m, perl))
                add(m[1].str() + "()", i, depth);
            break;
        case Rules::Vb:
            if (std::regex_search(raw, m, vb))
                add(m[2].str() + (std::tolower(static_cast<unsigned char>(m[1].str()[0])) == 'c' ||
                                          std::tolower(static_cast<unsigned char>(m[1].str()[0])) == 'm'
                                      ? std::string{}
                                      : "()"),
                    i, depth);
            break;
        case Rules::Lua:
            if (std::regex_search(raw, m, lua))
                add(m[1].str() + "()", i, depth);
            break;
        case Rules::None:
            break;
        }
    }
    return items;
}
void App::createOutlinePanel() {
    outlineList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL |
                                      LVS_SHOWSELALWAYS,
                                  0, 0, 0, 0, window, reinterpret_cast<HMENU>(15), instance, nullptr);
    if (!outlineList)
        fail(L"함수 목록 창을 만들 수 없습니다.");
    SendMessageW(outlineList, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ListView_SetExtendedListViewStyle(outlineList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    int index = 0;
    for (auto [title, width, align] : {std::tuple{L"함수 목록", 200, LVCFMT_LEFT}, std::tuple{L"줄", 56, LVCFMT_RIGHT}}) {
        LVCOLUMNW header{};
        header.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        header.pszText = const_cast<wchar_t *>(title);
        header.cx = MulDiv(width, dpi, 96);
        header.fmt = align;
        ListView_InsertColumn(outlineList, index++, &header);
    }
}
void App::showOutline(bool visible) {
    outlineVisible = visible && outlineList;
    if (outlineList)
        ShowWindow(outlineList, outlineVisible ? SW_SHOW : SW_HIDE);
    layout();
    if (outlineVisible)
        refreshOutline(true);
    update();
}
void App::refreshOutline(bool force) {
    if (!outlineVisible || !outlineList)
        return;
    auto d = current();
    std::string id = d ? d->id : std::string{};
    uint64_t revision = d ? d->revision : 0;
    if (!force && id == outlineDocId && revision == outlineRevision)
        return;
    outlineDocId = id;
    outlineRevision = revision;
    std::wstring title = L"함수 목록";
    if (!d || !d->editor) {
        outlineItems.clear();
        title += L" (텍스트 탭 아님)";
    } else if (!outlineSupported(d->language)) {
        outlineItems.clear();
        title += L" (이 언어는 미지원)";
    } else {
        Pos length = std::min<Pos>(sci(d->editor, SCI_GETLENGTH), 16 * 1024 * 1024);
        outlineItems = buildOutline(textRange(d->editor, 0, length), d->language);
        title += L" (" + std::to_wstring(outlineItems.size()) + L")";
    }
    LVCOLUMNW header{};
    header.mask = LVCF_TEXT;
    header.pszText = title.data();
    ListView_SetColumn(outlineList, 0, &header);
    ListView_SetItemCountEx(outlineList, static_cast<int>(outlineItems.size()), LVSICF_NOSCROLL);
    InvalidateRect(outlineList, nullptr, TRUE);
}
void App::goToOutline(int index) {
    auto d = current();
    if (!d || !d->editor || index < 0 || index >= static_cast<int>(outlineItems.size()))
        return;
    auto h = d->editor;
    Pos line = std::clamp<Pos>(outlineItems[static_cast<size_t>(index)].line, 0, sci(h, SCI_GETLINECOUNT) - 1);
    bool suppressed = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_ENSUREVISIBLEENFORCEPOLICY, line);
    sci(h, SCI_GOTOLINE, line);
    sci(h, SCI_SETFIRSTVISIBLELINE, std::max<Pos>(0, sci(h, SCI_VISIBLEFROMDOCLINE, line) - 3));
    macroSuppressed = suppressed;
    SetFocus(h);
}
std::vector<HWND> App::views(const Document &d) const {
    std::vector<HWND> result;
    for (auto h : {d.editor, d.secondary})
        if (h)
            result.push_back(h);
    return result;
}
void App::toggleBookmark() {
    auto d = current();
    if (!d || !d->editor)
        return;
    auto h = d->editor;
    Pos line = sci(h, SCI_LINEFROMPOSITION, sci(h, SCI_GETCURRENTPOS));
    if (sci(h, SCI_MARKERGET, line) & (1 << bookmarkMarker))
        sci(h, SCI_MARKERDELETE, line, bookmarkMarker);
    else
        sci(h, SCI_MARKERADD, line, bookmarkMarker);
}
void App::nextBookmark(bool backwards) {
    auto d = current();
    if (!d || !d->editor)
        return;
    auto h = d->editor;
    Pos line = sci(h, SCI_LINEFROMPOSITION, sci(h, SCI_GETCURRENTPOS)), count = sci(h, SCI_GETLINECOUNT);
    constexpr int mask = 1 << bookmarkMarker;
    Pos target = backwards ? (line > 0 ? sci(h, SCI_MARKERPREVIOUS, line - 1, mask) : -1)
                           : sci(h, SCI_MARKERNEXT, line + 1, mask);
    if (target < 0) // Wrap around like Notepad++.
        target = backwards ? sci(h, SCI_MARKERPREVIOUS, count - 1, mask) : sci(h, SCI_MARKERNEXT, 0, mask);
    if (target < 0)
        return;
    bool suppressed = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_ENSUREVISIBLEENFORCEPOLICY, target);
    sci(h, SCI_GOTOLINE, target);
    macroSuppressed = suppressed;
}
void App::clearBookmarks() {
    if (auto d = current(); d && d->editor)
        sci(d->editor, SCI_MARKERDELETEALL, bookmarkMarker);
}
std::string App::copyBookmarked() {
    auto d = current();
    if (!d || !d->editor)
        return {};
    auto h = d->editor;
    auto m = sci(h, SCI_GETEOLMODE);
    std::string eol = m == SC_EOL_CRLF ? "\r\n" : m == SC_EOL_CR ? "\r" : "\n", text;
    size_t count = 0;
    for (Pos line = sci(h, SCI_MARKERNEXT, 0, 1 << bookmarkMarker); line >= 0;
         line = sci(h, SCI_MARKERNEXT, line + 1, 1 << bookmarkMarker)) {
        Pos a = sci(h, SCI_POSITIONFROMLINE, line), b = sci(h, SCI_GETLINEENDPOSITION, line);
        if (count++)
            text += eol;
        text += textRange(h, a, b - a);
        if (text.size() > 64 * 1024 * 1024)
            fail(L"북마크한 줄이 너무 커서 복사할 수 없습니다.");
    }
    if (!count)
        fail(L"북마크한 줄이 없습니다. Ctrl+F2로 북마크를 표시하세요.");
    copyToClipboard(wide(text, CP_UTF8, false), window);
    return text;
}
size_t App::deleteBookmarked() {
    auto d = current();
    if (!d || !d->editor || d->monitor)
        return 0;
    auto h = d->editor;
    std::vector<Pos> lines;
    for (Pos line = sci(h, SCI_MARKERNEXT, 0, 1 << bookmarkMarker); line >= 0;
         line = sci(h, SCI_MARKERNEXT, line + 1, 1 << bookmarkMarker))
        lines.push_back(line);
    if (lines.empty())
        fail(L"북마크한 줄이 없습니다.");
    bool suppressed = macroSuppressed;
    macroSuppressed = true;
    sci(h, SCI_BEGINUNDOACTION);
    Pos count = sci(h, SCI_GETLINECOUNT);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        Pos line = *it, a = sci(h, SCI_POSITIONFROMLINE, line), b = sci(h, SCI_POSITIONFROMLINE, line + 1);
        sci(h, SCI_MARKERDELETE, line, bookmarkMarker); // Deleted lines pass markers to their neighbour.
        if (line == count - 1) { // The last line has no line break of its own: take the previous one.
            b = sci(h, SCI_GETLENGTH);
            if (line > 0)
                a = sci(h, SCI_GETLINEENDPOSITION, line - 1);
        }
        sci(h, SCI_DELETERANGE, a, b - a);
        count = sci(h, SCI_GETLINECOUNT);
    }
    sci(h, SCI_ENDUNDOACTION);
    macroSuppressed = suppressed;
    return lines.size();
}
void App::toggleSplit() {
    auto d = current();
    if (!d || !d->editor)
        return;
    if (d->secondary) {
        auto view = std::exchange(d->secondary, nullptr);
        d->topPane = nullptr;
        DestroyWindow(view);
        layout();
        SetFocus(d->editor);
        return;
    }
    if (d->viewMode)
        setView(0);
    auto view = createEditor(d);
    sci(view, SCI_SETDOCPOINTER, 0, sci(d->editor, SCI_GETDOCPOINTER));
    languages.apply(view, d->language, d->large);
    sci(view, SCI_SETWRAPMODE, sci(d->editor, SCI_GETWRAPMODE));
    sci(view, SCI_SETZOOM, sci(d->editor, SCI_GETZOOM));
    sci(view, SCI_SETSEL, sci(d->editor, SCI_GETANCHOR), sci(d->editor, SCI_GETCURRENTPOS));
    sci(view, SCI_SETFIRSTVISIBLELINE, sci(d->editor, SCI_GETFIRSTVISIBLELINE));
    if (recording)
        sci(view, SCI_STARTRECORD);
    d->secondary = view;
    d->topPane = d->editor;
    layout();
}
} // namespace lp

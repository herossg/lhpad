// Auto-completion: words from the document plus the language's keywords (Notepad++ langs.xml).
#include "app.h"
namespace lp {
namespace {
bool wordByte(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80; // UTF-8 letters (Korean etc.) count as word bytes.
}
bool startsWithNoCase(const std::string &word, const std::string &prefix) {
    if (word.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); i++)
        if (std::tolower(static_cast<unsigned char>(word[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}
// Case-insensitive order that still keeps "Foo" and "foo" as separate entries.
struct NoCaseLess {
    bool operator()(const std::string &a, const std::string &b) const {
        int c = _stricmp(a.c_str(), b.c_str());
        return c < 0 || (c == 0 && a < b);
    }
};
} // namespace
std::vector<std::string> Languages::keywords(const std::string &name) const {
    std::vector<std::string> result;
    for (auto l : words.child("NotepadPlus").child("Languages").children("Language"))
        if (name == l.attribute("name").as_string()) {
            for (auto k : l.children("Keywords")) {
                std::istringstream stream(k.child_value());
                std::string word;
                while (stream >> word)
                    result.push_back(word);
            }
            break;
        }
    return result;
}
std::vector<std::string> App::completionWords(Document &d, const std::string &prefix) {
    auto h = d.editor;
    auto now = GetTickCount64();
    // Rescanning on every keystroke is wasteful; a cache up to 1.5 s old only misses words typed just now.
    if (completion.docId != d.id || (completion.revision != d.revision && now - completion.built > 1500)) {
        std::set<std::string> found;
        Pos length = std::min<Pos>(sci(h, SCI_GETLENGTH), 8 * 1024 * 1024);
        auto text = textRange(h, 0, length);
        size_t i = 0;
        while (i < text.size()) {
            if (!wordByte(static_cast<unsigned char>(text[i])) || std::isdigit(static_cast<unsigned char>(text[i]))) {
                // Skip separators, and whole tokens that start with a digit (numbers).
                if (std::isdigit(static_cast<unsigned char>(text[i])))
                    while (i < text.size() && wordByte(static_cast<unsigned char>(text[i])))
                        i++;
                else
                    i++;
                continue;
            }
            size_t start = i;
            while (i < text.size() && wordByte(static_cast<unsigned char>(text[i])))
                i++;
            if (i - start >= 3 && i - start <= 80 && found.size() < 200000)
                found.insert(text.substr(start, i - start));
        }
        completion.docId = d.id;
        completion.revision = d.revision;
        completion.built = now;
        completion.words.assign(found.begin(), found.end());
        completion.language.clear();
    }
    if (completion.language != d.language) {
        completion.language = d.language;
        completion.keywords = languages.keywords(d.language);
    }
    std::set<std::string, NoCaseLess> list;
    for (auto *source : {&completion.words, &completion.keywords})
        for (auto &word : *source)
            if (word != prefix && startsWithNoCase(word, prefix) && list.size() < 500)
                list.insert(word);
    return {list.begin(), list.end()};
}
void App::showCompletion(bool manual) {
    auto d = current();
    // While recording, an automatic list would capture arrow keys and make the macro differ from what
    // the user saw; Ctrl+Space still works.
    if (!d || !d->editor || d->monitor || d->large || playing || (!manual && (!autoComplete || recording)))
        return;
    auto h = d->editor;
    if (!manual && sci(h, SCI_AUTOCACTIVE))
        return; // Scintilla already narrows an open list as the user types.
    if (sci(h, SCI_GETSELECTIONS) > 1)
        return; // Multi-caret typing: a list would only apply to one caret.
    Pos caret = sci(h, SCI_GETCURRENTPOS), start = sci(h, SCI_WORDSTARTPOSITION, caret, 1);
    if (caret - start > 200)
        return;
    auto prefix = textRange(h, start, caret - start);
    size_t letters = 0;
    for (size_t i = 0; i < prefix.size(); i++)
        if ((static_cast<unsigned char>(prefix[i]) & 0xC0) != 0x80)
            letters++; // Count characters, not UTF-8 bytes.
    if (prefix.empty() || (!manual && static_cast<int>(letters) < completeMinChars) ||
        std::isdigit(static_cast<unsigned char>(prefix[0])))
        return;
    auto list = completionWords(*d, prefix);
    if (list.empty()) {
        if (sci(h, SCI_AUTOCACTIVE))
            sci(h, SCI_AUTOCCANCEL);
        return;
    }
    std::string joined;
    for (auto &word : list) {
        if (!joined.empty())
            joined += '\n';
        joined += word;
    }
    sci(h, SCI_AUTOCSETSEPARATOR, '\n');
    sci(h, SCI_AUTOCSETIGNORECASE, 1);
    sci(h, SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR, SC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
    sci(h, SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT);
    sci(h, SCI_AUTOCSETAUTOHIDE, 1);
    sci(h, SCI_AUTOCSETDROPRESTOFWORD, 0);
    sci(h, SCI_AUTOCSETMAXHEIGHT, 10);
    bool suppressed = macroSuppressed;
    macroSuppressed = true;
    str(h, SCI_AUTOCSHOW, joined, static_cast<WPARAM>(caret - start));
    macroSuppressed = suppressed;
}
} // namespace lp

#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <wrl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "vendor/json/json.hpp"
#include "vendor/pugixml/pugixml.hpp"
#include "vendor/scintilla/Scintilla.h"
#include "vendor/scintilla/ILexer.h"
#include "vendor/webview2/WebView2.h"
namespace lp {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
using Pos = int64_t;
template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : h(h) {}
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE && h)
            CloseHandle(h);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&x) noexcept : h(std::exchange(x.h, INVALID_HANDLE_VALUE)) {}
    Handle &operator=(Handle &&x) noexcept {
        if (this != &x) {
            if (h != INVALID_HANDLE_VALUE && h)
                CloseHandle(h);
            h = std::exchange(x.h, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    explicit operator bool() const { return h != INVALID_HANDLE_VALUE && h; }
};
std::wstring wide(std::string_view s, UINT cp = CP_UTF8, bool strict = true);
std::string narrow(std::wstring_view s, UINT cp = CP_UTF8, bool strict = true);
std::wstring errorText(DWORD code = GetLastError());
[[noreturn]] void fail(const std::wstring &message);
fs::path exeDir();
fs::path winPath(const fs::path &path);
namespace disk {
inline bool exists(const fs::path &p) {
    return fs::exists(winPath(p));
}
inline uintmax_t file_size(const fs::path &p) {
    return fs::file_size(winPath(p));
}
inline bool is_regular_file(const fs::path &p) {
    return fs::is_regular_file(winPath(p));
}
inline bool is_directory(const fs::path &p) {
    return fs::is_directory(winPath(p));
}
inline bool create_directories(const fs::path &p) {
    return fs::create_directories(winPath(p));
}
inline bool copy_file(const fs::path &from, const fs::path &to, fs::copy_options options) {
    return fs::copy_file(winPath(from), winPath(to), options);
}
inline bool remove(const fs::path &p, std::error_code &error) {
    return fs::remove(winPath(p), error);
}
inline fs::path weakly_canonical(const fs::path &p) {
    return fs::weakly_canonical(winPath(p));
}
} // namespace disk
fs::path defaultProfile();
std::string guid();
Bytes readAll(const fs::path &path, uint64_t limit = 64ull * 1024 * 1024);
void writeAtomic(const fs::path &path, const Bytes &data, bool backup = false);
Json readJson(const fs::path &path);
void writeJson(const fs::path &path, const Json &json, bool backup = true);
void pump();
struct Encoding {
    std::string name = "UTF-8";
    UINT cp = 65001;
    bool bom = false;
};
void to_json(Json &j, const Encoding &e);
void from_json(const Json &j, Encoding &e);
const std::vector<Encoding> &encodings();
struct Detection {
    bool binary = false;
    Encoding encoding;
    int skip = 0;
};
Detection detect(const fs::path &path);
Bytes encode(std::string_view utf8, const Encoding &e);
std::string decode(const Bytes &bytes, const Encoding &e, bool stripBom = false);
using ReadChunk = std::function<void(std::string_view)>;
void readText(const fs::path &path, const Encoding &e, int skip, const ReadChunk &chunk);
using GetChunk = std::function<std::string(Pos, Pos)>;
void saveText(const fs::path &path, Pos length, const Encoding &e, const GetChunk &chunk);
struct Stamp {
    bool exists = false;
    uint64_t size = 0, ticks = 0, id = 0;
    DWORD volume = 0;
    bool operator==(const Stamp &) const = default;
};
Stamp stamp(const fs::path &path);
class BinaryBuffer {
  public:
    struct Piece {
        Pos offset = 0, length = 0;
        std::shared_ptr<Bytes> data;
    };
    struct Revision {
        std::vector<Piece> pieces;
        uint64_t id;
    };
    fs::path path;
    Handle file;
    std::vector<Piece> pieces;
    std::vector<Revision> undo, redo;
    uint64_t revision = 0, savedRevision = 0, nextRevision = 0;
    explicit BinaryBuffer(const fs::path &path);
    Pos size() const;
    bool dirty() const { return revision != savedRevision; }
    bool changed(Pos offset) const;
    Bytes read(Pos offset, Pos count) const;
    void replace(Pos offset, Pos remove, const Bytes &insert);
    void undoEdit();
    void redoEdit();
    void save(const fs::path &target);
    Pos find(const Bytes &needle, Pos start, std::atomic_bool *cancel = nullptr) const;
};
struct Language {
    std::string name, display, lexer;
};
class Languages {
    pugi::xml_document styles, words;

  public:
    std::vector<Language> items;
    Languages();
    std::string detect(const fs::path &path) const;
    void apply(HWND sci, const std::string &name, bool large = false) const;
};
inline LRESULT sci(HWND h, UINT m, WPARAM w = 0, LPARAM l = 0) {
    return SendMessageW(h, m, w, l);
}
inline LRESULT str(HWND h, UINT m, const std::string &s, WPARAM w = 0) {
    return sci(h, m, w, reinterpret_cast<LPARAM>(s.c_str()));
}
std::string textRange(HWND sci, Pos start, Pos count);
std::string formatText(const std::string &input, const std::string &language, const std::string &eol);
std::vector<std::string> sqlTokens(const std::string &s);
std::string markdown(const std::string &s);
std::string htmlEscape(const std::string &s);
struct DiffRow {
    int64_t left = -1, right = -1;
    int kind = 0;
};
std::vector<DiffRow> diffLines(const std::vector<std::string> &a, const std::vector<std::string> &b);
std::vector<std::string> lines(const std::string &text);
std::string sha256(const BinaryBuffer &data, std::atomic_bool *cancel = nullptr);
void configureModernExplorer(bool enable);
bool modernExplorerRegistered();
void registerShell(const fs::path &exe);
void unregisterShell();
bool shellRegistered(const fs::path &exe);
} // namespace lp

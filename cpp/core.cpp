#include "core.h"
#include <bcrypt.h>
#include <regex>
#include <iomanip>
#include "vendor/md4c/md4c-html.h"
namespace lp {
[[noreturn]] void fail(const std::wstring &s) {
    throw std::runtime_error(narrow(s));
}
std::wstring errorText(DWORD code) {
    wchar_t *p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&p), 0, nullptr);
    std::wstring s = p ? p : L"Windows 오류";
    LocalFree(p);
    return s;
}
std::wstring wide(std::string_view s, UINT cp, bool strict) {
    if (s.empty())
        return {};
    DWORD flags = strict && (cp == 65001 || cp == 54936) ? MB_ERR_INVALID_CHARS : 0;
    int n = MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n)
        fail(L"텍스트를 해석할 수 없습니다. " + errorText());
    std::wstring out(n, 0);
    MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string narrow(std::wstring_view s, UINT cp, bool strict) {
    if (s.empty())
        return {};
    BOOL used = false;
    bool unicode = cp == 65001 || cp == 54936;
    bool special = cp == 65000 || cp == 42 || cp == 50220 || cp == 50221 || cp == 50222 || cp == 50225 ||
                   cp == 50227 || cp == 50229 || cp == 52936 || cp == 57002;
    DWORD flags = unicode ? (strict ? WC_ERR_INVALID_CHARS : 0) : special ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL *usedPtr = unicode || special ? nullptr : &used;
    int n =
        WideCharToMultiByte(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, usedPtr);
    if (!n || strict && used)
        fail(L"선택한 인코딩으로 표현할 수 없는 문자가 있습니다.");
    std::string out(n, 0);
    WideCharToMultiByte(cp, flags, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, usedPtr);
    if (strict && used)
        fail(L"선택한 인코딩으로 표현할 수 없는 문자가 있습니다.");
    if (strict && !unicode) {
        auto back = wide(out, cp, false);
        if (back != s)
            fail(L"선택한 인코딩으로 손실 없이 저장할 수 없습니다.");
    }
    return out;
}
fs::path exeDir() {
    std::wstring s(32768, 0);
    auto n = GetModuleFileNameW(nullptr, s.data(), static_cast<DWORD>(s.size()));
    s.resize(n);
    return fs::path(s).parent_path();
}
fs::path winPath(const fs::path &path) {
    auto p = fs::absolute(path).lexically_normal();
    p.make_preferred();
    auto s = p.wstring();
    if (s.starts_with(L"\\\\?\\"))
        return p;
    if (s.starts_with(L"\\\\"))
        return fs::path(L"\\\\?\\UNC\\" + s.substr(2));
    return fs::path(L"\\\\?\\" + s);
}
fs::path defaultProfile() {
    PWSTR p = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p);
    fs::path path = fs::path(p) / L"LitePad";
    CoTaskMemFree(p);
    return path;
}
std::string guid() {
    GUID g{};
    CoCreateGuid(&g);
    wchar_t p[40];
    StringFromGUID2(g, p, 40);
    return narrow(p).substr(1, 36);
}
void pump() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(m.wParam));
            break;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}
Bytes readAll(const fs::path &p, uint64_t limit) {
    auto n = disk::file_size(p);
    if (n > limit)
        fail(L"파일이 이 작업의 크기 제한을 초과합니다.");
    std::ifstream f(winPath(p), std::ios::binary);
    if (!f)
        fail(L"파일을 열 수 없습니다: " + p.wstring());
    Bytes b(static_cast<size_t>(n));
    if (n && !f.read(reinterpret_cast<char *>(b.data()), n))
        fail(L"파일을 읽을 수 없습니다.");
    return b;
}
static Handle createFile(const fs::path &p, DWORD access, DWORD disposition,
                         DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) {
    Handle h(
        CreateFileW(winPath(p).c_str(), access, share, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h)
        fail(p.wstring() + L"\n" + errorText());
    return h;
}
static void writeBytes(HANDLE h, const uint8_t *p, size_t n) {
    while (n) {
        DWORD count = static_cast<DWORD>(std::min<size_t>(n, 1 << 20)), done = 0;
        if (!WriteFile(h, p, count, &done, nullptr) || !done)
            fail(errorText());
        n -= done;
        p += done;
    }
}
static void commit(const fs::path &p, const fs::path &tmp, bool backup) {
    const auto target = winPath(p), temporary = winPath(tmp);
    if (disk::exists(p)) {
        fs::path bak = p.wstring() + L".bak";
        if (!ReplaceFileW(target.c_str(), temporary.c_str(), backup ? winPath(bak).c_str() : nullptr, 0,
                          nullptr, nullptr))
            fail(L"저장 파일을 교체할 수 없습니다: " + errorText());
    } else if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        fail(errorText());
}
void writeAtomic(const fs::path &p, const Bytes &b, bool backup) {
    disk::create_directories(p.parent_path());
    fs::path tmp = p.wstring() + L"." + wide(guid()) + L".tmp";
    try {
        {
            auto h = createFile(tmp, GENERIC_WRITE, CREATE_NEW, 0);
            writeBytes(h.h, b.data(), b.size());
            if (!FlushFileBuffers(h.h))
                fail(errorText());
        }
        commit(p, tmp, backup);
    } catch (...) {
        std::error_code ec;
        disk::remove(tmp, ec);
        throw;
    }
}
Json readJson(const fs::path &p) {
    auto b = readAll(p);
    return Json::parse(b);
}
void writeJson(const fs::path &p, const Json &j, bool backup) {
    auto s = j.dump(2, ' ', false, Json::error_handler_t::strict);
    writeAtomic(p, Bytes(s.begin(), s.end()), backup);
}
void to_json(Json &j, const Encoding &e) {
    j = Json{{"Name", e.name}, {"CodePage", e.cp}, {"Bom", e.bom}};
}
void from_json(const Json &j, Encoding &e) {
    e.name = j.value("Name", "UTF-8");
    e.cp = j.value("CodePage", 65001u);
    e.bom = j.value("Bom", false);
}
const std::vector<Encoding> &encodings() {
    static const std::vector<Encoding> e = [] {
        std::vector<Encoding> v = {{"UTF-8", 65001},
                                   {"UTF-8 BOM", 65001, true},
                                   {"UTF-16 LE BOM", 1200, true},
                                   {"UTF-16 BE BOM", 1201, true},
                                   {"UTF-16 LE", 1200},
                                   {"UTF-16 BE", 1201},
                                   {"UTF-32 LE BOM", 12000, true},
                                   {"UTF-32 BE BOM", 12001, true},
                                   {"ANSI (시스템)", GetACP()},
                                   {"한국어 Windows-949 (CP949)", 949},
                                   {"한국어 EUC-KR", 51949},
                                   {"한국어 ISO-2022-KR", 50225},
                                   {"일본어 Shift-JIS", 932},
                                   {"일본어 EUC-JP", 51932},
                                   {"일본어 ISO-2022-JP", 50220},
                                   {"중국어 GB2312 / GBK", 936},
                                   {"중국어 GB18030", 54936},
                                   {"중국어 Big5", 950},
                                   {"중국어 ISO-2022-CN", 50227}};
        for (UINT cp : {1256,  1257,  1250,  1251,  1252,  1253,  1254,  1255,  1258,  874,   437,
                        850,   852,   855,   857,   866,   20866, 21866, 10000, 10007, 28591, 28592,
                        28593, 28594, 28595, 28596, 28597, 28598, 28599, 28603, 28605, 28604, 720,
                        737,   775,   858,   860,   861,   862,   863,   865,   869})
            v.push_back({cp >= 28591 && cp <= 28599 ? "ISO-8859-" + std::to_string(cp - 28590)
                         : cp == 28604              ? "ISO-8859-14 (Celtic)"
                                                    : "Code page " + std::to_string(cp),
                         cp});
        return v;
    }();
    return e;
}
static const std::array<wchar_t, 256> &celtic() {
    static auto m = [] {
        std::array<wchar_t, 256> a{};
        std::ifstream f(winPath(exeDir() / L"data/iso8859-14.txt"));
        std::string s;
        while (std::getline(f, s)) {
            unsigned x, y;
            if (sscanf_s(s.c_str(), "0x%x 0x%x", &x, &y) == 2 && x < 256)
                a[x] = static_cast<wchar_t>(y);
        }
        return a;
    }();
    return m;
}
Bytes encode(std::string_view s, const Encoding &e) {
    auto w = wide(s);
    Bytes b;
    if (e.cp == 1200 || e.cp == 1201) {
        for (auto c : w) {
            b.push_back(static_cast<uint8_t>(e.cp == 1200 ? c : c >> 8));
            b.push_back(static_cast<uint8_t>(e.cp == 1200 ? c >> 8 : c));
        }
    } else if (e.cp == 12000 || e.cp == 12001) {
        for (size_t i = 0; i < w.size(); ++i) {
            uint32_t c = w[i];
            if (c >= 0xD800 && c <= 0xDBFF) {
                if (++i == w.size() || w[i] < 0xDC00 || w[i] > 0xDFFF)
                    fail(L"잘못된 Unicode 문자입니다.");
                c = 0x10000 + ((c - 0xD800) << 10) + (w[i] - 0xDC00);
            }
            for (int k = 0; k < 4; k++)
                b.push_back(static_cast<uint8_t>(c >> (e.cp == 12000 ? k * 8 : (3 - k) * 8)));
        }
    } else if (e.cp == 28604) {
        for (auto c : w) {
            auto it = std::find(celtic().begin(), celtic().end(), c);
            if (it == celtic().end())
                fail(L"ISO-8859-14로 표현할 수 없습니다.");
            b.push_back(static_cast<uint8_t>(it - celtic().begin()));
        }
    } else {
        auto a = e.cp == 65001 ? std::string(s) : narrow(w, e.cp);
        b.assign(a.begin(), a.end());
    }
    return b;
}
static Bytes preamble(const Encoding &e) {
    if (!e.bom)
        return {};
    switch (e.cp) {
    case 65001:
        return {0xEF, 0xBB, 0xBF};
    case 1200:
        return {0xFF, 0xFE};
    case 1201:
        return {0xFE, 0xFF};
    case 12000:
        return {0xFF, 0xFE, 0, 0};
    case 12001:
        return {0, 0, 0xFE, 0xFF};
    default:
        return {};
    }
}
std::string decode(const Bytes &b, const Encoding &e, bool strip) {
    size_t start = 0;
    if (strip) {
        auto bom = preamble(e);
        if (b.size() >= bom.size() && std::equal(bom.begin(), bom.end(), b.begin()))
            start = bom.size();
    }
    std::wstring w;
    if (e.cp == 1200 || e.cp == 1201) {
        if ((b.size() - start) % 2)
            fail(L"불완전한 UTF-16 문자입니다.");
        for (size_t i = start; i < b.size(); i += 2)
            w.push_back(static_cast<wchar_t>(e.cp == 1200 ? b[i] | b[i + 1] << 8 : b[i] << 8 | b[i + 1]));
        return narrow(w);
    }
    if (e.cp == 12000 || e.cp == 12001) {
        if ((b.size() - start) % 4)
            fail(L"불완전한 UTF-32 문자입니다.");
        for (size_t i = start; i < b.size(); i += 4) {
            uint32_t c = 0;
            for (int k = 0; k < 4; k++)
                c |= static_cast<uint32_t>(b[i + k]) << (e.cp == 12000 ? k * 8 : (3 - k) * 8);
            if (c > 0x10FFFF || c >= 0xD800 && c <= 0xDFFF)
                fail(L"잘못된 UTF-32 문자입니다.");
            if (c >= 0x10000) {
                c -= 0x10000;
                w.push_back(static_cast<wchar_t>(0xD800 + (c >> 10)));
                w.push_back(static_cast<wchar_t>(0xDC00 + (c & 1023)));
            } else
                w.push_back(static_cast<wchar_t>(c));
        }
        return narrow(w);
    }
    if (e.cp == 28604) {
        for (size_t i = start; i < b.size(); i++)
            w.push_back(celtic()[b[i]]);
        return narrow(w);
    }
    std::string s(reinterpret_cast<const char *>(b.data() + start), b.size() - start);
    auto decoded = wide(s, e.cp);
    if (e.cp != 65001 && narrow(decoded, e.cp) != s)
        fail(L"선택한 인코딩으로 파일을 해석할 수 없습니다.");
    return e.cp == 65001 ? s : narrow(decoded);
}
Detection detect(const fs::path &p) {
    auto h = createFile(p, GENERIC_READ, OPEN_EXISTING);
    Bytes b(65536);
    DWORD got = 0;
    if (!ReadFile(h.h, b.data(), static_cast<DWORD>(b.size()), &got, nullptr))
        fail(errorText());
    b.resize(got);
    auto begins = [&](std::initializer_list<uint8_t> a) {
        return b.size() >= a.size() && std::equal(a.begin(), a.end(), b.begin());
    };
    for (auto e : {Encoding{"UTF-32 LE BOM", 12000, true}, Encoding{"UTF-32 BE BOM", 12001, true},
                   Encoding{"UTF-8 BOM", 65001, true}, Encoding{"UTF-16 LE BOM", 1200, true},
                   Encoding{"UTF-16 BE BOM", 1201, true}}) {
        auto bom = preamble(e);
        if (b.size() >= bom.size() && std::equal(bom.begin(), bom.end(), b.begin()))
            return {false, e, static_cast<int>(bom.size())};
    }
    if (begins({'M', 'Z'}) || begins({'P', 'K', 3, 4}) || begins({0x89, 'P', 'N', 'G'}) ||
        begins({'%', 'P', 'D', 'F'}) || begins({0x7F, 'E', 'L', 'F'}))
        return {true};
    size_t even = 0, odd = 0, bad = 0;
    for (size_t i = 0; i < b.size(); i++) {
        if (!b[i]) {
            if (i % 2)
                odd++;
            else
                even++;
        }
        if (b[i] < 9 || b[i] > 13 && b[i] < 32)
            bad++;
    }
    if (b.size() >= 8) {
        if (odd > b.size() * .25 && even < b.size() * .02)
            return {false, {"UTF-16 LE", 1200}};
        if (even > b.size() * .25 && odd < b.size() * .02)
            return {false, {"UTF-16 BE", 1201}};
    }
    if (even || odd || bad > b.size() * .02)
        return {true};
    for (auto e : {Encoding{}, Encoding{"한국어 Windows-949 (CP949)", 949},
                   Encoding{"ANSI (시스템)", GetACP()}, Encoding{"Windows-1252", 1252}}) {
        int trims = disk::file_size(p) > b.size() ? 4 : 1;
        for (int trim = 0; trim < trims && trim <= static_cast<int>(b.size()); trim++) {
            try {
                Bytes test(b.begin(), b.end() - trim);
                decode(test, e);
                return {false, e};
            } catch (...) {
            }
        }
    }
    return {true};
}
void readText(const fs::path &p, const Encoding &e, int skip, const ReadChunk &callback) {
    auto h = createFile(p, GENERIC_READ, OPEN_EXISTING);
    LARGE_INTEGER off{};
    off.QuadPart = skip;
    SetFilePointerEx(h.h, off, nullptr, FILE_BEGIN);
    Bytes pending;
    bool stateful = e.cp >= 50220 && e.cp <= 50229;
    DWORD count = 0;
    Bytes buf(256 * 1024);
    for (;;) {
        if (!ReadFile(h.h, buf.data(), static_cast<DWORD>(buf.size()), &count, nullptr))
            fail(errorText());
        pending.insert(pending.end(), buf.begin(), buf.begin() + count);
        if (stateful && count) {
            if (pending.size() > 64ull * 1024 * 1024)
                fail(L"ISO-2022 파일은 64 MiB 이하에서 지원합니다.");
            continue;
        }
        size_t n = pending.size();
        if (count) {
            if (e.cp == 1200 || e.cp == 1201) {
                n -= n % 2;
                if (n >= 2) {
                    uint16_t last = e.cp == 1200 ? pending[n - 2] | pending[n - 1] << 8
                                                 : pending[n - 2] << 8 | pending[n - 1];
                    if (last >= 0xD800 && last <= 0xDBFF)
                        n -= 2;
                }
            } else if (e.cp == 12000 || e.cp == 12001)
                n -= n % 4;
            else if (e.cp == 65001) {
                size_t i = n;
                while (i && ((pending[i - 1] & 0xC0) == 0x80))
                    --i;
                if (i) {
                    auto lead = pending[i - 1];
                    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
                    if (n - (i - 1) < need)
                        n = i - 1;
                }
            } else if (e.cp != 28604) {
                size_t i = 0;
                while (i < n) {
                    size_t step = (e.cp == 54936 ? pending[i] >= 0x81 && pending[i] <= 0xFE
                                                 : IsDBCSLeadByteEx(e.cp, pending[i]) != 0)
                                      ? 2
                                      : 1;
                    if (e.cp == 54936 && pending[i] >= 0x81 && pending[i] <= 0xFE && i + 1 < n &&
                        pending[i + 1] >= 0x30 && pending[i + 1] <= 0x39)
                        step = 4;
                    if (i + step > n) {
                        n = i;
                        break;
                    }
                    i += step;
                }
            }
        }
        if (n) {
            Bytes part(pending.begin(), pending.begin() + n);
            callback(decode(part, e));
            pending.erase(pending.begin(), pending.begin() + n);
        }
        if (!count)
            break;
    }
    if (!pending.empty())
        fail(L"파일 끝에 불완전한 문자가 있습니다.");
}
void saveText(const fs::path &p, Pos length, const Encoding &e, const GetChunk &get) {
    fs::path tmp = p.wstring() + L"." + wide(guid()) + L".tmp";
    try {
        {
            auto h = createFile(tmp, GENERIC_WRITE, CREATE_NEW, 0);
            auto bom = preamble(e);
            writeBytes(h.h, bom.data(), bom.size());
            bool stateful = e.cp >= 50220 && e.cp <= 50229;
            if (stateful && length > 64 * 1024 * 1024)
                fail(L"ISO-2022 파일은 64 MiB 이하에서 지원합니다.");
            for (Pos pos = 0; pos < length;) {
                auto s = get(pos, std::min<Pos>(stateful ? length : 256 * 1024, length - pos));
                size_t n = s.size();
                if (pos + static_cast<Pos>(n) < length) {
                    size_t i = n;
                    while (i && ((static_cast<uint8_t>(s[i - 1]) & 0xC0) == 0x80))
                        i--;
                    if (i) {
                        auto c = static_cast<uint8_t>(s[i - 1]);
                        size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
                        if (n - i + 1 < need)
                            n = i - 1;
                    }
                }
                s.resize(n);
                if (!n)
                    fail(L"저장 중 문자 경계를 찾지 못했습니다.");
                auto b = encode(s, e);
                writeBytes(h.h, b.data(), b.size());
                pos += n;
            }
            if (!FlushFileBuffers(h.h))
                fail(errorText());
        }
        commit(p, tmp, false);
    } catch (...) {
        std::error_code ec;
        disk::remove(tmp, ec);
        throw;
    }
}
Stamp stamp(const fs::path &p) {
    Handle h(CreateFileW(winPath(p).c_str(), FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h)
        return {};
    BY_HANDLE_FILE_INFORMATION i{};
    if (!GetFileInformationByHandle(h.h, &i))
        return {};
    return {true, (uint64_t(i.nFileSizeHigh) << 32) | i.nFileSizeLow,
            (uint64_t(i.ftLastWriteTime.dwHighDateTime) << 32) | i.ftLastWriteTime.dwLowDateTime,
            (uint64_t(i.nFileIndexHigh) << 32) | i.nFileIndexLow, i.dwVolumeSerialNumber};
}
BinaryBuffer::BinaryBuffer(const fs::path &p)
    : path(p), file(createFile(p, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_DELETE)) {
    LARGE_INTEGER s{};
    if (!GetFileSizeEx(file.h, &s))
        fail(errorText());
    if (s.QuadPart)
        pieces.push_back({0, s.QuadPart, {}});
}
Pos BinaryBuffer::size() const {
    Pos n = 0;
    for (auto &p : pieces)
        n += p.length;
    return n;
}
bool BinaryBuffer::changed(Pos offset) const {
    Pos start = 0;
    for (const auto &p : pieces) {
        if (offset < start + p.length)
            return p.data != nullptr;
        start += p.length;
    }
    return false;
}
Bytes BinaryBuffer::read(Pos offset, Pos count) const {
    if (offset < 0 || count < 0 || offset > size())
        fail(L"잘못된 바이트 범위입니다.");
    count = std::min(count, size() - offset);
    Bytes b(static_cast<size_t>(count));
    Pos start = 0, written = 0;
    for (auto &p : pieces) {
        auto end = start + p.length;
        if (offset >= end) {
            start = end;
            continue;
        }
        auto within = std::max<Pos>(0, offset - start), n = std::min(count - written, p.length - within);
        if (n <= 0)
            break;
        if (p.data)
            std::copy_n(p.data->data() + p.offset + within, static_cast<size_t>(n), b.data() + written);
        else {
            LARGE_INTEGER pos{};
            pos.QuadPart = p.offset + within;
            if (!SetFilePointerEx(file.h, pos, nullptr, FILE_BEGIN))
                fail(errorText());
            Pos done = 0;
            while (done < n) {
                DWORD got = 0;
                if (!ReadFile(file.h, b.data() + written + done,
                              static_cast<DWORD>(std::min<Pos>(n - done, 1 << 20)), &got, nullptr) ||
                    !got)
                    fail(L"원본 바이너리 파일이 변경되었습니다.");
                done += got;
            }
        }
        written += n;
        offset += n;
        start = end;
        if (written == count)
            break;
    }
    return b;
}
void BinaryBuffer::replace(Pos offset, Pos remove, const Bytes &insert) {
    if (offset < 0 || remove < 0 || offset > size() || remove > size() - offset)
        fail(L"잘못된 바이트 범위입니다.");
    if (!remove && insert.empty())
        return;
    if (remove == static_cast<Pos>(insert.size()) && remove <= 1 << 20 && read(offset, remove) == insert)
        return;
    std::vector<Piece> next;
    Pos pos = 0, end = offset + remove;
    bool added = false;
    auto add = [&] {
        if (!added && !insert.empty())
            next.push_back({0, static_cast<Pos>(insert.size()), std::make_shared<Bytes>(insert)});
        added = true;
    };
    for (auto &p : pieces) {
        Pos last = pos + p.length;
        if (last <= offset)
            next.push_back(p);
        else if (pos >= end) {
            add();
            next.push_back(p);
        } else {
            if (pos < offset)
                next.push_back({p.offset, offset - pos, p.data});
            add();
            if (last > end)
                next.push_back({p.offset + end - pos, last - end, p.data});
        }
        pos = last;
    }
    add();
    undo.push_back({pieces, revision});
    redo.clear();
    pieces = std::move(next);
    revision = ++nextRevision;
}
void BinaryBuffer::undoEdit() {
    if (undo.empty())
        return;
    redo.push_back({pieces, revision});
    auto r = std::move(undo.back());
    undo.pop_back();
    pieces = std::move(r.pieces);
    revision = r.id;
}
void BinaryBuffer::redoEdit() {
    if (redo.empty())
        return;
    undo.push_back({pieces, revision});
    auto r = std::move(redo.back());
    redo.pop_back();
    pieces = std::move(r.pieces);
    revision = r.id;
}
void BinaryBuffer::save(const fs::path &p) {
    auto tmp = p.wstring() + L"." + wide(guid()) + L".tmp";
    try {
        {
            auto h = createFile(tmp, GENERIC_WRITE, CREATE_NEW, 0);
            for (Pos pos = 0; pos < size();) {
                auto b = read(pos, std::min<Pos>(1 << 20, size() - pos));
                writeBytes(h.h, b.data(), b.size());
                pos += b.size();
                pump();
            }
            if (!FlushFileBuffers(h.h))
                fail(errorText());
        }
        commit(p, tmp, false);
    } catch (...) {
        std::error_code ec;
        disk::remove(tmp, ec);
        throw;
    }
}
Pos BinaryBuffer::find(const Bytes &needle, Pos start, std::atomic_bool *cancel) const {
    if (needle.empty())
        return -1;
    std::vector<size_t> prefix(needle.size());
    for (size_t i = 1, j = 0; i < needle.size(); i++) {
        while (j && needle[i] != needle[j])
            j = prefix[j - 1];
        if (needle[i] == needle[j])
            j++;
        prefix[i] = j;
    }
    size_t matched = 0;
    for (Pos pos = std::max<Pos>(0, start); pos < size();) {
        if (cancel && *cancel)
            return -1;
        auto b = read(pos, std::min<Pos>(1 << 20, size() - pos));
        for (size_t i = 0; i < b.size(); i++) {
            while (matched && b[i] != needle[matched])
                matched = prefix[matched - 1];
            if (b[i] == needle[matched])
                matched++;
            if (matched == needle.size())
                return pos + static_cast<Pos>(i) + 1 - static_cast<Pos>(matched);
        }
        pos += b.size();
        pump();
    }
    return -1;
}
std::string textRange(HWND h, Pos start, Pos count) {
    Sci_TextRangeFull r{{start, start + count}, nullptr};
    std::string s(static_cast<size_t>(count) + 1, 0);
    r.lpstrText = s.data();
    sci(h, SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&r));
    s.resize(static_cast<size_t>(count));
    return s;
}
Languages::Languages() {
    auto dir = winPath(exeDir() / L"data");
    for (auto &j : readJson(dir / L"languages.json")) {
        Language l{j["Name"], j["Display"], j["Lexer"]};
        if (l.name != "user" && l.name != "external" && l.name != "searchResult")
            items.push_back(l);
    }
    if (std::none_of(items.begin(), items.end(), [](auto &l) { return l.name == "markdown"; }))
        items.push_back({"markdown", "Markdown", "markdown"});
    if (!styles.load_file((dir / L"stylers.xml").c_str()) || !words.load_file((dir / L"langs.xml").c_str()))
        fail(L"언어 설정 파일을 읽을 수 없습니다.");
}
std::string Languages::detect(const fs::path &p) const {
    auto ext = narrow(p.extension().wstring());
    if (!ext.empty())
        ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(tolower(c)); });
    if (ext == "md" || ext == "markdown" || ext == "mdown" || ext == "mkd")
        return "markdown";
    auto name = p.filename().wstring();
    if (!_wcsicmp(name.c_str(), L"Dockerfile"))
        return "dockerfile";
    if (!_wcsicmp(name.c_str(), L"CMakeLists.txt"))
        return "cmake";
    for (auto l : words.child("NotepadPlus").child("Languages").children("Language")) {
        std::istringstream stream(l.attribute("ext").as_string());
        std::string x;
        while (stream >> x)
            if (_stricmp(x.c_str(), ext.c_str()) == 0 && !ext.empty()) {
                std::string n = l.attribute("name").as_string();
                return n == "javascript" ? "javascript.js" : n;
            }
    }
    return "normal";
}
void Languages::apply(HWND h, const std::string &name, bool large) const {
    using CreateLexerFn = Scintilla::ILexer5 *(__stdcall *)(const char *);
    auto create =
        reinterpret_cast<CreateLexerFn>(GetProcAddress(GetModuleHandleW(L"Lexilla.dll"), "CreateLexer"));
    auto it = std::find_if(items.begin(), items.end(), [&](auto &l) { return l.name == name; });
    auto lexer = large || it == items.end() ? "null" : it->lexer.c_str();
    sci(h, SCI_CLEARDOCUMENTSTYLE);
    sci(h, SCI_SETILEXER, 0, reinterpret_cast<LPARAM>(create(lexer)));
    sci(h, SCI_STYLECLEARALL);
    if (large)
        return;
    for (int i = 0; i < 9; i++)
        str(h, SCI_SETKEYWORDS, "", i);
    pugi::xml_node style, word;
    for (auto n : styles.child("NotepadPlus").child("LexerStyles").children("LexerType"))
        if (name == n.attribute("name").as_string())
            style = n;
    for (auto n : words.child("NotepadPlus").child("Languages").children("Language"))
        if (name == n.attribute("name").as_string())
            word = n;
    std::vector<std::string> classes = {"instre1", "instre2", "type1", "type2", "type3",
                                        "type4",   "type5",   "type6", "type7"};
    for (auto s : style.children("WordsStyle")) {
        int id = s.attribute("styleID").as_int(-1);
        if (id < 0)
            continue;
        for (auto pair : {std::pair{"fgColor", SCI_STYLESETFORE}, std::pair{"bgColor", SCI_STYLESETBACK}}) {
            auto value = s.attribute(pair.first);
            if (value) {
                auto rgb = strtoul(value.value(), nullptr, 16);
                sci(h, pair.second, id, RGB(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255));
            }
        }
        auto font = s.attribute("fontStyle").as_int();
        sci(h, SCI_STYLESETBOLD, id, font & 1);
        sci(h, SCI_STYLESETITALIC, id, (font >> 1) & 1);
        std::string cls = s.attribute("keywordClass").as_string();
        int index = -1;
        for (size_t i = 0; i < classes.size(); i++)
            if (classes[i] == cls)
                index = static_cast<int>(i);
        if (std::string(lexer) == "cpp")
            index = cls == "instre1"   ? 0
                    : cls == "type1"   ? 1
                    : cls == "type2"   ? 2
                    : cls == "instre2" ? 3
                                       : -1;
        if (std::string(lexer) == "hypertext" && cls == "instre2")
            index = 5;
        if (index >= 0) {
            std::string kw;
            for (auto n : word.children("Keywords"))
                if (cls == n.attribute("name").as_string())
                    kw += std::string(n.child_value()) + " ";
            str(h, SCI_SETKEYWORDS, kw, index);
        }
    }
    for (auto key : {"fold", "fold.html", "fold.html.preprocessor", "fold.hypertext.comment",
                     "fold.xml.at.tag.open", "fold.comment", "fold.preprocessor"})
        sci(h, SCI_SETPROPERTY, reinterpret_cast<WPARAM>(key), reinterpret_cast<LPARAM>("1"));
    for (auto key : {"fold.compact", "lexer.cpp.track.preprocessor", "fold.cpp.comment.explicit"})
        sci(h, SCI_SETPROPERTY, reinterpret_cast<WPARAM>(key), reinterpret_cast<LPARAM>("0"));
    sci(h, SCI_SETPROPERTY, reinterpret_cast<WPARAM>("lexer.json.allow.comments"),
        reinterpret_cast<LPARAM>(name == "json5" ? "1" : "0"));
    bool folding = std::string(lexer) != "null" && std::string(lexer) != "markdown";
    sci(h, SCI_SETMARGINWIDTHN, 1, folding ? MulDiv(20, GetDpiForWindow(h), 96) : 0);
    sci(h, SCI_SETMARGINSENSITIVEN, 1, folding);
    sci(h, SCI_COLOURISE, 0, std::min<LRESULT>(sci(h, SCI_GETLENGTH), 65536));
}
std::vector<std::string> lines(const std::string &text) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); i++)
        if (text[i] == '\r' || text[i] == '\n') {
            out.push_back(text.substr(start, i - start));
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                i++;
            start = i + 1;
        }
    out.push_back(text.substr(start));
    return out;
}
std::string htmlEscape(const std::string &s) {
    std::string r;
    for (char c : s)
        switch (c) {
        case '&':
            r += "&amp;";
            break;
        case '<':
            r += "&lt;";
            break;
        case '>':
            r += "&gt;";
            break;
        case '"':
            r += "&quot;";
            break;
        default:
            r += c;
        }
    return r;
}
std::string markdown(const std::string &s) {
    std::string out;
    int result = md_html(
        s.data(), static_cast<MD_SIZE>(s.size()),
        [](const MD_CHAR *s, MD_SIZE n, void *p) { static_cast<std::string *>(p)->append(s, n); }, &out,
        MD_DIALECT_GITHUB, 0);
    if (result)
        fail(L"Markdown 렌더링에 실패했습니다.");
    return out;
}
std::string sha256(const BinaryBuffer &data, std::atomic_bool *cancel) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        fail(L"SHA-256 초기화 오류");
    try {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0)
            fail(L"SHA-256 오류");
        for (Pos pos = 0; pos < data.size();) {
            if (cancel && *cancel)
                fail(L"중지했습니다.");
            auto b = data.read(pos, std::min<Pos>(1 << 20, data.size() - pos));
            if (BCryptHashData(hash, b.data(), static_cast<ULONG>(b.size()), 0) < 0)
                fail(L"SHA-256 오류");
            pos += b.size();
            pump();
        }
        std::array<uint8_t, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), 32, 0) < 0)
            fail(L"SHA-256 오류");
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        std::ostringstream s;
        for (auto c : digest)
            s << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        return s.str();
    } catch (...) {
        if (hash)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        throw;
    }
}
} // namespace lp

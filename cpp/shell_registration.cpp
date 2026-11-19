#include "core.h"
#include "shell_mode.hpp"
#include <exception>
#include <system_error>
namespace lp {
namespace {
constexpr wchar_t ownerName[] = L"LHPad.Owner";
constexpr wchar_t ownerValue[] = L"LHPad.Explorer.v1";
struct Key {
    HKEY value{};
    ~Key() { if (value) RegCloseKey(value); }
};
void check(LSTATUS status) {
    if (status != ERROR_SUCCESS)
        throw std::system_error(status, std::system_category(), "LHPad Explorer registration");
}
bool open(HKEY hive, const std::wstring &path, Key &key) {
    auto status = RegOpenKeyExW(hive, path.c_str(), 0, KEY_READ | KEY_WRITE, &key.value);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return false;
    check(status); return true;
}
std::wstring read(HKEY key, const wchar_t *name = L"") {
    DWORD bytes{}, type{};
    auto status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) return {};
    check(status);
    if (type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 65536 || bytes % sizeof(wchar_t)) return {};
    std::wstring text(bytes / sizeof(wchar_t), 0);
    check(RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(text.data()), &bytes));
    if (text.back() != 0) return {};
    text.pop_back(); return text;
}
bool same(const std::wstring &a, const std::wstring &b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
bool ownCommand(const std::wstring &command) {
    if (command.empty()) return false;
    int count{};
    auto args = CommandLineToArgvW(command.c_str(), &count);
    if (!args) return false;
    const auto file = count ? fs::path(args[0]).filename().wstring() : L"";
    LocalFree(args);
    return same(file, L"LHPad.exe") || same(file, L"LitePad.exe");
}
bool owned(HKEY hive, const std::wstring &path, HKEY key) {
    if (read(key, ownerName) == ownerValue) return true;
    Key command;
    return open(hive, path + L"\\command", command) && ownCommand(read(command.value));
}
bool empty(HKEY key) {
    DWORD children{}, values{};
    check(RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &children, nullptr, nullptr,
                          &values, nullptr, nullptr, nullptr, nullptr));
    return !children && !values;
}
void erase(HKEY key, const wchar_t *name) {
    auto status = RegDeleteValueW(key, name);
    if (status != ERROR_FILE_NOT_FOUND) check(status);
}
void deleteEmpty(HKEY hive, const std::wstring &path) {
    { Key key; if (!open(hive, path, key) || !empty(key.value)) return; }
    auto status = RegDeleteKeyW(hive, path.c_str());
    if (status != ERROR_FILE_NOT_FOUND) check(status);
}
std::vector<std::wstring> legacyPaths(const std::wstring &base) {
    std::vector<std::wstring> paths;
    for (auto container : {L"*", L"AllFilesystemObjects"})
        for (auto verb : {L"LHPad", L"LitePad"})
            paths.push_back(base + L"\\" + container + L"\\shell\\" + verb);
    return paths;
}
void cleanLegacy(HKEY hive, const std::wstring &base, bool rejectForeign) {
    // Preflight all reserved keys before changing any. Old releases had no
    // ownership marker, so recognize only an explicit LHPad/LitePad executable.
    for (const auto &path : legacyPaths(base)) {
        Key key;
        if (open(hive, path, key) && !owned(hive, path, key.value) && rejectForeign) {
            Key command;
            const bool active = (open(hive, path + L"\\command", command) && !read(command.value).empty()) ||
                                !read(key.value, L"ExplorerCommandHandler").empty() ||
                                !read(key.value, L"SubCommands").empty();
            if (active) throw std::runtime_error("A LHPad menu key belongs to another application.");
        }
    }
    for (const auto &path : legacyPaths(base)) {
        {
            Key key;
            if (!open(hive, path, key) || !owned(hive, path, key.value)) continue;
            Key command;
            if (open(hive, path + L"\\command", command)) erase(command.value, L"");
            for (auto name : {L"", L"Icon", L"MUIVerb", L"MultiSelectModel", ownerName, L"LHPad.Executable"})
                erase(key.value, name);
        }
        deleteEmpty(hive, path + L"\\command");
        deleteEmpty(hive, path);
    }
}
void write(HKEY key, const wchar_t *name, const std::wstring &value) {
    check(RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))));
}
void status(bool enable, bool classic) {
    std::ofstream(exeDir() / L"shell-package-status.txt")
        << (enable ? "Explorer menu registration checked successfully.\n"
                   : "Explorer menu registration removed.\n")
        << "Mode: " << (classic ? "classic" : "packaged") << '\n';
}
} // namespace
namespace detail {
void configureShellAt(HKEY hive, const std::wstring &base, const fs::path &exe,
                      bool enable, bool classic) {
    if (!hive || base.empty()) throw std::invalid_argument("A private registry base is required.");
    cleanLegacy(hive, base, enable);
    if (!enable || !classic) return;
    const auto path = base + L"\\*\\shell\\LHPad";
    Key key;
    check(RegCreateKeyExW(hive, path.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE,
                          nullptr, &key.value, nullptr));
    write(key.value, L"", L"LHPad로 열기");
    write(key.value, L"Icon", L"\"" + exe.wstring() + L"\",0");
    write(key.value, L"MultiSelectModel", L"Document");
    write(key.value, ownerName, ownerValue);
    write(key.value, L"LHPad.Executable", exe.wstring());
    Key command;
    check(RegCreateKeyExW(hive, (path + L"\\command").c_str(), 0, nullptr, 0,
                          KEY_WRITE, nullptr, &command.value, nullptr));
    write(command.value, L"", L"\"" + exe.wstring() + L"\" \"%1\"");
}
} // namespace detail
void registerShell(const fs::path &exe) {
    explorer_menu::RegistrationLock lock(L"Local\\LHPad.ExplorerRegistration");
    const bool classic = !explorer_menu::preferPackaged();
    try {
        if (classic) configureModernExplorer(false);
        detail::configureShellAt(HKEY_CURRENT_USER, L"Software\\Classes", exe, true, classic);
        if (!classic) configureModernExplorer(true);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        status(true, classic);
    } catch (const std::exception &e) {
        std::ofstream(exeDir() / L"shell-package-status.txt") << e.what();
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        throw;
    }
}
void unregisterShell() {
    explorer_menu::RegistrationLock lock(L"Local\\LHPad.ExplorerRegistration");
    std::exception_ptr failure;
    try { detail::configureShellAt(HKEY_CURRENT_USER, L"Software\\Classes", {}, false, false); }
    catch (...) { failure = std::current_exception(); }
    try { configureModernExplorer(false); }
    catch (...) { if (!failure) failure = std::current_exception(); }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (failure) {
        try { std::rethrow_exception(failure); }
        catch (const std::exception &e) {
            std::ofstream(exeDir() / L"shell-package-status.txt") << e.what();
            throw;
        }
    }
    status(false, !explorer_menu::preferPackaged());
}
bool shellRegistered(const fs::path &exe) {
    const bool classic = !explorer_menu::preferPackaged();
    if (!classic) {
        for (const auto &path : legacyPaths(L"Software\\Classes")) {
            Key key;
            if (open(HKEY_CURRENT_USER, path, key) && owned(HKEY_CURRENT_USER, path, key.value)) return false;
        }
        return modernExplorerRegistered();
    }
    Key command;
    return open(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\LHPad\\command", command) &&
           read(command.value) == L"\"" + exe.wstring() + L"\" \"%1\"";
}
} // namespace lp

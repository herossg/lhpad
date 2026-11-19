#include "core.h"
#include "shell_mode.hpp"
#include <iostream>
#include <stdexcept>
namespace lp {
// The test exercises only private registry trees; package deployment is tested
// separately in the desktop session. No editor/format/archive functions link.
fs::path exeDir() { return fs::temp_directory_path(); }
void configureModernExplorer(bool) { throw std::runtime_error("Unexpected package deployment in registry test"); }
bool modernExplorerRegistered() { return false; }
namespace detail {
void configureShellAt(HKEY, const std::wstring &, const fs::path &, bool, bool);
}
}
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void put(HKEY root, const std::wstring &path, const wchar_t *name, const std::wstring &text) {
    HKEY key{};
    require(RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr) == 0, "create fixture key");
    auto result = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)text.c_str(), DWORD((text.size()+1)*sizeof(wchar_t)));
    RegCloseKey(key); require(result == 0, "write fixture value");
}
std::wstring get(HKEY root, const std::wstring &path, const wchar_t *name = L"") {
    wchar_t value[32768]{}; DWORD bytes = sizeof(value);
    if (RegGetValueW(root, path.c_str(), name, RRF_RT_REG_SZ, nullptr, value, &bytes) != 0) return {};
    return value;
}
bool exists(HKEY root, const std::wstring &path) {
    HKEY key{}; auto result = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
    if (key) RegCloseKey(key); return result == 0;
}
int main() {
    auto base = L"Software\\LHPad-ShellRegistrationTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HKEY sandbox{}; DWORD created{};
    require(RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &sandbox, &created) == 0 && created == REG_CREATED_NEW_KEY, "reserve private test root");
    int failed = 0;
    auto run = [&](const char *name, auto test) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception &e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    const std::filesystem::path exe = L"C:\\fixture & apps\\LHPad.exe";
    using lp::detail::configureShellAt;
    run("Windows client/server and version boundary", [] {
        using explorer_menu::packagedForVersion;
        require(!packagedForVersion(10,19045,VER_NT_WORKSTATION), "Windows 10");
        require(!packagedForVersion(10,21999,VER_NT_WORKSTATION), "pre-Windows 11");
        require(packagedForVersion(10,22000,VER_NT_WORKSTATION), "Windows 11 boundary");
        require(packagedForVersion(10,26100,VER_NT_WORKSTATION), "later Windows 11");
        require(!packagedForVersion(10,26100,VER_NT_SERVER), "Windows Server");
        require(packagedForVersion(11,1,VER_NT_WORKSTATION), "future Windows client");
    });
    run("classic idempotence and portable relocation", [&] {
        for (int i=0;i<3;++i) configureShellAt(sandbox,L"Classic",exe,true,true);
        require(get(sandbox,L"Classic\\*\\shell\\LHPad\\command") == L"\""+exe.wstring()+L"\" \"%1\"", "one quoted classic command");
        auto moved = std::filesystem::path(L"D:\\moved\\LHPad.exe");
        configureShellAt(sandbox,L"Classic",moved,true,true);
        require(get(sandbox,L"Classic\\*\\shell\\LHPad\\command").find(moved.wstring()) != std::wstring::npos, "moved executable replaced");
        require(!exists(sandbox,L"Classic\\AllFilesystemObjects\\shell\\LHPad"), "no redundant provider");
    });
    run("unmarked LHPad and LitePad migration to packaged", [&] {
        for (auto name : {L"LHPad",L"LitePad"})
            for (auto container : {L"*",L"AllFilesystemObjects"}) {
                auto path=std::wstring(L"Old\\")+container+L"\\shell\\"+name;
                put(sandbox,path,L"",L"old menu");
                put(sandbox,path+L"\\command",L"",std::wstring(L"\"C:\\old\\")+name+L".exe\" \"%1\"");
            }
        for(int i=0;i<3;++i) configureShellAt(sandbox,L"Old",exe,true,false);
        for(auto name:{L"LHPad",L"LitePad"})for(auto container:{L"*",L"AllFilesystemObjects"})
            require(!exists(sandbox,std::wstring(L"Old\\")+container+L"\\shell\\"+name), "all legacy menus removed");
    });
    run("unknown values survive migration and repeated cleanup", [&] {
        configureShellAt(sandbox,L"Preserve",exe,true,true);
        put(sandbox,L"Preserve\\*\\shell\\LHPad",L"ForeignValue",L"keep");
        configureShellAt(sandbox,L"Preserve",exe,true,false);
        configureShellAt(sandbox,L"Preserve",exe,true,false);
        require(get(sandbox,L"Preserve\\*\\shell\\LHPad",L"ForeignValue") == L"keep", "foreign data retained");
        require(get(sandbox,L"Preserve\\*\\shell\\LHPad\\command").empty(), "no active legacy command");
    });
    run("foreign activation rejected and preserved", [&] {
        put(sandbox,L"Conflict\\*\\shell\\LHPad\\command",L"",L"\"C:\\OtherApp.exe\" \"%1\"");
        bool rejected=false;
        try { configureShellAt(sandbox,L"Conflict",exe,true,false); } catch (...) { rejected=true; }
        require(rejected,"foreign command rejected");
        configureShellAt(sandbox,L"Conflict",exe,false,false);
        require(get(sandbox,L"Conflict\\*\\shell\\LHPad\\command").find(L"OtherApp.exe") != std::wstring::npos, "foreign command untouched");
    });
    run("unregister is complete and repeatable", [&] {
        configureShellAt(sandbox,L"Classic",exe,false,false);
        configureShellAt(sandbox,L"Classic",exe,false,false);
        require(!exists(sandbox,L"Classic\\*\\shell\\LHPad"), "classic removed");
    });
    RegCloseKey(sandbox);
    require(base.starts_with(L"Software\\LHPad-ShellRegistrationTests-"), "cleanup boundary");
    require(RegDeleteTreeW(HKEY_CURRENT_USER,base.c_str()) == 0, "cleanup private fixtures");
    std::cout << "Failures: " << failed << '\n';
    return failed ? 1 : 0;
}

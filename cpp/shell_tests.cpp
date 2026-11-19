#include "shell_command.h"
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <atomic>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
static void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
// A remote/cloud Shell provider may block on these callbacks. Presentation
// must work without touching any per-item or attribute/path callback at all.
class NoInspectItems final : public IShellItemArray {
    std::atomic_ulong refs{1};
public:
    DWORD count = 100000;
    unsigned inspected = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IShellItemArray) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE BindToHandler(IBindCtx *, REFGUID, REFIID, void **) override { ++inspected; return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE GetPropertyStore(GETPROPERTYSTOREFLAGS, REFIID, void **) override { ++inspected; return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE GetPropertyDescriptionList(REFPROPERTYKEY, REFIID, void **) override { ++inspected; return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE GetAttributes(SIATTRIBFLAGS, SFGAOF, SFGAOF *) override { ++inspected; return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE GetCount(DWORD *out) override { *out = count; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetItemAt(DWORD, IShellItem **) override { ++inspected; return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE EnumItems(IEnumShellItems **) override { ++inspected; return E_UNEXPECTED; }
};
static ComPtr<IShellItemArray> items(const std::vector<fs::path> &paths) {
    std::vector<PIDLIST_ABSOLUTE> ids;
    for (const auto &p : paths) {
        PIDLIST_ABSOLUTE id{};
        if (FAILED(SHParseDisplayName(p.c_str(), nullptr, &id, 0, nullptr))) throw std::runtime_error("parse path");
        ids.push_back(id);
    }
    ComPtr<IShellItemArray> result;
    auto hr = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()),
        const_cast<PCIDLIST_ABSOLUTE *>(ids.data()), &result);
    for (auto id : ids) CoTaskMemFree(id);
    if (FAILED(hr)) throw std::runtime_error("create selection");
    return result;
}
int wmain(int argc, wchar_t **argv) {
    wchar_t own[32768]; GetModuleFileNameW(nullptr, own, 32768);
    auto directory = fs::path(own).parent_path();
    if (fs::path(own).filename() == L"LHPad.exe") {
        std::ofstream capture(directory / L"invocation.bin", std::ios::binary);
        for (int i = 1; i < argc; ++i) {
            auto size = static_cast<uint32_t>(wcslen(argv[i]));
            capture.write(reinterpret_cast<const char *>(&size), sizeof(size));
            capture.write(reinterpret_cast<const char *>(argv[i]), size * sizeof(wchar_t));
        }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 3, "DLL path and isolated fixture directory supplied");
        fs::path fixture = fs::absolute(argv[2]); fs::create_directories(fixture);
        fs::copy_file(own, fixture / L"LHPad.exe", fs::copy_options::overwrite_existing);
        fs::copy_file(argv[1], fixture / L"LHPadShell.dll", fs::copy_options::overwrite_existing);
        std::vector<fs::path> paths{fixture / L"한글 & spaces.txt", fixture / L"--second.json"};
        for (const auto &path : paths) std::ofstream(path) << "fixture\n";
        auto module = LoadLibraryW((fixture / L"LHPadShell.dll").c_str());
        check(module != nullptr, "shell DLL loads without editor engines");
        auto factoryEntry = reinterpret_cast<HRESULT (__stdcall *)(REFCLSID, REFIID, void **)>(GetProcAddress(module, "DllGetClassObject"));
        auto unload = reinterpret_cast<HRESULT (__stdcall *)()>(GetProcAddress(module, "DllCanUnloadNow"));
        check(factoryEntry && unload && unload() == S_OK, "COM exports and initial lifetime");
        ComPtr<IClassFactory> factory;
        check(factoryEntry(lp::explorerCommandId, IID_PPV_ARGS(&factory)) == S_OK, "independent LHPad COM class");
        ComPtr<IExplorerCommand> command;
        check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&command)) == S_OK, "IExplorerCommand activation");
        check(unload() == S_FALSE, "live COM references prevent unload");
        PWSTR title{}; command->GetTitle(nullptr, &title);
        check(title && std::wstring(title) == L"LHPad로 열기", "first-menu command title"); CoTaskMemFree(title);
        ComPtr<NoInspectItems> cheap;
        cheap.Attach(new NoInspectItems);
        EXPCMDSTATE state{};
        check(command->GetState(cheap.Get(), FALSE, &state) == S_OK && state == ECS_ENABLED,
              "fast state enables large selection without E_PENDING");
        check(command->GetState(cheap.Get(), TRUE, &state) == S_OK && state == ECS_ENABLED,
              "slow state also avoids per-item queries");
        command->GetTitle(cheap.Get(), &title); CoTaskMemFree(title);
        command->GetToolTip(cheap.Get(), &title); CoTaskMemFree(title);
        check(command->GetIcon(cheap.Get(), &title) == S_OK && title &&
                  std::wstring(title).ends_with(L"LHPadShell.dll\",-2"),
              "menu icon comes from the loaded shell DLL"); CoTaskMemFree(title);
        HICON largeIcon{}, smallIcon{};
        check(ExtractIconExW((fixture / L"LHPadShell.dll").c_str(), -2, &largeIcon, &smallIcon, 1) > 0 &&
                  largeIcon && smallIcon,
              "embedded shell DLL icon resource can be extracted");
        if (largeIcon) DestroyIcon(largeIcon);
        if (smallIcon) DestroyIcon(smallIcon);
        check(cheap->inspected == 0, "all presentation callbacks avoid item, attribute and property queries");
        cheap->count = 0;
        check(command->GetState(cheap.Get(), FALSE, &state) == S_OK && state == ECS_HIDDEN,
              "empty selection stays hidden without deferred work");
        cheap->count = 100001;
        check(command->GetState(cheap.Get(), FALSE, &state) == S_OK && state == ECS_HIDDEN,
              "over-limit selection stays hidden without enumeration");
        auto selection = items(paths);
        check(command->GetState(selection.Get(), TRUE, &state) == S_OK && state == ECS_ENABLED, "multiple files enabled");
        auto folders = items({fixture});
        check(FAILED(command->Invoke(folders.Get(), nullptr)), "folder invocation rejected before editor launch");
        check(FAILED(command->Invoke(nullptr, nullptr)), "empty selection rejected");
        fs::remove(fixture / L"invocation.bin");
        check(command->Invoke(selection.Get(), nullptr) == S_OK, "actual CreateProcess invocation");
        auto capturePath = fixture / L"invocation.bin";
        for (int i = 0; i < 200 && (!fs::exists(capturePath) || fs::file_size(capturePath) == 0); ++i) Sleep(25);
        std::ifstream capture(capturePath, std::ios::binary);
        std::vector<std::wstring> received;
        uint32_t size{};
        while (capture.read(reinterpret_cast<char *>(&size), sizeof(size))) {
            std::wstring arg(size, 0); capture.read(reinterpret_cast<char *>(arg.data()), size * sizeof(wchar_t)); received.push_back(arg);
        }
        check(received == std::vector<std::wstring>{L"--", paths[0].wstring(), paths[1].wstring()}, "Unicode, spaces, ampersand and multiple paths survive invocation");
        auto quoted = L"probe.exe " + lp::quoteShellArgument(L"C:\\a b\\") + L" " + lp::quoteShellArgument(L"quote\"inside");
        int count{}; auto roundtrip = CommandLineToArgvW(quoted.c_str(), &count);
        check(count == 3 && std::wstring(roundtrip[1]) == L"C:\\a b\\" && std::wstring(roundtrip[2]) == L"quote\"inside", "Windows argument escaping roundtrip"); LocalFree(roundtrip);
        command.Reset(); factory.Reset();
        check(unload() == S_OK, "COM references released"); FreeLibrary(module);
        CoUninitialize(); return 0;
    } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; CoUninitialize(); return 1; }
}

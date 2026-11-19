#include "shell_command.h"
#include <shobjidl.h>
#include <atomic>
#include <filesystem>
#include <vector>
#include <new>

namespace {
HMODULE module{};
std::atomic_long live{};
std::filesystem::path modulePath() {
    wchar_t path[32768];
    auto n = GetModuleFileNameW(module, path, 32768);
    if (!n || n >= 32768) throw std::bad_alloc();
    return std::filesystem::path(std::wstring(path, n));
}
std::filesystem::path executable() { return modulePath().parent_path() / L"LHPad.exe"; }
HRESULT copyString(const std::wstring &s, LPWSTR *out) {
    if (!out) return E_POINTER;
    *out = static_cast<LPWSTR>(CoTaskMemAlloc((s.size() + 1) * sizeof(wchar_t)));
    if (!*out) return E_OUTOFMEMORY;
    memcpy(*out, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
    return S_OK;
}
HRESULT selection(IShellItemArray *items, std::vector<std::wstring> &paths) {
    DWORD count{};
    if (!items || FAILED(items->GetCount(&count)) || !count || count > 100000) return E_INVALIDARG;
    for (DWORD i = 0; i < count; ++i) {
        IShellItem *item{};
        auto hr = items->GetItemAt(i, &item);
        if (FAILED(hr)) return hr;
        SFGAOF attributes{};
        hr = item->GetAttributes(SFGAO_FILESYSTEM | SFGAO_FOLDER, &attributes);
        if (FAILED(hr) || !(attributes & SFGAO_FILESYSTEM) || (attributes & SFGAO_FOLDER)) {
            item->Release(); return E_INVALIDARG;
        }
        PWSTR path{};
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        item->Release();
        if (FAILED(hr)) return hr;
        try { paths.emplace_back(path); }
        catch (...) { CoTaskMemFree(path); throw; }
        CoTaskMemFree(path);
    }
    return S_OK;
}
class Command final : public IExplorerCommand {
    std::atomic_ulong refs{1};
public:
    Command() { ++live; }
    ~Command() { --live; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IExplorerCommand)) return E_NOINTERFACE;
        *out = static_cast<IExplorerCommand *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTitle(IShellItemArray *, LPWSTR *out) override {
        return copyString(L"LHPad로 열기", out);
    }
    HRESULT STDMETHODCALLTYPE GetIcon(IShellItemArray *, LPWSTR *out) override {
        try { return copyString(L"\"" + modulePath().wstring() + L"\",-2", out); }
        catch (...) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE GetToolTip(IShellItemArray *, LPWSTR *out) override {
        return copyString(L"선택한 파일을 LHPad의 새 탭으로 엽니다.", out);
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalName(GUID *id) override {
        if (!id) return E_POINTER;
        *id = lp::explorerCommandId; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetState(IShellItemArray *items, BOOL /*okToBeSlow*/, EXPCMDSTATE *state) override {
        if (!state) return E_POINTER;
        *state = ECS_HIDDEN;
        DWORD count{};
        // The manifest limits this verb to file types. Do not resolve Shell
        // items, attributes or paths while Explorer is building its menu.
        // Invoke still validates every selected item before starting the editor.
        if (items && SUCCEEDED(items->GetCount(&count)) && count && count <= 100000)
            *state = ECS_ENABLED;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IShellItemArray *items, IBindCtx *) override {
        try {
            std::vector<std::wstring> paths;
            auto hr = selection(items, paths);
            if (FAILED(hr)) return hr;
            auto exe = executable();
            // Batch very large selections without truncating Windows' command line.
            // Each batch forwards to the editor's existing-instance IPC.
            std::vector<std::wstring> batches;
            auto prefix = lp::quoteShellArgument(exe.wstring()) + L" --";
            auto command = prefix;
            for (const auto &path : paths) {
                auto argument = L" " + lp::quoteShellArgument(path);
                if (prefix.size() + argument.size() >= 32767) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
                if (command.size() + argument.size() >= 32767) { batches.push_back(command); command = prefix; }
                command += argument;
            }
            batches.push_back(command);
            for (auto &batch : batches) {
                STARTUPINFOW startup{sizeof(startup)};
                PROCESS_INFORMATION process{};
                if (!CreateProcessW(exe.c_str(), batch.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                    exe.parent_path().c_str(), &startup, &process)) return HRESULT_FROM_WIN32(GetLastError());
                CloseHandle(process.hThread); CloseHandle(process.hProcess);
            }
            return S_OK;
        } catch (...) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE GetFlags(EXPCMDFLAGS *flags) override {
        if (!flags) return E_POINTER;
        *flags = ECF_DEFAULT; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumSubCommands(IEnumExplorerCommand **out) override {
        if (!out) return E_POINTER;
        *out = nullptr; return E_NOTIMPL;
    }
};
class Factory final : public IClassFactory {
    std::atomic_ulong refs{1};
public:
    Factory() { ++live; }
    ~Factory() { --live; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto command = new (std::nothrow) Command;
        if (!command) return E_OUTOFMEMORY;
        auto hr = command->QueryInterface(iid, out); command->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) ++live; else --live; return S_OK; }
};
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void **out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != lp::explorerCommandId) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new (std::nothrow) Factory;
    if (!factory) return E_OUTOFMEMORY;
    auto hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return live == 0 ? S_OK : S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}

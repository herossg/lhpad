#include "app.h"
namespace lp {
static std::wstring profileKey(const fs::path &profile) {
    auto s = fs::absolute(profile).lexically_normal().wstring();
    uint64_t hash = 14695981039346656037ull;
    for (auto c : s) {
        hash ^= towlower(c);
        hash *= 1099511628211ull;
    }
    return L"LHPad.Profile." + std::to_wstring(hash);
}
static HWND existingWindow(const std::wstring &key) {
    struct State {
        const wchar_t *key;
        HWND result;
    };
    State state{key.c_str(), nullptr};
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto &state = *reinterpret_cast<State *>(p);
            if (GetPropW(h, state.key)) {
                state.result = h;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));
    return state.result;
}
} // namespace lp
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    using namespace lp;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&common);
    int exit = 0;
    try {
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> args;
        for (int i = 1; i < argc; i++)
            args.emplace_back(argv[i]);
        LocalFree(argv);
        auto has = [&](const wchar_t *x) { return std::find(args.begin(), args.end(), x) != args.end(); };
        if (has(L"--register-shell")) {
            registerShell(exeDir() / L"LHPad.exe");
            OleUninitialize();
            return 0;
        }
        if (has(L"--unregister-shell")) {
            unregisterShell();
            OleUninitialize();
            return 0;
        }
        bool test = has(L"--self-test") || has(L"--integration-test");
        fs::path profile = defaultProfile();
        std::vector<std::string> files;
        bool positional = false;
        for (size_t i = 0; i < args.size(); i++) {
            if (!positional && args[i] == L"--") {
                positional = true;
                continue;
            }
            if (!positional && args[i] == L"--profile") {
                if (i + 1 == args.size())
                    fail(L"--profile 경로가 필요합니다.");
                profile = fs::absolute(args[++i]);
                continue;
            }
            if (!positional && args[i].starts_with(L"--")) {
                if (args[i] != L"--demo" && args[i] != L"--self-test" && args[i] != L"--integration-test")
                    fail(L"알 수 없는 옵션: " + args[i]);
                continue;
            }
            files.push_back(narrow(fs::absolute(args[i]).wstring()));
        }
        if (test)
            profile = exeDir() / L"test-results" / wide("profile-" + guid());
        disk::create_directories(profile);
        auto key = profileKey(profile);
        Handle mutex(CreateMutexW(nullptr, TRUE, (L"Local\\" + key).c_str()));
        if (!mutex)
            fail(errorText());
        bool existing = GetLastError() == ERROR_ALREADY_EXISTS;
        if (existing && !test) {
            HWND target = nullptr;
            auto start = GetTickCount64();
            while (!target && GetTickCount64() - start < 8000) {
                target = existingWindow(key);
                if (!target)
                    Sleep(50);
            }
            if (!target)
                fail(L"LHPad가 시작 중이거나 종료 중입니다. 잠시 후 다시 시도해 주세요.");
            DWORD pid = 0;
            GetWindowThreadProcessId(target, &pid);
            AllowSetForegroundWindow(pid);
            Json value = files;
            auto text = value.dump();
            COPYDATASTRUCT data{0x4C504144, static_cast<DWORD>(text.size()), text.data()};
            DWORD_PTR result = 0;
            if (!SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                     SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &result) ||
                !result)
                fail(L"실행 중인 LHPad에 파일을 전달하지 못했습니다.");
            OleUninitialize();
            return 0;
        }
        Handle lock(CreateFileW(winPath(profile / L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!lock)
            fail(L"같은 프로필의 LHPad가 실행 중입니다. 이전 버전 창을 닫고 다시 실행해 주세요.");
        auto scintilla = LoadLibraryW(winPath(exeDir() / L"Scintilla.dll").c_str()),
             lexilla = LoadLibraryW(winPath(exeDir() / L"Lexilla.dll").c_str());
        if (!scintilla || !lexilla)
            fail(L"Scintilla.dll 또는 Lexilla.dll을 읽을 수 없습니다. 배포 ZIP 전체를 같은 폴더에 풀어 "
                 L"주세요.\n" +
                 errorText());
        {
            App native(instance, profile);
            native.demo = has(L"--demo") || test;
            native.initialFiles = files;
            native.create(test ? SW_HIDE : show);
            SetPropW(native.window, key.c_str(), reinterpret_cast<HANDLE>(1));
            if (test) {
                exit = runTests(native, has(L"--self-test"));
                DestroyWindow(native.window);
            } else {
                native.start();
                MSG msg;
                while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    if (native.findWindow && IsWindowVisible(native.findWindow) &&
                        (msg.hwnd == native.findWindow || IsChild(native.findWindow, msg.hwnd)) &&
                        IsDialogMessageW(native.findWindow, &msg))
                        continue;
                    if (native.shortcut(msg))
                        continue;
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
        FreeLibrary(lexilla);
        FreeLibrary(scintilla);
    } catch (const std::exception &e) {
        if (!wcsstr(GetCommandLineW(), L"--quiet"))
            MessageBoxW(nullptr, (wide(e.what()) + L"\n탐색기 메뉴 등록 시에는 ZIP의 Register-LHPad.cmd를 실행해 주세요.").c_str(), L"LHPad", MB_OK | MB_ICONERROR);
        exit = 1;
    }
    OleUninitialize();
    return exit;
}

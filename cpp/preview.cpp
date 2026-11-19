#include "app.h"
#include <shlwapi.h>
namespace lp {
static int previewKind(const Document &d) {
    auto path = d.path.empty() ? fs::path(wide(d.name)) : d.path;
    auto ext = path.extension().wstring();
    if (d.language == "markdown" || !_wcsicmp(ext.c_str(), L".md") || !_wcsicmp(ext.c_str(), L".markdown") ||
        !_wcsicmp(ext.c_str(), L".mdown") || !_wcsicmp(ext.c_str(), L".mkd"))
        return 1;
    if (d.language == "html" || !_wcsicmp(ext.c_str(), L".html") || !_wcsicmp(ext.c_str(), L".htm") ||
        !_wcsicmp(ext.c_str(), L".xhtml"))
        return 2;
    if (!_wcsicmp(ext.c_str(), L".svg"))
        return 3;
    return 0;
}
void App::setView(int mode) {
    auto d = current();
    if (!d || d->binary)
        return;
    if (mode && !previewKind(*d))
        fail(L"미리보기는 Markdown, HTML, SVG에서 지원합니다. 언어 메뉴에서 문서 종류를 지정할 수 있습니다.");
    if (mode && d->secondary)
        toggleSplit(); // Preview panes and the same-document split share the same space.
    compareIds.clear();
    d->viewMode = mode;
    if (mode)
        refreshPreview(*d, true);
    layout();
}
static void checkHr(HRESULT hr, const wchar_t *text) {
    if (FAILED(hr))
        fail(std::wstring(text) + L"\n" + errorText(static_cast<DWORD>(hr)));
}
static std::wstring mime(const fs::path &path) {
    auto ext = path.extension().wstring();
    for (auto &c : ext)
        c = towlower(c);
    static const std::map<std::wstring, std::wstring> types = {
        {L".html", L"text/html"},     {L".htm", L"text/html"},         {L".css", L"text/css"},
        {L".js", L"text/javascript"}, {L".json", L"application/json"}, {L".svg", L"image/svg+xml"},
        {L".png", L"image/png"},      {L".jpg", L"image/jpeg"},        {L".jpeg", L"image/jpeg"},
        {L".gif", L"image/gif"},      {L".webp", L"image/webp"},       {L".avif", L"image/avif"},
        {L".ico", L"image/x-icon"},   {L".woff", L"font/woff"},        {L".woff2", L"font/woff2"},
        {L".ttf", L"font/ttf"}};
    auto it = types.find(ext);
    return it == types.end() ? L"application/octet-stream" : it->second;
}
static std::optional<fs::path> localResource(const Document &d, std::wstring uri) {
    const auto prefix = L"https://" + wide(d.origin) + L"/";
    if (!uri.starts_with(prefix) || d.path.empty())
        return {};
    auto relative = uri.substr(prefix.size());
    auto end = relative.find_first_of(L"?#");
    if (end != std::wstring::npos)
        relative.resize(end);
    std::wstring decoded(32768, 0);
    DWORD length = static_cast<DWORD>(decoded.size());
    if (FAILED(UrlUnescapeW(relative.data(), decoded.data(), &length, URL_UNESCAPE_AS_UTF8)))
        return {};
    decoded.resize(length);
    if (decoded.find(L':') != std::wstring::npos || decoded.find(L'\0') != std::wstring::npos)
        return {};
    auto root = disk::weakly_canonical(d.path.parent_path());
    auto path = disk::weakly_canonical(root / decoded);
    auto rel = path.lexically_relative(root);
    if (rel.empty() || *rel.begin() == L".." || rel.is_absolute() || !disk::is_regular_file(path))
        return {};
    return path;
}
void App::refreshPreview(Document &d, bool force) {
    d.previewPending = false;
    if (!d.viewMode || !d.editor)
        return;
    if (!d.previewHost) {
        d.previewHost =
            CreateWindowExW(0, L"STATIC", L"미리보기 준비 중…", WS_CHILD | WS_CLIPSIBLINGS | SS_LEFT, 0, 0,
                            100, 100, window, nullptr, instance, nullptr);
        SendMessageW(d.previewHost, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        DragAcceptFiles(d.previewHost, TRUE);
        SetWindowSubclass(d.previewHost, childProc, 1, reinterpret_cast<DWORD_PTR>(this));
        layout();
    }
    if (sci(d.editor, SCI_GETLENGTH) > 4 * 1024 * 1024) {
        if (d.controller)
            d.controller->put_IsVisible(FALSE);
        SetWindowTextW(d.previewHost, L"미리보기는 4 MiB 이하 문서에서 지원합니다.");
        return;
    }
    if (!previewKind(d)) {
        if (d.controller)
            d.controller->put_IsVisible(FALSE);
        SetWindowTextW(d.previewHost, L"이 문서 형식은 미리보기를 지원하지 않습니다.");
        return;
    }
    if (!d.browser) {
        if (d.previewInitializing)
            return;
        auto it = std::find_if(docs.begin(), docs.end(), [&](auto &p) { return p.get() == &d; });
        if (it != docs.end())
            ensureBrowser(*it);
        return;
    }
    if (force || d.renderedRevision != d.revision)
        renderPreview(d);
}
void App::ensureBrowser(std::shared_ptr<Document> doc) {
    using CreateEnvironment =
        HRESULT(STDAPICALLTYPE *)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
                                  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
    static HMODULE loader = LoadLibraryW(winPath(exeDir() / L"WebView2Loader.dll").c_str());
    auto create = loader ? reinterpret_cast<CreateEnvironment>(
                               GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))
                         : nullptr;
    if (!create) {
        SetWindowTextW(doc->previewHost, L"WebView2Loader.dll을 찾을 수 없습니다.");
        return;
    }
    doc->previewInitializing = true;
    std::weak_ptr<Document> weak = doc;
    auto userData = profile / L"webview2-native";
    auto handler = Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this, weak](HRESULT hr, ICoreWebView2Environment *environment) -> HRESULT {
            auto d = weak.lock();
            if (!d || closing)
                return S_OK;
            if (FAILED(hr) || !environment) {
                d->previewInitializing = false;
                SetWindowTextW(d->previewHost, L"미리보기를 사용하려면 Microsoft Edge WebView2 Runtime이 "
                                               L"필요합니다.\n원문 편집은 계속 사용할 수 있습니다.");
                return S_OK;
            }
            ComPtr<ICoreWebView2Environment> env = environment;
            auto controllerHandler =
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, weak, env](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                        auto d = weak.lock();
                        if (!d || closing) {
                            if (controller)
                                controller->Close();
                            return S_OK;
                        }
                        d->previewInitializing = false;
                        if (FAILED(result) || !controller) {
                            SetWindowTextW(d->previewHost, L"WebView2 미리보기를 시작할 수 없습니다.");
                            return S_OK;
                        }
                        try {
                            d->controller = controller;
                            checkHr(controller->get_CoreWebView2(&d->browser), L"브라우저 생성 오류");
                            d->origin = "lp-" + d->id + ".invalid";
                            std::transform(d->origin.begin(), d->origin.end(), d->origin.begin(),
                                           [](unsigned char c) { return static_cast<char>(tolower(c)); });
                            ComPtr<ICoreWebView2Settings> settings;
                            d->browser->get_Settings(&settings);
                            settings->put_AreHostObjectsAllowed(FALSE);
                            settings->put_IsWebMessageEnabled(FALSE);
                            settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_IsScriptEnabled(FALSE);
                            EventRegistrationToken token{};
                            d->browser->add_PermissionRequested(
                                Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(
                                    [](ICoreWebView2 *,
                                       ICoreWebView2PermissionRequestedEventArgs *e) -> HRESULT {
                                        e->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            ComPtr<ICoreWebView2_4> browser4;
                            if (SUCCEEDED(d->browser.As(&browser4)))
                                browser4->add_DownloadStarting(
                                    Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                                        [](ICoreWebView2 *,
                                           ICoreWebView2DownloadStartingEventArgs *e) -> HRESULT {
                                            e->put_Cancel(TRUE);
                                            return S_OK;
                                        })
                                        .Get(),
                                    &token);
                            d->browser->add_NewWindowRequested(
                                Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [this](ICoreWebView2 *,
                                           ICoreWebView2NewWindowRequestedEventArgs *e) -> HRESULT {
                                        e->put_Handled(TRUE);
                                        BOOL user = FALSE;
                                        e->get_IsUserInitiated(&user);
                                        LPWSTR uri = nullptr;
                                        e->get_Uri(&uri);
                                        if (user && uri &&
                                            (wcsncmp(uri, L"https://", 8) == 0 ||
                                             wcsncmp(uri, L"http://", 7) == 0))
                                            ShellExecuteW(window, L"open", uri, nullptr, nullptr,
                                                          SW_SHOWNORMAL);
                                        CoTaskMemFree(uri);
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            auto filter = L"https://" + wide(d->origin) + L"/*";
                            d->browser->AddWebResourceRequestedFilter(filter.c_str(),
                                                                      COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                            d->browser->add_WebResourceRequested(
                                Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                    [weak, env](ICoreWebView2 *,
                                                ICoreWebView2WebResourceRequestedEventArgs *e) -> HRESULT {
                                        auto d = weak.lock();
                                        if (!d)
                                            return S_OK;
                                        LPWSTR raw = nullptr;
                                        ComPtr<ICoreWebView2WebResourceRequest> request;
                                        e->get_Request(&request);
                                        request->get_Uri(&raw);
                                        std::wstring uri = raw ? raw : L"";
                                        CoTaskMemFree(raw);
                                        auto main =
                                            L"https://" + wide(d->origin) + L"/__lhpad_preview__.html";
                                        Bytes bytes;
                                        std::wstring type = L"text/html; charset=utf-8";
                                        int status = 200;
                                        try {
                                            if (uri.starts_with(main))
                                                bytes.assign(d->previewHtml.begin(), d->previewHtml.end());
                                            else if (auto path = localResource(*d, uri)) {
                                                bytes = readAll(*path, 32ull * 1024 * 1024);
                                                type = mime(*path);
                                            } else
                                                status = 404;
                                        } catch (...) {
                                            status = 404;
                                        }
                                        ComPtr<IStream> stream;
                                        stream.Attach(
                                            SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
                                        ComPtr<ICoreWebView2WebResourceResponse> response;
                                        auto headers =
                                            L"Content-Type: " + type + L"\r\nCache-Control: no-store";
                                        env->CreateWebResourceResponse(stream.Get(), status,
                                                                       status == 200 ? L"OK" : L"Not Found",
                                                                       headers.c_str(), &response);
                                        e->put_Response(response.Get());
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            d->browser->add_NavigationStarting(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [this, weak](ICoreWebView2 *,
                                                 ICoreWebView2NavigationStartingEventArgs *e) -> HRESULT {
                                        auto d = weak.lock();
                                        if (!d)
                                            return S_OK;
                                        LPWSTR raw = nullptr;
                                        e->get_Uri(&raw);
                                        std::wstring uri = raw ? raw : L"";
                                        CoTaskMemFree(raw);
                                        auto prefix = L"https://" + wide(d->origin) + L"/";
                                        if (uri.starts_with(prefix + L"__lhpad_preview__.html") ||
                                            uri == L"about:blank")
                                            return S_OK;
                                        e->put_Cancel(TRUE);
                                        BOOL user = FALSE;
                                        e->get_IsUserInitiated(&user);
                                        if (!user)
                                            return S_OK;
                                        if (uri.starts_with(prefix) && !d->path.empty()) {
                                            std::wstring relative = uri.substr(prefix.size());
                                            auto hash = relative.find_first_of(L"?#");
                                            if (hash != std::wstring::npos)
                                                relative.resize(hash);
                                            std::wstring decoded(32768, 0);
                                            DWORD count = static_cast<DWORD>(decoded.size());
                                            if (SUCCEEDED(UrlUnescapeW(relative.data(), decoded.data(),
                                                                       &count, URL_UNESCAPE_AS_UTF8))) {
                                                decoded.resize(count);
                                                auto root = disk::weakly_canonical(d->path.parent_path());
                                                auto path = disk::weakly_canonical(root / decoded);
                                                auto rel = path.lexically_relative(root);
                                                if (!rel.empty() && *rel.begin() != L".." &&
                                                    disk::is_regular_file(path))
                                                    PostMessageW(
                                                        window, WM_APP + 4, 0,
                                                        reinterpret_cast<LPARAM>(new std::vector<std::string>{
                                                            narrow(path.wstring())}));
                                            }
                                        } else if (uri.starts_with(L"file:///")) {
                                            std::wstring path(32768, 0);
                                            DWORD count = static_cast<DWORD>(path.size());
                                            if (SUCCEEDED(PathCreateFromUrlW(uri.c_str(), path.data(), &count,
                                                                             0))) {
                                                path.resize(wcslen(path.c_str()));
                                                PostMessageW(window, WM_APP + 4, 0,
                                                             reinterpret_cast<LPARAM>(
                                                                 new std::vector<std::string>{narrow(path)}));
                                            }
                                        } else if (uri.starts_with(L"https://") ||
                                                   uri.starts_with(L"http://") || uri.starts_with(L"mailto:"))
                                            ShellExecuteW(window, L"open", uri.c_str(), nullptr, nullptr,
                                                          SW_SHOWNORMAL);
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            renderPreview(*d);
                            layout();
                        } catch (const std::exception &e) {
                            SetWindowTextW(d->previewHost, wide(e.what()).c_str());
                        }
                        return S_OK;
                    });
            return environment->CreateCoreWebView2Controller(d->previewHost, controllerHandler.Get());
        });
    auto result = create(nullptr, userData.c_str(), nullptr, handler.Get());
    if (FAILED(result)) {
        doc->previewInitializing = false;
        SetWindowTextW(doc->previewHost, L"미리보기 초기화 오류입니다. WebView2 Runtime을 확인하세요.");
    }
}
void App::renderPreview(Document &d) {
    auto source = textRange(d.editor, 0, sci(d.editor, SCI_GETLENGTH));
    int kind = previewKind(d);
    if (kind == 2)
        d.previewHtml = source;
    else {
        auto body = kind == 1 ? markdown(source) : "<div class=\"svg-preview\">" + source + "</div>";
        d.previewHtml =
            "<!doctype html><html lang=\"ko\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\"><title>" +
            htmlEscape(d.name) +
            "</title><style>body{font:16px/1.65 'Segoe UI','Malgun "
            "Gothic',sans-serif;color:#223039;background:#fff;margin:0}main{max-width:960px;margin:auto;"
            "padding:30px 38px}h1,h2{border-bottom:1px solid "
            "#dbe6e2;padding-bottom:.3em}a{color:#087e78}pre{background:#f1f5f4;padding:18px;border-radius:"
            "8px;overflow:auto}code{font-family:Consolas,monospace}table{border-collapse:collapse}td,th{"
            "border:1px solid #cad8d4;padding:8px 14px}blockquote{border-left:4px solid "
            "#a8cfc4;padding-left:18px;color:#60746d}img,svg{max-width:100%;height:auto}.svg-preview{text-"
            "align:center}</style></head><body><main>" +
            body + "</main></body></html>";
    }
    ComPtr<ICoreWebView2Settings> settings;
    d.browser->get_Settings(&settings);
    settings->put_IsScriptEnabled(kind == 2 && d.scripts);
    auto url =
        L"https://" + wide(d.origin) + L"/__lhpad_preview__.html?revision=" + std::to_wstring(d.revision);
    d.browser->Navigate(url.c_str());
    d.renderedRevision = d.revision;
    d.previewPending = false;
    if (d.controller)
        d.controller->put_IsVisible(d.viewMode != 0);
}
} // namespace lp

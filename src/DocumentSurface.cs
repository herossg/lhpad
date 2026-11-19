using System.Diagnostics;
using System.Net;
using System.Text;
using System.Text.Json;
using Markdig;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace LHPad;

internal enum DocumentViewMode
{
    Source, Preview, Split
}
internal enum PreviewKind
{
    None, Markdown, Html, Svg
}

internal static class PreviewRenderer
{
    public const long MaximumBytes = 4L * 1024 * 1024;
    static readonly MarkdownPipeline pipeline = new MarkdownPipelineBuilder()
        .UsePipeTables().UseTaskLists().UseAutoLinks().UseEmphasisExtras().UseFootnotes().UseAutoIdentifiers().Build();

    public static PreviewKind Kind(string? path, string language)
    {
        if (language == "markdown")
            return PreviewKind.Markdown;
        if (language == "html")
            return PreviewKind.Html;
        return Path.GetExtension(path ?? "").ToLowerInvariant() switch
        {
            ".md" or ".markdown" or ".mdown" or ".mkd" => PreviewKind.Markdown,
            ".html" or ".htm" or ".xhtml" => PreviewKind.Html,
            ".svg" => PreviewKind.Svg,
            _ => PreviewKind.None
        };
    }

    public static string Render(string text, PreviewKind kind, string title)
    {
        if (kind == PreviewKind.Html)
            return text; // Preserve the author's document, CSS and base URL.
        string body = kind == PreviewKind.Svg
            ? "<div class=\"svg-preview\"><img alt=\"SVG 미리보기\" src=\"data:image/svg+xml;base64," + Convert.ToBase64String(Encoding.UTF8.GetBytes(text)) + "\"></div>"
            : Markdown.ToHtml(text, pipeline);
        return "<!doctype html><html lang=\"ko\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + WebUtility.HtmlEncode(title) + "</title><style>" + Styles + "</style></head><body><main class=\"markdown-body\">" + body + "</main></body></html>";
    }

    const string Styles = """
        :root { color-scheme: light; font: 16px/1.75 'Segoe UI','Malgun Gothic',sans-serif; color: #253342; background: #fff; }
        * { box-sizing: border-box; } body { margin: 0; } .markdown-body { max-width: 940px; margin: auto; padding: 32px 42px 72px; overflow-wrap: anywhere; }
        h1,h2,h3,h4,h5,h6 { line-height: 1.35; font-weight: 650; margin: 1.4em 0 .6em; color: #172b3a; } h1 { font-size: 2em; border-bottom: 1px solid #dce5e8; padding-bottom: .35em; margin-top: 0; }
        h2 { font-size: 1.5em; border-bottom: 1px solid #e4eaed; padding-bottom: .3em; } p,ul,ol { margin: .85em 0; } li+li { margin-top: .2em; }
        a { color: #007f73; text-decoration: none; } a:hover { text-decoration: underline; } img { max-width: 100%; height: auto; }
        pre,code { font-family: Consolas,'D2Coding',monospace; font-size: .9em; } code { background: #f0f4f5; border-radius: 4px; padding: .15em .35em; }
        pre { padding: 18px 20px; background: #f4f7f8; border: 1px solid #e1e8ea; border-radius: 7px; overflow: auto; line-height: 1.6; } pre code { font-size: 1em; padding: 0; background: none; }
        table { display: block; max-width: 100%; overflow-x: auto; border-collapse: collapse; margin: 1.2em 0; } th,td { border: 1px solid #dce4e6; padding: 8px 14px; text-align: left; } th { background: #eef5f3; } tr:nth-child(even) { background: #f9fbfa; }
        blockquote { margin: 1em 0; padding: .1em 1.1em; color: #5c6f7a; border-left: 4px solid #80b8ae; background: #f6faf9; } hr { border: 0; border-top: 1px solid #dce4e6; margin: 2em 0; }
        input[type=checkbox] { accent-color: #008677; margin-right: .45em; } .task-list-item { list-style: none; } .svg-preview { display: grid; place-items: center; min-height: 360px; }
        @media(max-width:600px) { .markdown-body { padding: 22px 24px 50px; } }
        """;
}

internal sealed class DocumentSurface : UserControl
{
    readonly Document document;
    readonly string browserProfile;
    readonly SplitContainer split = new() { Dock = DockStyle.Fill, Size = new Size(1200, 600), SplitterWidth = 5 };
    readonly Panel preview = new() { Dock = DockStyle.Fill, BackColor = Color.White };
    readonly Panel previewContent = new() { Dock = DockStyle.Fill, BackColor = Color.White };
    readonly Label message = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter, Padding = new Padding(30), Text = "미리보기 준비 중…" };
    readonly ToolStrip previewBar = new() { Dock = DockStyle.Top, GripStyle = ToolStripGripStyle.Hidden, Padding = new Padding(5), BackColor = Color.FromArgb(243, 248, 246) };
    readonly ToolStripLabel caption = new("미리보기");
    readonly ToolStripButton scripts = new("HTML 스크립트") { CheckOnClick = true, ToolTipText = "이 탭의 HTML에서 JavaScript 실행" };
    readonly System.Windows.Forms.Timer debounce = new() { Interval = 400 };
    WebView2? browser;
    Task? initialization;
    TaskCompletionSource<bool>? navigation;
    TaskCompletionSource? renderingFinished;
    bool rendering, pending, preserveScroll;
    string context = "", renderedContext = "", mappedFolder = "", currentAddress = "";
    long renderedRevision = -1;
    ulong navigationId;
    string navigationFailure = "";
    int served;
    byte[] content = [];
    readonly string origin;
    public DocumentViewMode Mode
    {
        get; private set;
    }
    public bool CanPreview => PreviewRenderer.Kind(document.Path ?? document.Name, document.Language) != PreviewKind.None;
    public bool BrowserCreated => browser != null;
    public bool PreviewHasFocus => browser?.ContainsFocus == true;
    public string? LastError
    {
        get; private set;
    }
    internal long RenderedRevision => renderedRevision;
    internal void SetScriptsEnabled(bool enabled) => scripts.Checked = enabled;
    public event Action<string>? LocalLinkClicked;

    public DocumentSurface(Document document, string profile)
    {
        this.document = document;
        browserProfile = Path.Combine(profile, "webview2");
        origin = "lp-" + Guid.NewGuid().ToString("N") + ".invalid";
        Dock = DockStyle.Fill;
        split.Panel1.Controls.Add(document.Editor!);
        split.Panel2.Controls.Add(preview);
        preview.Controls.Add(previewContent);
        previewContent.Controls.Add(message);
        previewBar.Items.Add(caption);
        previewBar.Items.Add(new ToolStripButton("새로고침", null, async (_, _) => await RefreshAsync(true)));
        previewBar.Items.Add(new ToolStripSeparator());
        previewBar.Items.Add(scripts);
        preview.Controls.Add(previewBar);
        Controls.Add(split);
        split.Panel2Collapsed = true;
        scripts.CheckedChanged += async (_, _) => { renderedRevision = -1; await RefreshAsync(); };
        debounce.Tick += async (_, _) => { debounce.Stop(); await RefreshAsync(); };
        document.Editor!.Changed += RequestRefresh;
        VisibleChanged += (_, _) => { if (Visible && Mode != DocumentViewMode.Source) RequestRefresh(); else debounce.Stop(); };
        Resize += (_, _) => { if (Mode == DocumentViewMode.Split && split.Width > 400 && split.SplitterDistance < 100) split.SplitterDistance = split.Width / 2; };
    }

    public void UpdateContext()
    {
        var next = $"{document.Path}|{document.Name}|{document.Language}";
        if (next == context)
            return;
        context = next;
        if (Mode != DocumentViewMode.Source)
            RequestRefresh();
    }

    public async Task SetModeAsync(DocumentViewMode mode)
    {
        if (!Enum.IsDefined(mode))
            mode = DocumentViewMode.Source;
        if (mode != DocumentViewMode.Source && !CanPreview)
            throw new InvalidOperationException("미리보기는 Markdown, HTML, SVG 문서에서 사용할 수 있습니다. 새 문서는 언어 메뉴에서 Markdown 또는 HTML을 선택하세요.");
        Mode = mode;
        if (mode == DocumentViewMode.Source)
        {
            debounce.Stop();
            split.Panel1Collapsed = false;
            split.Panel2Collapsed = true;
            document.Editor!.Focus();
            if (browser?.CoreWebView2 != null)
                try
                {
                    await browser.CoreWebView2.TrySuspendAsync();
                }
                catch (InvalidOperationException) { }
            return;
        }
        split.Panel2Collapsed = false;
        split.Panel1Collapsed = mode == DocumentViewMode.Preview;
        if (mode == DocumentViewMode.Split && split.Width > 400)
            split.SplitterDistance = split.Width / 2;
        browser?.CoreWebView2?.Resume();
        await RefreshAsync();
    }

    public void RequestRefresh()
    {
        if (IsDisposed || document.Loading || Mode == DocumentViewMode.Source)
            return;
        pending = true;
        if (Visible)
        {
            debounce.Stop();
            debounce.Start();
        }
    }

    async Task EnsureBrowserAsync()
    {
        if (initialization != null)
        {
            await initialization;
            return;
        }
        initialization = InitializeAsync();
        try
        {
            await initialization;
        }
        catch { initialization = null; browser?.Dispose(); browser = null; throw; }
    }

    async Task InitializeAsync()
    {
        browser = new WebView2 { Dock = DockStyle.Fill, DefaultBackgroundColor = Color.White };
        previewContent.Controls.Add(browser);
        browser.BringToFront();
        var environment = await CoreWebView2Environment.CreateAsync(null, browserProfile);
        if (IsDisposed)
            return;
        await browser.EnsureCoreWebView2Async(environment);
        var core = browser.CoreWebView2;
        core.Settings.AreHostObjectsAllowed = false;
        core.Settings.IsWebMessageEnabled = false;
        core.Settings.AreDefaultScriptDialogsEnabled = false;
        core.Settings.IsStatusBarEnabled = false;
        core.Settings.IsScriptEnabled = false;
        core.PermissionRequested += (_, e) => e.State = CoreWebView2PermissionState.Deny;
        core.DownloadStarting += (_, e) => e.Cancel = true;
        core.NewWindowRequested += (_, e) => { e.Handled = true; if (e.IsUserInitiated) OpenLink(e.Uri); };
        core.AddWebResourceRequestedFilter($"https://{origin}/*", CoreWebView2WebResourceContext.All);
        core.WebResourceRequested += (_, e) =>
        {
            served++;
            var uri = new Uri(e.Request.Uri);
            if (uri.AbsolutePath == "/__lhpad_preview__.html")
                e.Response = environment.CreateWebResourceResponse(new MemoryStream(content, false), 200, "OK", "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store");
            else
            {
                string? path = ResolveLocalPath(uri);
                if (path == null || !File.Exists(path))
                {
                    e.Response = environment.CreateWebResourceResponse(null, 404, "Not Found", "");
                    return;
                }
                try
                {
                    e.Response = environment.CreateWebResourceResponse(new AssetStream(File.OpenRead(path)), 200, "OK", "Content-Type: " + Mime(path) + "\r\nCache-Control: no-store");
                }
                catch (IOException) { e.Response = environment.CreateWebResourceResponse(null, 404, "Not Found", ""); }
                catch (UnauthorizedAccessException) { e.Response = environment.CreateWebResourceResponse(null, 403, "Forbidden", ""); }
            }
        };
        core.NavigationStarting += (_, e) =>
        {
            if (e.Uri.Split('#')[0] == currentAddress)
            {
                navigationId = e.NavigationId;
                return;
            }
            e.Cancel = true;
            if (e.IsUserInitiated)
                OpenLink(e.Uri);
        };
        core.NavigationCompleted += (_, e) => { if (e.NavigationId != navigationId) return; navigationFailure = $"{e.WebErrorStatus} / HTTP {e.HttpStatusCode}"; navigation?.TrySetResult(e.IsSuccess); };
        core.ProcessFailed += (_, _) => { LastError = "미리보기 프로세스가 종료되었습니다. 원문 보기로 전환한 뒤 다시 열어 주세요."; ShowMessage(LastError); };
    }

    void OpenLink(string address)
    {
        if (!Uri.TryCreate(address, UriKind.Absolute, out var uri))
            return;
        if (uri.Host == origin && mappedFolder.Length > 0)
        {
            string? path = ResolveLocalPath(uri);
            if (path != null && File.Exists(path))
                LocalLinkClicked?.Invoke(path);
        }
        else if (uri.Scheme is "http" or "https" or "mailto")
            try
            {
                Process.Start(new ProcessStartInfo(address) { UseShellExecute = true });
            }
            catch (Exception ex) { ShowMessage(ex.Message); }
    }

    string? ResolveLocalPath(Uri uri)
    {
        if (mappedFolder.Length == 0 || uri.Host != origin)
            return null;
        try
        {
            string relative = Uri.UnescapeDataString(uri.AbsolutePath).TrimStart('/').Replace('/', Path.DirectorySeparatorChar);
            if (relative.Contains(':') || relative.IndexOf('\0') >= 0)
                return null;
            string path = Path.GetFullPath(Path.Combine(mappedFolder, relative));
            string root = Path.GetFullPath(mappedFolder).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            return path.StartsWith(root, StringComparison.OrdinalIgnoreCase) ? path : null;
        }
        catch (ArgumentException) { return null; }
    }

    static string Mime(string path) => Path.GetExtension(path).ToLowerInvariant() switch
    {
        ".css" => "text/css",
        ".js" or ".mjs" => "text/javascript",
        ".html" or ".htm" => "text/html",
        ".json" => "application/json",
        ".svg" => "image/svg+xml",
        ".png" => "image/png",
        ".jpg" or ".jpeg" => "image/jpeg",
        ".gif" => "image/gif",
        ".webp" => "image/webp",
        ".avif" => "image/avif",
        ".ico" => "image/x-icon",
        ".woff" => "font/woff",
        ".woff2" => "font/woff2",
        ".ttf" => "font/ttf",
        _ => "application/octet-stream"
    };

    void ShowMessage(string text)
    {
        message.Text = text;
        message.BringToFront();
    }

    public async Task RefreshAsync(bool force = false)
    {
        if (force)
            renderedRevision = -1;
        if (IsDisposed || document.Loading || Mode == DocumentViewMode.Source)
            return;
        if (rendering)
        {
            pending = true;
            if (force && renderingFinished is { } finished)
            {
                await finished.Task;
                await RefreshAsync(true);
            }
            return;
        }
        rendering = true;
        var completion = renderingFinished = new(TaskCreationOptions.RunContinuationsAsynchronously);
        pending = false;
        debounce.Stop();
        try
        {
            var kind = PreviewRenderer.Kind(document.Path ?? document.Name, document.Language);
            scripts.Visible = kind == PreviewKind.Html;
            caption.Text = kind + " 미리보기";
            if (document.Editor!.Length > PreviewRenderer.MaximumBytes)
            {
                ShowMessage("미리보기는 4 MiB 이하 문서에서 지원합니다.\n큰 문서는 원문 보기에서 계속 편집할 수 있습니다.");
                return;
            }
            if (kind == PreviewKind.None)
            {
                ShowMessage("이 문서 형식은 미리보기를 지원하지 않습니다.");
                return;
            }
            if (renderedRevision == document.Revision && renderedContext == context && browser != null)
            {
                browser.BringToFront();
                return;
            }
            long revision = document.Revision;
            string text = document.Editor.TextRange(0, document.Editor.Length);
            string title = document.Name;
            var html = await Task.Run(() => PreviewRenderer.Render(text, kind, title));
            if (IsDisposed || Mode == DocumentViewMode.Source)
                return;
            if (document.Revision != revision)
            {
                pending = true;
                return;
            }
            await EnsureBrowserAsync();
            if (IsDisposed || Mode == DocumentViewMode.Source || browser?.CoreWebView2 == null)
                return;
            var core = browser.CoreWebView2;
            core.Resume();
            double scroll = 0;
            if (preserveScroll)
            {
                try
                {
                    double.TryParse(await core.ExecuteScriptAsync("window.scrollY"), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out scroll);
                }
                catch (InvalidOperationException) { }
            }
            var folder = document.Path == null ? "" : Path.GetDirectoryName(document.Path)!;
            if (mappedFolder != folder)
            {
                mappedFolder = folder;
            }
            core.Settings.IsScriptEnabled = kind == PreviewKind.Html && scripts.Checked;
            content = Encoding.UTF8.GetBytes(html);
            currentAddress = $"https://{origin}/__lhpad_preview__.html?v={Guid.NewGuid():N}";
            navigation = new(TaskCreationOptions.RunContinuationsAsynchronously);
            core.Navigate(currentAddress);
            if (!await navigation.Task.WaitAsync(TimeSpan.FromSeconds(20)))
                throw new IOException($"미리보기 문서를 불러오지 못했습니다: {navigationFailure} (responses={served})");
            if (IsDisposed)
                return;
            browser.BringToFront();
            if (scroll > 0)
                await core.ExecuteScriptAsync($"window.scrollTo(0,{scroll.ToString(System.Globalization.CultureInfo.InvariantCulture)})");
            renderedRevision = revision;
            renderedContext = context;
            preserveScroll = true;
            LastError = null;
        }
        catch (Exception ex)
        {
            if (IsDisposed)
                return;
            LastError = ex is WebView2RuntimeNotFoundException ? "HTML 미리보기에 Microsoft Edge WebView2 Runtime이 필요합니다.\n원문 편집은 계속 사용할 수 있습니다." : ex.Message;
            ShowMessage(LastError);
        }
        finally { rendering = false; completion.TrySetResult(); if (pending && !IsDisposed) RequestRefresh(); }
    }

    internal async Task<string> EvaluateAsync(string script)
    {
        if (browser?.CoreWebView2 == null)
            throw new InvalidOperationException("미리보기가 초기화되지 않았습니다.");
        return await browser.CoreWebView2.ExecuteScriptAsync(script);
    }

    public async Task EditAsync(int message)
    {
        if (browser?.CoreWebView2 == null)
            return;
        var core = browser.CoreWebView2;
        if (message is S.SCI_COPY or S.SCI_CUT)
        {
            var selected = JsonSerializer.Deserialize<string>(await core.ExecuteScriptAsync("(()=>{const e=document.activeElement;return e&&typeof e.selectionStart==='number'?e.value.substring(e.selectionStart,e.selectionEnd):window.getSelection().toString()})()"));
            if (!string.IsNullOrEmpty(selected))
                Clipboard.SetText(selected);
            if (message == S.SCI_CUT)
                await core.ExecuteScriptAsync("document.execCommand('delete')");
        }
        else if (message == S.SCI_SELECTALL)
            await core.ExecuteScriptAsync("document.execCommand('selectAll')");
        else if (message == S.SCI_PASTE && Clipboard.ContainsText())
            await core.ExecuteScriptAsync("document.execCommand('insertText',false," + JsonSerializer.Serialize(Clipboard.GetText()) + ")");
        else if (message == S.SCI_UNDO)
            await core.ExecuteScriptAsync("document.execCommand('undo')");
        else if (message == S.SCI_REDO)
            await core.ExecuteScriptAsync("document.execCommand('redo')");
    }

    internal async Task CaptureAsync(string path)
    {
        if (browser?.CoreWebView2 == null)
            throw new InvalidOperationException("미리보기가 초기화되지 않았습니다.");
        await using var output = File.Create(path);
        await browser.CoreWebView2.CapturePreviewAsync(CoreWebView2CapturePreviewImageFormat.Png, output);
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            debounce.Stop();
            debounce.Dispose();
            document.Editor!.Changed -= RequestRefresh;
            navigation?.TrySetCanceled();
            browser?.Dispose();
        }
        base.Dispose(disposing);
    }
}

internal sealed class AssetStream(Stream source) : Stream
{
    public override bool CanRead => source.CanRead;
    public override bool CanSeek => source.CanSeek;
    public override bool CanWrite => false;
    public override long Length => source.Length;
    public override long Position
    {
        get => source.Position; set => source.Position = value;
    }
    public override int Read(byte[] buffer, int offset, int count)
    {
        try
        {
            int read = source.Read(buffer, offset, count);
            if (read == 0)
                source.Dispose();
            return read;
        }
        catch { source.Dispose(); throw; }
    }
    public override long Seek(long offset, SeekOrigin origin) => source.Seek(offset, origin);
    public override void Flush() => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    protected override void Dispose(bool disposing)
    {
        if (disposing)
            source.Dispose();
        base.Dispose(disposing);
    }
}

using System.Text.Json;

namespace LHPad;

internal static class PreviewTests
{
    static async Task UntilAsync(Func<Task<bool>> predicate, string description)
    {
        var timeout = DateTime.UtcNow.AddSeconds(15);
        while (DateTime.UtcNow < timeout)
        {
            if (await predicate())
                return;
            await Task.Delay(50);
        }
        throw new Exception("Timed out: " + description);
    }
    static async Task<string?> TextAsync(DocumentSurface surface, string expression) =>
        JsonSerializer.Deserialize<string>(await surface.EvaluateAsync(expression));

    public static async Task RunAsync(Form host, string folder, string output, Action<bool, string> check)
    {
        check(PreviewRenderer.Kind("readme.MD", "normal") == PreviewKind.Markdown && PreviewRenderer.Kind("page.htm", "normal") == PreviewKind.Html && PreviewRenderer.Kind("image.svg", "xml") == PreviewKind.Svg, "Preview detects Markdown HTML and SVG extensions");
        check(PreviewRenderer.Kind("huge.log", "normal") == PreviewKind.None, "Plain text stays in lightweight source mode");
        string markdown = "# 미리보기 테스트\n\n문서의 **굵은 글씨**와 [로컬 링크](page.html)를 표시합니다.\n\n| 기능 | 상태 |\n|---|---|\n| 한글 | 정상 |\n\n- [x] 완료\n- [ ] 대기\n\n```html\n<b>코드 그대로</b>\n```\n\n![로컬 이미지](그림%20테스트.svg)\n\n<script>document.body.dataset.injected='yes';</script>\n";
        string svg = "<svg xmlns='http://www.w3.org/2000/svg' width='240' height='80'><rect width='240' height='80' rx='12' fill='#007f73'/><text x='22' y='49' font-size='26' fill='white'>LHPad Preview</text></svg>";
        File.WriteAllText(Path.Combine(folder, "그림 테스트.svg"), svg);
        File.WriteAllText(Path.Combine(folder, "sample.css"), "body { font-family: sans-serif; background: rgb(12,34,56); color: white; padding: 40px; } h1 {color:rgb(80,220,170)}");
        string html = "<!doctype html><html lang='ko'><head><meta charset='utf-8'><link rel='stylesheet' href='sample.css'><title>HTML 테스트</title></head><body><h1>HTML 미리보기</h1><p id='script-state'>initial</p><script>document.getElementById('script-state').textContent='executed';</script><img src='그림%20테스트.svg' alt='로컬 이미지'></body></html>";
        string mdPath = Path.Combine(folder, "preview.md"), htmlPath = Path.Combine(folder, "page.html");
        File.WriteAllText(mdPath, markdown);
        File.WriteAllText(htmlPath, html);
        string profile = Path.Combine(folder, "preview-profile");
        using (var app = new MainForm(profile, []))
        {
            app.Show(host);
            await app.Ready;
            await app.OpenAsync(mdPath);
            var md = app.Current!;
            check(md.Language == "markdown", "Markdown extension selects the Markdown syntax lexer");
            check(!md.Surface!.BrowserCreated && md.Surface.Mode == DocumentViewMode.Source, "Opening Markdown does not start WebView2");
            await app.ShowDocumentViewAsync(DocumentViewMode.Split);
            check(md.Surface.LastError == null, "WebView2 Markdown render: " + md.Surface.LastError);
            check(await TextAsync(md.Surface, "document.querySelector('h1')?.textContent") == "미리보기 테스트", "Markdown heading is rendered rather than shown as source");
            check(await TextAsync(md.Surface, "document.querySelector('strong')?.textContent") == "굵은 글씨", "Markdown emphasis renders");
            check(await md.Surface.EvaluateAsync("document.querySelectorAll('table tbody tr').length") == "1", "Markdown pipe table renders");
            check(await md.Surface.EvaluateAsync("document.querySelectorAll('input[type=checkbox]').length") == "2", "Markdown task list renders");
            check((await TextAsync(md.Surface, "document.querySelector('pre code')?.textContent"))?.Contains("<b>코드 그대로</b>") == true, "Markdown fenced HTML remains literal code");
            check(await md.Surface.EvaluateAsync("document.querySelector('img').naturalWidth") == "240", "Markdown relative image with Korean name and space resolves");
            check(await md.Surface.EvaluateAsync("document.body.dataset.injected===undefined") == "true", "Preview does not automatically execute document JavaScript");
            await md.Surface.CaptureAsync(Path.Combine(output, "markdown-preview.png"));
            SelfTests.Screenshot(app, Path.Combine(output, "markdown-split-preview.png"));
            md.Editor!.SetText(markdown.Replace("미리보기 테스트", "저장 전 실시간 변경"));
            await UntilAsync(async () => md.Surface.RenderedRevision == md.Revision, "live preview update");
            check(await TextAsync(md.Surface, "document.querySelector('h1')?.textContent") == "저장 전 실시간 변경", "Debounced preview reflects unsaved edits");
            check(File.ReadAllText(mdPath) == markdown, "Live preview never saves or changes the source file");
            await app.ShowDocumentViewAsync(DocumentViewMode.Preview);
            check(md.Surface.Mode == DocumentViewMode.Preview, "Full preview mode switches without changing source");
            await app.ShowDocumentViewAsync(DocumentViewMode.Source);
            md.Editor.SetText(new string('x', (int)PreviewRenderer.MaximumBytes + 1));
            await app.ShowDocumentViewAsync(DocumentViewMode.Preview);
            check(md.Surface.Mode == DocumentViewMode.Preview && md.Surface.RenderedRevision != md.Revision, "Large document preview is bounded without disabling source editing");
            md.Editor.SetText(markdown.Replace("미리보기 테스트", "저장 전 실시간 변경"));
            await md.Surface.RefreshAsync(true);
            await app.ShowDocumentViewAsync(DocumentViewMode.Split);
            await app.OpenAsync(htmlPath);
            var page = app.Current!;
            await app.ShowDocumentViewAsync(DocumentViewMode.Preview);
            check(page.Surface!.LastError == null, "WebView2 HTML render: " + page.Surface.LastError);
            check(await TextAsync(page.Surface, "document.querySelector('h1')?.textContent") == "HTML 미리보기", "HTML document body renders");
            check(await TextAsync(page.Surface, "getComputedStyle(document.body).backgroundColor") == "rgb(12, 34, 56)", "HTML relative external stylesheet resolves");
            check(await page.Surface.EvaluateAsync("document.querySelector('img').naturalWidth") == "240", "HTML relative image resolves");
            check(await TextAsync(page.Surface, "document.querySelector('#script-state').textContent") == "initial", "HTML JavaScript starts disabled");
            page.Surface.SetScriptsEnabled(true);
            await UntilAsync(async () => await TextAsync(page.Surface, "document.querySelector('#script-state')?.textContent") == "executed", "opt-in HTML script");
            check(true, "HTML JavaScript can be enabled per tab");
            File.WriteAllText(Path.Combine(folder, "sample.css"), "body { background: rgb(34,56,78); color:white; padding:40px; }");
            await page.Surface.RefreshAsync(true);
            check(await TextAsync(page.Surface, "getComputedStyle(document.body).backgroundColor") == "rgb(34, 56, 78)", "Explicit refresh reloads assets with unchanged document text");
            await page.Surface.CaptureAsync(Path.Combine(output, "html-preview.png"));
            await app.OpenAsync(mdPath);
            await app.ShowDocumentViewAsync(DocumentViewMode.Split);
            var closed = new TaskCompletionSource();
            app.FormClosed += (_, _) => closed.TrySetResult();
            app.Close();
            await closed.Task;
        }
        using (var app = new MainForm(profile, []))
        {
            app.Show(host);
            await app.Ready;
            var md = app.Documents.First(d => d.Path == mdPath);
            var htmlDoc = app.Documents.First(d => d.Path == htmlPath);
            check(md.Surface!.Mode == DocumentViewMode.Split && htmlDoc.Surface!.Mode == DocumentViewMode.Preview, "Restart restores each tab's source/preview/split mode");
            check(md.Editor!.AllText().Contains("저장 전 실시간 변경") && md.Dirty, "Preview mode restart preserves unsaved Markdown source");
            await UntilAsync(async () => md.Surface.RenderedRevision == md.Revision, "restored preview");
            check(await TextAsync(md.Surface, "document.querySelector('h1')?.textContent") == "저장 전 실시간 변경", "Restored preview renders the recovered snapshot");
            await app.OpenAsync(Path.Combine(folder, "그림 테스트.svg"));
            await app.ShowDocumentViewAsync(DocumentViewMode.Preview);
            check(await app.Current!.Surface!.EvaluateAsync("document.querySelector('img').naturalWidth") == "240", "SVG document preview renders as an image");
            var closed = new TaskCompletionSource();
            app.FormClosed += (_, _) => closed.TrySetResult();
            app.Close();
            await closed.Task;
        }
    }
}

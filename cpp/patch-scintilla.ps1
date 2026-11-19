param([Parameter(Mandatory)][string]$SourceRoot)
$ErrorActionPreference = 'Stop'
$taskFile = Join-Path $SourceRoot 'scintilla/win32/ScintillaWin.cxx'
$taskSource = [IO.File]::ReadAllText($taskFile).Replace("`r`n","`n")
if (!$taskSource.Contains('LitePad native file drop')) {
    $taskSource = $taskSource.Replace('#include <ole2.h>',"#include <ole2.h>`n#include <shellapi.h>")
    $taskSource = $taskSource.Replace('bool hasOKText;',"bool hasOKText;`n`tbool hasOKFiles = false; // LitePad native file drop")
    $taskSource = $taskSource.Replace('hasOKText = (hrHasUText == S_OK);',@'
hasOKText = (hrHasUText == S_OK);
    FORMATETC files = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    hasOKFiles = pIDataSource->QueryGetData(&files) == S_OK;
    if (hasOKFiles) {
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }
'@)
    $taskSource = $taskSource.Replace('if (!dragDropEnabled || !hasOKText || pdoc->IsReadOnly()) {',@'
if (hasOKFiles) {
            *pdwEffect = DROPEFFECT_COPY;
            return S_OK;
        }
        if (!dragDropEnabled || !hasOKText || pdoc->IsReadOnly()) {
'@)
    $taskAnchor = "`t`tstd::string putf;`n`t`tFORMATETC fmtu"
    if (!$taskSource.Contains($taskAnchor)) { throw 'Scintilla source changed: file-drop patch anchor missing.' }
    $taskSource = $taskSource.Replace($taskAnchor,@'
        // File drops notify the container; native text drag/move stays in Scintilla.
        FORMATETC files = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM fileMedium{};
        if (SUCCEEDED(pIDataSource->GetData(&files, &fileMedium))) {
            std::string paths;
            const HDROP drop = static_cast<HDROP>(fileMedium.hGlobal);
            const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; i++) {
                const UINT size = ::DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(size + 1, L'\0');
                ::DragQueryFileW(drop, i, path.data(), size + 1);
                path.resize(size);
                paths += EncodeWString(path);
                paths += '\n';
            }
            ::ReleaseStgMedium(&fileMedium);
            NotificationData notification{};
            notification.nmhdr.code = Notification::URIDropped;
            notification.text = paths.c_str();
            NotifyParent(notification);
            *pdwEffect = DROPEFFECT_COPY;
            return S_OK;
        }
        std::string putf;
        FORMATETC fmtu
'@)
    [IO.File]::WriteAllText($taskFile,$taskSource,[Text.UTF8Encoding]::new($false))
    $taskMake = Join-Path $SourceRoot 'scintilla/win32/scintilla.mak'
    $taskMakeText = [IO.File]::ReadAllText($taskMake).Replace('ADVAPI32.lib','ADVAPI32.lib SHELL32.lib')
    [IO.File]::WriteAllText($taskMake,$taskMakeText,[Text.UTF8Encoding]::new($false))
}

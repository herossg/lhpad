LHPad 1.5.0 — C++ native, Windows x64

Explorer menu presentation now checks only selection count, without deferred state,
item attributes or path lookup. File validation runs when the command is invoked.
The menu icon comes from the already loaded shell DLL.



View > Text editor font selects a font family, size (6-72 pt), and extra line spacing

(0-24 logical pixels) for all text tabs. The picker includes a two-line preview.

The selection is saved across restarts. Syntax colours and document contents are retained.



Compact toolbar and document tabs, red active fold guides, curved undo/redo arrows,

and visible separators on both sides of the macro toolbar group.



Run LHPad.exe after extracting the entire ZIP into one folder.

.NET and the Visual C++ Redistributable do not need separate installation.

Scintilla.dll, Lexilla.dll, WebView2Loader.dll and the data folder must stay beside LHPad.exe.

Microsoft Edge WebView2 Runtime is used only for Markdown/HTML/SVG previews.



Drag files onto the editor, tabs or Hex view to open them. Multiple files are supported.

Tools > Register 'Open with LHPad' adds a per-user Explorer context menu.

Run Register-LHPad.cmd first. Windows 11 shows 'LHPad로 열기' directly in the first menu.

Trusting the bundled LHZip development certificate may require administrator approval.

All shipped EXE/DLL files and the MSIX carry the same LHZip development signature.

EXE/DLL signatures include RFC3161 timestamps; the private key is never distributed.

The context menu reuses an existing LHPad instance.



Command line:

  LHPad.exe "C:\path with spaces\file.txt"

  LHPad.exe --register-shell

  LHPad.exe --unregister-shell

  LHPad.exe --profile "C:\my-profile"

  LHPad.exe --self-test



The standard profile is %LOCALAPPDATA%\LitePad, compatible with the C# 0.5.2 session and macro JSON format.

Close the older C# instance before starting this version. Unsaved tabs are kept in recovery snapshots on exit.



Build: PowerShell -File cpp/build.ps1 -RebuildNative -Test

Toolchain: Visual Studio C++ desktop tools + Windows SDK. C++20, static MSVC runtime (/MT).

License: GPL-3.0-or-later. See LICENSE and THIRD-PARTY-NOTICES.txt.


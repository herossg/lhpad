param(
    [string]$Executable,
    [switch]$Remove,
    [switch]$Quiet
)
$ErrorActionPreference = 'Stop'

# Invoke the native LHPad registration from the existing desktop Explorer.
# A development runner can expose an isolated registry view even when a child
# reports APPMODEL_ERROR_NO_PACKAGE; registry readback in that runner is not proof
# that the user's Explorer sees the same registration.
$taskRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $taskPortable = Join-Path $taskRoot 'LHPad.exe'
    if (Test-Path -LiteralPath $taskPortable -PathType Leaf) {
        $Executable = $taskPortable
    } else {
        $Executable = Join-Path $taskRoot 'artifacts\native-1.1.2\LHPad.exe'
    }
}
$taskResolved = (Resolve-Path -LiteralPath $Executable).ProviderPath
if ([IO.Path]::GetFileName($taskResolved) -ine 'LHPad.exe') {
    throw 'Specify the LHPad.exe from the intended LHPad release directory.'
}
$taskDirectory = Split-Path -Parent $taskResolved
foreach ($taskDependency in @('LHPadShell.dll', 'Scintilla.dll','Lexilla.dll','WebView2Loader.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $taskDirectory $taskDependency) -PathType Leaf)) {
        throw "Missing dependency beside LHPad.exe: $taskDependency"
    }
}
$taskOption = if ($Remove) { '--unregister-shell' } else { '--register-shell' }
if ($Quiet) { $taskOption += ' --quiet' }
$taskShell = New-Object -ComObject Shell.Application
$taskWindows = $taskShell.Windows()
$taskDispatched = $false
for ($taskIndex = 0; $taskIndex -lt $taskWindows.Count; $taskIndex++) {
    try {
        # .Application directly on the browser is not the shell dispatcher.
        # The folder view's Application is the Explorer-hosted shell proxy.
        $taskApplication = $taskWindows.Item($taskIndex).Document.Application
        if ($null -eq $taskApplication) { continue }
        $taskApplication.ShellExecute($taskResolved, $taskOption, $taskDirectory, 'open', 0)
        $taskDispatched = $true
        break
    } catch {
        # ShellWindows can contain objects without the folder-view interface.
    }
}
if (!$taskDispatched) {
    throw 'Open one folder in File Explorer, then run this script again.'
}
Write-Output "Requested $taskOption through desktop Explorer: $taskResolved"
Write-Output 'Windows 11 uses one packaged menu; Windows 10/Server uses one classic menu. The inactive registration is removed.'

# Removes only the explicitly listed obsolete LHPad build artifacts.
# Run from PowerShell: & ./tools/cleanup-old-artifacts.ps1
$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path $PSScriptRoot -Parent
$taskArtifacts = (Resolve-Path -LiteralPath (Join-Path $taskProjectRoot 'artifacts')).Path
$taskCurrentExecutable = Join-Path $taskArtifacts 'native-1.1.0/LHPad.exe'
if (!(Test-Path -LiteralPath $taskCurrentExecutable -PathType Leaf)) {
    throw 'The current native-1.1.0 executable must exist before cleanup.'
}
$taskOldNames = @(
    'native-1.0.0',
    'native-1.0.1',
    'native-1.0.2',
    'native-1.0.3',
    'native-1.0.4',
    'portable-0.5.2',
    'cmake',
    'cmake-verification-results-fcb60f470f164a99b354b7a9b9ba140a',
    'native-package-check-fe456b5286a9422da9dca7d5727c5e26',
    'native-verification-results-6e7be07ce1524ac1b8f18f13bc5f2a75',
    'nested-data-backup-4f5b5253d72543e6837e656d02044516',
    'tab-close-package-06f89362f5d54d1cbec2ea15de716587',
    'tab-close-verification-ec80c93841e1428e9792bd874dc20908',
    'tab-flicker-package-check-6036178e5fd5411fb649c9d319c447e1',
    'tab-flicker-repro',
    'toolbar-export',
    'ui-1.0.3-profile',
    'ui-font-profile',
    'ui-spacing-profile',
    'LitePad-csharp-0.5.2-windows-x64.zip',
    'cmake-build.log',
    'cmake-configure.log',
    'native-build.log',
    'native-package.log',
    'tab-close-build.log',
    'tab-close-package-build.log',
    'tab-flicker-fix-build.log',
    'tab-flicker-fix-package.log',
    'tab-flicker-repro-build.log'
)
# Validate every absolute target before deleting any of them.
$taskTargets = foreach ($taskName in $taskOldNames) {
    $taskPath = [IO.Path]::GetFullPath((Join-Path $taskArtifacts $taskName))
    if ([IO.Path]::GetDirectoryName($taskPath) -ne $taskArtifacts) {
        throw "Target is outside the artifacts directory: $taskPath"
    }
    if (Test-Path -LiteralPath $taskPath) {
        $taskEntry = Get-Item -LiteralPath $taskPath -Force
        if ($taskEntry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to delete a reparse point: $taskPath"
        }
        $taskEntry
    }
}
foreach ($taskEntry in $taskTargets) {
    Remove-Item -LiteralPath $taskEntry.FullName -Recurse -Force
    Write-Host "Removed: $($taskEntry.Name)"
}
Write-Host 'Kept: native-1.1.0, native-obj, current release ZIPs and all source/profile files.'

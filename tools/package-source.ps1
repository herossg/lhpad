$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$projectRoot = Split-Path $PSScriptRoot -Parent
$sourceArchive = Join-Path $projectRoot 'artifacts/LHPad-source.zip'
$items = @('LHPad.csproj','app.manifest','build.ps1','build-csharp.ps1','Register-LHPad.cmd','packages.lock.json','README.md','LICENSE','THIRD-PARTY-NOTICES.txt','.editorconfig','.gitignore','vendor/notepad-plus-plus-src.zip','native/Scintilla.dll','native/Lexilla.dll','native/build-native.ps1') | ForEach-Object { Get-Item -LiteralPath (Join-Path $projectRoot $_) }
$items += @('src','cpp','data','docs','tools') | ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $projectRoot $_) -File -Recurse }
$output = [System.IO.File]::Open($sourceArchive,[System.IO.FileMode]::Create)
$zip = [System.IO.Compression.ZipArchive]::new($output,[System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($item in $items) {
        $relative = $item.FullName.Substring($projectRoot.Length).TrimStart([char[]]'\/').Replace('\','/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$item.FullName,$relative,[System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $zip.Dispose(); $output.Dispose() }
Write-Host 'Source package: artifacts/LHPad-source.zip'

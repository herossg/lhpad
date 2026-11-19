param([switch]$Test,[switch]$RebuildNative,[switch]$CSharp,[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$taskBuild=if($CSharp){Join-Path $PSScriptRoot 'build-csharp.ps1'}else{Join-Path $PSScriptRoot 'cpp/build.ps1'}
$taskArguments=@{Test=$Test;RebuildNative=$RebuildNative}
if($OutputDirectory){$taskArguments.OutputDirectory=$OutputDirectory}
& $taskBuild @taskArguments
& (Join-Path $PSScriptRoot 'tools/package-source.ps1')
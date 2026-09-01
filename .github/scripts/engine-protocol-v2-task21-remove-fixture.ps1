$ErrorActionPreference = 'Stop'
$installPath = Join-Path (Get-Location) 'build_x64/install'
if (-not (Test-Path -LiteralPath $installPath -PathType Container)) { exit 0 }
$installRoot = (Resolve-Path -LiteralPath $installPath).Path
$installed = @(Get-ChildItem -LiteralPath $installRoot -Filter 'task21-audio-source.dll' -File -Recurse)
if ($installed.Count -ne 1) { throw "Expected one explicitly staged Task 21 fixture, found $($installed.Count)." }
Remove-Item -LiteralPath $installed[0].FullName -Force
$remaining = @(Get-ChildItem -LiteralPath $installRoot -Filter 'task21-audio-source.dll' -File -Recurse)
if ($remaining.Count -ne 0) { throw 'Task 21 audio fixture remained after cleanup.' }

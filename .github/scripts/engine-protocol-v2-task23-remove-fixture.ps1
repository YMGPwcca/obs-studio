param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) { exit 0 }
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
Get-ChildItem -LiteralPath $root -Filter 'task23-encoder.dll' -File -Recurse | Remove-Item -Force
if (@(Get-ChildItem -LiteralPath $root -Filter 'task23-encoder.dll' -File -Recurse).Count -ne 0) {
    throw 'Task 23 encoder fixture remained after cleanup.'
}
Get-ChildItem -LiteralPath $root -Filter 'obs-engine-task23-encoder-bridge-test.exe' -File -Recurse | Remove-Item -Force
if (@(Get-ChildItem -LiteralPath $root -Filter 'obs-engine-task23-encoder-bridge-test.exe' -File -Recurse).Count -ne 0) {
    throw 'Task 23 bridge test executable remained after cleanup.'
}

param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) { exit 0 }
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
Get-ChildItem -LiteralPath $root -Filter 'task24-encoder-source.dll' -File -Recurse | Remove-Item -Force
if (@(Get-ChildItem -LiteralPath $root -Filter 'task24-encoder-source.dll' -File -Recurse).Count -ne 0) {
    throw 'Task 24 CI-only encoder fixture remained after cleanup.'
}

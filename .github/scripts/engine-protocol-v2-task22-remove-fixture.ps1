param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) { exit 0 }
$installed = @(Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $InstallRoot).Path -Filter 'task22-hotkey-source.dll' -File -Recurse)
foreach ($file in $installed) { Remove-Item -LiteralPath $file.FullName -Force }
if (@(Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $InstallRoot).Path -Filter 'task22-hotkey-source.dll' -File -Recurse).Count -ne 0) {
    throw 'Task 22 hotkey fixture remained after cleanup.'
}

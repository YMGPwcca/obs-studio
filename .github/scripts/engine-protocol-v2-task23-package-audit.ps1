param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
$forbidden = @(Get-ChildItem -LiteralPath $root -Filter 'task23-encoder.dll' -File -Recurse)
if ($forbidden.Count -ne 0) { throw 'Task 23 CI-only encoder fixture leaked into the package.' }
if (@(Get-ChildItem -LiteralPath $root -Filter 'obs-engine-task23-encoder-bridge-test.exe' -File -Recurse).Count -ne 0) {
    throw 'Task 23 bridge test executable leaked into the package.'
}
if (@(Get-ChildItem -LiteralPath $root -Filter 'obs-frontend.exe' -File -Recurse).Count -ne 0) {
    throw 'OBS frontend executable leaked into the headless engine package.'
}
Write-Output 'Task 23 package audit: PASS'

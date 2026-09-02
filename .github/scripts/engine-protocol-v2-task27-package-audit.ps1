param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
foreach ($name in @('task23-encoder.dll', 'task27-recording.dll')) {
    if (@(Get-ChildItem -LiteralPath $root -Filter $name -File -Recurse).Count -ne 0) { throw "Task 27 CI-only fixture leaked: $name" }
}
if (@(Get-ChildItem -LiteralPath $root -Filter 'obs-browser.dll' -File -Recurse).Count -ne 0) { throw 'OBS browser frontend artifact leaked into the package.' }
Write-Output 'Task 27 package audit: PASS'

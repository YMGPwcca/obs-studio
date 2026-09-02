param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
foreach ($name in @('task23-encoder.dll', 'task25-service.dll', 'task26-output.dll', 'task27-recording.dll', 'task29-replay.dll')) {
    if (@(Get-ChildItem -LiteralPath $root -Filter $name -File -Recurse).Count -ne 0) { throw "CI-only fixture leaked: $name" }
}
if (@(Get-ChildItem -LiteralPath $root -Filter 'obs-browser.dll' -File -Recurse).Count -ne 0) { throw 'OBS browser frontend artifact leaked into the package.' }
$virtualModule = Get-ChildItem -LiteralPath $root -Filter 'obs-virtualcam-module64.dll' -File -Recurse | Select-Object -First 1
if ($null -eq $virtualModule) { throw 'packaged x64 Virtual Camera COM module is missing.' }
Write-Output 'Task 30 package audit: PASS'

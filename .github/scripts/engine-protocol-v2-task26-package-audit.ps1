param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
$leaked = @(Get-ChildItem -LiteralPath $root -Filter 'task2*-*.dll' -File -Recurse)
if ($leaked.Count -ne 0) { throw "Task 26 CI-only fixture leaked into the package: $($leaked.FullName -join ', ')" }
if (@(Get-ChildItem -LiteralPath $root -Filter 'obs-browser.dll' -File -Recurse).Count -ne 0) { throw 'OBS browser frontend artifact leaked into the package.' }
Write-Output 'Task 26 package audit: PASS'

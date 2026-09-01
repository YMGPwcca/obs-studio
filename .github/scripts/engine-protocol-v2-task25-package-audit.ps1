param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
if (@(Get-ChildItem -LiteralPath $root -Filter 'task25-service.dll' -File -Recurse).Count -ne 0) {
    throw 'Task 25 CI-only service fixture leaked into the package.'
}
Write-Output 'Task 25 package audit: PASS'

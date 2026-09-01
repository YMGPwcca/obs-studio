param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
if (@(Get-ChildItem -LiteralPath $root -Filter 'task24-encoder-source.dll' -File -Recurse).Count -ne 0) {
    throw 'Task 24 CI-only encoder fixture leaked into the package.'
}
Write-Output 'Task 24 package audit: PASS'

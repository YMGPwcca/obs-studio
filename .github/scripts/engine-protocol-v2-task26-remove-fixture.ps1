param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
foreach ($name in @('task23-encoder.dll', 'task25-service.dll', 'task26-output.dll')) {
    Get-ChildItem -LiteralPath $root -Filter $name -File -Recurse | Remove-Item -Force
}

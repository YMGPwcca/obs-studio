param(
    [Parameter(Mandatory = $true)] [string] $InstallRoot,
    [Parameter(Mandatory = $true)] [string] $FixtureBinary
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
$fixture = (Resolve-Path -LiteralPath $FixtureBinary).Path
$module = Get-ChildItem -LiteralPath $root -Filter 'image-source.dll' -File -Recurse | Select-Object -First 1
if ($null -eq $module) { throw 'Installed OBS module directory was not found.' }
Copy-Item -LiteralPath $fixture -Destination (Join-Path $module.Directory.FullName 'task25-service.dll') -Force

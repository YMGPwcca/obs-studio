param(
    [Parameter(Mandatory = $true)] [string] $InstallRoot,
    [Parameter(Mandatory = $true)] [string] $FixtureDirectory
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
$fixtureRoot = (Resolve-Path -LiteralPath $FixtureDirectory).Path
$module = Get-ChildItem -LiteralPath $root -Filter 'image-source.dll' -File -Recurse | Select-Object -First 1
if ($null -eq $module) { throw 'Installed OBS module directory was not found.' }
foreach ($name in @('task23-encoder', 'task27-recording')) {
    $fixture = Join-Path $fixtureRoot "$name.dll"
    if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) { throw "Missing fixture $fixture." }
    Copy-Item -LiteralPath $fixture -Destination (Join-Path $module.Directory.FullName "$name.dll") -Force
}

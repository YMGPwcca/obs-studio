param(
    [Parameter(Mandatory = $true)] [string] $InstallRoot,
    [Parameter(Mandatory = $true)] [string] $FixtureBinary
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $InstallRoot).Path
$fixture = (Resolve-Path -LiteralPath $FixtureBinary).Path
$module = Get-ChildItem -LiteralPath $root -Filter 'image-source.dll' -File -Recurse | Select-Object -First 1
if ($null -eq $module) { throw 'Installed OBS module directory was not found.' }
Copy-Item -LiteralPath $fixture -Destination (Join-Path $module.Directory.FullName 'task23-encoder.dll') -Force
$engine = Get-ChildItem -LiteralPath $root -Filter 'obs-engine.exe' -File -Recurse | Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
if ($null -eq $engine) { throw 'Installed engine binary was not found.' }
$bridge = Join-Path $engine.Directory.FullName 'obs-engine-task23-encoder-bridge-test.exe'
$bridgeSource = Get-ChildItem -LiteralPath (Split-Path -Parent $FixtureBinary) -Filter 'obs-engine-task23-encoder-bridge-test.exe' -File | Select-Object -First 1
if ($null -eq $bridgeSource) { throw 'Task 23 bridge test executable was not built.' }
Copy-Item -LiteralPath $bridgeSource.FullName -Destination $bridge -Force

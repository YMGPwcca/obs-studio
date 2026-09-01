$ErrorActionPreference = 'Stop'
$installRoot = (Resolve-Path -LiteralPath 'build_x64/install').Path
$plugin = Get-ChildItem -LiteralPath 'build_x64' -Filter 'task21-audio-source.dll' -File -Recurse |
    Where-Object { $_.FullName -notlike "$installRoot*" } | Select-Object -First 1
if ($null -eq $plugin) { throw 'Built Task 21 audio source was not found.' }
$module = Get-ChildItem -LiteralPath $installRoot -Filter 'image-source.dll' -File -Recurse | Select-Object -First 1
if ($null -eq $module) { throw 'Installed OBS module directory was not found.' }
Copy-Item -LiteralPath $plugin.FullName -Destination (Join-Path $module.Directory.FullName $plugin.Name) -Force

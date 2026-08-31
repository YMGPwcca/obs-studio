$ErrorActionPreference = 'Stop'

function Invoke-Task10FixtureStage {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$Plugin = Get-ChildItem -Path 'build_x64' -Filter 'task10-media-source.dll' -File -Recurse |
  Where-Object { $_.FullName -notlike "$InstallRoot*" } |
  Select-Object -First 1
if ($null -eq $Plugin) {
  throw 'Built Task 10 media source was not found.'
}
$RuntimeModule = Get-ChildItem -Path $InstallRoot -Filter 'image-source.dll' -File -Recurse |
  Select-Object -First 1
if ($null -eq $RuntimeModule) {
  throw 'Could not locate the installed OBS module directory.'
}
Copy-Item -LiteralPath $Plugin.FullName -Destination (Join-Path $RuntimeModule.Directory.FullName $Plugin.Name) -Force

}

Invoke-Task10FixtureStage

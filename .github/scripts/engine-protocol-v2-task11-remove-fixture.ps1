$ErrorActionPreference = 'Stop'

function Invoke-Task11FixtureRemove {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$Installed = Get-ChildItem -Path $InstallRoot -Filter 'task11-filter-source.dll' -File -Recurse
foreach ($File in @($Installed)) {
  Remove-Item -LiteralPath $File.FullName -Force
}
if (@(Get-ChildItem -Path $InstallRoot -Filter 'task11-filter-source.dll' -File -Recurse).Count -ne 0) {
  throw 'Task 11 CI-only filter module remained after cleanup.'
}

}

Invoke-Task11FixtureRemove

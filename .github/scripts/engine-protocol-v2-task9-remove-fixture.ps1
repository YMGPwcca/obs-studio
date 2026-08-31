$ErrorActionPreference = 'Stop'

function Invoke-Task9FixtureRemove {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$Installed = Get-ChildItem -Path $InstallRoot -Filter 'task9-interaction-source.dll' -File -Recurse
if (@($Installed).Count -ne 1) {
  throw 'Expected exactly one explicitly staged Task 9 interaction module.'
}
Remove-Item -LiteralPath $Installed[0].FullName -Force
$Remaining = Get-ChildItem -Path $InstallRoot -Filter 'task9-interaction-source.dll' -File -Recurse
if (@($Remaining).Count -ne 0) {
  throw 'Task 9 CI-only interaction module remained in the install tree after cleanup.'
}

}

Invoke-Task9FixtureRemove

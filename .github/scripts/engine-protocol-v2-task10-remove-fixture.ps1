$ErrorActionPreference = 'Stop'

function Invoke-Task10FixtureRemove {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$Installed = Get-ChildItem -Path $InstallRoot -Filter 'task10-media-source.dll' -File -Recurse
if (@($Installed).Count -ne 1) {
  throw 'Expected exactly one explicitly staged Task 10 media module.'
}
Remove-Item -LiteralPath $Installed[0].FullName -Force
$Remaining = Get-ChildItem -Path $InstallRoot -Filter 'task10-media-source.dll' -File -Recurse
if (@($Remaining).Count -ne 0) {
  throw 'Task 10 CI-only media module remained in the install tree after cleanup.'
}

}

Invoke-Task10FixtureRemove

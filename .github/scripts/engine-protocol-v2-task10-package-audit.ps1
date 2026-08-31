$ErrorActionPreference = 'Stop'

function Invoke-Task10PackageAudit {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$Forbidden = Get-ChildItem -Path $InstallRoot -File -Recurse | Where-Object {
  $_.Name -in @(
    'task10-media-source.dll',
    'obs64.exe',
    'obs32.exe',
    'obs-websocket.dll',
    'obs-browser.dll'
  )
}
if (@($Forbidden).Count -ne 0) {
  throw "Forbidden or CI-only files leaked into the normal engine package:`n$($Forbidden.FullName -join "`n")"
}

}

Invoke-Task10PackageAudit

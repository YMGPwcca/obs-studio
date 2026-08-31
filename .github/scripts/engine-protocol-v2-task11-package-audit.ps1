$ErrorActionPreference = 'Stop'

function Invoke-Task11PackageAudit {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$ForbiddenNames = @(
  'task8-concurrency-source.dll',
  'task9-interaction-source.dll',
  'task10-media-source.dll',
  'task11-filter-source.dll',
  'obs64.exe',
  'obs32.exe',
  'obs-websocket.dll',
  'obs-browser.dll'
)
$Forbidden = Get-ChildItem -Path $InstallRoot -File -Recurse | Where-Object { $_.Name -in $ForbiddenNames }
if (@($Forbidden).Count -ne 0) {
  throw "Forbidden or CI-only files leaked into the normal engine package:`n$($Forbidden.FullName -join "`n")"
}

}

Invoke-Task11PackageAudit

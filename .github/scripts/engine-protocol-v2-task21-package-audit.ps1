$ErrorActionPreference = 'Stop'
$installRoot = (Resolve-Path -LiteralPath 'build_x64/install').Path
$forbidden = @(Get-ChildItem -LiteralPath $installRoot -File -Recurse | Where-Object {
    $_.Name -in @('task21-audio-source.dll', 'obs64.exe', 'obs32.exe', 'obs-websocket.dll', 'obs-browser.dll')
})
if ($forbidden.Count -ne 0) { throw "Forbidden files leaked into the normal engine package:`n$($forbidden.FullName -join "`n")" }
$engines = @(Get-ChildItem -LiteralPath $installRoot -Filter 'obs-engine.exe' -File -Recurse)
if ($engines.Count -ne 1) { throw "Expected one obs-engine.exe, found $($engines.Count)." }

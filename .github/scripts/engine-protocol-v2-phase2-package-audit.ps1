param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'

function Invoke-Phase2PackageAudit {
    $root = (Resolve-Path -LiteralPath $InstallRoot).Path
    $engines = @(Get-ChildItem -LiteralPath $root -Filter 'obs-engine.exe' -File -Recurse)
    if ($engines.Count -ne 1) {
        throw "Expected exactly one obs-engine.exe in the normal package, found $($engines.Count)."
    }
    $forbidden = @(
        'obs.exe', 'obs64.exe', 'obs32.exe', 'obs-browser.dll', 'obs-websocket.dll',
        'obs-engine-events-test.exe', 'obs-engine-properties-test.exe', 'obs-engine-preview-consumer-test.exe',
        'task8-concurrency-source.dll', 'task9-interaction-source.dll', 'task10-media-source.dll',
        'task11-filter-source.dll'
    )
    foreach ($name in $forbidden) {
        if (@(Get-ChildItem -LiteralPath $root -Filter $name -File -Recurse).Count -ne 0) {
            throw "Forbidden or CI-only file leaked into the normal engine package: $name"
        }
    }
    $websocket = @(Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object { $_.Name -match 'websocket' })
    if ($websocket.Count -ne 0) {
        throw "OBS WebSocket artifacts leaked into the normal engine package: $($websocket[0].FullName)"
    }
    Write-Output "Phase 2 normal package audit: PASS ($($engines[0].FullName))"
}

Invoke-Phase2PackageAudit

param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $InstallRoot).Path
$engines = @(Get-ChildItem -LiteralPath $root -Filter 'obs-engine.exe' -File -Recurse)
if ($engines.Count -ne 1) { throw "Expected one obs-engine.exe, found $($engines.Count)." }
$forbidden = @('task21-audio-source.dll', 'task22-hotkey-source.dll', 'obs64.exe', 'obs32.exe', 'obs-websocket.dll', 'obs-browser.dll')
foreach ($name in $forbidden) {
    if (@(Get-ChildItem -LiteralPath $root -Filter $name -File -Recurse).Count -ne 0) {
        throw "Forbidden or CI-only file leaked into the normal engine package: $name"
    }
}

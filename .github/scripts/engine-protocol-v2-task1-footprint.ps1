$ErrorActionPreference = 'Stop'

function Invoke-Task1Footprint {
$ErrorActionPreference = 'Stop'

$InstallRoot = Resolve-Path 'build_x64/install'
$Files = Get-ChildItem -Path $InstallRoot -File -Recurse
$Forbidden = $Files | Where-Object {
  $_.Name -like 'decklink-captions.*' -or
  $_.Name -like 'obs-frontend-api.*'
}

if ( $Forbidden ) {
  $Names = ($Forbidden | ForEach-Object { $_.FullName }) -join "`n"
  throw "Stock OBS frontend-only files leaked into the headless runtime:`n$Names"
}

$DeckLink = $Files | Where-Object { $_.Name -eq 'decklink.dll' } | Select-Object -First 1
if ( $null -eq $DeckLink ) {
  throw 'decklink.dll is missing; Task 1.1 should remove only decklink-captions, not DeckLink runtime support.'
}

$Vst = $Files | Where-Object { $_.Name -eq 'obs-vst.dll' } | Select-Object -First 1
if ( $null -eq $Vst ) {
  throw 'obs-vst.dll is missing; Qt is intentionally retained because VST support still depends on it.'
}

Write-Host 'Headless runtime footprint verified: DeckLink and VST kept; DeckLink captions and frontend API absent.'

}

Invoke-Task1Footprint

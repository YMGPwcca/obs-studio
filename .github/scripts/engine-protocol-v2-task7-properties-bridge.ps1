$ErrorActionPreference = 'Stop'

function Invoke-Task7PropertiesBridge {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$DiagnosticDir = Join-Path $InstallRoot '_task7-diagnostics'
New-Item -ItemType Directory -Force -Path $DiagnosticDir | Out-Null
$DiagnosticFile = Join-Path $DiagnosticDir 'properties-bridge-test.txt'
$FailureText = $null
$OutputText = ''

try {
  & cmake --build build_x64 --config RelWithDebInfo --target obs-engine-properties-test 2>&1 |
    Tee-Object -Variable BuildOutput | Write-Host
  if ( $LASTEXITCODE -ne 0 ) {
    throw "obs-engine-properties-test failed to build (exit=$LASTEXITCODE)."
  }

  $PropertiesTest = Get-ChildItem -Path 'build_x64' -Filter 'obs-engine-properties-test.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -eq $PropertiesTest ) {
    throw 'obs-engine-properties-test.exe was not found after explicit test-target build.'
  }

  $Engine = Get-ChildItem -Path $InstallRoot -Filter 'obs-engine.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -eq $Engine ) {
    throw 'obs-engine.exe was not found in the installed runtime.'
  }
  $Env:PATH = "$($Engine.Directory.FullName);$Env:PATH"

  $OutputText = (& $PropertiesTest.FullName 2>&1 | Out-String)
  Write-Host $OutputText
  if ( $LASTEXITCODE -ne 0 ) {
    throw "obs-engine-properties-test.exe failed (exit=$LASTEXITCODE)."
  }
  if ( $OutputText -notmatch 'properties-test: passed' ) {
    throw 'Properties bridge test did not report its success marker.'
  }

  $LeakedTest = Get-ChildItem -Path $InstallRoot -Filter 'obs-engine-properties-test.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -ne $LeakedTest ) {
    throw 'CI-only properties test executable leaked into the installed runtime.'
  }
}
catch {
  $FailureText = ($_ | Out-String)
  Write-Error $FailureText
}
finally {
  @(
    '=== properties test output ==='
    $OutputText
    ''
    '=== properties test failure ==='
    $FailureText
  ) | Set-Content -Path $DiagnosticFile -Encoding utf8
}

if ( $null -ne $FailureText ) {
  throw 'Generic properties bridge test failed. See _task7-diagnostics/properties-bridge-test.txt in the runtime artifact.'
}

}

Invoke-Task7PropertiesBridge

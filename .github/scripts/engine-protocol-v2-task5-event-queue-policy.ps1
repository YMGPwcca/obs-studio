$ErrorActionPreference = 'Stop'

function Invoke-Task5EventQueuePolicy {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$DiagnosticDir = Join-Path $InstallRoot '_task5-diagnostics'
New-Item -ItemType Directory -Force -Path $DiagnosticDir | Out-Null
$DiagnosticFile = Join-Path $DiagnosticDir 'event-queue-policy.txt'
$FailureText = $null
$OutputText = ''

try {
  & cmake --build build_x64 --config RelWithDebInfo --target obs-engine-events-test 2>&1 |
    Tee-Object -Variable BuildOutput | Write-Host
  if ( $LASTEXITCODE -ne 0 ) {
    throw "obs-engine-events-test failed to build (exit=$LASTEXITCODE)."
  }

  $QueueTest = Get-ChildItem -Path 'build_x64' -Filter 'obs-engine-events-test.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -eq $QueueTest ) {
    throw 'obs-engine-events-test.exe was not found after explicit test-target build.'
  }

  $Engine = Get-ChildItem -Path $InstallRoot -Filter 'obs-engine.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -eq $Engine ) {
    throw 'obs-engine.exe was not found in the installed runtime.'
  }
  $Env:PATH = "$($Engine.Directory.FullName);$Env:PATH"

  $OutputText = (& $QueueTest.FullName 2>&1 | Out-String)
  Write-Host $OutputText
  if ( $LASTEXITCODE -ne 0 ) {
    throw "obs-engine-events-test.exe failed (exit=$LASTEXITCODE)."
  }
  if ( $OutputText -notmatch 'events-test: passed' ) {
    throw 'Event queue test did not report its success marker.'
  }

  $LeakedTest = Get-ChildItem -Path $InstallRoot -Filter 'obs-engine-events-test.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -ne $LeakedTest ) {
    throw 'CI-only event queue test executable leaked into the installed runtime.'
  }
}
catch {
  $FailureText = ($_ | Out-String)
  Write-Error $FailureText
}
finally {
  @(
    '=== queue test output ==='
    $OutputText
    ''
    '=== queue test failure ==='
    $FailureText
  ) | Set-Content -Path $DiagnosticFile -Encoding utf8
}

if ( $null -ne $FailureText ) {
  throw 'Bounded event queue policy test failed. See _task5-diagnostics/event-queue-policy.txt in the runtime artifact.'
}

}

Invoke-Task5EventQueuePolicy

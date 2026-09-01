$ErrorActionPreference = 'Stop'

function Invoke-RequiredScript([string] $Path, [hashtable] $Parameters = @{}) {
    & $Path @Parameters
    if (-not $?) {
        throw "Regression script failed: $Path"
    }
}

function Invoke-Task8Regression([string] $installRoot) {
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task8-source-smoke.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task8-concurrency-capture-routing.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task8-concurrency-build-fixture.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task8-concurrency-run.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task8-remove-fixture.ps1'
}

function Invoke-Task9Regression([string] $installRoot) {
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task9-build-fixture.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task9-stage-fixture.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task9.ps1' @{ InstallRoot = $installRoot }
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task9-remove-fixture.ps1'
}

function Invoke-Task10Regression([string] $installRoot) {
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task10-build-fixture.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task10-stage-fixture.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task10.ps1' @{ InstallRoot = $installRoot }
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task10-remove-fixture.ps1'
}

function Invoke-Task11Regression([string] $installRoot) {
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task11-core-audit.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task11-package-audit.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task11-build-fixture.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task11-stage-fixture.ps1'
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task11.ps1' @{ InstallRoot = $installRoot }
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task11-timeout-race.ps1' @{ InstallRoot = $installRoot }
    Invoke-RequiredScript '.github/scripts/engine-protocol-v2-task11-remove-fixture.ps1'
}

function Invoke-Phase2Regression {
    $installRoot = (Resolve-Path 'build_x64/install').Path
    $early = @(
        '.github/scripts/engine-protocol-v2-task1-footprint.ps1',
        '.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1',
        '.github/scripts/engine-protocol-v2-task2-framing.ps1',
        '.github/scripts/engine-protocol-v2-task3-capabilities.ps1',
        '.github/scripts/engine-protocol-v2-task4-revisions.ps1',
        '.github/scripts/engine-protocol-v2-task5-event-queue-policy.ps1',
        '.github/scripts/engine-protocol-v2-task5-subscriptions.ps1',
        '.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1',
        '.github/scripts/engine-protocol-v2-task7-properties-bridge.ps1',
        '.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1'
    )
    foreach ($path in $early) {
        Invoke-RequiredScript $path
    }
    Invoke-Task8Regression $installRoot
    Invoke-Task9Regression $installRoot
    Invoke-Task10Regression $installRoot
    Invoke-Task11Regression $installRoot
    Write-Output 'Tasks 1-11 exact-SHA regression matrix: PASS'
}

Invoke-Phase2Regression

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Select-Phase2CanvasFailureGenerator([string] $BuildRoot) {
    $cachePath = Join-Path $BuildRoot 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath) {
        $cachedLine = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_GENERATOR:INTERNAL=' | Select-Object -First 1
        if ($null -ne $cachedLine) {
            $cachedGenerator = ([string]$cachedLine.Line).Substring('CMAKE_GENERATOR:INTERNAL='.Length)
            if ($cachedGenerator -in @('Visual Studio 18 2026', 'Visual Studio 17 2022')) { return $cachedGenerator }
        }
    }
    $help = (& cmake --help 2>&1) -join "`n"
    foreach ($candidate in @('Visual Studio 18 2026', 'Visual Studio 17 2022')) {
        if ($help.Contains($candidate)) { return $candidate }
    }
    throw 'No supported Visual Studio generator was found for the Canvas failure lane.'
}

function Invoke-Phase2CanvasFailureLane {
    $buildRoot = Join-Path (Get-Location) 'build_x64_phase2_canvas_failure'
    $installRoot = Join-Path $buildRoot 'install'
    $generator = Select-Phase2CanvasFailureGenerator $buildRoot
    cmake -S . -B $buildRoot -G $generator -A x64 `
        "-DCMAKE_INSTALL_PREFIX=$installRoot" `
        -DENABLE_FRONTEND=OFF -DENABLE_WEBSOCKET=OFF -DENABLE_BROWSER_SOURCE=OFF `
        -DENABLE_PLUGINS=ON -DENABLE_PHASE2_TEST_HOOKS=ON
    cmake --build $buildRoot --config RelWithDebInfo --target install obs-engine-preview-consumer-test --parallel 1
    & .github/scripts/engine-protocol-v2-task14-canvas-failure.ps1 `
        -InstallRoot (Resolve-Path -LiteralPath $installRoot) `
        -ConsumerPath (Resolve-Path -LiteralPath (Join-Path $buildRoot 'engine/RelWithDebInfo/obs-engine-preview-consumer-test.exe'))
    if (-not $?) { throw 'Canvas failure-atomicity lane failed.' }
}

Invoke-Phase2CanvasFailureLane

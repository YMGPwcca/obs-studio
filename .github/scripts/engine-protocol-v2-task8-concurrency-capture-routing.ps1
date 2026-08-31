$ErrorActionPreference = 'Stop'

function Invoke-Task8CaptureRouting {
$ErrorActionPreference = 'Stop'

$TestDir = Join-Path $Env:RUNNER_TEMP 'source-event-capture-regression'
New-Item -ItemType Directory -Force -Path $TestDir | Out-Null
$TestSource = Join-Path $TestDir 'capture-routing.cpp'
$TestExe = Join-Path $TestDir 'capture-routing.exe'

@'
#include "engine/source_event_capture.hpp"

#include <atomic>
#include <thread>

int main()
{
    using namespace obs_engine;

    SourceEventCaptureGate gate;
    if (gate.route_for_current_thread() != SourceEventCaptureRoute::Direct)
        return 1;

    gate.begin();
    if (!gate.active() || gate.route_for_current_thread() != SourceEventCaptureRoute::Capture)
        return 2;

    std::atomic<int> worker_route{-1};
    std::thread worker([&] {
        worker_route.store(static_cast<int>(gate.route_for_current_thread()), std::memory_order_release);
    });
    worker.join();

    if (worker_route.load(std::memory_order_acquire) !=
        static_cast<int>(SourceEventCaptureRoute::Defer))
        return 3;

    gate.end();
    if (gate.active() || gate.route_for_current_thread() != SourceEventCaptureRoute::Direct)
        return 4;

    return 0;
}
'@ | Set-Content -Path $TestSource -Encoding utf8

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $VsWhere)) {
    throw 'vswhere.exe was not found on the Windows runner.'
}
$VsInstall = (& $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($VsInstall)) {
    throw 'A Visual Studio installation with the x64 C++ toolchain was not found.'
}
$VcVars = Join-Path $VsInstall 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $VcVars)) {
    throw 'vcvars64.bat was not found.'
}

$CompileCmd = Join-Path $TestDir 'compile.cmd'
@"
@echo off
call "$VcVars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I"$Env:GITHUB_WORKSPACE" "$TestSource" /Fe:"$TestExe"
exit /b %errorlevel%
"@ | Set-Content -Path $CompileCmd -Encoding ascii

& cmd.exe /d /c $CompileCmd
if ($LASTEXITCODE -ne 0) {
    throw "Capture-routing regression did not compile (exit=$LASTEXITCODE)."
}

& $TestExe
if ($LASTEXITCODE -ne 0) {
    throw "Capture-routing regression failed (exit=$LASTEXITCODE)."
}

}

Invoke-Task8CaptureRouting

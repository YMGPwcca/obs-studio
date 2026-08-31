$ErrorActionPreference = 'Stop'

$script:Process = $null
$script:ErrorTask = $null
$script:Engine = $null
$script:DiagnosticFile = $null
$script:FailureText = $null

function Initialize-Task1Diagnostics {
    $diagnosticDir = Join-Path (Resolve-Path 'build_x64/install') '_task1-diagnostics'
    New-Item -ItemType Directory -Force -Path $diagnosticDir | Out-Null
    $script:DiagnosticFile = Join-Path $diagnosticDir 'smoke.txt'
    $runtimeListingFile = Join-Path $diagnosticDir 'runtime-files.txt'
    Get-ChildItem -Path 'build_x64/install' -File -Recurse |
        ForEach-Object { $_.FullName } |
        Set-Content -Path $runtimeListingFile -Encoding utf8
}

function Start-Task1Engine {
    $script:Engine = Get-ChildItem -Path 'build_x64/install' -Filter 'obs-engine.exe' -File -Recurse |
        Select-Object -First 1
    if ($null -eq $script:Engine) {
        throw 'obs-engine.exe was not found in the installed runtime.'
    }

    Write-Host "obs-engine path: $($script:Engine.FullName)"
    Write-Host "obs-engine cwd:  $($script:Engine.Directory.FullName)"

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $script:Engine.FullName
    $startInfo.WorkingDirectory = $script:Engine.Directory.FullName
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    $script:Process = [System.Diagnostics.Process]::new()
    $script:Process.StartInfo = $startInfo
    if (-not $script:Process.Start()) {
        throw 'Failed to start obs-engine.exe.'
    }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Read-Task1EngineMessage {
    $readTask = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $readTask.Wait(30000)) {
        throw 'Timed out waiting 30 seconds for obs-engine stdout.'
    }
    $line = $readTask.Result
    if ($null -eq $line) {
        $exitText = if ($script:Process.HasExited) { "exit=$($script:Process.ExitCode)" } else { 'process still running' }
        throw "obs-engine closed stdout unexpectedly ($exitText)."
    }
    Write-Host "obs-engine stdout: $line"
    return ($line | ConvertFrom-Json)
}

function Send-Task1EngineRequest([int] $Id, [hashtable] $Request) {
    $Request['id'] = $Id
    $json = $Request | ConvertTo-Json -Compress -Depth 20
    Write-Host "obs-engine stdin:  $json"
    $script:Process.StandardInput.WriteLine($json)
    $script:Process.StandardInput.Flush()

    while ($true) {
        $message = Read-Task1EngineMessage
        if ($null -ne $message.id -and [int64]$message.id -eq $Id) {
            if (-not $message.ok) {
                throw "Request $Id failed: $($message.error): $($message.message)"
            }
            return $message
        }
    }
}

function Get-Task1ColorType {
    $types = Send-Task1EngineRequest 2 @{ cmd = 'source.types' }
    $colorType = $types.result.types | Where-Object { $_.id -eq 'color_source_v3' } | Select-Object -First 1
    if ($null -eq $colorType) {
        $colorType = $types.result.types | Where-Object { $_.id -eq 'color_source' } | Select-Object -First 1
    }
    if ($null -eq $colorType) {
        throw 'No Color Source input type was registered; image-source may have failed to load.'
    }
    return $colorType
}

function Initialize-Task1Protocol {
    $ready = Read-Task1EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        throw 'obs-engine did not emit the expected protocol-v1 ready event.'
    }

    $hello = Send-Task1EngineRequest 1 @{ cmd = 'hello' }
    if ([int]$hello.result.protocol -ne 1) {
        throw 'hello returned an unexpected protocol version.'
    }
    return Get-Task1ColorType
}

function New-Task1SceneObjects([object] $ColorType) {
    $scene = Send-Task1EngineRequest 3 @{
        cmd = 'scene.create'
        name = 'task1-smoke-scene'
    }
    $sceneHandle = [int64]$scene.result.scene

    $source = Send-Task1EngineRequest 4 @{
        cmd = 'source.create'
        type = [string]$ColorType.id
        name = 'task1-smoke-color'
        settings = @{
            width = 320
            height = 180
            color = 4294901760
        }
    }
    $sourceHandle = [int64]$source.result.source

    $item = Send-Task1EngineRequest 5 @{
        cmd = 'scene.add'
        scene = $sceneHandle
        source = $sourceHandle
    }
    $itemHandle = [int64]$item.result.item

    $null = Send-Task1EngineRequest 6 @{
        cmd = 'item.transform'
        item = $itemHandle
        x = 32.0
        y = 24.0
        scale_x = 1.25
        scale_y = 1.25
        rotation = 5.0
    }
    $null = Send-Task1EngineRequest 7 @{
        cmd = 'program.set'
        scene = $sceneHandle
    }

    return [pscustomobject]@{
        Scene = $sceneHandle
        Source = $sourceHandle
    }
}

function Assert-Task1SourceSettings([object] $Objects) {
    $settings = Send-Task1EngineRequest 8 @{
        cmd = 'source.settings'
        source = $Objects.Source
    }
    if ([int]$settings.result.settings.width -ne 320 -or
        [int]$settings.result.settings.height -ne 180) {
        throw 'Source settings did not round-trip through libobs.'
    }
}

function Complete-Task1ProtocolV1 {
    $null = Send-Task1EngineRequest 9 @{
        cmd = 'program.set'
        scene = 0
    }
    $null = Send-Task1EngineRequest 10 @{ cmd = 'shutdown' }
    $script:Process.StandardInput.Close()

    if (-not $script:Process.WaitForExit(15000)) {
        $script:Process.Kill($true)
        throw 'obs-engine did not exit after shutdown.'
    }
    if ($script:Process.ExitCode -ne 0) {
        throw "obs-engine exited with code $($script:Process.ExitCode)."
    }
    Write-Host 'Protocol-v1 smoke test passed.'
}

function Invoke-Task1ProtocolV1Scenario {
    $colorType = Initialize-Task1Protocol
    $objects = New-Task1SceneObjects $colorType
    Assert-Task1SourceSettings $objects
    Complete-Task1ProtocolV1
}

function Stop-Task1AfterFailure {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) {
        try {
            $script:Process.Kill($true)
            $script:Process.WaitForExit(5000) | Out-Null
        }
        catch {
            Write-Warning "Failed to terminate obs-engine after smoke failure: $_"
        }
    }
}

function Write-Task1Diagnostics {
    $stderrText = ''
    if ($null -ne $script:ErrorTask) {
        try {
            $stderrText = $script:ErrorTask.GetAwaiter().GetResult()
        }
        catch {
            $stderrText = "Failed to collect redirected stderr: $_"
        }
    }

    $exitState = 'not-started'
    if ($null -ne $script:Process) {
        if ($script:Process.HasExited) {
            $exitState = "exited:$($script:Process.ExitCode)"
        }
        else {
            $exitState = 'still-running'
        }
    }

    @(
        "engine=$($script:Engine.FullName)"
        "exit_state=$exitState"
        ''
        '=== smoke failure ==='
        $script:FailureText
        ''
        '=== obs-engine stderr ==='
        $stderrText
    ) | Set-Content -Path $script:DiagnosticFile -Encoding utf8

    if (-not [string]::IsNullOrWhiteSpace($stderrText)) {
        Write-Host '=== obs-engine stderr ==='
        Write-Host $stderrText
    }
}

function Invoke-Task1ProtocolV1Smoke {
    Initialize-Task1Diagnostics
    try {
        Start-Task1Engine
        Invoke-Task1ProtocolV1Scenario
    }
    catch {
        $script:FailureText = ($_ | Out-String)
        Write-Error $script:FailureText
        Stop-Task1AfterFailure
    }
    finally {
        Write-Task1Diagnostics
    }

    if ($null -ne $script:FailureText) {
        throw 'Protocol-v1 smoke test failed. See _task1-diagnostics/smoke.txt in the runtime artifact.'
    }
}

Invoke-Task1ProtocolV1Smoke

$ErrorActionPreference = 'Stop'

$script:InstallRoot = $null
$script:Process = $null
$script:ErrorTask = $null
$script:Engine = $null
$script:DiagnosticFile = $null
$script:FailureText = $null

function Initialize-Task4Diagnostics {
    $script:InstallRoot = Resolve-Path 'build_x64/install'
    $diagnosticDir = Join-Path $script:InstallRoot '_task4-diagnostics'
    New-Item -ItemType Directory -Force -Path $diagnosticDir | Out-Null
    $script:DiagnosticFile = Join-Path $diagnosticDir 'v2-revisions-smoke.txt'
}

function Start-Task4Engine {
    $script:Engine = Get-ChildItem -Path $script:InstallRoot -Filter 'obs-engine.exe' -File -Recurse |
        Select-Object -First 1
    if ($null -eq $script:Engine) {
        throw 'obs-engine.exe was not found in the installed runtime.'
    }
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

function Read-Task4EngineMessage {
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

function Send-Task4Request([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 20
    Write-Host "obs-engine stdin:  $json"
    $script:Process.StandardInput.WriteLine($json)
    $script:Process.StandardInput.Flush()
    return Read-Task4EngineMessage
}

function Assert-Task4Hello([object] $Hello) {
    if ($Hello.id -ne 'task4.hello' -or -not $Hello.status.ok) {
        throw 'session.hello failed.'
    }
    if ([int64]$Hello.revision -ne 0 -or [int64]$Hello.data.revision -ne 0) {
        throw 'A new engine process must begin at revision 0.'
    }
}

function Initialize-Task4Protocol {
    $ready = Read-Task4EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        throw 'Migration bootstrap ready event changed unexpectedly.'
    }
    $hello = Send-Task4Request @{
        op = 'request'; id = 'task4.hello'; method = 'session.hello'; params = @{}
    }
    Assert-Task4Hello $hello
}

function Invoke-Task4ReadOnlyGuard {
    $readOnlyGuard = Send-Task4Request @{
        op = 'request'; id = 'task4.readonly-guard'; method = 'session.ping'; params = @{}; ifRevision = 0
    }
    if ($readOnlyGuard.status.ok -or $readOnlyGuard.status.code -ne 'bad_request' -or
        [int64]$readOnlyGuard.revision -ne 0) {
        throw 'Read-only methods must reject ifRevision without changing revision.'
    }
}

function Invoke-Task4UnsupportedGuard {
    $unsupportedGuard = Send-Task4Request @{
        op = 'request'; id = 'task4.unsupported-guard'; method = 'task4.nonexistent'; params = @{}; ifRevision = 0
    }
    if ($unsupportedGuard.status.ok -or $unsupportedGuard.status.code -ne 'unsupported_method' -or
        [int64]$unsupportedGuard.revision -ne 0) {
        throw 'Unknown methods must remain unsupported without consuming revision state.'
    }
}

function Invoke-Task4StaleClose {
    $staleClose = Send-Task4Request @{
        op = 'request'; id = 'task4.stale-close'; method = 'session.close'; params = @{}; ifRevision = 1
    }
    if ($staleClose.status.ok -or $staleClose.status.code -ne 'revision_conflict') {
        throw 'A stale guarded mutation must return revision_conflict.'
    }
    if ([int64]$staleClose.revision -ne 0 -or
        [int64]$staleClose.status.details.expectedRevision -ne 1 -or
        [int64]$staleClose.status.details.actualRevision -ne 0) {
        throw 'revision_conflict returned incorrect revision details.'
    }
    if ($script:Process.HasExited) {
        throw 'Rejected session.close unexpectedly terminated the engine.'
    }
}

function Invoke-Task4PostConflictPing {
    $pingAfterConflict = Send-Task4Request @{
        op = 'request'; id = 'task4.ping-after-conflict'; method = 'session.ping'; params = @{}
    }
    if (-not $pingAfterConflict.status.ok -or -not $pingAfterConflict.data.pong -or
        [int64]$pingAfterConflict.revision -ne 0) {
        throw 'Rejected mutation changed state or broke request processing.'
    }
}

function Complete-Task4Session {
    $close = Send-Task4Request @{
        op = 'request'; id = 'task4.close'; method = 'session.close'; params = @{}; ifRevision = 0
    }
    if ($close.id -ne 'task4.close' -or -not $close.status.ok -or [int64]$close.revision -ne 1) {
        throw 'Matching guarded mutation must commit exactly one revision.'
    }
    $script:Process.StandardInput.Close()
    if (-not $script:Process.WaitForExit(15000)) {
        $script:Process.Kill($true)
        throw 'obs-engine did not exit after committed session.close.'
    }
    if ($script:Process.ExitCode -ne 0) {
        throw "obs-engine exited with code $($script:Process.ExitCode)."
    }
    Write-Host 'Protocol-v2 revision smoke test passed.'
}

function Invoke-Task4ProtocolScenario {
    Initialize-Task4Protocol
    Invoke-Task4ReadOnlyGuard
    Invoke-Task4UnsupportedGuard
    Invoke-Task4StaleClose
    Invoke-Task4PostConflictPing
    Complete-Task4Session
}

function Stop-Task4AfterFailure {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) {
        try {
            $script:Process.Kill($true)
            $script:Process.WaitForExit(5000) | Out-Null
        }
        catch {
            Write-Warning "Failed to terminate obs-engine after Task 4 smoke failure: $_"
        }
    }
}

function Write-Task4Diagnostics {
    $stderrText = ''
    if ($null -ne $script:ErrorTask) {
        try { $stderrText = $script:ErrorTask.GetAwaiter().GetResult() }
        catch { $stderrText = "Failed to collect redirected stderr: $_" }
    }
    $exitState = 'not-started'
    if ($null -ne $script:Process) {
        if ($script:Process.HasExited) { $exitState = "exited:$($script:Process.ExitCode)" }
        else { $exitState = 'still-running' }
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
}

function Invoke-Task4RevisionsSmoke {
    Initialize-Task4Diagnostics
    try {
        Start-Task4Engine
        Invoke-Task4ProtocolScenario
    }
    catch {
        $script:FailureText = ($_ | Out-String)
        Write-Error $script:FailureText
        Stop-Task4AfterFailure
    }
    finally {
        Write-Task4Diagnostics
    }
    if ($null -ne $script:FailureText) {
        throw 'Protocol-v2 revision smoke test failed. See _task4-diagnostics/v2-revisions-smoke.txt in the runtime artifact.'
    }
}

Invoke-Task4RevisionsSmoke

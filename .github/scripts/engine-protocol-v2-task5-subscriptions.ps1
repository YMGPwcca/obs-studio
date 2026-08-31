$ErrorActionPreference = 'Stop'

$script:InstallRoot = $null
$script:Process = $null
$script:ErrorTask = $null
$script:Engine = $null
$script:DiagnosticFile = $null
$script:FailureText = $null

function Initialize-Task5Diagnostics {
    $script:InstallRoot = Resolve-Path 'build_x64/install'
    $diagnosticDir = Join-Path $script:InstallRoot '_task5-diagnostics'
    New-Item -ItemType Directory -Force -Path $diagnosticDir | Out-Null
    $script:DiagnosticFile = Join-Path $diagnosticDir 'v2-events-smoke.txt'
}

function Start-Task5Engine {
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

function Read-Task5EngineMessage {
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

function Send-Task5Request([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 20
    Write-Host "obs-engine stdin:  $json"
    $script:Process.StandardInput.WriteLine($json)
    $script:Process.StandardInput.Flush()
    return Read-Task5EngineMessage
}

function Get-Task5SubscriptionPatterns([object] $Response) {
    return @($Response.data.subscriptions | ForEach-Object { [string]$_.pattern })
}

function Assert-Task5Hello([object] $Hello, [string[]] $RequiredCapabilities) {
    if (-not $Hello.status.ok) {
        throw 'Task 5 session.hello failed.'
    }
    $capabilityNames = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($required in $RequiredCapabilities) {
        if ($capabilityNames -notcontains $required) {
            throw "Task 5 capability regressed: $required"
        }
    }
    if ([int64]$Hello.revision -ne 0) {
        throw 'New engine did not begin at revision 0.'
    }
}

function Initialize-Task5Protocol {
    $required = @(
        'engine.capabilities.v1', 'event.delivery.v1', 'session.close.v1',
        'session.getSubscriptions.v1', 'session.hello.v1', 'session.ping.v1',
        'session.subscribe.v1', 'session.unsubscribe.v1'
    )
    $ready = Read-Task5EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        throw 'Migration bootstrap ready event changed unexpectedly.'
    }
    $hello = Send-Task5Request @{
        op = 'request'; id = 'task5.hello'; method = 'session.hello'; params = @{}
    }
    Assert-Task5Hello $hello $required
}

function Invoke-Task5InitialSubscriptionChecks {
    $initial = Send-Task5Request @{
        op = 'request'; id = 'task5.subscriptions.initial'; method = 'session.getSubscriptions'; params = @{}
    }
    if (-not $initial.status.ok -or @($initial.data.subscriptions).Count -ne 0 -or [int64]$initial.revision -ne 0) {
        throw 'New session did not begin with an empty subscription set.'
    }

    $invalid = Send-Task5Request @{
        op = 'request'; id = 'task5.subscribe.invalid'; method = 'session.subscribe'
        params = @{ subscriptions = @(@{ pattern = 'engine.*.bad' }) }
    }
    if ($invalid.status.ok -or $invalid.status.code -ne 'bad_request' -or [int64]$invalid.revision -ne 0) {
        throw 'Invalid wildcard subscription was not rejected cleanly.'
    }

    $guardedSubscribe = Send-Task5Request @{
        op = 'request'; id = 'task5.subscribe.guarded'; method = 'session.subscribe'
        params = @{ subscriptions = @(@{ pattern = 'engine.*' }) }; ifRevision = 0
    }
    if ($guardedSubscribe.status.ok -or $guardedSubscribe.status.code -ne 'bad_request' -or
        [int64]$guardedSubscribe.revision -ne 0) {
        throw 'Session-local subscription changes must reject engine ifRevision guards.'
    }
}

function Invoke-Task5SubscriptionDedupe {
    $subscribe = Send-Task5Request @{
        op = 'request'; id = 'task5.subscribe'; method = 'session.subscribe'
        params = @{ subscriptions = @(
            @{ pattern = 'engine.*' }
            @{ pattern = 'engine.stopping' }
            @{ pattern = 'engine.*' }
            @{ pattern = 'meter.*'; telemetry = $false }
        ) }
    }
    $patterns = Get-Task5SubscriptionPatterns $subscribe
    if (-not $subscribe.status.ok -or ($patterns -join '|') -ne 'engine.*|engine.stopping|meter.*' -or
        [int64]$subscribe.revision -ne 0) {
        throw "Subscription dedupe/order failed: $($patterns -join ', ')"
    }
}

function Invoke-Task5TelemetryUpgrade {
    $upgrade = Send-Task5Request @{
        op = 'request'; id = 'task5.subscribe.telemetry'; method = 'session.subscribe'
        params = @{ subscriptions = @(@{ pattern = 'meter.*'; telemetry = $true }) }
    }
    $meter = @($upgrade.data.subscriptions | Where-Object { $_.pattern -eq 'meter.*' })
    if (-not $upgrade.status.ok -or $meter.Count -ne 1 -or -not [bool]$meter[0].telemetry -or
        [int64]$upgrade.revision -ne 0) {
        throw 'Telemetry opt-in did not upgrade the existing effective subscription.'
    }
}

function Invoke-Task5TelemetryRemoval {
    $unsubscribe = Send-Task5Request @{
        op = 'request'; id = 'task5.unsubscribe.telemetry'; method = 'session.unsubscribe'
        params = @{ subscriptions = @(@{ pattern = 'meter.*' }) }
    }
    $patterns = Get-Task5SubscriptionPatterns $unsubscribe
    if (-not $unsubscribe.status.ok -or ($patterns -join '|') -ne 'engine.*|engine.stopping' -or
        [int64]$unsubscribe.revision -ne 0) {
        throw 'Unsubscribe did not remove exactly the requested effective pattern.'
    }
}

function Assert-Task5FinalSubscriptions {
    $getSubscriptions = Send-Task5Request @{
        op = 'request'; id = 'task5.subscriptions.final'; method = 'session.getSubscriptions'; params = @{}
    }
    $patterns = Get-Task5SubscriptionPatterns $getSubscriptions
    if (-not $getSubscriptions.status.ok -or ($patterns -join '|') -ne 'engine.*|engine.stopping' -or
        [int64]$getSubscriptions.revision -ne 0) {
        throw 'session.getSubscriptions did not return the effective subscription set.'
    }
}

function Assert-Task5StoppingEvent {
    $stopping = Read-Task5EngineMessage
    if ($stopping.op -ne 'event' -or [uint64]$stopping.seq -ne 1 -or [int64]$stopping.revision -ne 1 -or
        $stopping.event -ne 'engine.stopping' -or $stopping.data.reason -ne 'session.close') {
        throw 'Subscribed engine.stopping event envelope was incorrect.'
    }
    if ($null -ne $stopping.telemetry) {
        throw 'State event was incorrectly identified as telemetry.'
    }
}

function Complete-Task5Session {
    $close = Send-Task5Request @{
        op = 'request'; id = 'task5.close'; method = 'session.close'; params = @{}; ifRevision = 0
    }
    if (-not $close.status.ok -or [int64]$close.revision -ne 1) {
        throw 'Guarded session.close did not commit revision 1.'
    }
    Assert-Task5StoppingEvent
    $script:Process.StandardInput.Close()
    if (-not $script:Process.WaitForExit(15000)) {
        $script:Process.Kill($true)
        throw 'obs-engine did not exit after session.close.'
    }
    if ($script:Process.ExitCode -ne 0) {
        throw "obs-engine exited with code $($script:Process.ExitCode)."
    }
    $remaining = $script:Process.StandardOutput.ReadToEnd()
    if (-not [string]::IsNullOrWhiteSpace($remaining)) {
        throw "Overlapping exact/wildcard subscriptions emitted duplicate/unexpected events: $remaining"
    }
    Write-Host 'Protocol-v2 event/subscription smoke test passed.'
}

function Invoke-Task5ProtocolScenario {
    Initialize-Task5Protocol
    Invoke-Task5InitialSubscriptionChecks
    Invoke-Task5SubscriptionDedupe
    Invoke-Task5TelemetryUpgrade
    Invoke-Task5TelemetryRemoval
    Assert-Task5FinalSubscriptions
    Complete-Task5Session
}

function Stop-Task5AfterFailure {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) {
        try {
            $script:Process.Kill($true)
            $script:Process.WaitForExit(5000) | Out-Null
        }
        catch {
            Write-Warning "Failed to terminate obs-engine after Task 5 smoke failure: $_"
        }
    }
}

function Write-Task5Diagnostics {
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

function Invoke-Task5SubscriptionsSmoke {
    Initialize-Task5Diagnostics
    try {
        Start-Task5Engine
        Invoke-Task5ProtocolScenario
    }
    catch {
        $script:FailureText = ($_ | Out-String)
        Write-Error $script:FailureText
        Stop-Task5AfterFailure
    }
    finally {
        Write-Task5Diagnostics
    }
    if ($null -ne $script:FailureText) {
        throw 'Protocol-v2 event/subscription smoke test failed. See _task5-diagnostics/v2-events-smoke.txt in the runtime artifact.'
    }
}

Invoke-Task5SubscriptionsSmoke

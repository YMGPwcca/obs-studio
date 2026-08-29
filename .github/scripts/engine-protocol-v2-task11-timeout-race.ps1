param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$InstallRoot = (Resolve-Path $InstallRoot).Path
$Engine = Get-ChildItem -Path $InstallRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
if ($null -eq $Engine) {
    throw 'obs-engine.exe was not found in the runtime root.'
}

$script:Failures = [System.Collections.Generic.List[string]]::new()
$script:Process = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSeq = [uint64]1

function Fail([string] $Message) {
    throw "Task 11 timeout ownership race: $Message"
}

function Start-RaceEngine {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Engine.FullName
    $startInfo.WorkingDirectory = $Engine.Directory.FullName
    $startInfo.ArgumentList.Add('--plugin=task11-filter-source')
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    $script:Process = [System.Diagnostics.Process]::new()
    $script:Process.StartInfo = $startInfo
    if (-not $script:Process.Start()) {
        Fail 'failed to start obs-engine.exe.'
    }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:NextSeq = [uint64]1

    $ready = Read-EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        Fail 'engine did not emit the migration bootstrap ready event.'
    }
}

function Stop-RaceEngine {
    if ($null -eq $script:Process) {
        return
    }
    try {
        if (-not $script:Process.HasExited) {
            $script:Process.StandardInput.Close()
            if (-not $script:Process.WaitForExit(5000)) {
                $script:Process.Kill($true)
                $script:Process.WaitForExit(5000)
            }
        }
    } catch {
    }
    $script:Process.Dispose()
    $script:Process = $null
}

function Read-EngineMessage {
    $readTask = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $readTask.Wait(30000)) {
        Fail 'timed out waiting 30 seconds for stdout.'
    }
    $line = $readTask.Result
    if ($null -eq $line) {
        $exitText = if ($script:Process.HasExited) { "exit=$($script:Process.ExitCode)" } else { 'process still running' }
        Fail "stdout closed unexpectedly ($exitText)."
    }
    Write-Host "stdout: $line"
    return ($line | ConvertFrom-Json)
}

function Send-V2Request([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 50
    Write-Host "stdin:  $json"
    $script:Process.StandardInput.WriteLine($json)
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-EngineMessage
        if ($message.op -eq 'event') {
            $null = $script:PendingEvents.Add($message)
            continue
        }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) {
            Fail "expected response '$($Request.id)' but received a different message."
        }
        return $message
    }
}

function Read-NextEvent {
    if ($script:PendingEvents.Count -gt 0) {
        $event = $script:PendingEvents[0]
        $script:PendingEvents.RemoveAt(0)
    } else {
        $event = Read-EngineMessage
    }
    if ($event.op -ne 'event') {
        Fail "expected an event but received response '$($event.id)'."
    }
    if ([uint64]$event.seq -ne $script:NextSeq) {
        Fail "event '$($event.event)' had seq=$($event.seq), expected $script:NextSeq."
    }
    $script:NextSeq++
    return $event
}

function Read-Until-Resync([int64] $MinimumRevision, [System.Collections.Generic.List[object]] $Batch) {
    while ($true) {
        $event = Read-NextEvent
        $null = $Batch.Add($event)
        if ([string]$event.event -eq 'session.resyncRequired') {
            if ([int64]$event.revision -lt $MinimumRevision -or
                [string]$event.data.reason -ne 'event_queue_overflow') {
                Fail 'resync event had an invalid revision or reason.'
            }
            return $event
        }
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) {
        Fail "$Label did not succeed at revision $Revision."
    }
}

function Assert-Timeout($Response, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne 'timeout' -or
        [int64]$Response.revision -ne $Revision) {
        Fail "$Label was not a timeout at revision $Revision."
    }
}

function Run-RaceScenario([string] $Label, [ValidateSet('rename', 'enable')] [string] $Action) {
    try {
        Start-RaceEngine
        $hello = Send-V2Request @{ op = 'request'; id = "$Label.hello"; method = 'session.hello'; params = @{} }
        Assert-Ok $hello 0 "$Label session.hello"
        $subscribe = Send-V2Request @{
            op = 'request'; id = "$Label.subscribe"; method = 'session.subscribe'
            params = @{ subscriptions = @(@{ pattern = 'filter.*' }, @{ pattern = 'source.*' }, @{ pattern = 'session.*' }) }
        }
        Assert-Ok $subscribe 0 "$Label session.subscribe"

        $source = Send-V2Request @{
            op = 'request'; id = "$Label.source"; method = 'source.create'; ifRevision = 0
            params = @{ kind = 'task11_filter_source'; name = "$Label-parent" }
        }
        Assert-Ok $source 1 "$Label source.create"
        $sourceEvent = Read-NextEvent
        if ($sourceEvent.event -ne 'source.created') {
            Fail "$Label did not emit source.created."
        }

        $filter = Send-V2Request @{
            op = 'request'; id = "$Label.filter"; method = 'filter.create'; ifRevision = 1
            params = @{ source = '1'; kind = 'task11_filter'; name = "$Label-filter"; settings = @{ value = 700 } }
        }
        Assert-Ok $filter 2 "$Label filter.create"
        $filterEvent = Read-NextEvent
        if ($filterEvent.event -ne 'filter.created') {
            Fail "$Label did not emit filter.created."
        }

        # A's 7.5-second callback overlaps both the timeout and the following
        # same-filter request. The plugin logs A before it blocks; libobs emits
        # the generic update signal only after the blocked callback returns.
        $timeoutWatch = [System.Diagnostics.Stopwatch]::StartNew()
        $timeoutA = Send-V2Request @{
            op = 'request'; id = "$Label.timeout-a"; method = 'filter.patchSettings'; ifRevision = 2
            params = @{ filter = '2'; settings = @{ value = 701; blockMs = 7500 } }
        }
        $timeoutWatch.Stop()
        Assert-Timeout $timeoutA 2 "$Label timeout A"
        Write-Host "$Label timeout A elapsed=$($timeoutWatch.Elapsed.TotalSeconds) seconds"
        $firstBatch = [System.Collections.Generic.List[object]]::new()
        $firstResync = Read-Until-Resync 3 $firstBatch
        $revision = [int64]$firstResync.revision

        $actionParams = if ($Action -eq 'rename') {
            @{ filter = '2'; name = "$Label-renamed" }
        } else {
            @{ filter = '2'; enabled = $false }
        }
        $actionResponse = Send-V2Request @{
            op = 'request'; id = "$Label.$Action"; method = if ($Action -eq 'rename') { 'filter.rename' } else { 'filter.setEnabled' }
            ifRevision = $revision; params = $actionParams
        }
        Assert-Ok $actionResponse ($revision + 1) "$Label $Action"
        $revision = [int64]$actionResponse.revision

        # The pre-fix candidate consumes the settings quarantine here and emits
        # a resync instead of the command-owned action event. The fixed bridge
        # must leave the quarantine intact while still preserving this action.
        $actionEvent = Read-NextEvent
        if ($actionEvent.event -eq 'session.resyncRequired') {
            Write-Host "$Label observed pre-fix action resync at revision $($actionEvent.revision)"
            $revision = [int64]$actionEvent.revision
        } else {
            $expectedActionEvent = if ($Action -eq 'rename') { 'filter.renamed' } else { 'filter.enabledChanged' }
            if ($actionEvent.event -ne $expectedActionEvent) {
                Fail "$Label expected $expectedActionEvent or a diagnostic resync, got $($actionEvent.event)."
            }
            if ([int64]$actionEvent.revision -ne $revision) {
                Fail "$Label action event revision mismatch."
            }
        }

        $newerWatch = [System.Diagnostics.Stopwatch]::StartNew()
        $newerB = Send-V2Request @{
            op = 'request'; id = "$Label.newer-b"; method = 'filter.patchSettings'; ifRevision = $revision
            params = @{ filter = '2'; settings = @{ value = 702; blockMs = 0 } }
        }
        $newerWatch.Stop()
        Write-Host "$Label newer B elapsed=$($newerWatch.Elapsed.TotalSeconds) seconds"
        if ($newerB.status.ok) {
            $null = $script:Failures.Add("${Label}: newer B incorrectly settled successfully from old A completion.")
            if ($script:PendingEvents.Count -gt 0) {
                $settingsEvent = Read-NextEvent
                if ($settingsEvent.event -eq 'filter.settingsChanged') {
                    $null = $script:Failures.Add("${Label}: normal filter.settingsChanged was emitted for the misattributed B settlement.")
                }
            }
            Write-Host "$Label RESULT=FAIL (B succeeded)" -ForegroundColor Red
            return
        }
        Assert-Timeout $newerB $revision "$Label newer B"
        $secondBatch = [System.Collections.Generic.List[object]]::new()
        $secondResync = Read-Until-Resync ($revision + 1) $secondBatch
        $lateSettings = @($secondBatch | Where-Object {
            $_.event -eq 'filter.settingsChanged' -and [string]$_.data.filter -eq '2'
        })
        if ($lateSettings.Count -ne 0) {
            Fail "$Label emitted a normal same-filter settings event before ownership was proven."
        }
        Write-Host "$Label RESULT=PASS (B timeout/resync; quarantine survived $Action)" -ForegroundColor Green
    } catch {
        $null = $script:Failures.Add("${Label}: $($_.Exception.Message)")
        Write-Host "$Label RESULT=ERROR: $($_.Exception.Message)" -ForegroundColor Red
    } finally {
        Stop-RaceEngine
    }
}

Run-RaceScenario 'task11-race-rename' 'rename'
Run-RaceScenario 'task11-race-enable' 'enable'

if ($script:Failures.Count -ne 0) {
    throw ($script:Failures -join "`n")
}

Write-Host 'Task 11 timeout ownership race regression: PASS' -ForegroundColor Green

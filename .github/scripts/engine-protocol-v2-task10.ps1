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

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Engine.FullName
$StartInfo.WorkingDirectory = $Engine.Directory.FullName
$StartInfo.ArgumentList.Add('--plugin=task10-media-source')
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardInput = $true
$StartInfo.RedirectStandardOutput = $true
$StartInfo.RedirectStandardError = $true
$StartInfo.CreateNoWindow = $true

$Process = [System.Diagnostics.Process]::new()
$Process.StartInfo = $StartInfo
if (-not $Process.Start()) {
    throw 'Failed to start obs-engine.exe.'
}
$ErrorTask = $Process.StandardError.ReadToEndAsync()
$script:Events = [System.Collections.Generic.List[object]]::new()
$script:NextSeq = [uint64]1

function Fail([string] $Message) {
    throw "Task 10: $Message"
}

function Read-EngineMessage {
    $ReadTask = $Process.StandardOutput.ReadLineAsync()
    if (-not $ReadTask.Wait(30000)) {
        Fail 'timed out waiting 30 seconds for obs-engine stdout.'
    }
    $Line = $ReadTask.Result
    if ($null -eq $Line) {
        $ExitText = if ($Process.HasExited) { "exit=$($Process.ExitCode)" } else { 'process still running' }
        Fail "obs-engine closed stdout unexpectedly ($ExitText)."
    }
    Write-Host "stdout: $Line"
    return ($Line | ConvertFrom-Json)
}

function Send-V2Request([hashtable] $Request) {
    $Json = $Request | ConvertTo-Json -Compress -Depth 50
    Write-Host "stdin:  $Json"
    $Process.StandardInput.WriteLine($Json)
    $Process.StandardInput.Flush()

    while ($true) {
        $Message = Read-EngineMessage
        if ($Message.op -eq 'event') {
            $script:Events.Add($Message)
            continue
        }
        if ($Message.op -ne 'response' -or [string]$Message.id -ne [string]$Request.id) {
            Fail "expected response '$($Request.id)' but received a different message."
        }
        return $Message
    }
}

function Assert-Ok($Response, [int64] $ExpectedRevision, [string] $Label) {
    if (-not $Response.status.ok) {
        Fail "$Label failed: $($Response.status.code) $($Response.status.message)"
    }
    if ([int64]$Response.revision -ne $ExpectedRevision) {
        Fail "$Label returned revision=$($Response.revision), expected $ExpectedRevision."
    }
}

function Assert-Error($Response, [string] $Code, [int64] $ExpectedRevision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or
        [int64]$Response.revision -ne $ExpectedRevision) {
        Fail "$Label did not return $Code at revision $ExpectedRevision."
    }
}

function Read-Event([string] $Name, [int64] $ExpectedRevision, [string] $Source = '') {
    while ($true) {
        if ($script:Events.Count -gt 0) {
            $Event = $script:Events[0]
            $script:Events.RemoveAt(0)
        } else {
            $Event = Read-EngineMessage
        }
        if ($Event.op -ne 'event') {
            Fail "expected event '$Name' but received response '$($Event.id)'."
        }
        if ([string]$Event.event -ne $Name) {
            Fail "expected event '$Name' but received '$($Event.event)'."
        }
        if ([uint64]$Event.seq -ne $script:NextSeq) {
            Fail "event '$Name' seq=$($Event.seq), expected $script:NextSeq."
        }
        if ([int64]$Event.revision -ne $ExpectedRevision) {
            Fail "event '$Name' revision=$($Event.revision), expected $ExpectedRevision."
        }
        if ($Source -and [string]$Event.data.source -ne $Source) {
            Fail "event '$Name' source=$($Event.data.source), expected $Source."
        }
        if (($Event.PSObject.Properties.Name -contains 'telemetry') -and $null -ne $Event.telemetry) {
            Fail "media state event '$Name' was incorrectly marked as telemetry."
        }
        $script:NextSeq++
        return $Event
    }
}

function Read-Until-Resync([int64] $MinimumRevision) {
    while ($true) {
        if ($script:Events.Count -gt 0) {
            $Event = $script:Events[0]
            $script:Events.RemoveAt(0)
        } else {
            $Event = Read-EngineMessage
        }
        if ($Event.op -ne 'event') {
            Fail 'received a response while waiting for the overflow resync event.'
        }
        Write-Host "overflow event: $($Event.event) revision=$($Event.revision) seq=$($Event.seq)"
        if ([uint64]$Event.seq -ne $script:NextSeq) {
            Fail "overflow event seq=$($Event.seq), expected $script:NextSeq."
        }
        $script:NextSeq++
        if ([string]$Event.event -eq 'session.resyncRequired') {
            if ([int64]$Event.revision -lt $MinimumRevision -or
                [string]$Event.data.reason -ne 'event_queue_overflow') {
                Fail 'overflow resync event had an invalid revision or reason.'
            }
            return $Event
        }
    }
}

function Create-Source([string] $Id, [string] $Kind, [string] $Name, [hashtable] $Settings, [int64] $Revision) {
    $Response = Send-V2Request @{
        op = 'request'; id = $Id; method = 'source.create'; ifRevision = $Revision
        params = @{ kind = $Kind; name = $Name; settings = $Settings }
    }
    Assert-Ok $Response ($Revision + 1) $Id
    $Handle = [string]$Response.data.source
    if ($Handle -notmatch '^[1-9][0-9]*$') {
        Fail "$Id returned a non-canonical source handle '$Handle'."
    }
    $null = Read-Event 'source.created' ($Revision + 1) $Handle
    return [pscustomobject]@{ Handle = $Handle; Revision = $Revision + 1 }
}

try {
    $Ready = Read-EngineMessage
    if ($Ready.event -ne 'ready' -or [int]$Ready.protocol -ne 1) {
        Fail 'migration bootstrap ready event changed unexpectedly.'
    }

    $Hello = Send-V2Request @{ op = 'request'; id = 'task10.hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $Hello 0 'session.hello'
    $Capabilities = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($Required in @(
        'media.v1', 'media.getState.v1', 'media.play.v1', 'media.pause.v1',
        'media.togglePause.v1', 'media.stop.v1', 'media.restart.v1', 'media.next.v1',
        'media.previous.v1', 'media.getDuration.v1', 'media.getPosition.v1', 'media.setPosition.v1'
    )) {
        if ($Capabilities -notcontains $Required) {
            Fail "required capability was not advertised: $Required"
        }
    }

    $Subscribe = Send-V2Request @{
        op = 'request'; id = 'task10.subscribe'; method = 'session.subscribe'
        params = @{ subscriptions = @(@{ pattern = 'source.*' }, @{ pattern = 'media.*' }, @{ pattern = 'session.*' }) }
    }
    Assert-Ok $Subscribe 0 'session.subscribe'

    $Kinds = Send-V2Request @{ op = 'request'; id = 'task10.kinds'; method = 'source.kindList'; params = @{} }
    Assert-Ok $Kinds 0 'source.kindList'
    $MediaKind = @($Kinds.data.kinds | Where-Object { $_.id -eq 'task10_media_source' }) | Select-Object -First 1
    if ($null -eq $MediaKind -or -not [bool]$MediaKind.controllableMedia) {
        Fail 'deterministic media source kind was not registered with controllableMedia=true.'
    }
    $ColorKind = @($Kinds.data.kinds | Where-Object { $_.id -eq 'color_source_v3' }) | Select-Object -First 1
    if ($null -eq $ColorKind) {
        $ColorKind = @($Kinds.data.kinds | Where-Object { $_.id -eq 'color_source' }) | Select-Object -First 1
    }
    if ($null -eq $ColorKind) {
        Fail 'no non-media Color Source kind was available for unsupported-capability coverage.'
    }

    $Revision = [int64]0
    $Peer = Create-Source 'task10.create-peer' 'task10_media_source' 'task10-peer' @{ label = 'B' } $Revision
    $Revision = $Peer.Revision
    if ($Peer.Handle -ne '1') {
        Fail "fresh deterministic peer source expected handle 1, got $($Peer.Handle)."
    }
    $Source = Create-Source 'task10.create-source' 'task10_media_source' 'task10-source' @{
        label = 'A'; scenario = 'peer'; peerLabel = 'B'
    } $Revision
    $Revision = $Source.Revision
    if ($Source.Handle -ne '2') {
        Fail "fresh deterministic media source expected handle 2, got $($Source.Handle)."
    }

    $State = Send-V2Request @{ op = 'request'; id = 'task10.state.initial'; method = 'media.getState'; params = @{ source = $Source.Handle } }
    Assert-Ok $State $Revision 'media.getState initial'
    if ([string]$State.data.state -ne 'stopped') { Fail 'initial media state was not stopped.' }
    $GuardedQuery = Send-V2Request @{ op = 'request'; id = 'task10.state.guarded'; method = 'media.getState'; ifRevision = $Revision; params = @{ source = $Source.Handle } }
    Assert-Error $GuardedQuery 'bad_request' $Revision 'ifRevision on media.getState'
    $Missing = Send-V2Request @{ op = 'request'; id = 'task10.state.missing'; method = 'media.getState'; params = @{ source = '999' } }
    Assert-Error $Missing 'not_found' $Revision 'media.getState missing source'

    $Duration = Send-V2Request @{ op = 'request'; id = 'task10.duration'; method = 'media.getDuration'; params = @{ source = $Source.Handle } }
    Assert-Ok $Duration $Revision 'media.getDuration'
    if ([int64]$Duration.data.durationMs -ne 10000) { Fail 'deterministic media duration was not 10000 ms.' }

    $Position = Send-V2Request @{ op = 'request'; id = 'task10.position.initial'; method = 'media.getPosition'; params = @{ source = $Source.Handle } }
    Assert-Ok $Position $Revision 'media.getPosition initial'
    if ([int64]$Position.data.positionMs -ne 0) { Fail 'initial media position was not zero.' }

    $Play = Send-V2Request @{
        op = 'request'; id = 'task10.play'; method = 'media.play'; ifRevision = $Revision
        params = @{ source = $Source.Handle }
    }
    Assert-Ok $Play ($Revision + 1) 'media.play'
    $Revision++
    if ([string]$Play.data.state -ne 'playing' -or -not [bool]$Play.data.processed) { Fail 'media.play did not settle as processed/playing.' }
    $null = Read-Event 'media.stateChanged' $Revision $Source.Handle
    $null = Read-Event 'media.playing' $Revision $Source.Handle
    $null = Read-Event 'media.stateChanged' ($Revision + 1) $Peer.Handle
    $null = Read-Event 'media.started' ($Revision + 1) $Peer.Handle
    $Revision++

    $State = Send-V2Request @{ op = 'request'; id = 'task10.state.playing'; method = 'media.getState'; params = @{ source = $Source.Handle } }
    Assert-Ok $State $Revision 'media.getState playing'
    if ([string]$State.data.state -ne 'playing') { Fail 'media.getState did not report playing after settlement.' }

    $PlayNoOp = Send-V2Request @{ op = 'request'; id = 'task10.play.noop'; method = 'media.play'; params = @{ source = $Source.Handle } }
    Assert-Ok $PlayNoOp $Revision 'idempotent media.play'
    if ([bool]$PlayNoOp.data.processed) { Fail 'idempotent media.play unexpectedly processed an action.' }

    $Pause = Send-V2Request @{
        op = 'request'; id = 'task10.pause'; method = 'media.pause'; ifRevision = $Revision
        params = @{ source = $Source.Handle }
    }
    Assert-Ok $Pause ($Revision + 1) 'media.pause'
    $Revision++
    if ([string]$Pause.data.state -ne 'paused' -or -not [bool]$Pause.data.processed) { Fail 'media.pause did not settle as paused.' }
    $null = Read-Event 'media.stateChanged' $Revision $Source.Handle
    $null = Read-Event 'media.paused' $Revision $Source.Handle

    $PauseNoOp = Send-V2Request @{ op = 'request'; id = 'task10.pause.noop'; method = 'media.pause'; params = @{ source = $Source.Handle } }
    Assert-Ok $PauseNoOp $Revision 'idempotent media.pause'
    if ([bool]$PauseNoOp.data.processed) { Fail 'idempotent media.pause unexpectedly processed an action.' }

    $ToggleToPlay = Send-V2Request @{
        op = 'request'; id = 'task10.toggle.play'; method = 'media.togglePause'; ifRevision = $Revision
        params = @{ source = $Source.Handle }
    }
    Assert-Ok $ToggleToPlay ($Revision + 1) 'media.togglePause paused-to-play'
    $Revision++
    $null = Read-Event 'media.stateChanged' $Revision $Source.Handle
    $null = Read-Event 'media.playing' $Revision $Source.Handle

    $ToggleToPause = Send-V2Request @{
        op = 'request'; id = 'task10.toggle.pause'; method = 'media.togglePause'; ifRevision = $Revision
        params = @{ source = $Source.Handle }
    }
    Assert-Ok $ToggleToPause ($Revision + 1) 'media.togglePause playing-to-paused'
    $Revision++
    $null = Read-Event 'media.stateChanged' $Revision $Source.Handle
    $null = Read-Event 'media.paused' $Revision $Source.Handle

    $Seek = Send-V2Request @{
        op = 'request'; id = 'task10.seek'; method = 'media.setPosition'; ifRevision = $Revision
        params = @{ source = $Source.Handle; positionMs = 2500 }
    }
    Assert-Ok $Seek ($Revision + 1) 'media.setPosition'
    $Revision++
    if ([int64]$Seek.data.positionMs -ne 2500 -or -not [bool]$Seek.data.processed) { Fail 'media.setPosition did not settle at 2500 ms.' }
    $Position = Send-V2Request @{ op = 'request'; id = 'task10.position.seek'; method = 'media.getPosition'; params = @{ source = $Source.Handle } }
    Assert-Ok $Position $Revision 'media.getPosition after seek'
    if ([int64]$Position.data.positionMs -ne 2500) { Fail 'media.getPosition did not read back the settled seek.' }

    foreach ($Boundary in @(0, 10000)) {
        $BoundaryResponse = Send-V2Request @{
            op = 'request'; id = "task10.seek.boundary.$Boundary"; method = 'media.setPosition'; ifRevision = $Revision
            params = @{ source = $Source.Handle; positionMs = $Boundary }
        }
        Assert-Ok $BoundaryResponse ($Revision + 1) "media.setPosition boundary $Boundary"
        $Revision++
        if ([int64]$BoundaryResponse.data.positionMs -ne $Boundary) { Fail "boundary seek $Boundary did not read back exactly." }
    }
    $NegativeSeek = Send-V2Request @{ op = 'request'; id = 'task10.seek.negative'; method = 'media.setPosition'; params = @{ source = $Source.Handle; positionMs = -1 } }
    Assert-Error $NegativeSeek 'bad_request' $Revision 'negative media.setPosition'
    $LargeSeek = Send-V2Request @{ op = 'request'; id = 'task10.seek.too-large'; method = 'media.setPosition'; params = @{ source = $Source.Handle; positionMs = 10001 } }
    Assert-Error $LargeSeek 'bad_request' $Revision 'out-of-range media.setPosition'
    $WrongTypeSeek = Send-V2Request @{ op = 'request'; id = 'task10.seek.wrong-type'; method = 'media.setPosition'; params = @{ source = $Source.Handle; positionMs = 1.5 } }
    Assert-Error $WrongTypeSeek 'bad_request' $Revision 'wrong-type media.setPosition'

    $Restart = Send-V2Request @{
        op = 'request'; id = 'task10.restart'; method = 'media.restart'; ifRevision = $Revision
        params = @{ source = $Source.Handle }
    }
    Assert-Ok $Restart ($Revision + 1) 'media.restart'
    $Revision++
    if ([string]$Restart.data.state -ne 'playing' -or -not [bool]$Restart.data.processed) { Fail 'media.restart did not settle.' }
    $null = Read-Event 'media.stateChanged' $Revision $Source.Handle

    $Next = Send-V2Request @{
        op = 'request'; id = 'task10.next'; method = 'media.next'; ifRevision = $Revision
        params = @{ source = $Source.Handle }
    }
    Assert-Ok $Next ($Revision + 1) 'media.next'
    $Revision++
    if (-not [bool]$Next.data.processed) { Fail 'media.next was not reported processed.' }

    $Previous = Send-V2Request @{
        op = 'request'; id = 'task10.previous'; method = 'media.previous'; ifRevision = $Revision
        params = @{ source = $Source.Handle }
    }
    Assert-Ok $Previous ($Revision + 1) 'media.previous'
    $Revision++
    if (-not [bool]$Previous.data.processed) { Fail 'media.previous was not reported processed.' }

    $Stop = Send-V2Request @{
        op = 'request'; id = 'task10.stop'; method = 'media.stop'; ifRevision = $Revision
        params = @{ source = $Source.Handle }
    }
    Assert-Ok $Stop ($Revision + 1) 'media.stop'
    $Revision++
    $null = Read-Event 'media.stateChanged' $Revision $Source.Handle
    $null = Read-Event 'media.stopped' $Revision $Source.Handle
    $StopNoOp = Send-V2Request @{ op = 'request'; id = 'task10.stop.noop'; method = 'media.stop'; params = @{ source = $Source.Handle } }
    Assert-Ok $StopNoOp $Revision 'idempotent media.stop'
    if ([bool]$StopNoOp.data.processed) { Fail 'idempotent media.stop unexpectedly processed an action.' }

    $InvalidToggle = Send-V2Request @{ op = 'request'; id = 'task10.toggle.invalid'; method = 'media.togglePause'; params = @{ source = $Source.Handle } }
    Assert-Error $InvalidToggle 'invalid_state' $Revision 'media.togglePause from stopped'

    $EndPatch = Send-V2Request @{
        op = 'request'; id = 'task10.trigger-ended'; method = 'source.patchSettings'; ifRevision = $Revision
        params = @{ source = $Source.Handle; settings = @{ scenario = 'ended' } }
    }
    Assert-Ok $EndPatch ($Revision + 1) 'source.patchSettings ended trigger'
    $Revision++
    $null = Read-Event 'source.settingsChanged' $Revision $Source.Handle
    $null = Read-Event 'media.stateChanged' ($Revision + 1) $Source.Handle
    $null = Read-Event 'media.ended' ($Revision + 1) $Source.Handle
    $Revision++

    $ErrorPatch = Send-V2Request @{
        op = 'request'; id = 'task10.trigger-error'; method = 'source.patchSettings'; ifRevision = $Revision
        params = @{ source = $Source.Handle; settings = @{ scenario = 'error' } }
    }
    Assert-Ok $ErrorPatch ($Revision + 1) 'source.patchSettings error trigger'
    $Revision++
    $null = Read-Event 'source.settingsChanged' $Revision $Source.Handle
    $null = Read-Event 'media.stateChanged' ($Revision + 1) $Source.Handle
    $null = Read-Event 'media.ended' ($Revision + 1) $Source.Handle
    $null = Read-Event 'media.error' ($Revision + 1) $Source.Handle
    $Revision++

    $StateError = Send-V2Request @{ op = 'request'; id = 'task10.state.error'; method = 'media.getState'; params = @{ source = $Source.Handle } }
    Assert-Ok $StateError $Revision 'media.getState error'
    if ([string]$StateError.data.state -ne 'error') { Fail 'media.getState did not report error after trigger.' }

    $Stale = Send-V2Request @{
        op = 'request'; id = 'task10.stale'; method = 'media.play'; ifRevision = ($Revision - 1)
        params = @{ source = $Source.Handle }
    }
    Assert-Error $Stale 'revision_conflict' $Revision 'stale media.play'

    $Unsupported = Create-Source 'task10.create-non-media' ([string]$ColorKind.id) 'task10-non-media' @{ width = 320; height = 180 } $Revision
    $Revision = $Unsupported.Revision
    $UnsupportedState = Send-V2Request @{ op = 'request'; id = 'task10.unsupported.state'; method = 'media.getState'; params = @{ source = $Unsupported.Handle } }
    Assert-Error $UnsupportedState 'unsupported_capability' $Revision 'media.getState on non-media source'
    $UnsupportedPlay = Send-V2Request @{ op = 'request'; id = 'task10.unsupported.play'; method = 'media.play'; params = @{ source = $Unsupported.Handle } }
    Assert-Error $UnsupportedPlay 'unsupported_capability' $Revision 'media.play on non-media source'

    $NoSeek = Create-Source 'task10.create-no-seek' 'task10_media_no_seek' 'task10-no-seek' @{} $Revision
    $Revision = $NoSeek.Revision
    $Timeout = Send-V2Request @{
        op = 'request'; id = 'task10.seek.timeout'; method = 'media.setPosition'; ifRevision = $Revision
        params = @{ source = $NoSeek.Handle; positionMs = 1 }
    }
    Assert-Error $Timeout 'timeout' $Revision 'media.setPosition without a plugin callback'
    $TimeoutResync = Read-Until-Resync ($Revision + 1)
    $Revision = [int64]$TimeoutResync.revision

    $Malformed = Send-V2Request @{ op = 'request'; id = 'task10.bad-handle'; method = 'media.getState'; params = @{ source = '01' } }
    Assert-Error $Malformed 'bad_request' $Revision 'non-canonical media source handle'

    $OverflowTrigger = Send-V2Request @{
        op = 'request'; id = 'task10.overflow'; method = 'source.patchSettings'; ifRevision = $Revision
        params = @{ source = $Source.Handle; settings = @{ scenario = 'overflow' } }
    }
    Assert-Ok $OverflowTrigger ($Revision + 1) 'media deferred overflow trigger'
    $Revision++
    $Resync = Read-Until-Resync ($Revision + 1)
    $Revision = [int64]$Resync.revision

    foreach ($Entry in @(
        @{ Handle = $NoSeek.Handle; Name = 'task10.remove-no-seek' },
        @{ Handle = $Unsupported.Handle; Name = 'task10.remove-non-media' },
        @{ Handle = $Source.Handle; Name = 'task10.remove-source' },
        @{ Handle = $Peer.Handle; Name = 'task10.remove-peer' }
    )) {
        $Remove = Send-V2Request @{
            op = 'request'; id = $Entry.Name; method = 'source.remove'; ifRevision = $Revision
            params = @{ source = $Entry.Handle }
        }
        Assert-Ok $Remove ($Revision + 1) $Entry.Name
        $Revision++
        $null = Read-Event 'source.removed' $Revision $Entry.Handle
    }

    $Close = Send-V2Request @{
        op = 'request'; id = 'task10.close'; method = 'session.close'; ifRevision = $Revision; params = @{}
    }
    Assert-Ok $Close ($Revision + 1) 'session.close'
    $Process.StandardInput.Close()
    if (-not $Process.WaitForExit(30000)) {
        $Process.Kill($true)
        Fail 'obs-engine did not exit after session.close.'
    }
    if ($Process.ExitCode -ne 0) {
        Fail "obs-engine exited with code $($Process.ExitCode)."
    }

    $Stderr = $ErrorTask.GetAwaiter().GetResult()
    Write-Host '=== obs-engine stderr ==='
    Write-Host $Stderr
    if ($Stderr -notmatch '\[task10-media\] deterministic media source loaded') {
        Fail 'deterministic Task 10 module load evidence was missing from stderr.'
    }
    Write-Host 'Task 10 media namespace: PASS' -ForegroundColor Green
}
catch {
    if ($null -ne $Process -and -not $Process.HasExited) {
        try { $Process.Kill($true) } catch {}
        try { $Process.WaitForExit(5000) | Out-Null } catch {}
    }
    if ($null -ne $ErrorTask) {
        try {
            $Stderr = $ErrorTask.GetAwaiter().GetResult()
            if ($Stderr) { Write-Host "=== obs-engine stderr ===`n$Stderr" }
        } catch {}
    }
    throw
}
finally {
    if ($null -ne $Process) {
        if (-not $Process.HasExited) {
            try { $Process.StandardInput.Close() } catch {}
            try { $Process.Kill($true) } catch {}
            try { $Process.WaitForExit(5000) | Out-Null } catch {}
        }
        $Process.Dispose()
    }
}

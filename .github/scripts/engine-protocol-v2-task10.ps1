param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Process = $null
$script:ErrorTask = $null
$script:Events = [System.Collections.Generic.List[object]]::new()
$script:NextSeq = [uint64]1

function Start-Task10Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path $Root).Path
    $engine = Get-ChildItem -Path $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
    if ($null -eq $engine) {
        throw 'obs-engine.exe was not found in the runtime root.'
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $engine.FullName
    $startInfo.WorkingDirectory = $engine.Directory.FullName
    $startInfo.ArgumentList.Add('--plugin=task10-media-source')
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
    $script:Events = [System.Collections.Generic.List[object]]::new()
    $script:NextSeq = [uint64]1
}

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

function Read-PendingEvent {
    if ($script:Events.Count -gt 0) {
        $Event = $script:Events[0]
        $script:Events.RemoveAt(0)
    } else {
        $Event = Read-EngineMessage
    }
    return $Event
}

function Assert-EventSequence($Event, [string] $Label) {
    if ($Event.op -ne 'event') {
        Fail "$Label expected an event but received response '$($Event.id)'."
    }
    if ([uint64]$Event.seq -ne $script:NextSeq) {
        Fail "$Label seq=$($Event.seq), expected $script:NextSeq."
    }
    $script:NextSeq++
}

function Read-Event([string] $Name, [int64] $ExpectedRevision, [string] $Source = '') {
	$Event = Read-PendingEvent
	Assert-EventSequence $Event "event '$Name'"
	if ([string]$Event.event -ne $Name) {
		Fail "expected event '$Name' but received '$($Event.event)'."
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
	return $Event
}

function Read-Until-Resync([int64] $MinimumRevision) {
	while ($true) {
		$Event = Read-PendingEvent
		Assert-EventSequence $Event 'overflow event'
		Write-Host "overflow event: $($Event.event) revision=$($Event.revision) seq=$($Event.seq)"
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

function Assert-Task10Capabilities($Hello) {
    $capabilities = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($required in @(
        'media.v1', 'media.getState.v1', 'media.play.v1', 'media.pause.v1',
        'media.togglePause.v1', 'media.stop.v1', 'media.restart.v1', 'media.next.v1',
        'media.previous.v1', 'media.getDuration.v1', 'media.getPosition.v1', 'media.setPosition.v1'
    )) {
        if ($capabilities -notcontains $required) {
            Fail "required capability was not advertised: $required"
        }
    }
}

function Get-Task10ColorKind($Kinds) {
    $colorKind = @($Kinds.data.kinds | Where-Object { $_.id -eq 'color_source_v3' }) | Select-Object -First 1
    if ($null -eq $colorKind) {
        $colorKind = @($Kinds.data.kinds | Where-Object { $_.id -eq 'color_source' }) | Select-Object -First 1
    }
    if ($null -eq $colorKind) {
        Fail 'no non-media Color Source kind was available for unsupported-capability coverage.'
    }
    return $colorKind
}

function Initialize-Task10Session {
    $ready = Read-EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        Fail 'migration bootstrap ready event changed unexpectedly.'
    }

    $hello = Send-V2Request @{ op = 'request'; id = 'task10.hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $hello 0 'session.hello'
    Assert-Task10Capabilities $hello

    $subscribe = Send-V2Request @{
        op = 'request'; id = 'task10.subscribe'; method = 'session.subscribe'
        params = @{ subscriptions = @(@{ pattern = 'source.*' }, @{ pattern = 'media.*' }, @{ pattern = 'session.*' }) }
    }
    Assert-Ok $subscribe 0 'session.subscribe'

    $kinds = Send-V2Request @{ op = 'request'; id = 'task10.kinds'; method = 'source.kindList'; params = @{} }
    Assert-Ok $kinds 0 'source.kindList'
    $mediaKind = @($kinds.data.kinds | Where-Object { $_.id -eq 'task10_media_source' }) | Select-Object -First 1
    if ($null -eq $mediaKind -or -not [bool]$mediaKind.controllableMedia) {
        Fail 'deterministic media source kind was not registered with controllableMedia=true.'
    }
    return Get-Task10ColorKind $kinds
}

function Initialize-Task10Sources([object] $ColorKind) {
    $state = [pscustomobject]@{
        Revision = [int64]0
        Peer = $null
        Source = $null
        Prequeue = $null
        Unsupported = $null
        NoSeek = $null
        LateSeek = $null
        BlockingFollowup = $null
        BlockingLate = $null
        RemovalOutstanding = $null
        ColorKind = $ColorKind
    }
    $state.Peer = Create-Source 'task10.create-peer' 'task10_media_source' 'task10-peer' @{ label = 'B' } $state.Revision
    $state.Revision = $state.Peer.Revision
    if ($state.Peer.Handle -ne '1') {
        Fail "fresh deterministic peer source expected handle 1, got $($state.Peer.Handle)."
    }
    $state.Source = Create-Source 'task10.create-source' 'task10_media_source' 'task10-source' @{
        label = 'A'; scenario = 'peer'; peerLabel = 'B'
    } $state.Revision
    $state.Revision = $state.Source.Revision
    if ($state.Source.Handle -ne '2') {
        Fail "fresh deterministic media source expected handle 2, got $($state.Source.Handle)."
    }

    $state.Prequeue = Create-Source 'task10.create-prequeue' 'task10_media_source' 'task10-prequeue' @{
        label = 'prequeue'; scenario = 'prequeue'
    } $state.Revision
    $state.Revision = $state.Prequeue.Revision
    $prequeuedSeek = Send-V2Request @{
        op = 'request'; id = 'task10.seek.after-prequeue'; method = 'media.setPosition'; ifRevision = $state.Revision
        params = @{ source = $state.Prequeue.Handle; positionMs = 2000 }
    }
    Assert-Ok $prequeuedSeek ($state.Revision + 1) 'media.setPosition after pre-existing same-source seek'
    $state.Revision++
    if ([int64]$prequeuedSeek.data.positionMs -ne 2000 -or -not [bool]$prequeuedSeek.data.processed) {
        Fail 'media.setPosition claimed the wrong pre-existing same-source action.'
    }
    $prequeueResync = Read-Until-Resync ($state.Revision + 1)
    $state.Revision = [int64]$prequeueResync.revision

    $initialState = Send-V2Request @{ op = 'request'; id = 'task10.state.initial'; method = 'media.getState'; params = @{ source = $state.Source.Handle } }
    Assert-Ok $initialState $state.Revision 'media.getState initial'
    if ([string]$initialState.data.state -ne 'stopped') { Fail 'initial media state was not stopped.' }
    $guardedQuery = Send-V2Request @{ op = 'request'; id = 'task10.state.guarded'; method = 'media.getState'; ifRevision = $state.Revision; params = @{ source = $state.Source.Handle } }
    Assert-Error $guardedQuery 'bad_request' $state.Revision 'ifRevision on media.getState'
    $missing = Send-V2Request @{ op = 'request'; id = 'task10.state.missing'; method = 'media.getState'; params = @{ source = '999' } }
    Assert-Error $missing 'not_found' $state.Revision 'media.getState missing source'

    $duration = Send-V2Request @{ op = 'request'; id = 'task10.duration'; method = 'media.getDuration'; params = @{ source = $state.Source.Handle } }
    Assert-Ok $duration $state.Revision 'media.getDuration'
    if ([int64]$duration.data.durationMs -ne 10000) { Fail 'deterministic media duration was not 10000 ms.' }
    $position = Send-V2Request @{ op = 'request'; id = 'task10.position.initial'; method = 'media.getPosition'; params = @{ source = $state.Source.Handle } }
    Assert-Ok $position $state.Revision 'media.getPosition initial'
    if ([int64]$position.data.positionMs -ne 0) { Fail 'initial media position was not zero.' }
    return $state
}

function Invoke-Task10PlayPause([object] $State) {
    $play = Send-V2Request @{
        op = 'request'; id = 'task10.play'; method = 'media.play'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle }
    }
    Assert-Ok $play ($State.Revision + 1) 'media.play'
    $State.Revision++
    if ([string]$play.data.state -ne 'playing' -or -not [bool]$play.data.processed) { Fail 'media.play did not settle as processed/playing.' }
    $null = Read-Event 'media.stateChanged' $State.Revision $State.Source.Handle
    $null = Read-Event 'media.playing' $State.Revision $State.Source.Handle
    $null = Read-Event 'media.stateChanged' ($State.Revision + 1) $State.Peer.Handle
    $null = Read-Event 'media.started' ($State.Revision + 1) $State.Peer.Handle
    $State.Revision++

    $playingState = Send-V2Request @{ op = 'request'; id = 'task10.state.playing'; method = 'media.getState'; params = @{ source = $State.Source.Handle } }
    Assert-Ok $playingState $State.Revision 'media.getState playing'
    if ([string]$playingState.data.state -ne 'playing') { Fail 'media.getState did not report playing after settlement.' }
    $playNoOp = Send-V2Request @{ op = 'request'; id = 'task10.play.noop'; method = 'media.play'; params = @{ source = $State.Source.Handle } }
    Assert-Ok $playNoOp $State.Revision 'idempotent media.play'
    if ([bool]$playNoOp.data.processed) { Fail 'idempotent media.play unexpectedly processed an action.' }

    $pause = Send-V2Request @{
        op = 'request'; id = 'task10.pause'; method = 'media.pause'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle }
    }
    Assert-Ok $pause ($State.Revision + 1) 'media.pause'
    $State.Revision++
    if ([string]$pause.data.state -ne 'paused' -or -not [bool]$pause.data.processed) { Fail 'media.pause did not settle as paused.' }
    $null = Read-Event 'media.stateChanged' $State.Revision $State.Source.Handle
    $null = Read-Event 'media.paused' $State.Revision $State.Source.Handle
    $pauseNoOp = Send-V2Request @{ op = 'request'; id = 'task10.pause.noop'; method = 'media.pause'; params = @{ source = $State.Source.Handle } }
    Assert-Ok $pauseNoOp $State.Revision 'idempotent media.pause'
    if ([bool]$pauseNoOp.data.processed) { Fail 'idempotent media.pause unexpectedly processed an action.' }
}

function Invoke-Task10ToggleAndSeek([object] $State) {
    $toggleToPlay = Send-V2Request @{
        op = 'request'; id = 'task10.toggle.play'; method = 'media.togglePause'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle }
    }
    Assert-Ok $toggleToPlay ($State.Revision + 1) 'media.togglePause paused-to-play'
    $State.Revision++
    $null = Read-Event 'media.stateChanged' $State.Revision $State.Source.Handle
    $null = Read-Event 'media.playing' $State.Revision $State.Source.Handle

    $toggleToPause = Send-V2Request @{
        op = 'request'; id = 'task10.toggle.pause'; method = 'media.togglePause'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle }
    }
    Assert-Ok $toggleToPause ($State.Revision + 1) 'media.togglePause playing-to-paused'
    $State.Revision++
    $null = Read-Event 'media.stateChanged' $State.Revision $State.Source.Handle
    $null = Read-Event 'media.paused' $State.Revision $State.Source.Handle

    $seek = Send-V2Request @{
        op = 'request'; id = 'task10.seek'; method = 'media.setPosition'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle; positionMs = 2500 }
    }
    Assert-Ok $seek ($State.Revision + 1) 'media.setPosition'
    $State.Revision++
    if ([int64]$seek.data.positionMs -ne 2500 -or -not [bool]$seek.data.processed) { Fail 'media.setPosition did not settle at 2500 ms.' }
    $position = Send-V2Request @{ op = 'request'; id = 'task10.position.seek'; method = 'media.getPosition'; params = @{ source = $State.Source.Handle } }
    Assert-Ok $position $State.Revision 'media.getPosition after seek'
    if ([int64]$position.data.positionMs -ne 2500) { Fail 'media.getPosition did not read back the settled seek.' }

    foreach ($boundary in @(0, 10000)) {
        $boundaryResponse = Send-V2Request @{
            op = 'request'; id = "task10.seek.boundary.$boundary"; method = 'media.setPosition'; ifRevision = $State.Revision
            params = @{ source = $State.Source.Handle; positionMs = $boundary }
        }
        Assert-Ok $boundaryResponse ($State.Revision + 1) "media.setPosition boundary $boundary"
        $State.Revision++
        if ([int64]$boundaryResponse.data.positionMs -ne $boundary) { Fail "boundary seek $boundary did not read back exactly." }
    }
    $equalSeek = Send-V2Request @{
        op = 'request'; id = 'task10.seek.equal-noop'; method = 'media.setPosition'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle; positionMs = 10000 }
    }
    Assert-Ok $equalSeek $State.Revision 'equal-position media.setPosition no-op'
    if ([bool]$equalSeek.data.processed) { Fail 'equal-position media.setPosition unexpectedly processed an action.' }
    $negativeSeek = Send-V2Request @{ op = 'request'; id = 'task10.seek.negative'; method = 'media.setPosition'; params = @{ source = $State.Source.Handle; positionMs = -1 } }
    Assert-Error $negativeSeek 'bad_request' $State.Revision 'negative media.setPosition'
    $largeSeek = Send-V2Request @{ op = 'request'; id = 'task10.seek.too-large'; method = 'media.setPosition'; params = @{ source = $State.Source.Handle; positionMs = 10001 } }
    Assert-Error $largeSeek 'bad_request' $State.Revision 'out-of-range media.setPosition'
    $wrongTypeSeek = Send-V2Request @{ op = 'request'; id = 'task10.seek.wrong-type'; method = 'media.setPosition'; params = @{ source = $State.Source.Handle; positionMs = 1.5 } }
    Assert-Error $wrongTypeSeek 'bad_request' $State.Revision 'wrong-type media.setPosition'
}

function Invoke-Task10RestartAndStop([object] $State) {
    $restart = Send-V2Request @{
        op = 'request'; id = 'task10.restart'; method = 'media.restart'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle }
    }
    Assert-Ok $restart ($State.Revision + 1) 'media.restart'
    $State.Revision++
    if ([string]$restart.data.state -ne 'playing' -or -not [bool]$restart.data.processed) { Fail 'media.restart did not settle.' }
    $null = Read-Event 'media.stateChanged' $State.Revision $State.Source.Handle

    $next = Send-V2Request @{
        op = 'request'; id = 'task10.next'; method = 'media.next'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle }
    }
    Assert-Ok $next ($State.Revision + 1) 'media.next'
    $State.Revision++
    if (-not [bool]$next.data.processed) { Fail 'media.next was not reported processed.' }
    $previous = Send-V2Request @{
        op = 'request'; id = 'task10.previous'; method = 'media.previous'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle }
    }
    Assert-Ok $previous ($State.Revision + 1) 'media.previous'
    $State.Revision++
    if (-not [bool]$previous.data.processed) { Fail 'media.previous was not reported processed.' }

    $stop = Send-V2Request @{
        op = 'request'; id = 'task10.stop'; method = 'media.stop'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle }
    }
    Assert-Ok $stop ($State.Revision + 1) 'media.stop'
    $State.Revision++
    $null = Read-Event 'media.stateChanged' $State.Revision $State.Source.Handle
    $null = Read-Event 'media.stopped' $State.Revision $State.Source.Handle
    $stopNoOp = Send-V2Request @{ op = 'request'; id = 'task10.stop.noop'; method = 'media.stop'; params = @{ source = $State.Source.Handle } }
    Assert-Ok $stopNoOp $State.Revision 'idempotent media.stop'
    if ([bool]$stopNoOp.data.processed) { Fail 'idempotent media.stop unexpectedly processed an action.' }
    $invalidToggle = Send-V2Request @{ op = 'request'; id = 'task10.toggle.invalid'; method = 'media.togglePause'; params = @{ source = $State.Source.Handle } }
    Assert-Error $invalidToggle 'invalid_state' $State.Revision 'media.togglePause from stopped'
}

function Invoke-Task10SourceTransitions([object] $State) {
    $endPatch = Send-V2Request @{
        op = 'request'; id = 'task10.trigger-ended'; method = 'source.patchSettings'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle; settings = @{ scenario = 'ended' } }
    }
    Assert-Ok $endPatch ($State.Revision + 1) 'source.patchSettings ended trigger'
    $State.Revision++
    $null = Read-Event 'source.settingsChanged' $State.Revision $State.Source.Handle
    $null = Read-Event 'media.stateChanged' ($State.Revision + 1) $State.Source.Handle
    $null = Read-Event 'media.ended' ($State.Revision + 1) $State.Source.Handle
    $State.Revision++

    $errorPatch = Send-V2Request @{
        op = 'request'; id = 'task10.trigger-error'; method = 'source.patchSettings'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle; settings = @{ scenario = 'error' } }
    }
    Assert-Ok $errorPatch ($State.Revision + 1) 'source.patchSettings error trigger'
    $State.Revision++
    $null = Read-Event 'source.settingsChanged' $State.Revision $State.Source.Handle
    $null = Read-Event 'media.stateChanged' ($State.Revision + 1) $State.Source.Handle
    $null = Read-Event 'media.ended' ($State.Revision + 1) $State.Source.Handle
    $null = Read-Event 'media.error' ($State.Revision + 1) $State.Source.Handle
    $State.Revision++

    $stateError = Send-V2Request @{ op = 'request'; id = 'task10.state.error'; method = 'media.getState'; params = @{ source = $State.Source.Handle } }
    Assert-Ok $stateError $State.Revision 'media.getState error'
    if ([string]$stateError.data.state -ne 'error') { Fail 'media.getState did not report error after trigger.' }
    $stale = Send-V2Request @{
        op = 'request'; id = 'task10.stale'; method = 'media.play'; ifRevision = ($State.Revision - 1)
        params = @{ source = $State.Source.Handle }
    }
    Assert-Error $stale 'revision_conflict' $State.Revision 'stale media.play'
    $stateAfterStale = Send-V2Request @{ op = 'request'; id = 'task10.state.after-stale'; method = 'media.getState'; params = @{ source = $State.Source.Handle } }
    Assert-Ok $stateAfterStale $State.Revision 'media.getState after stale media.play'
    if ([string]$stateAfterStale.data.state -ne 'error') { Fail 'stale media.play changed media state.' }
    $staleMediaEvents = @($script:Events | Where-Object {
        [string]$_.event -like 'media.*' -and [string]$_.data.source -eq $State.Source.Handle
    })
    if ($staleMediaEvents.Count -ne 0) { Fail 'stale media.play produced media events or queued work.' }
}

function Invoke-Task10UnsupportedAndLateSeek([object] $State) {
    $State.Unsupported = Create-Source 'task10.create-non-media' ([string]$State.ColorKind.id) 'task10-non-media' @{ width = 320; height = 180 } $State.Revision
    $State.Revision = $State.Unsupported.Revision
    $unsupportedState = Send-V2Request @{ op = 'request'; id = 'task10.unsupported.state'; method = 'media.getState'; params = @{ source = $State.Unsupported.Handle } }
    Assert-Error $unsupportedState 'unsupported_capability' $State.Revision 'media.getState on non-media source'
    $unsupportedPlay = Send-V2Request @{ op = 'request'; id = 'task10.unsupported.play'; method = 'media.play'; params = @{ source = $State.Unsupported.Handle } }
    Assert-Error $unsupportedPlay 'unsupported_capability' $State.Revision 'media.play on non-media source'

    $State.NoSeek = Create-Source 'task10.create-no-seek' 'task10_media_no_seek' 'task10-no-seek' @{} $State.Revision
    $State.Revision = $State.NoSeek.Revision
    $timeout = Send-V2Request @{
        op = 'request'; id = 'task10.seek.timeout'; method = 'media.setPosition'; ifRevision = $State.Revision
        params = @{ source = $State.NoSeek.Handle; positionMs = 1 }
    }
    Assert-Error $timeout 'timeout' $State.Revision 'media.setPosition without a plugin callback'
    $timeoutResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$timeoutResync.revision

    $State.LateSeek = Create-Source 'task10.create-late-seek' 'task10_media_source' 'task10-late-seek' @{
        label = 'late-seek'; scenario = 'blockTimeAutoRelease'
    } $State.Revision
    $State.Revision = $State.LateSeek.Revision
    $lateSeekTimeout = Send-V2Request @{
        op = 'request'; id = 'task10.seek.late-timeout'; method = 'media.setPosition'; ifRevision = $State.Revision
        params = @{ source = $State.LateSeek.Handle; positionMs = 1000 }
    }
    Assert-Error $lateSeekTimeout 'timeout' $State.Revision 'media.setPosition with late completion'
    $lateSeekTimeoutResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$lateSeekTimeoutResync.revision
    $latePosition = Send-V2Request @{ op = 'request'; id = 'task10.position.late-seek'; method = 'media.getPosition'; params = @{ source = $State.LateSeek.Handle } }
    if (-not $latePosition.status.ok -or [int64]$latePosition.data.positionMs -ne 1000) {
        Fail 'timed-out setPosition did not eventually reach the fixture callback.'
    }
    $State.Revision = [int64]$latePosition.revision
    $lateSeekOrphanResync = Read-Until-Resync $State.Revision
    $State.Revision = [int64]$lateSeekOrphanResync.revision
    $removeLateSeek = Send-V2Request @{
        op = 'request'; id = 'task10.remove-late-seek'; method = 'source.remove'; ifRevision = $State.Revision
        params = @{ source = $State.LateSeek.Handle }
    }
    Assert-Ok $removeLateSeek ($State.Revision + 1) 'source.remove after late setPosition completion'
    $State.Revision++
    $null = Read-Event 'source.removed' $State.Revision $State.LateSeek.Handle
}

function Invoke-Task10BlockingFollowup([object] $State) {
    $State.BlockingFollowup = Create-Source 'task10.create-blocking-followup' 'task10_media_source' 'task10-blocking-followup' @{
        label = 'blocking-followup'; scenario = 'blockSignalFollowup'
    } $State.Revision
    $State.Revision = $State.BlockingFollowup.Revision
    $blockedFollowupPlay = Send-V2Request @{
        op = 'request'; id = 'task10.play.blocking-followup'; method = 'media.play'; ifRevision = $State.Revision
        params = @{ source = $State.BlockingFollowup.Handle }
    }
    Assert-Error $blockedFollowupPlay 'timeout' $State.Revision 'media.play before same-source follow-up action'
    $followupTimeoutResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$followupTimeoutResync.revision
    $followupPause = Send-V2Request @{
        op = 'request'; id = 'task10.pause.following-orphan'; method = 'media.pause'; ifRevision = $State.Revision
        params = @{ source = $State.BlockingFollowup.Handle }
    }
    Assert-Ok $followupPause ($State.Revision + 1) 'media.pause while prior media.play is unresolved'
    $State.Revision++
    if ([string]$followupPause.data.state -ne 'paused' -or -not [bool]$followupPause.data.processed) {
        Fail 'same-source follow-up media.pause did not settle on its own action ticket.'
    }
    $null = Read-Event 'media.paused' $State.Revision $State.BlockingFollowup.Handle
    $followupOrphanResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$followupOrphanResync.revision
    $null = Read-Event 'media.stateChanged' ($State.Revision + 1) $State.BlockingFollowup.Handle
    $null = Read-Event 'media.paused' ($State.Revision + 1) $State.BlockingFollowup.Handle
    $State.Revision++
}

function Invoke-Task10BlockingSignal([object] $State) {
    $State.BlockingLate = Create-Source 'task10.create-blocking-signal' 'task10_media_source' 'task10-blocking-signal' @{
        label = 'blocking-signal'; scenario = 'blockSignal'
    } $State.Revision
    $State.Revision = $State.BlockingLate.Revision
    $blockedPlay = Send-V2Request @{
        op = 'request'; id = 'task10.play.blocking-signal'; method = 'media.play'; ifRevision = $State.Revision
        params = @{ source = $State.BlockingLate.Handle }
    }
    Assert-Error $blockedPlay 'timeout' $State.Revision 'media.play with blocking signal callback'
    $blockingTimeoutResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$blockingTimeoutResync.revision
    $releasedState = Send-V2Request @{ op = 'request'; id = 'task10.state.release-blocking-signal'; method = 'media.getState'; params = @{ source = $State.BlockingLate.Handle } }
    if (-not $releasedState.status.ok -or [string]$releasedState.data.state -ne 'playing') {
        Fail 'blocking-signal source did not release to the expected playing state.'
    }
    $State.Revision = [int64]$releasedState.revision
    $lateOrphanResync = Read-Until-Resync $State.Revision
    $State.Revision = [int64]$lateOrphanResync.revision
    $pauseAfterOrphan = Send-V2Request @{
        op = 'request'; id = 'task10.pause.after-orphan'; method = 'media.pause'; ifRevision = $State.Revision
        params = @{ source = $State.BlockingLate.Handle }
    }
    Assert-Ok $pauseAfterOrphan ($State.Revision + 1) 'media.pause after orphan completion'
    $State.Revision++
    $null = Read-Event 'media.stateChanged' $State.Revision $State.BlockingLate.Handle
    $null = Read-Event 'media.paused' $State.Revision $State.BlockingLate.Handle
}

function Invoke-Task10RemovalOutstanding([object] $State) {
    $State.RemovalOutstanding = Create-Source 'task10.create-removal-outstanding' 'task10_media_source' 'task10-removal-outstanding' @{
        label = 'removal-outstanding'; scenario = 'blockSignalAutoRelease'
    } $State.Revision
    $State.Revision = $State.RemovalOutstanding.Revision
    $removalTimeout = Send-V2Request @{
        op = 'request'; id = 'task10.play.removal-outstanding'; method = 'media.play'; ifRevision = $State.Revision
        params = @{ source = $State.RemovalOutstanding.Handle }
    }
    Assert-Error $removalTimeout 'timeout' $State.Revision 'media.play before source removal with outstanding callback'
    $removalTimeoutResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$removalTimeoutResync.revision
    $removeOutstanding = Send-V2Request @{
        op = 'request'; id = 'task10.remove.outstanding'; method = 'source.remove'; ifRevision = $State.Revision
        params = @{ source = $State.RemovalOutstanding.Handle }
    }
    Assert-Ok $removeOutstanding ($State.Revision + 1) 'source.remove with timed-out media callback'
    $State.Revision++
    $null = Read-Event 'source.removed' $State.Revision $State.RemovalOutstanding.Handle
}

function Remove-Task10Sources([object] $State) {
    foreach ($entry in @(
        @{ Handle = $State.Prequeue.Handle; Name = 'task10.remove-prequeue' },
        @{ Handle = $State.BlockingFollowup.Handle; Name = 'task10.remove-blocking-followup' },
        @{ Handle = $State.BlockingLate.Handle; Name = 'task10.remove-blocking-signal' },
        @{ Handle = $State.NoSeek.Handle; Name = 'task10.remove-no-seek' },
        @{ Handle = $State.Unsupported.Handle; Name = 'task10.remove-non-media' },
        @{ Handle = $State.Source.Handle; Name = 'task10.remove-source' },
        @{ Handle = $State.Peer.Handle; Name = 'task10.remove-peer' }
    )) {
        $remove = Send-V2Request @{
            op = 'request'; id = $entry.Name; method = 'source.remove'; ifRevision = $State.Revision
            params = @{ source = $entry.Handle }
        }
        Assert-Ok $remove ($State.Revision + 1) $entry.Name
        $State.Revision++
        $null = Read-Event 'source.removed' $State.Revision $entry.Handle
    }
}

function Complete-Task10Scenario([object] $State) {
    $malformed = Send-V2Request @{ op = 'request'; id = 'task10.bad-handle'; method = 'media.getState'; params = @{ source = '01' } }
    Assert-Error $malformed 'bad_request' $State.Revision 'non-canonical media source handle'
    $overflowTrigger = Send-V2Request @{
        op = 'request'; id = 'task10.overflow'; method = 'source.patchSettings'; ifRevision = $State.Revision
        params = @{ source = $State.Source.Handle; settings = @{ scenario = 'overflow' } }
    }
    Assert-Ok $overflowTrigger ($State.Revision + 1) 'media deferred overflow trigger'
    $State.Revision++
    $resync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$resync.revision
    Remove-Task10Sources $State

    $shutdownOutstanding = Create-Source 'task10.create-shutdown-outstanding' 'task10_media_source' 'task10-shutdown-outstanding' @{
        label = 'shutdown-outstanding'; scenario = 'blockSignalAutoRelease'
    } $State.Revision
    $State.Revision = $shutdownOutstanding.Revision
    $shutdownTimeout = Send-V2Request @{
        op = 'request'; id = 'task10.play.shutdown-outstanding'; method = 'media.play'; ifRevision = $State.Revision
        params = @{ source = $shutdownOutstanding.Handle }
    }
    Assert-Error $shutdownTimeout 'timeout' $State.Revision 'media.play before shutdown with outstanding callback'
    $shutdownTimeoutResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$shutdownTimeoutResync.revision

    $close = Send-V2Request @{
        op = 'request'; id = 'task10.close'; method = 'session.close'; ifRevision = $State.Revision; params = @{}
    }
    Assert-Ok $close ($State.Revision + 1) 'session.close'
    $script:Process.StandardInput.Close()
    if (-not $script:Process.WaitForExit(30000)) {
        $script:Process.Kill($true)
        Fail 'obs-engine did not exit after session.close.'
    }
    if ($script:Process.ExitCode -ne 0) {
        Fail "obs-engine exited with code $($script:Process.ExitCode)."
    }
    $stderr = $script:ErrorTask.GetAwaiter().GetResult()
    Write-Host '=== obs-engine stderr ==='
    Write-Host $stderr
    if ($stderr -notmatch '\[task10-media\] deterministic media source loaded') {
        Fail 'deterministic Task 10 module load evidence was missing from stderr.'
    }
    Write-Host 'Task 10 media namespace: PASS' -ForegroundColor Green
}

function Invoke-Task10Scenario {
    $colorKind = Initialize-Task10Session
    $state = Initialize-Task10Sources $colorKind
    Invoke-Task10PlayPause $state
    Invoke-Task10ToggleAndSeek $state
    Invoke-Task10RestartAndStop $state
    Invoke-Task10SourceTransitions $state
    Invoke-Task10UnsupportedAndLateSeek $state
    Invoke-Task10BlockingFollowup $state
    Invoke-Task10BlockingSignal $state
    Invoke-Task10RemovalOutstanding $state
    Complete-Task10Scenario $state
}

function Stop-Task10AfterFailure {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) {
        try { $script:Process.Kill($true) } catch {}
        try { $script:Process.WaitForExit(5000) | Out-Null } catch {}
    }
    if ($null -ne $script:ErrorTask) {
        try {
            $stderr = $script:ErrorTask.GetAwaiter().GetResult()
            if ($stderr) { Write-Host "=== obs-engine stderr ===`n$stderr" }
        } catch {}
    }
}

function Stop-Task10Engine {
    if ($null -eq $script:Process) {
        return
    }
    if (-not $script:Process.HasExited) {
        try { $script:Process.StandardInput.Close() } catch {}
        try { $script:Process.Kill($true) } catch {}
        try { $script:Process.WaitForExit(5000) | Out-Null } catch {}
    }
    $script:Process.Dispose()
    $script:Process = $null
}

try {
    Start-Task10Engine $InstallRoot
    Invoke-Task10Scenario
}
catch {
    Stop-Task10AfterFailure
    throw
}
finally {
    Stop-Task10Engine
}

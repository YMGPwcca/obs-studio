param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Process = $null
$script:ErrorTask = $null
$script:Events = [System.Collections.Generic.List[object]]::new()
$script:Wire = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:LastResponseIndex = -1
$script:LastMessage = $null
$script:ProgressEvents = [System.Collections.Generic.List[object]]::new()

function Fail-Task18([string] $Message) { throw "Task 18: $Message" }

function Start-Task18Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task18 'obs-engine.exe was not found.' }
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Process = [System.Diagnostics.Process]::new()
    $script:Process.StartInfo = $info
    if (-not $script:Process.Start()) { Fail-Task18 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task18Engine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task18 'fallback canvas warning was emitted.' }
    if ($script:Process.ExitCode -ne 0) { Fail-Task18 "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task18Message {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task18 'timed out waiting for stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task18 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task18Request([hashtable] $Request) {
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task18Message
        $script:LastMessage = $message
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task18 "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Send-Task18Guarded([string] $Id, [string] $Method, [hashtable] $Params, [int64] $Revision) {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $attemptId = if ($attempt -eq 0) { $Id } else { "$Id-retry$attempt" }
        $request = @{ op = 'request'; id = $attemptId; method = $Method; ifRevision = $Revision }
        if ($null -ne $Params) { $request.params = $Params }
        $response = Send-Task18Request $request
        if ($response.status.ok -or [string]$response.status.code -ne 'revision_conflict') {
            $response | Add-Member -NotePropertyName GuardRevision -NotePropertyValue $Revision -Force
            return $response
        }
        $Revision = [int64]$response.revision
    }
    return $response
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task18 "$Label failed at revision $($Response.revision)." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task18 "$Label did not return $Code at revision $Revision." }
}

function Take-Task18Event {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0); return $event }
    return Read-Task18Message
}

function Record-Task18Progress($Event) {
    if ($Event.op -ne 'event' -or [uint64]$Event.seq -ne $script:NextSequence -or $Event.telemetry -ne $true) { Fail-Task18 'transition.progress envelope was invalid.' }
    $script:ProgressEvents.Add($Event)
    $script:NextSequence++
}

function Assert-Task18ExpectedEvent($Event, [string] $Name, [int64] $Revision) {
    if ($Event.op -ne 'event' -or [string]$Event.event -ne $Name -or [uint64]$Event.seq -ne $script:NextSequence -or [int64]$Event.revision -ne $Revision) { Fail-Task18 "unexpected event; expected $Name at revision $Revision." }
}

function Assert-Task18EventAfterResponse($Event, [string] $Name) {
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$Event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task18 "event $Name preceded its response." }
}

function Read-Task18Event([string] $Name, [int64] $Revision, [bool] $AllowBeforeResponse = $false) {
    while ($true) {
        $event = Take-Task18Event
        if ([string]$event.event -eq 'transition.progress') { Record-Task18Progress $event; continue }
        Assert-Task18ExpectedEvent $event $Name $Revision
        $script:NextSequence++
        if (-not $AllowBeforeResponse) { Assert-Task18EventAfterResponse $event $Name }
        return $event
    }
}

function Invoke-Task18Bootstrap {
    Start-Task18Engine $InstallRoot
    $ready = Read-Task18Message
    if ($ready.event -ne 'ready') { Fail-Task18 'ready marker was not received.' }
    Assert-Ok (Send-Task18Request @{ op = 'request'; id = 's-hello'; method = 'session.hello' }) 0 'hello'
    Assert-Ok (Send-Task18Request @{ op = 'request'; id = 's-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'scene.*' }, @{ pattern = 'program.*' }, @{ pattern = 'preview.*' }, @{ pattern = 'transition.*' }, @{ pattern = 'studio.*' }) } }) 0 'subscribe'
    $script:T18SceneA = Send-Task18Request @{ op = 'request'; id = 's-a'; method = 'scene.create'; params = @{ name = 'Studio A' } }
    Assert-Ok $script:T18SceneA 1 'scene A'
    Read-Task18Event 'scene.created' 1 | Out-Null
    $script:T18SceneB = Send-Task18Guarded 's-b' 'scene.create' @{ name = 'Studio B' } 1
    Assert-Ok $script:T18SceneB ($script:T18SceneB.GuardRevision + 1) 'scene B'
    $script:T18Revision = [int64]$script:T18SceneB.revision
    Read-Task18Event 'scene.created' $script:T18Revision | Out-Null
    $script:T18SceneC = Send-Task18Guarded 's-c' 'scene.create' @{ name = 'Studio C' } $script:T18Revision
    Assert-Ok $script:T18SceneC ($script:T18SceneC.GuardRevision + 1) 'scene C'
    $script:T18Revision = [int64]$script:T18SceneC.revision
    Read-Task18Event 'scene.created' $script:T18Revision | Out-Null
    $programA = Send-Task18Guarded 's-program-a' 'program.setScene' @{ scene = [string]$script:T18SceneA.data.scene } $script:T18Revision
    Assert-Ok $programA ($programA.GuardRevision + 1) 'Program A'
    $script:T18Revision = [int64]$programA.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
    $previewB = Send-Task18Guarded 's-preview-b' 'preview.setScene' @{ scene = [string]$script:T18SceneB.data.scene } $script:T18Revision
    Assert-Ok $previewB ($previewB.GuardRevision + 1) 'Preview B'
    $script:T18Revision = [int64]$previewB.revision
    Read-Task18Event 'preview.sceneChanged' $script:T18Revision | Out-Null
}

function Invoke-Task18TransitionSetup {
    $disabledTransition = Send-Task18Request @{ op = 'request'; id = 's-disabled-transition'; method = 'studio.transition' }
    Assert-Error $disabledTransition 'invalid_state' $script:T18Revision 'disabled studio.transition'
    $kinds = Send-Task18Request @{ op = 'request'; id = 's-kinds'; method = 'transition.kindList' }
    Assert-Ok $kinds $script:T18Revision 'transition kinds'
    $fade = @($kinds.data.kinds | Where-Object { $_.kind -eq 'fade_transition' }) | Select-Object -First 1
    if ($null -eq $fade) { Fail-Task18 'fade_transition was not dynamically available.' }
    $transition = Send-Task18Guarded 's-transition' 'transition.create' @{ kind = 'fade_transition'; name = 'Studio Fade' } $script:T18Revision
    Assert-Ok $transition ($transition.GuardRevision + 1) 'create transition'
    $script:T18Revision = [int64]$transition.revision
    Read-Task18Event 'transition.created' $script:T18Revision | Out-Null
    $script:T18TransitionHandle = [string]$transition.data.transition
    $select = Send-Task18Guarded 's-select' 'studio.setTransition' @{ transition = $script:T18TransitionHandle } $script:T18Revision
    Assert-Ok $select ($select.GuardRevision + 1) 'select transition'
    $script:T18Revision = [int64]$select.revision
    Read-Task18Event 'studio.transitionChanged' $script:T18Revision | Out-Null
    $duration = Send-Task18Guarded 's-duration' 'studio.setTransitionDuration' @{ durationMs = 750 } $script:T18Revision
    Assert-Ok $duration ($duration.GuardRevision + 1) 'set Studio duration'
    $script:T18Revision = [int64]$duration.revision
    Read-Task18Event 'transition.durationChanged' $script:T18Revision | Out-Null
    $transitionDuration = Send-Task18Request @{ op = 'request'; id = 's-duration-read'; method = 'transition.getDuration'; params = @{ transition = $script:T18TransitionHandle } }
    Assert-Ok $transitionDuration $script:T18Revision 'transition duration readback'
    if ([int]$transitionDuration.data.durationMs -ne 750) { Fail-Task18 'Studio duration did not update the Transition object.' }
    $directDuration = Send-Task18Guarded 's-direct-duration' 'transition.setDuration' @{ transition = $script:T18TransitionHandle; durationMs = 900 } $script:T18Revision
    Assert-Ok $directDuration ($directDuration.GuardRevision + 1) 'direct Transition duration'
    $script:T18Revision = [int64]$directDuration.revision
    Read-Task18Event 'transition.durationChanged' $script:T18Revision | Out-Null
    $studioDuration = Send-Task18Request @{ op = 'request'; id = 's-studio-duration-read'; method = 'studio.getTransitionDuration' }
    Assert-Ok $studioDuration $script:T18Revision 'Studio duration readback'
    if ([int]$studioDuration.data.durationMs -ne 900) { Fail-Task18 'Studio duration getter disagreed with direct Transition setter.' }
    $durationNoop = Send-Task18Guarded 's-duration-noop' 'transition.setDuration' @{ transition = $script:T18TransitionHandle; durationMs = 900 } $script:T18Revision
    Assert-Ok $durationNoop $script:T18Revision 'equal Transition duration no-op'
    $enable = Send-Task18Guarded 's-enable' 'studio.setEnabled' @{ enabled = $true } $script:T18Revision
    Assert-Ok $enable ($enable.GuardRevision + 1) 'enable Studio'
    $script:T18Revision = [int64]$enable.revision
    Read-Task18Event 'studio.enabledChanged' $script:T18Revision | Out-Null
}

function Invoke-Task18DirectProgramChanges {
    $programC = Send-Task18Guarded 's-program-c' 'program.setScene' @{ scene = [string]$script:T18SceneC.data.scene } $script:T18Revision
    Assert-Ok $programC ($programC.GuardRevision + 1) 'direct Program change with Studio enabled'
    $script:T18Revision = [int64]$programC.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
    $programA2 = Send-Task18Guarded 's-program-a2' 'program.setScene' @{ scene = [string]$script:T18SceneA.data.scene } $script:T18Revision
    Assert-Ok $programA2 ($programA2.GuardRevision + 1) 'restore Program A with Studio enabled'
    $script:T18Revision = [int64]$programA2.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
}

function Assert-Task18RunningProgram($Running) {
    if (-not $Running.status.ok -or [string]$Running.data.scene -ne [string]$script:T18SceneA.data.scene -or -not $Running.data.transitioning) { Fail-Task18 'Program did not remain logically on Scene A while transitioning.' }
}

function Assert-Task18FirstCompletion($ProgramEnd, $Program) {
    if ([string]$ProgramEnd.data.scene -ne [string]$script:T18SceneB.data.scene -or [string]$ProgramEnd.data.previousScene -ne [string]$script:T18SceneA.data.scene) { Fail-Task18 'Program completion event had the wrong destination.' }
    if ([string]$Program.data.scene -ne [string]$script:T18SceneB.data.scene -or $Program.data.transitioning) { Fail-Task18 'Program did not commit Scene B after transition completion.' }
}

function Invoke-Task18FirstTransition {
    Invoke-Task18DirectProgramChanges
    $start = Send-Task18Guarded 's-start' 'studio.transition' $null $script:T18Revision
    Assert-Ok $start ($start.GuardRevision + 1) 'Studio transition start'
    $script:T18Revision = [int64]$start.revision
    Read-Task18Event 'transition.started' $script:T18Revision | Out-Null
    $running = Send-Task18Request @{ op = 'request'; id = 's-running'; method = 'program.getScene' }
    Assert-Task18RunningProgram $running
    Start-Sleep -Milliseconds 1200
    $settled = Send-Task18Request @{ op = 'request'; id = 's-settled'; method = 'studio.getEnabled' }
    if (-not $settled.status.ok -or $settled.data.transitioning) { Fail-Task18 'Studio transition did not settle.' }
    $script:T18Revision = [int64]$settled.revision
    $programEnd = Read-Task18Event 'program.sceneChanged' $script:T18Revision $true
    Read-Task18Event 'transition.ended' $script:T18Revision $true | Out-Null
    $programB = Send-Task18Request @{ op = 'request'; id = 's-program-b'; method = 'program.getScene' }
    Assert-Ok $programB $script:T18Revision 'Program after transition'
    Assert-Task18FirstCompletion $programEnd $programB
    if ($script:ProgressEvents.Count -ne 0) { Fail-Task18 'transition.progress was delivered without telemetry opt-in.' }
}

function Assert-Task18IdleAgreement {
    $studio = Send-Task18Request @{ op = 'request'; id = 's-cancel-studio'; method = 'studio.getEnabled' }
    Assert-Ok $studio $script:T18Revision 'Studio after cancellation'
    if ($studio.data.transitioning) { Fail-Task18 'Studio remained running after Program cancellation.' }
    $transition = Send-Task18Request @{ op = 'request'; id = 's-cancel-transition'; method = 'transition.getState'; params = @{ transition = $script:T18TransitionHandle } }
    Assert-Ok $transition $script:T18Revision 'Transition after cancellation'
    if ([string]$transition.data.state -ne 'idle' -or $transition.data.active) { Fail-Task18 'Transition state remained active after cancellation.' }
    $program = Send-Task18Request @{ op = 'request'; id = 's-cancel-program'; method = 'program.getScene' }
    Assert-Ok $program $script:T18Revision 'Program after cancellation'
    if ([string]$program.data.scene -ne [string]$script:T18SceneC.data.scene -or $program.data.transitioning) { Fail-Task18 'Program did not remain on cancellation Scene C.' }
}

function Invoke-Task18CancellationChecks {
    $restoreA = Send-Task18Guarded 's-cancel-restore-a' 'program.setScene' @{ scene = [string]$script:T18SceneA.data.scene } $script:T18Revision
    Assert-Ok $restoreA ($restoreA.GuardRevision + 1) 'restore Program A before cancellation'
    $script:T18Revision = [int64]$restoreA.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
    $start = Send-Task18Guarded 's-cancel-start' 'studio.transition' $null $script:T18Revision
    Assert-Ok $start ($start.GuardRevision + 1) 'start cancellation transition'
    $script:T18Revision = [int64]$start.revision
    Read-Task18Event 'transition.started' $script:T18Revision | Out-Null
    $cancel = Send-Task18Guarded 's-cancel-program-c' 'program.setScene' @{ scene = [string]$script:T18SceneC.data.scene } $script:T18Revision
    Assert-Ok $cancel ($cancel.GuardRevision + 1) 'cancel transition with Program C'
    $script:T18Revision = [int64]$cancel.revision
    $programEnd = Read-Task18Event 'program.sceneChanged' $script:T18Revision
    $transitionEnd = Read-Task18Event 'transition.ended' $script:T18Revision
    if ([string]$programEnd.data.scene -ne [string]$script:T18SceneC.data.scene -or [string]$programEnd.data.previousScene -ne [string]$script:T18SceneA.data.scene) { Fail-Task18 'Cancellation Program event had the wrong current or previous Scene.' }
    if ([string]$transitionEnd.data.transition -ne $script:T18TransitionHandle -or [string]$transitionEnd.data.state -ne 'idle') { Fail-Task18 'Cancellation transition.ended identified the wrong transition or state.' }
    Start-Sleep -Milliseconds 1200
    Assert-Task18IdleAgreement
    if (@($script:Events | Where-Object { [string]$_.event -eq 'transition.ended' }).Count -ne 0) { Fail-Task18 'Cancellation produced a delayed duplicate transition.ended.' }

    $duration = Send-Task18Guarded 's-race-duration' 'transition.setDuration' @{ transition = $script:T18TransitionHandle; durationMs = 600 } $script:T18Revision
    Assert-Ok $duration ($duration.GuardRevision + 1) 'near-completion duration'
    $script:T18Revision = [int64]$duration.revision
    Read-Task18Event 'transition.durationChanged' $script:T18Revision | Out-Null
    $restoreA2 = Send-Task18Guarded 's-race-restore-a' 'program.setScene' @{ scene = [string]$script:T18SceneA.data.scene } $script:T18Revision
    Assert-Ok $restoreA2 ($restoreA2.GuardRevision + 1) 'restore Program A before near-completion race'
    $script:T18Revision = [int64]$restoreA2.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
    $raceStart = Send-Task18Guarded 's-race-start' 'studio.transition' $null $script:T18Revision
    Assert-Ok $raceStart ($raceStart.GuardRevision + 1) 'near-completion transition start'
    $script:T18Revision = [int64]$raceStart.revision
    Read-Task18Event 'transition.started' $script:T18Revision | Out-Null
    Start-Sleep -Milliseconds 500
    $raceCancel = Send-Task18Guarded 's-race-cancel' 'program.setScene' @{ scene = [string]$script:T18SceneC.data.scene } $script:T18Revision
    Assert-Ok $raceCancel ($raceCancel.GuardRevision + 1) 'near-completion Program cancellation'
    $script:T18Revision = [int64]$raceCancel.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
    Read-Task18Event 'transition.ended' $script:T18Revision | Out-Null
    Start-Sleep -Milliseconds 1200
    Assert-Task18IdleAgreement
    if (@($script:Events | Where-Object { [string]$_.event -eq 'transition.ended' }).Count -ne 0) { Fail-Task18 'Near-completion cancellation produced a duplicate transition.ended.' }
    Write-Output 'Task 18 cancellation and near-completion race: PASS'
}

function Enable-Task18TransitionTelemetry {
    $telemetry = Send-Task18Request @{ op = 'request'; id = 's-progress-subscribe'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'transition.*'; telemetry = $true }) } }
    Assert-Ok $telemetry $script:T18Revision 'transition telemetry subscription'
}

function Assert-Task18SecondTransitionState($Settled) {
    if (-not $Settled.status.ok -or [string]$Settled.data.scene -ne [string]$script:T18SceneA.data.scene -or $Settled.data.transitioning) { Fail-Task18 'transition did not complete after Studio was disabled.' }
}

function Assert-Task18ProgressSample($Sample, [ref] $Maximum) {
    if ([string]$Sample.data.transition -ne $script:T18TransitionHandle -or [string]$Sample.data.state -ne 'running') { Fail-Task18 'transition.progress identified the wrong transition state.' }
    $value = [double]$Sample.data.progress
    if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0.0 -or $value -gt 1.0) { Fail-Task18 'transition.progress was outside the finite 0..1 range.' }
    if ($value -gt $Maximum.Value) { $Maximum.Value = $value }
    if ([int64]$Sample.revision -gt $script:T18Revision) { Fail-Task18 'transition.progress advanced beyond the settled engine revision.' }
}

function Assert-Task18ProgressSamples($Samples, [int64] $MaximumRevision) {
    if ($Samples.Count -eq 0) { Fail-Task18 'transition.progress was not delivered after telemetry opt-in.' }
    $maximum = 0.0
    foreach ($sample in $Samples) {
        Assert-Task18ProgressSample $sample ([ref]$maximum)
        if ([int64]$sample.revision -gt $MaximumRevision) { Fail-Task18 'transition.progress consumed a later mutation revision.' }
    }
    if ($maximum -le 0.0) { Fail-Task18 'transition.progress did not advance while Studio was running.' }
    if (@($script:Events | Where-Object { [string]$_.event -eq 'session.resyncRequired' }).Count -ne 0) { Fail-Task18 'telemetry delivery caused an unexpected state resync.' }
}

function Invoke-Task18SecondTransition {
    $previewA = Send-Task18Guarded 's-preview-a' 'preview.setScene' @{ scene = [string]$script:T18SceneA.data.scene } $script:T18Revision
    Assert-Ok $previewA ($previewA.GuardRevision + 1) 'Preview A'
    $script:T18Revision = [int64]$previewA.revision
    Read-Task18Event 'preview.sceneChanged' $script:T18Revision | Out-Null
    $progressBefore = $script:ProgressEvents.Count
    Enable-Task18TransitionTelemetry
    $startAgain = Send-Task18Guarded 's-start-again' 'studio.transition' $null $script:T18Revision
    Assert-Ok $startAgain ($startAgain.GuardRevision + 1) 'second Studio transition start'
    $script:T18Revision = [int64]$startAgain.revision
    Read-Task18Event 'transition.started' $script:T18Revision | Out-Null
    $disable = Send-Task18Guarded 's-disable-running' 'studio.setEnabled' @{ enabled = $false } $script:T18Revision
    Assert-Ok $disable ($disable.GuardRevision + 1) 'disable Studio while running'
    $disableRevision = [int64]$disable.revision
    $script:T18Revision = [int64]$disable.revision
    Read-Task18Event 'studio.enabledChanged' $script:T18Revision | Out-Null
    $busy = Send-Task18Request @{ op = 'request'; id = 's-busy'; method = 'studio.transition' }
    Assert-Error $busy 'invalid_state' $script:T18Revision 'transition while Studio disabled'
    Start-Sleep -Milliseconds 1200
    $settledAgain = Send-Task18Request @{ op = 'request'; id = 's-settled-again'; method = 'program.getScene' }
    Assert-Task18SecondTransitionState $settledAgain
    if ([int64]$settledAgain.revision -ne $disableRevision + 1) { Fail-Task18 'transition.progress consumed a mutation revision.' }
    $script:T18Revision = [int64]$settledAgain.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision $true | Out-Null
    Read-Task18Event 'transition.ended' $script:T18Revision $true | Out-Null
    $progress = @($script:ProgressEvents | Select-Object -Skip $progressBefore)
    Assert-Task18ProgressSamples $progress $disableRevision
}

function Invoke-Task18Cleanup {
    Assert-Error (Send-Task18Request @{ op = 'request'; id = 's-remove-selected'; method = 'transition.remove'; params = @{ transition = $script:T18TransitionHandle }; ifRevision = $script:T18Revision }) 'object_in_use' $script:T18Revision 'remove selected Transition'
    $clearSelection = Send-Task18Guarded 's-clear-selection' 'studio.setTransition' @{ transition = $null } $script:T18Revision
    Assert-Ok $clearSelection ($clearSelection.GuardRevision + 1) 'clear selected Transition'
    $script:T18Revision = [int64]$clearSelection.revision
    Read-Task18Event 'studio.transitionChanged' $script:T18Revision | Out-Null
    $removeTransition = Send-Task18Guarded 's-remove-transition' 'transition.remove' @{ transition = $script:T18TransitionHandle } $script:T18Revision
    Assert-Ok $removeTransition ($removeTransition.GuardRevision + 1) 'remove Transition'
    $script:T18Revision = [int64]$removeTransition.revision
    Read-Task18Event 'transition.removed' $script:T18Revision | Out-Null
    $removeProgramPreview = Send-Task18Guarded 's-remove-current-scene' 'scene.remove' @{ scene = [string]$script:T18SceneA.data.scene } $script:T18Revision
    Assert-Ok $removeProgramPreview ($removeProgramPreview.GuardRevision + 1) 'remove current Program and Preview Scene'
    $script:T18Revision = [int64]$removeProgramPreview.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
    Read-Task18Event 'preview.sceneChanged' $script:T18Revision | Out-Null
    Read-Task18Event 'scene.removed' $script:T18Revision | Out-Null
    $close = Send-Task18Guarded 's-close' 'session.close' $null $script:T18Revision
    Assert-Ok $close ($close.GuardRevision + 1) 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task18Engine
    Write-Output 'Task 18 Studio integration: PASS'
}

function Invoke-Task18Scenario {
    Invoke-Task18Bootstrap
    Invoke-Task18TransitionSetup
    Invoke-Task18FirstTransition
    Invoke-Task18CancellationChecks
    Invoke-Task18SecondTransition
    Invoke-Task18Cleanup
}

try {
    Invoke-Task18Scenario
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:ErrorTask) { Write-Host ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

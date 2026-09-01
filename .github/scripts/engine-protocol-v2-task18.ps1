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

function Read-Task18Event([string] $Name, [int64] $Revision, [bool] $AllowBeforeResponse = $false) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task18Message }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) { Fail-Task18 "unexpected event; expected $Name at revision $Revision." }
    $script:NextSequence++
    if (-not $AllowBeforeResponse) {
        $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
        if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task18 "event $Name preceded its response." }
    }
    return $event
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
    Read-Task18Event 'studio.transitionDurationChanged' $script:T18Revision | Out-Null
    $transitionDuration = Send-Task18Request @{ op = 'request'; id = 's-duration-read'; method = 'transition.getDuration'; params = @{ transition = $script:T18TransitionHandle } }
    Assert-Ok $transitionDuration $script:T18Revision 'transition duration readback'
    if ([int]$transitionDuration.data.durationMs -ne 750) { Fail-Task18 'Studio duration did not update the Transition object.' }
    $enable = Send-Task18Guarded 's-enable' 'studio.setEnabled' @{ enabled = $true } $script:T18Revision
    Assert-Ok $enable ($enable.GuardRevision + 1) 'enable Studio'
    $script:T18Revision = [int64]$enable.revision
    Read-Task18Event 'studio.enabledChanged' $script:T18Revision | Out-Null
}

function Invoke-Task18FirstTransition {
    $programC = Send-Task18Guarded 's-program-c' 'program.setScene' @{ scene = [string]$script:T18SceneC.data.scene } $script:T18Revision
    Assert-Ok $programC ($programC.GuardRevision + 1) 'direct Program change with Studio enabled'
    $script:T18Revision = [int64]$programC.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
    $programA2 = Send-Task18Guarded 's-program-a2' 'program.setScene' @{ scene = [string]$script:T18SceneA.data.scene } $script:T18Revision
    Assert-Ok $programA2 ($programA2.GuardRevision + 1) 'restore Program A with Studio enabled'
    $script:T18Revision = [int64]$programA2.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision | Out-Null
    $start = Send-Task18Guarded 's-start' 'studio.transition' $null $script:T18Revision
    Assert-Ok $start ($start.GuardRevision + 1) 'Studio transition start'
    $script:T18Revision = [int64]$start.revision
    Read-Task18Event 'transition.started' $script:T18Revision | Out-Null
    $running = Send-Task18Request @{ op = 'request'; id = 's-running'; method = 'program.getScene' }
    if (-not $running.status.ok -or [string]$running.data.scene -ne [string]$script:T18SceneA.data.scene -or -not $running.data.transitioning) { Fail-Task18 'Program did not remain logically on Scene A while transitioning.' }
    Start-Sleep -Milliseconds 1200
    $settled = Send-Task18Request @{ op = 'request'; id = 's-settled'; method = 'studio.getEnabled' }
    if (-not $settled.status.ok -or $settled.data.transitioning) { Fail-Task18 'Studio transition did not settle.' }
    $script:T18Revision = [int64]$settled.revision
    $programEnd = Read-Task18Event 'program.sceneChanged' $script:T18Revision $true
    Read-Task18Event 'transition.ended' $script:T18Revision $true | Out-Null
    if ([string]$programEnd.data.scene -ne [string]$script:T18SceneB.data.scene -or [string]$programEnd.data.previousScene -ne [string]$script:T18SceneA.data.scene) { Fail-Task18 'Program completion event had the wrong destination.' }
    $programB = Send-Task18Request @{ op = 'request'; id = 's-program-b'; method = 'program.getScene' }
    Assert-Ok $programB $script:T18Revision 'Program after transition'
    if ([string]$programB.data.scene -ne [string]$script:T18SceneB.data.scene -or $programB.data.transitioning) { Fail-Task18 'Program did not commit Scene B after transition completion.' }
}

function Invoke-Task18SecondTransition {
    $previewA = Send-Task18Guarded 's-preview-a' 'preview.setScene' @{ scene = [string]$script:T18SceneA.data.scene } $script:T18Revision
    Assert-Ok $previewA ($previewA.GuardRevision + 1) 'Preview A'
    $script:T18Revision = [int64]$previewA.revision
    Read-Task18Event 'preview.sceneChanged' $script:T18Revision | Out-Null
    $startAgain = Send-Task18Guarded 's-start-again' 'studio.transition' $null $script:T18Revision
    Assert-Ok $startAgain ($startAgain.GuardRevision + 1) 'second Studio transition start'
    $script:T18Revision = [int64]$startAgain.revision
    Read-Task18Event 'transition.started' $script:T18Revision | Out-Null
    $disable = Send-Task18Guarded 's-disable-running' 'studio.setEnabled' @{ enabled = $false } $script:T18Revision
    Assert-Ok $disable ($disable.GuardRevision + 1) 'disable Studio while running'
    $script:T18Revision = [int64]$disable.revision
    Read-Task18Event 'studio.enabledChanged' $script:T18Revision | Out-Null
    $busy = Send-Task18Request @{ op = 'request'; id = 's-busy'; method = 'studio.transition' }
    Assert-Error $busy 'invalid_state' $script:T18Revision 'transition while Studio disabled'
    Start-Sleep -Milliseconds 1200
    $settledAgain = Send-Task18Request @{ op = 'request'; id = 's-settled-again'; method = 'program.getScene' }
    if (-not $settledAgain.status.ok -or [string]$settledAgain.data.scene -ne [string]$script:T18SceneA.data.scene -or $settledAgain.data.transitioning) { Fail-Task18 'transition did not complete after Studio was disabled.' }
    $script:T18Revision = [int64]$settledAgain.revision
    Read-Task18Event 'program.sceneChanged' $script:T18Revision $true | Out-Null
    Read-Task18Event 'transition.ended' $script:T18Revision $true | Out-Null
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

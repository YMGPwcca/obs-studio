param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:AllEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1

function Fail-Task29([string] $Message) { throw "Task 29: $Message" }

function Start-Task29Engine([string] $Root) {
    $engine = Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $Root).Path -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task29 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    foreach ($plugin in @('task23-encoder', 'task29-replay')) { $info.ArgumentList.Add("--plugin=$plugin") }
    $info.UseShellExecute = $false; $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true; $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task29 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:AllEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Stop-Task29Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task29 "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task29Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task29 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task29 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task29 "engine emitted non-JSON stdout: $line" }
}

function Send-Task29([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task29Message
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); $script:AllEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task29 "wrong response for $($Request.id)." }
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task29 "$Label did not succeed at revision $Revision." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    $actual = if ($null -ne $Response.status.PSObject.Properties['code']) { [string]$Response.status.code } else { '<missing>' }
    if ($Response.status.ok -or $actual -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task29 "$Label did not return $Code at revision $Revision (actual=$actual revision=$($Response.revision))." }
}

function Read-Task29Event([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task29Message }
        if ($event.op -ne 'event') { Fail-Task29 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task29 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task29 "event $Name has the wrong revision." }
        return $event
    }
}

function Invoke-Task29Mutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task29 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-Ok $response ($State.Current + 1) $Label; $State.Current++
    foreach ($event in $Events) { Read-Task29Event $event $State.Current | Out-Null }
    return $response
}

function Initialize-Task29Session {
    Start-Task29Engine $InstallRoot
    if ([string](Read-Task29Message).event -ne 'ready') { Fail-Task29 'ready marker was not received.' }
    $hello = Send-Task29 @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }; Assert-Ok $hello 0 'session.hello'
    $required = @('replayBuffer.v1', 'replayBuffer.getConfig.v1', 'replayBuffer.configure.v1', 'replayBuffer.unconfigure.v1',
        'replayBuffer.start.v1', 'replayBuffer.stop.v1', 'replayBuffer.save.v1', 'replayBuffer.getState.v1',
        'replayBuffer.getStats.v1', 'replayBuffer.getLastFile.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task29 "missing capability $name." } }
    $sub = Send-Task29 @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'replayBuffer.*' }, @{ pattern = 'encoder.*' },
                @{ pattern = 'engine.stopping' }) } }; Assert-Ok $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0; Video = ''; Audio = ''; Output = ''; Path = '' }
}

function Invoke-Task29Create($State) {
    $video = Invoke-Task29Mutation $State 'video' 'encoder.create' @{ type = 'video'; kind = 'task23_test_video'; name = 'task29-video' } @('encoder.created') 'video encoder.create'
    $State.Video = [string]$video.data.encoder
    $audio = Invoke-Task29Mutation $State 'audio' 'encoder.create' @{ type = 'audio'; kind = 'task23_test_audio'; name = 'task29-audio'; audioTrack = 1 } @('encoder.created') 'audio encoder.create'
    $State.Audio = [string]$audio.data.encoder
    $State.Path = Join-Path (Get-Location).Path ("task29-synthetic-$([Guid]::NewGuid().ToString('N')).mp4")
    $output = Invoke-Task29Mutation $State 'output' 'output.create' @{ kind = 'task29_test_replay'; name = 'task29-replay'; settings = @{
            async_save = $true; async_stop = $true; save_delay_ms = 5000; path = $State.Path } } @('output.created') 'replay Output create'
    $State.Output = [string]$output.data.output
}

function Invoke-Task29Configure($State) {
    $configured = Invoke-Task29Mutation $State 'configure' 'replayBuffer.configure' @{ output = $State.Output } @('replayBuffer.configChanged') 'replayBuffer.configure'
    if (-not $configured.data.configured -or [string]$configured.data.output -ne $State.Output) { Fail-Task29 'replay-buffer role was not assigned.' }
    $view = Send-Task29 @{ op = 'request'; id = 'output-view'; method = 'output.get'; params = @{ output = $State.Output } }; Assert-Ok $view $State.Current 'replay Output view'
    if ([string]$view.data.state.role -ne 'replayBuffer' -or [string]$view.data.state.managedBy -ne 'replayBuffer') { Fail-Task29 'replay role was not visible on the Output.' }
    $remove = Send-Task29 @{ op = 'request'; id = 'role-remove'; method = 'output.remove'; params = @{ output = $State.Output } }; Assert-Error $remove 'object_in_use' $State.Current 'remove assigned replay Output'
    $null = Invoke-Task29Mutation $State 'video-bind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Video } @('encoder.bindingChanged', 'output.configurationChanged') 'video binding'
    $null = Invoke-Task29Mutation $State 'audio-bind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Audio } @('encoder.bindingChanged', 'output.configurationChanged') 'audio binding'
    foreach ($request in @(
        @{ op = 'request'; id = 'config-read'; method = 'replayBuffer.getConfig'; params = @{} },
        @{ op = 'request'; id = 'state-read'; method = 'replayBuffer.getState'; params = @{} },
        @{ op = 'request'; id = 'stats-read'; method = 'replayBuffer.getStats'; params = @{} },
        @{ op = 'request'; id = 'last-read'; method = 'replayBuffer.getLastFile'; params = @{} }
    )) { Assert-Ok (Send-Task29 $request) $State.Current $request.id }
}

function Invoke-Task29Save($State, [string] $Id, [string] $EventId) {
    $save = Invoke-Task29Mutation $State $Id 'replayBuffer.save' @{} @() "replayBuffer.save $Id"
    if (-not $save.data.pending) { Fail-Task29 "replayBuffer.save $Id did not report an asynchronous pending save." }
    $saved = Read-Task29Event 'replayBuffer.saved' ($State.Current + 1)
    $State.Current++
    if ([string]$saved.data.output -ne $State.Output -or [string]$saved.data.path -ne $State.Path) { Fail-Task29 "replayBuffer.saved $EventId had the wrong output or path." }
}

function Invoke-Task29SaveRace($State) {
    $first = Invoke-Task29Mutation $State 'save-one' 'replayBuffer.save' @{} @() 'first replayBuffer.save'
    if (-not $first.data.pending) { Fail-Task29 'first save did not remain pending.' }
    $repeat = Send-Task29 @{ op = 'request'; id = 'save-repeat'; method = 'replayBuffer.save'; params = @{} }
    Assert-Error $repeat 'busy' $State.Current 'repeated pending save'
    $saved = Read-Task29Event 'replayBuffer.saved' ($State.Current + 1); $State.Current++
    if ([string]$saved.data.path -ne $State.Path) { Fail-Task29 'first replayBuffer.saved path was wrong.' }
    $last = Send-Task29 @{ op = 'request'; id = 'last-after-save'; method = 'replayBuffer.getLastFile'; params = @{} }; Assert-Ok $last $State.Current 'replayBuffer.getLastFile after save'
    if ([string]$last.data.lastFile -ne $State.Path) { Fail-Task29 'lastFile was not settled by replayBuffer.saved.' }
    $second = Invoke-Task29Mutation $State 'save-two' 'replayBuffer.save' @{} @() 'second replayBuffer.save'
    if (-not $second.data.pending) { Fail-Task29 'second save did not remain pending.' }
    $stop = Invoke-Task29Mutation $State 'stop' 'replayBuffer.stop' @{} @('output.stopping') 'replayBuffer.stop after save'
    $during_stop = Send-Task29 @{ op = 'request'; id = 'save-during-stop'; method = 'replayBuffer.save'; params = @{} }
    Assert-Error $during_stop 'invalid_state' ([int64]$stop.revision + 1) 'save during stop'
    $next = $State.Current + 1
    Read-Task29Event 'output.stopped' $next | Out-Null
    Read-Task29Event 'encoder.activeChanged' $next | Out-Null
    Read-Task29Event 'encoder.activeChanged' $next | Out-Null
    $State.Current = $next
    $saved_after_stop = Read-Task29Event 'replayBuffer.saved' ($State.Current + 1); $State.Current++
    if ([string]$saved_after_stop.data.path -ne $State.Path) { Fail-Task29 'save-after-stop completion path was wrong.' }
    $stateResponse = Send-Task29 @{ op = 'request'; id = 'state-after-save'; method = 'replayBuffer.getState'; params = @{} }; Assert-Ok $stateResponse $State.Current 'replayBuffer.getState after stop/save race'
    if ($stateResponse.data.pendingSave) { Fail-Task29 'pendingSave remained set after saved callback.' }
}

function Invoke-Task29FailedSave($State) {
    $null = Invoke-Task29Mutation $State 'fail-settings' 'output.patchSettings' @{ output = $State.Output; settings = @{ fail_save = $true; save_delay_ms = 100 } } @('output.configurationChanged') 'replay save failure settings'
    $restart = Invoke-Task29Mutation $State 'restart' 'replayBuffer.start' @{} @('output.started', 'encoder.activeChanged', 'encoder.activeChanged') 'replayBuffer.start for failed save'
    if ([string]$restart.data.state.state -ne 'active') { Fail-Task29 'replay buffer did not restart for failed save.' }
    $failedSave = Invoke-Task29Mutation $State 'save-failed' 'replayBuffer.save' @{} @() 'failed replayBuffer.save'
    if (-not $failedSave.data.pending) { Fail-Task29 'failed save did not report an honest pending operation.' }
    Start-Sleep -Milliseconds 300
    $failedState = Send-Task29 @{ op = 'request'; id = 'failed-state'; method = 'replayBuffer.getState'; params = @{} }; Assert-Ok $failedState $State.Current 'replayBuffer.getState after failed save'
    if ($failedState.data.pendingSave) { Fail-Task29 'failed save remained pending after the no-path completion.' }
    if (@($script:PendingEvents | Where-Object { [string]$_.event -eq 'replayBuffer.saved' }).Count -ne 0) { Fail-Task29 'failed save fabricated replayBuffer.saved.' }
    $failedStop = Invoke-Task29Mutation $State 'failed-stop' 'replayBuffer.stop' @{} @('output.stopping') 'stop after failed save'
    $failedNext = $State.Current + 1
    Read-Task29Event 'output.stopped' $failedNext | Out-Null
    Read-Task29Event 'encoder.activeChanged' $failedNext | Out-Null
    Read-Task29Event 'encoder.activeChanged' $failedNext | Out-Null
    $State.Current = $failedNext
}

function Invoke-Task29Lifecycle($State) {
    $start = Invoke-Task29Mutation $State 'start' 'replayBuffer.start' @{} @('output.started', 'encoder.activeChanged', 'encoder.activeChanged') 'replayBuffer.start'
    if ([string]$start.data.state.state -ne 'active') { Fail-Task29 'replay buffer did not become active.' }
    Invoke-Task29SaveRace $State
    Invoke-Task29FailedSave $State
}

function Invoke-Task29Cleanup($State) {
    $null = Invoke-Task29Mutation $State 'video-unbind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'video unbind'
    $null = Invoke-Task29Mutation $State 'audio-unbind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'audio unbind'
    $null = Invoke-Task29Mutation $State 'unconfigure' 'replayBuffer.unconfigure' @{} @('replayBuffer.configChanged') 'replayBuffer.unconfigure'
    $null = Invoke-Task29Mutation $State 'output-remove' 'output.remove' @{ output = $State.Output } @('output.removed') 'replay Output remove'
    $null = Invoke-Task29Mutation $State 'video-remove' 'encoder.remove' @{ encoder = $State.Video } @('encoder.removed') 'video encoder remove'
    $null = Invoke-Task29Mutation $State 'audio-remove' 'encoder.remove' @{ encoder = $State.Audio } @('encoder.removed') 'audio encoder remove'
    $close = Send-Task29 @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }; Assert-Ok $close ($State.Current + 1) 'session.close'
    Read-Task29Event 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task29 'engine did not exit.' }
    Stop-Task29Engine
    if (@($script:AllEvents | Where-Object { [string]$_.event -in @('replayBuffer.started', 'replayBuffer.stopped') }).Count -ne 0) { Fail-Task29 'replay buffer emitted duplicate lifecycle aliases.' }
    Write-Output 'Task 29 replay-buffer integration: PASS'
}

try {
    $state = Initialize-Task29Session
    Invoke-Task29Create $state
    Invoke-Task29Configure $state
    Invoke-Task29Lifecycle $state
    Invoke-Task29Cleanup $state
} catch {
    try { Stop-Task29Engine } catch { }
    throw
}

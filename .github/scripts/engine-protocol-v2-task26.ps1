param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:Sentinel = [Guid]::NewGuid().ToString('N')

function Fail-Task26([string] $Message) { throw "Task 26: $Message" }

function Start-Task26Engine([string] $Root) {
    $resolved = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolved -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task26 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    foreach ($plugin in @('task23-encoder', 'task25-service', 'task26-output')) { $info.ArgumentList.Add("--plugin=$plugin") }
    $info.UseShellExecute = $false; $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true; $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task26 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Stop-Task26Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr.Contains($script:Sentinel)) { Fail-Task26 'secret sentinel appeared on stderr.' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task26 "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task26Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task26 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task26 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task26 "engine emitted non-JSON stdout: $line" }
}

function Send-Task26([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task26Message
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task26 "wrong response for $($Request.id)." }
        return $message
    }
}

function Assert-NoSentinel($Value, [string] $Label) {
    $json = $Value | ConvertTo-Json -Compress -Depth 60
    if ($json.Contains($script:Sentinel)) { Fail-Task26 "$Label exposed the secret sentinel." }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    Assert-NoSentinel $Response $Label
    if (-not $Response.status.ok) { Fail-Task26 "$Label failed: $($Response.status.code): $($Response.status.message)." }
    if ([int64]$Response.revision -ne $Revision) { Fail-Task26 "$Label revision=$($Response.revision), expected $Revision." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    Assert-NoSentinel $Response $Label
    $actualCode = if ($null -ne $Response.status.PSObject.Properties['code']) { [string]$Response.status.code } else { '<missing>' }
    if ($Response.status.ok -or $actualCode -ne $Code -or [int64]$Response.revision -ne $Revision) {
        Fail-Task26 "$Label did not return $Code (actual=$actualCode, response=$($Response | ConvertTo-Json -Compress -Depth 20))."
    }
}

function Read-Task26Event([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task26Message }
        Assert-NoSentinel $event "event $Name"
        if ($event.op -ne 'event') { Fail-Task26 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task26 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task26 "event $Name has the wrong revision." }
        return $event
    }
}

function Invoke-Task26Mutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task26 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-Ok $response ($State.Current + 1) $Label
    $State.Current++
    foreach ($event in $Events) { Read-Task26Event $event $State.Current | Out-Null }
    return $response
}

function Initialize-Task26Session {
    Start-Task26Engine $InstallRoot
    if ([string](Read-Task26Message).event -ne 'ready') { Fail-Task26 'ready marker was not received.' }
    $hello = Send-Task26 @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $hello 0 'session.hello'
    $required = @(
        'output.v1', 'output.kindList.v1', 'output.kindGet.v1', 'output.kindDefaults.v1',
        'output.kindProperties.v1', 'output.kindCapabilities.v1', 'output.list.v1', 'output.get.v1',
        'output.create.v1', 'output.remove.v1', 'output.rename.v1', 'output.getSettings.v1',
        'output.patchSettings.v1', 'output.replaceSettings.v1', 'output.getProperties.v1',
        'output.setService.v1', 'output.getService.v1', 'output.setVideoEncoder.v1',
        'output.setAudioEncoder.v1', 'output.getEncoders.v1', 'output.start.v1', 'output.stop.v1',
        'output.forceStop.v1', 'output.getState.v1', 'output.setPaused.v1', 'output.getPaused.v1',
        'output.setDelay.v1', 'output.getDelay.v1', 'output.setReconnect.v1', 'output.getReconnect.v1',
        'output.getStats.v1', 'output.getLastError.v1', 'output.getSupportedCodecs.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task26 "missing capability $name." } }
    if ($caps -contains 'output.sendCaption.v1') { Fail-Task26 'output.sendCaption was advertised.' }
    $sub = Send-Task26 @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'encoder.*' }, @{ pattern = 'service.*' },
                @{ pattern = 'engine.stopping' }) } }
    Assert-Ok $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0 }
}

function Assert-Task26KindCapabilities($Kind) {
    if (-not $Kind.capabilities.video -or -not $Kind.capabilities.audio -or -not $Kind.capabilities.encoded -or
        -not $Kind.capabilities.requiresService -or -not $Kind.capabilities.multiTrackAudio -or
        -not $Kind.capabilities.multiTrackVideo -or -not $Kind.capabilities.canPause) {
        Fail-Task26 'output kind flags were not canonicalized.'
    }
}

function Assert-Task26KindCodecs($Kind) {
    if (@($Kind.capabilities.videoCodecs | Where-Object { [string]$_.value -eq 'task23-video' }).Count -ne 1 -or
        @($Kind.capabilities.audioCodecs | Where-Object { [string]$_.value -eq 'task23-audio' }).Count -ne 1) {
        Fail-Task26 'output codec metadata was incomplete.'
    }
}

function Invoke-Task26KindChecks {
    $kinds = Send-Task26 @{ op = 'request'; id = 'kind-list'; method = 'output.kindList'; params = @{} }
    Assert-Ok $kinds 0 'output.kindList'
    $kind = @($kinds.data.kinds | Where-Object { [string]$_.id -eq 'task26_test_output' }) | Select-Object -First 1
    if ($null -eq $kind) { Fail-Task26 'deterministic output kind was not listed.' }
    Assert-Task26KindCapabilities $kind
    Assert-Task26KindCodecs $kind
    foreach ($request in @(
        @{ op = 'request'; id = 'kind-get'; method = 'output.kindGet'; params = @{ kind = 'task26_test_output' } },
        @{ op = 'request'; id = 'kind-defaults'; method = 'output.kindDefaults'; params = @{ kind = 'task26_test_output' } },
        @{ op = 'request'; id = 'kind-properties'; method = 'output.kindProperties'; params = @{ kind = 'task26_test_output' } },
        @{ op = 'request'; id = 'kind-capabilities'; method = 'output.kindCapabilities'; params = @{ kind = 'task26_test_output' } }
    )) { Assert-Ok (Send-Task26 $request) 0 $request.id }
    $bad = Send-Task26 @{ op = 'request'; id = 'bad-kind'; method = 'output.kindGet'; params = @{ kind = 'no_such_output' } }
    Assert-Error $bad 'not_found' 0 'invalid output kind'
}

function Invoke-Task26CreateObjects($Context) {
    $state = $Context.State
    $service = Invoke-Task26Mutation $state 'service-create' 'service.create' @{
        kind = 'task25_test_service'; name = 'task26-service'; settings = @{
            server = 'rtmp://127.0.0.1/task26'; key = $script:Sentinel; label = 'task26' }
    } @('service.created') 'service.create'
    $Context.ServiceHandle = [string]$service.data.service
    $video = Invoke-Task26Mutation $state 'video-create' 'encoder.create' @{
        type = 'video'; kind = 'task23_test_video'; name = 'task26-video'; settings = @{ bitrate = 1500 }
    } @('encoder.created') 'video encoder.create'
    $Context.VideoHandle = [string]$video.data.encoder
    $audio1 = Invoke-Task26Mutation $state 'audio1-create' 'encoder.create' @{
        type = 'audio'; kind = 'task23_test_audio'; name = 'task26-audio1'; audioTrack = 1
    } @('encoder.created') 'audio1 encoder.create'
    $Context.Audio1Handle = [string]$audio1.data.encoder
    $audio2 = Invoke-Task26Mutation $state 'audio2-create' 'encoder.create' @{
        type = 'audio'; kind = 'task23_test_audio'; name = 'task26-audio2'; audioTrack = 2
    } @('encoder.created') 'audio2 encoder.create'
    $Context.Audio2Handle = [string]$audio2.data.encoder
    $output = Invoke-Task26Mutation $state 'output-create' 'output.create' @{
        kind = 'task26_test_output'; name = 'task26-output'; settings = @{
            fail_start = $false; async_start = $false; async_stop = $true }
    } @('output.created') 'output.create'
    $Context.OutputHandle = [string]$output.data.output
    $incompatible = Invoke-Task26Mutation $state 'incompatible-create' 'encoder.create' @{
        type = 'video'; kind = 'obs_x264'; name = 'task26-incompatible'
    } @('encoder.created') 'incompatible encoder.create'
    $Context.IncompatibleHandle = [string]$incompatible.data.encoder
    $createFailure = Send-Task26 @{ op = 'request'; id = 'create-failure'; method = 'output.create'; params = @{
            kind = 'task26_test_output'; name = 'task26-create-failure'; settings = @{ fail_create = $true } } }
    Assert-Error $createFailure 'obs_error' $state.Current 'plugin create failure'
    foreach ($request in @(
        @{ op = 'request'; id = 'output-get'; method = 'output.get'; params = @{ output = $Context.OutputHandle } },
        @{ op = 'request'; id = 'output-list'; method = 'output.list'; params = @{} },
        @{ op = 'request'; id = 'output-settings'; method = 'output.getSettings'; params = @{ output = $Context.OutputHandle } },
        @{ op = 'request'; id = 'output-properties'; method = 'output.getProperties'; params = @{ output = $Context.OutputHandle } },
        @{ op = 'request'; id = 'output-service'; method = 'output.getService'; params = @{ output = $Context.OutputHandle } },
        @{ op = 'request'; id = 'output-delay'; method = 'output.getDelay'; params = @{ output = $Context.OutputHandle } },
        @{ op = 'request'; id = 'output-reconnect'; method = 'output.getReconnect'; params = @{ output = $Context.OutputHandle } },
        @{ op = 'request'; id = 'output-codecs'; method = 'output.getSupportedCodecs'; params = @{ output = $Context.OutputHandle } }
    )) { Assert-Ok (Send-Task26 $request) $state.Current $request.id }
    $nullOutput = Send-Task26 @{ op = 'request'; id = 'null-create'; method = 'output.create'; params = @{ kind = 'null_output'; name = 'task26-null' } }
    Assert-Ok $nullOutput ($state.Current + 1) 'null output.create'; $state.Current++
    Read-Task26Event 'output.created' $state.Current | Out-Null
    $unsupportedService = Send-Task26 @{ op = 'request'; id = 'null-service'; method = 'output.setService'; params = @{ output = [string]$nullOutput.data.output; service = $Context.ServiceHandle } }
    Assert-Error $unsupportedService 'unsupported_capability' $state.Current 'service on non-service output'
    $null = Invoke-Task26Mutation $state 'null-remove' 'output.remove' @{ output = [string]$nullOutput.data.output } @('output.removed') 'null output.remove'
}

function Invoke-Task26ConfigureOutput($Context) {
    $state = $Context.State
    $bound = Invoke-Task26Mutation $state 'service-bind' 'output.setService' @{ output = $Context.OutputHandle; service = $Context.ServiceHandle } @('service.bindingChanged', 'output.configurationChanged') 'output.setService'
    if ([string]$bound.data.state.service -ne $Context.ServiceHandle) { Fail-Task26 'output service binding was not reflected in state.' }
    $null = Invoke-Task26Mutation $state 'video-bind' 'output.setVideoEncoder' @{ output = $Context.OutputHandle; slot = 0; encoder = $Context.VideoHandle } @('encoder.bindingChanged', 'output.configurationChanged') 'output.setVideoEncoder'
    $null = Invoke-Task26Mutation $state 'audio1-bind' 'output.setAudioEncoder' @{ output = $Context.OutputHandle; slot = 0; encoder = $Context.Audio1Handle } @('encoder.bindingChanged', 'output.configurationChanged') 'output.setAudioEncoder 0'
    $null = Invoke-Task26Mutation $state 'audio2-bind' 'output.setAudioEncoder' @{ output = $Context.OutputHandle; slot = 1; encoder = $Context.Audio2Handle } @('encoder.bindingChanged', 'output.configurationChanged') 'output.setAudioEncoder 1'
    $encoders = Send-Task26 @{ op = 'request'; id = 'encoders'; method = 'output.getEncoders'; params = @{ output = $Context.OutputHandle } }
    Assert-Ok $encoders $state.Current 'output.getEncoders'
    if (@($encoders.data.videoEncoders).Count -ne 1 -or @($encoders.data.audioEncoders).Count -ne 2) { Fail-Task26 'output encoder slots were not explicit.' }
    $codecBad = Send-Task26 @{ op = 'request'; id = 'codec-bad'; method = 'output.setVideoEncoder'; params = @{ output = $Context.OutputHandle; slot = 0; encoder = '999999' } }
    Assert-Error $codecBad 'not_found' $state.Current 'unknown output encoder'
    $codecMismatch = Send-Task26 @{ op = 'request'; id = 'codec-mismatch'; method = 'output.setVideoEncoder'; params = @{ output = $Context.OutputHandle; slot = 0; encoder = $Context.IncompatibleHandle } }
    Assert-Error $codecMismatch 'unsupported_capability' $state.Current 'incompatible output codec'
    $settings = Invoke-Task26Mutation $state 'settings' 'output.patchSettings' @{ output = $Context.OutputHandle; settings = @{ async_stop = $false } } @('output.configurationChanged') 'output.patchSettings'
    $delay = Invoke-Task26Mutation $state 'delay' 'output.setDelay' @{ output = $Context.OutputHandle; seconds = 3; preserve = $true } @('output.configurationChanged') 'output.setDelay'
    if ([int]$delay.data.state.delay.seconds -ne 3 -or -not $delay.data.state.delay.preserve) { Fail-Task26 'output delay was not applied.' }
    $null = Invoke-Task26Mutation $state 'delay-clear' 'output.setDelay' @{ output = $Context.OutputHandle; seconds = 0; preserve = $false } @('output.configurationChanged') 'output.clearDelay'
    $reconnect = Invoke-Task26Mutation $state 'reconnect' 'output.setReconnect' @{ output = $Context.OutputHandle; enabled = $true; retryCount = 4; retryDelaySeconds = 1 } @('output.configurationChanged') 'output.setReconnect'
    if ([int]$reconnect.data.state.reconnectPolicy.retryCount -ne 4) { Fail-Task26 'output reconnect policy was not applied.' }
}

function Invoke-Task26SynchronousLifecycle($Context) {
    $state = $Context.State
    $start = Invoke-Task26Mutation $state 'start' 'output.start' @{ output = $Context.OutputHandle } @('output.started', 'encoder.activeChanged', 'encoder.activeChanged', 'service.activeChanged') 'output.start'
    if ([string]$start.data.state.state -ne 'active') { Fail-Task26 'synchronous output start did not become active.' }
    $shared = Invoke-Task26Mutation $state 'shared-output-create' 'output.create' @{ kind = 'null_output'; name = 'task26-shared' } @('output.created') 'shared output.create'
    $Context.SharedOutputHandle = [string]$shared.data.output
    $null = Invoke-Task26Mutation $state 'shared-video-bind' 'output.setVideoEncoder' @{ output = $Context.SharedOutputHandle; slot = 0; encoder = $Context.VideoHandle } @('encoder.bindingChanged', 'output.configurationChanged') 'shared video binding'
    $null = Invoke-Task26Mutation $state 'shared-audio-bind' 'output.setAudioEncoder' @{ output = $Context.SharedOutputHandle; slot = 0; encoder = $Context.Audio1Handle } @('encoder.bindingChanged', 'output.configurationChanged') 'shared audio binding'
    $null = Invoke-Task26Mutation $state 'shared-start' 'output.start' @{ output = $Context.SharedOutputHandle } @('output.started') 'shared output.start'
    $null = Invoke-Task26Mutation $state 'shared-stop' 'output.stop' @{ output = $Context.SharedOutputHandle } @('output.stopping') 'shared output.stop'
    Read-Task26Event 'output.stopped' ($state.Current + 1) | Out-Null; $state.Current++
    $sharedState = Send-Task26 @{ op = 'request'; id = 'shared-video-state'; method = 'encoder.getState'; params = @{ encoder = $Context.VideoHandle } }
    Assert-Ok $sharedState $state.Current 'shared encoder remains active'
    if (-not $sharedState.data.active) { Fail-Task26 'shared video encoder became inactive while another output was active.' }
    $null = Invoke-Task26Mutation $state 'shared-video-unbind' 'output.setVideoEncoder' @{ output = $Context.SharedOutputHandle; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'shared video unbind'
    $null = Invoke-Task26Mutation $state 'shared-audio-unbind' 'output.setAudioEncoder' @{ output = $Context.SharedOutputHandle; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'shared audio unbind'
    $null = Invoke-Task26Mutation $state 'shared-remove' 'output.remove' @{ output = $Context.SharedOutputHandle } @('output.removed') 'shared output.remove'
    $paused = Invoke-Task26Mutation $state 'pause' 'output.setPaused' @{ output = $Context.OutputHandle; paused = $true } @('output.paused') 'output.pause'
    if (-not $paused.data.state.paused) { Fail-Task26 'output pause state was not true.' }
    $unpaused = Invoke-Task26Mutation $state 'unpause' 'output.setPaused' @{ output = $Context.OutputHandle; paused = $false } @('output.paused') 'output.unpause'
    if ($unpaused.data.state.paused) { Fail-Task26 'output pause state was not false.' }
    $activeSettings = Send-Task26 @{ op = 'request'; id = 'active-settings'; method = 'output.patchSettings'; params = @{ output = $Context.OutputHandle; settings = @{ async_start = $true } } }
    Assert-Error $activeSettings 'busy' $state.Current 'active output settings'
    $activeRemove = Send-Task26 @{ op = 'request'; id = 'active-remove'; method = 'output.remove'; params = @{ output = $Context.OutputHandle } }
    Assert-Error $activeRemove 'object_in_use' $state.Current 'active output removal'
    $duplicateStart = Send-Task26 @{ op = 'request'; id = 'duplicate-start'; method = 'output.start'; params = @{ output = $Context.OutputHandle } }
    Assert-Error $duplicateStart 'invalid_state' $state.Current 'duplicate output.start'
    $stats = Send-Task26 @{ op = 'request'; id = 'stats'; method = 'output.getStats'; params = @{ output = $Context.OutputHandle } }
    Assert-Ok $stats $state.Current 'output.getStats'
}

function Invoke-Task26AsyncLifecycle($Context) {
    $state = $Context.State
    $null = Invoke-Task26Mutation $state 'stop' 'output.stop' @{ output = $Context.OutputHandle } @('output.stopping') 'output.stop'
    Read-Task26Event 'output.stopped' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'service.activeChanged' ($state.Current + 1) | Out-Null; $state.Current++
    Assert-Ok (Send-Task26 @{ op = 'request'; id = 'idle-stop'; method = 'output.stop'; params = @{ output = $Context.OutputHandle } }) $state.Current 'idle output.stop'
    $null = Invoke-Task26Mutation $state 'async-patch' 'output.patchSettings' @{ output = $Context.OutputHandle; settings = @{ async_start = $true; async_stop = $true } } @('output.configurationChanged') 'async output settings'
    $asyncStart = Invoke-Task26Mutation $state 'async-start' 'output.start' @{ output = $Context.OutputHandle } @('output.starting') 'async output.start'
    if ([string]$asyncStart.data.state.state -notin @('starting', 'active')) { Fail-Task26 'async output did not report starting/active.' }
    Read-Task26Event 'output.started' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'service.activeChanged' ($state.Current + 1) | Out-Null; $state.Current++
    $null = Invoke-Task26Mutation $state 'async-stop' 'output.stop' @{ output = $Context.OutputHandle } @('output.stopping') 'async output.stop'
    Read-Task26Event 'output.stopped' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'service.activeChanged' ($state.Current + 1) | Out-Null; $state.Current++
    $null = Invoke-Task26Mutation $state 'reconnect-setup' 'output.patchSettings' @{ output = $Context.OutputHandle; settings = @{ async_start = $false; disconnect_once = $true } } @('output.configurationChanged') 'reconnect settings'
    $null = Invoke-Task26Mutation $state 'reconnect-policy' 'output.setReconnect' @{ output = $Context.OutputHandle; enabled = $true; retryCount = 1; retryDelaySeconds = 1 } @('output.configurationChanged') 'reconnect test policy'
    $null = Invoke-Task26Mutation $state 'reconnect-start' 'output.start' @{ output = $Context.OutputHandle } @('output.started', 'encoder.activeChanged', 'encoder.activeChanged', 'service.activeChanged') 'reconnect test start'
    Read-Task26Event 'output.reconnecting' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'service.activeChanged' ($state.Current + 1) | Out-Null; $state.Current++
    Read-Task26Event 'output.reconnected' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'service.activeChanged' ($state.Current + 1) | Out-Null; $state.Current++
    $null = Invoke-Task26Mutation $state 'force-stop' 'output.forceStop' @{ output = $Context.OutputHandle } @('output.stopping') 'output.forceStop'
    Read-Task26Event 'output.stopped' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'encoder.activeChanged' ($state.Current + 1) | Out-Null
    Read-Task26Event 'service.activeChanged' ($state.Current + 1) | Out-Null; $state.Current++
}

function Invoke-Task26Cleanup($Context) {
    $state = $Context.State
    $null = Invoke-Task26Mutation $state 'failure-patch' 'output.patchSettings' @{ output = $Context.OutputHandle; settings = @{ fail_start = $true; async_start = $false } } @('output.configurationChanged') 'failure settings'
    $failedStart = Send-Task26 @{ op = 'request'; id = 'failed-start'; method = 'output.start'; params = @{ output = $Context.OutputHandle } }
    Assert-Error $failedStart 'obs_error' $state.Current 'rejected output.start'
    $lastError = Send-Task26 @{ op = 'request'; id = 'last-error'; method = 'output.getLastError'; params = @{ output = $Context.OutputHandle } }
    Assert-Ok $lastError $state.Current 'output.getLastError'
    if ([string]$lastError.data.message -notmatch '\[redacted\]' -or [string]$lastError.data.message -match $script:Sentinel) { Fail-Task26 'output last error was not secret-redacted.' }
    $null = Invoke-Task26Mutation $state 'unbind-service' 'output.setService' @{ output = $Context.OutputHandle; service = $null } @('service.bindingChanged', 'output.configurationChanged') 'output.unbind service'
    $null = Invoke-Task26Mutation $state 'unbind-video' 'output.setVideoEncoder' @{ output = $Context.OutputHandle; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'output.unbind video'
    $null = Invoke-Task26Mutation $state 'unbind-audio1' 'output.setAudioEncoder' @{ output = $Context.OutputHandle; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'output.unbind audio 0'
    $null = Invoke-Task26Mutation $state 'unbind-audio2' 'output.setAudioEncoder' @{ output = $Context.OutputHandle; slot = 1; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'output.unbind audio 1'
    $null = Invoke-Task26Mutation $state 'output-rename' 'output.rename' @{ output = $Context.OutputHandle; name = 'task26-output-renamed' } @('output.renamed') 'output.rename'
    $null = Invoke-Task26Mutation $state 'output-remove' 'output.remove' @{ output = $Context.OutputHandle } @('output.removed') 'output.remove'
    $null = Invoke-Task26Mutation $state 'service-remove' 'service.remove' @{ service = $Context.ServiceHandle } @('service.removed') 'service.remove'
    $videoFinal = Send-Task26 @{ op = 'request'; id = 'video-final'; method = 'encoder.getState'; params = @{ encoder = $Context.VideoHandle } }
    Assert-Ok $videoFinal $state.Current 'video final state'
    if ($videoFinal.data.active -or @($videoFinal.data.boundOutputs).Count -ne 0) { Fail-Task26 "video final state retained active/bound output: $($videoFinal | ConvertTo-Json -Compress -Depth 20)" }
    $null = Invoke-Task26Mutation $state 'video-remove' 'encoder.remove' @{ encoder = $Context.VideoHandle } @('encoder.removed') 'video encoder.remove'
    $null = Invoke-Task26Mutation $state 'audio1-remove' 'encoder.remove' @{ encoder = $Context.Audio1Handle } @('encoder.removed') 'audio1 encoder.remove'
    $null = Invoke-Task26Mutation $state 'audio2-remove' 'encoder.remove' @{ encoder = $Context.Audio2Handle } @('encoder.removed') 'audio2 encoder.remove'
    $null = Invoke-Task26Mutation $state 'incompatible-remove' 'encoder.remove' @{ encoder = $Context.IncompatibleHandle } @('encoder.removed') 'incompatible encoder.remove'
    $close = Send-Task26 @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $state.Current; params = @{} }
    Assert-Ok $close ($state.Current + 1) 'session.close'
    Read-Task26Event 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task26 'engine did not exit.' }
    Stop-Task26Engine
    Write-Output 'Task 26 output integration: PASS'
}

function Invoke-Task26Scenario($state) {
    Invoke-Task26KindChecks
    $context = [pscustomobject]@{ State = $state; ServiceHandle = ''; VideoHandle = ''; Audio1Handle = ''; Audio2Handle = ''; OutputHandle = ''; IncompatibleHandle = ''; SharedOutputHandle = '' }
    Invoke-Task26CreateObjects $context
    Invoke-Task26ConfigureOutput $context
    Invoke-Task26SynchronousLifecycle $context
    Invoke-Task26AsyncLifecycle $context
    Invoke-Task26Cleanup $context
}

try {
    $state = Initialize-Task26Session
    Invoke-Task26Scenario $state
} catch {
    try { Stop-Task26Engine } catch { }
    throw
}

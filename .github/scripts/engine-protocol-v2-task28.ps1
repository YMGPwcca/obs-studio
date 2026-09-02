param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:AllEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:Sentinel = [Guid]::NewGuid().ToString('N')

function Fail-Task28([string] $Message) { throw "Task 28: $Message" }

function Start-Task28Engine([string] $Root) {
    $engine = Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $Root).Path -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task28 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    foreach ($plugin in @('task23-encoder', 'task25-service', 'task26-output')) { $info.ArgumentList.Add("--plugin=$plugin") }
    $info.UseShellExecute = $false; $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true; $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task28 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:AllEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Stop-Task28Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr.Contains($script:Sentinel)) { Fail-Task28 'secret sentinel appeared on stderr.' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task28 "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task28Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task28 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task28 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task28 "non-JSON stdout: $line" }
}

function Assert-NoSentinel($Value, [string] $Label) {
    if (($Value | ConvertTo-Json -Compress -Depth 60).Contains($script:Sentinel)) { Fail-Task28 "$Label exposed the secret sentinel." }
}

function Send-Task28([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task28Message
        if ($message.op -eq 'event') { Assert-NoSentinel $message 'streaming event'; $script:PendingEvents.Add($message); $script:AllEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task28 "wrong response for $($Request.id)." }
        Assert-NoSentinel $message "response $($Request.id)"; return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task28 "$Label did not succeed at the expected revision." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    $actual = if ($null -ne $Response.status.PSObject.Properties['code']) { [string]$Response.status.code } else { '<missing>' }
    if ($Response.status.ok -or $actual -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task28 "$Label did not return $Code (actual=$actual)." }
}

function Read-Task28Event([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task28Message }
        Assert-NoSentinel $event "event $Name"
        if ($event.op -ne 'event') { Fail-Task28 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task28 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task28 "event $Name has the wrong revision." }
        return $event
    }
}

function Invoke-Task28Mutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task28 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-Ok $response ($State.Current + 1) $Label; $State.Current++
    foreach ($event in $Events) { Read-Task28Event $event $State.Current | Out-Null }
    return $response
}

function Initialize-Task28Session {
    Start-Task28Engine $InstallRoot
    if ([string](Read-Task28Message).event -ne 'ready') { Fail-Task28 'ready marker was not received.' }
    $hello = Send-Task28 @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }; Assert-Ok $hello 0 'session.hello'
    $required = @('streaming.v1', 'streaming.getConfig.v1', 'streaming.configure.v1', 'streaming.unconfigure.v1',
        'streaming.start.v1', 'streaming.stop.v1', 'streaming.forceStop.v1', 'streaming.getState.v1',
        'streaming.getStats.v1', 'streaming.getService.v1', 'streaming.setService.v1',
        'streaming.getReconnectState.v1', 'streaming.getLastError.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task28 "missing capability $name." } }
    $sub = Send-Task28 @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'streaming.*' }, @{ pattern = 'encoder.*' },
                @{ pattern = 'service.*' }, @{ pattern = 'engine.stopping' }) } }; Assert-Ok $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0; Service = ''; Video = ''; Audio = ''; Output = '' }
}

function Invoke-Task28Create($State) {
    $service = Invoke-Task28Mutation $State 'service' 'service.create' @{ kind = 'task25_test_service'; name = 'task28-service'; settings = @{ key = $script:Sentinel; server = 'rtmp://127.0.0.1/task28' } } @('service.created') 'service.create'
    $State.Service = [string]$service.data.service
    $video = Invoke-Task28Mutation $State 'video' 'encoder.create' @{ type = 'video'; kind = 'task23_test_video'; name = 'task28-video' } @('encoder.created') 'video encoder.create'; $State.Video = [string]$video.data.encoder
    $audio = Invoke-Task28Mutation $State 'audio' 'encoder.create' @{ type = 'audio'; kind = 'task23_test_audio'; name = 'task28-audio'; audioTrack = 1 } @('encoder.created') 'audio encoder.create'; $State.Audio = [string]$audio.data.encoder
    $output = Invoke-Task28Mutation $State 'output' 'output.create' @{ kind = 'task26_test_output'; name = 'task28-output'; settings = @{ async_stop = $true } } @('output.created') 'output.create'; $State.Output = [string]$output.data.output
}

function Invoke-Task28Configure($State) {
    $config = Invoke-Task28Mutation $State 'configure' 'streaming.configure' @{ output = $State.Output } @('streaming.configChanged') 'streaming.configure'
    if (-not $config.data.configured -or [string]$config.data.output -ne $State.Output) { Fail-Task28 'streaming configuration was not assigned.' }
    $view = Send-Task28 @{ op = 'request'; id = 'output-view'; method = 'output.get'; params = @{ output = $State.Output } }; Assert-Ok $view $State.Current 'streaming Output view'
    if ([string]$view.data.state.role -ne 'streaming') { Fail-Task28 'streaming role was not visible on the Output.' }
    $null = Invoke-Task28Mutation $State 'service-bind' 'streaming.setService' @{ service = $State.Service } @('service.bindingChanged', 'output.configurationChanged') 'streaming.setService'
    $null = Invoke-Task28Mutation $State 'video-bind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Video } @('encoder.bindingChanged', 'output.configurationChanged') 'video binding'
    $null = Invoke-Task28Mutation $State 'audio-bind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Audio } @('encoder.bindingChanged', 'output.configurationChanged') 'audio binding'
    foreach ($request in @(
        @{ op = 'request'; id = 'config-read'; method = 'streaming.getConfig'; params = @{} },
        @{ op = 'request'; id = 'state-read'; method = 'streaming.getState'; params = @{} },
        @{ op = 'request'; id = 'stats-read'; method = 'streaming.getStats'; params = @{} },
        @{ op = 'request'; id = 'service-read'; method = 'streaming.getService'; params = @{} },
        @{ op = 'request'; id = 'reconnect-read'; method = 'streaming.getReconnectState'; params = @{} },
        @{ op = 'request'; id = 'error-read'; method = 'streaming.getLastError'; params = @{} }
    )) { Assert-Ok (Send-Task28 $request) $State.Current $request.id }
}

function Invoke-Task28Lifecycle($State) {
    $start = Invoke-Task28Mutation $State 'start' 'streaming.start' @{} @('output.started', 'encoder.activeChanged', 'encoder.activeChanged', 'service.activeChanged') 'streaming.start'
    if ([string]$start.data.state.state -ne 'active') { Fail-Task28 'streaming did not become active.' }
    Start-Sleep -Milliseconds 100
    $stop = Invoke-Task28Mutation $State 'stop' 'streaming.stop' @{} @('output.stopping') 'streaming.stop'
    $next = $State.Current + 1
    Read-Task28Event 'output.stopped' $next | Out-Null
    Read-Task28Event 'encoder.activeChanged' $next | Out-Null
    Read-Task28Event 'encoder.activeChanged' $next | Out-Null
    Read-Task28Event 'service.activeChanged' $next | Out-Null
    $State.Current = $next
    $idleForceStop = Send-Task28 @{ op = 'request'; id = 'force-stop-idle'; method = 'streaming.forceStop'; params = @{} }
    Assert-Ok $idleForceStop $State.Current 'streaming.forceStop idle'
}

function Invoke-Task28Cleanup($State) {
    $null = Invoke-Task28Mutation $State 'service-unbind' 'streaming.setService' @{ service = $null } @('service.bindingChanged', 'output.configurationChanged') 'streaming service unbind'
    $null = Invoke-Task28Mutation $State 'unconfigure' 'streaming.unconfigure' @{} @('streaming.configChanged') 'streaming.unconfigure'
    $null = Invoke-Task28Mutation $State 'video-unbind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'video unbind'
    $null = Invoke-Task28Mutation $State 'audio-unbind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'audio unbind'
    $null = Invoke-Task28Mutation $State 'output-remove' 'output.remove' @{ output = $State.Output } @('output.removed') 'output.remove'
    $null = Invoke-Task28Mutation $State 'service-remove' 'service.remove' @{ service = $State.Service } @('service.removed') 'service.remove'
    $null = Invoke-Task28Mutation $State 'video-remove' 'encoder.remove' @{ encoder = $State.Video } @('encoder.removed') 'video remove'
    $null = Invoke-Task28Mutation $State 'audio-remove' 'encoder.remove' @{ encoder = $State.Audio } @('encoder.removed') 'audio remove'
    $close = Send-Task28 @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }; Assert-Ok $close ($State.Current + 1) 'session.close'
    Read-Task28Event 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task28 'engine did not exit.' }
    Stop-Task28Engine
    if (@($script:AllEvents | Where-Object { [string]$_.event -in @('streaming.started', 'streaming.stopped', 'streaming.reconnecting', 'streaming.reconnected') }).Count -ne 0) { Fail-Task28 'streaming emitted a duplicate Output lifecycle alias.' }
    Write-Output 'Task 28 streaming integration: PASS'
}

try {
    $state = Initialize-Task28Session
    Invoke-Task28Create $state
    Invoke-Task28Configure $state
    Invoke-Task28Lifecycle $state
    Invoke-Task28Cleanup $state
} catch {
    try { Stop-Task28Engine } catch { }
    throw
}

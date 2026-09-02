param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:EngineErrorTask = $null
$script:Receiver = $null
$script:ReceiverErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:AllEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:Sentinel = [Guid]::NewGuid().ToString('N')
$script:ReceiverPaths = [System.Collections.Generic.List[string]]::new()

function Fail-Task28Physical([string] $Message) { throw "Task 28 physical: $Message" }

function Start-Task28PhysicalEngine([string] $Root) {
    $engine = Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $Root).Path -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task28Physical 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName; $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task28Physical 'failed to start the production engine package.' }
    $script:EngineErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:AllEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Start-Task28LoopbackReceiver([string] $CapturePath) {
    $ffmpeg = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($null -eq $ffmpeg) { Fail-Task28Physical 'ffmpeg.exe is required for the local loopback receiver.' }
    $script:ReceiverPaths.Add($CapturePath)
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $ffmpeg.Source
    $info.WorkingDirectory = (Split-Path -Parent $CapturePath); $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $listenUrl = "rtmp://127.0.0.1:1935/live/$($script:Sentinel)"
    foreach ($argument in @('-hide_banner', '-loglevel', 'warning', '-listen', '1', '-i', $listenUrl, '-c', 'copy', '-t', '12', '-y', $CapturePath)) { $info.ArgumentList.Add($argument) }
    $script:Receiver = [Diagnostics.Process]::new(); $script:Receiver.StartInfo = $info
    if (-not $script:Receiver.Start()) { Fail-Task28Physical 'failed to start the local RTMP receiver.' }
    $script:ReceiverErrorTask = $script:Receiver.StandardError.ReadToEndAsync()
    Start-Sleep -Milliseconds 700
    if ($script:Receiver.HasExited) { $stderr = $script:ReceiverErrorTask.GetAwaiter().GetResult(); Fail-Task28Physical "local RTMP receiver exited: $stderr" }
}

function Stop-Task28LoopbackReceiver {
    if ($null -eq $script:Receiver) { return }
    if (-not $script:Receiver.HasExited) { $script:Receiver.Kill(); $script:Receiver.WaitForExit() }
    $stderr = if ($null -ne $script:ReceiverErrorTask) { $script:ReceiverErrorTask.GetAwaiter().GetResult() } else { '' }
    $script:Receiver = $null
}

function Stop-Task28PhysicalEngine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:EngineErrorTask) { $script:EngineErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr.Contains($script:Sentinel)) { Fail-Task28Physical 'secret sentinel appeared on engine stderr.' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task28Physical "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task28PhysicalMessage {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task28Physical 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task28Physical 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task28Physical "non-JSON stdout: $line" }
}

function Assert-NoSentinel($Value, [string] $Label) {
    if (($Value | ConvertTo-Json -Compress -Depth 60).Contains($script:Sentinel)) { Fail-Task28Physical "$Label exposed the secret sentinel." }
}

function Send-Task28Physical([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task28PhysicalMessage
        if ($message.op -eq 'event') { Assert-NoSentinel $message 'streaming event'; $script:PendingEvents.Add($message); $script:AllEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task28Physical "wrong response for $($Request.id)." }
        Assert-NoSentinel $message "response $($Request.id)"; return $message
    }
}

function Assert-PhysicalOk($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task28Physical "$Label did not succeed at revision $Revision." }
}

function Read-PhysicalEvent([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task28PhysicalMessage }
        Assert-NoSentinel $event "event $Name"
        if ($event.op -ne 'event') { Fail-Task28Physical 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task28Physical 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task28Physical "event $Name has the wrong revision." }
        return $event
    }
}

function Invoke-Task28PhysicalMutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task28Physical @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-PhysicalOk $response ($State.Current + 1) $Label; $State.Current++
    foreach ($event in $Events) { Read-PhysicalEvent $event $State.Current | Out-Null }
    return $response
}

function Initialize-Task28PhysicalSession {
    Start-Task28PhysicalEngine $InstallRoot
    if ([string](Read-Task28PhysicalMessage).event -ne 'ready') { Fail-Task28Physical 'ready marker was not received.' }
    $hello = Send-Task28Physical @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }; Assert-PhysicalOk $hello 0 'session.hello'
    $required = @('streaming.v1', 'streaming.getConfig.v1', 'streaming.configure.v1', 'streaming.unconfigure.v1', 'streaming.start.v1', 'streaming.stop.v1', 'streaming.forceStop.v1', 'streaming.getState.v1', 'streaming.getStats.v1', 'streaming.getService.v1', 'streaming.setService.v1', 'streaming.getReconnectState.v1', 'streaming.getLastError.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name }); foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task28Physical "missing capability $name." } }
    $sub = Send-Task28Physical @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'streaming.*' }, @{ pattern = 'encoder.*' }, @{ pattern = 'service.*' }, @{ pattern = 'engine.stopping' }) } }; Assert-PhysicalOk $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0; Service = ''; Video = ''; Audio = ''; Output = '' }
}

function Invoke-Task28PhysicalSetup($State) {
    $service = Invoke-Task28PhysicalMutation $State 'service' 'service.create' @{ kind = 'rtmp_custom'; name = 'loopback-service'; settings = @{ server = 'rtmp://127.0.0.1:1935/live'; key = $script:Sentinel; use_auth = $false } } @('service.created') 'rtmp_custom service.create'; $State.Service = [string]$service.data.service
    $video = Invoke-Task28PhysicalMutation $State 'video' 'encoder.create' @{ type = 'video'; kind = 'obs_x264'; name = 'loopback-video'; settings = @{ bitrate = 1200 } } @('encoder.created') 'production video encoder.create'; $State.Video = [string]$video.data.encoder
    $audio = Invoke-Task28PhysicalMutation $State 'audio' 'encoder.create' @{ type = 'audio'; kind = 'ffmpeg_aac'; name = 'loopback-audio'; audioTrack = 1; settings = @{ bitrate = 96 } } @('encoder.created') 'production audio encoder.create'; $State.Audio = [string]$audio.data.encoder
    $output = Invoke-Task28PhysicalMutation $State 'output' 'output.create' @{ kind = 'rtmp_output'; name = 'loopback-output' } @('output.created') 'real RTMP output.create'; $State.Output = [string]$output.data.output
    $null = Invoke-Task28PhysicalMutation $State 'configure' 'streaming.configure' @{ output = $State.Output } @('streaming.configChanged') 'streaming.configure'
    $null = Invoke-Task28PhysicalMutation $State 'service-bind' 'streaming.setService' @{ service = $State.Service } @('service.bindingChanged', 'output.configurationChanged') 'streaming.setService'
    $null = Invoke-Task28PhysicalMutation $State 'video-bind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Video } @('encoder.bindingChanged', 'output.configurationChanged') 'RTMP video binding'
    $null = Invoke-Task28PhysicalMutation $State 'audio-bind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Audio } @('encoder.bindingChanged', 'output.configurationChanged') 'RTMP audio binding'
    $null = Invoke-Task28PhysicalMutation $State 'reconnect' 'output.setReconnect' @{ output = $State.Output; enabled = $true; retryCount = 1; retryDelaySeconds = 1 } @('output.configurationChanged') 'RTMP reconnect policy'
}

function Invoke-Task28PhysicalStream($State, [string] $OutputDirectory) {
    $firstCapture = Join-Path $OutputDirectory ("task28-loopback-first-$([Guid]::NewGuid().ToString('N')).flv")
    Start-Task28LoopbackReceiver $firstCapture
    $start = Send-Task28Physical @{ op = 'request'; id = 'start'; method = 'streaming.start'; params = @{} }; Assert-PhysicalOk $start ($State.Current + 1) 'streaming.start'; $State.Current++
    Read-PhysicalEvent 'output.starting' $State.Current | Out-Null
    Read-PhysicalEvent 'output.started' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'service.activeChanged' ($State.Current + 1) | Out-Null; $State.Current++
    Start-Sleep -Seconds 3
    $active = Send-Task28Physical @{ op = 'request'; id = 'active'; method = 'streaming.getState'; params = @{} }; Assert-PhysicalOk $active $State.Current 'streaming active state'
    if (-not $active.data.active -or [string]$active.data.service -eq '') { Fail-Task28Physical 'streaming active state was not connected.' }
    Stop-Task28LoopbackReceiver
    Read-PhysicalEvent 'output.reconnecting' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'service.activeChanged' ($State.Current + 1) | Out-Null; $State.Current++
    $secondCapture = Join-Path $OutputDirectory ("task28-loopback-reconnect-$([Guid]::NewGuid().ToString('N')).flv")
    Start-Task28LoopbackReceiver $secondCapture
    Read-PhysicalEvent 'output.reconnected' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' ($State.Current + 1) | Out-Null
    Read-PhysicalEvent 'service.activeChanged' ($State.Current + 1) | Out-Null; $State.Current++
    Start-Sleep -Seconds 2
    $null = Invoke-Task28PhysicalMutation $State 'stop' 'streaming.stop' @{} @('output.stopping') 'streaming.stop'
    $next = $State.Current + 1
    Read-PhysicalEvent 'output.stopped' $next | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' $next | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' $next | Out-Null
    Read-PhysicalEvent 'service.activeChanged' $next | Out-Null; $State.Current = $next
    Stop-Task28LoopbackReceiver
}

function Assert-Task28LoopbackFiles {
    $ffprobe = Get-Command ffprobe.exe -ErrorAction SilentlyContinue
    if ($null -eq $ffprobe) { Fail-Task28Physical 'ffprobe.exe is required for loopback verification.' }
    $valid = 0
    foreach ($path in @($script:ReceiverPaths | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0) { continue }
        $json = & $ffprobe.Source -v error -show_entries stream=codec_type -show_entries format=duration -of json -- $path
        if ($LASTEXITCODE -ne 0) { continue }
        $probe = $json | ConvertFrom-Json
        $streams = @($probe.streams)
        if (@($streams | Where-Object { [string]$_.codec_type -eq 'video' }).Count -eq 0 -or @($streams | Where-Object { [string]$_.codec_type -eq 'audio' }).Count -eq 0) { continue }
        if ([double]$probe.format.duration -le 0.25) { continue }
        $valid++
    }
    if ($valid -eq 0) { Fail-Task28Physical 'local RTMP receiver did not capture a parseable audio/video stream.' }
    Write-Output "Task 28 physical loopback: PASS ($valid parseable capture file(s))"
}

function Invoke-Task28PhysicalCleanup($State) {
    $null = Invoke-Task28PhysicalMutation $State 'service-unbind' 'streaming.setService' @{ service = $null } @('service.bindingChanged', 'output.configurationChanged') 'streaming service unbind'
    $null = Invoke-Task28PhysicalMutation $State 'unconfigure' 'streaming.unconfigure' @{} @('streaming.configChanged') 'streaming.unconfigure'
    $null = Invoke-Task28PhysicalMutation $State 'video-unbind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'video unbind'
    $null = Invoke-Task28PhysicalMutation $State 'audio-unbind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'audio unbind'
    $null = Invoke-Task28PhysicalMutation $State 'output-remove' 'output.remove' @{ output = $State.Output } @('output.removed') 'output.remove'
    $null = Invoke-Task28PhysicalMutation $State 'service-remove' 'service.remove' @{ service = $State.Service } @('service.removed') 'service.remove'
    $null = Invoke-Task28PhysicalMutation $State 'video-remove' 'encoder.remove' @{ encoder = $State.Video } @('encoder.removed') 'video remove'
    $null = Invoke-Task28PhysicalMutation $State 'audio-remove' 'encoder.remove' @{ encoder = $State.Audio } @('encoder.removed') 'audio remove'
    $close = Send-Task28Physical @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }; Assert-PhysicalOk $close ($State.Current + 1) 'session.close'
    Read-PhysicalEvent 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task28Physical 'engine did not exit.' }
    Stop-Task28PhysicalEngine
    if (@($script:AllEvents | Where-Object { [string]$_.event -in @('streaming.started', 'streaming.stopped', 'streaming.reconnecting', 'streaming.reconnected') }).Count -ne 0) { Fail-Task28Physical 'streaming lifecycle aliases were emitted.' }
}

try {
    $state = Initialize-Task28PhysicalSession
    $outputDirectory = Join-Path (Get-Location).Path 'build_x64\physical-streaming'
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Invoke-Task28PhysicalSetup $state
    Invoke-Task28PhysicalStream $state $outputDirectory
    Assert-Task28LoopbackFiles
    Invoke-Task28PhysicalCleanup $state
} catch {
    try { Stop-Task28LoopbackReceiver } catch { }
    try { Stop-Task28PhysicalEngine } catch { }
    throw
}

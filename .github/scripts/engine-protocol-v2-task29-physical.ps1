param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:AllEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1

function Fail-Task29Physical([string] $Message) { throw "Task 29 physical: $Message" }

function Start-Task29PhysicalEngine([string] $Root) {
    $engine = Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $Root).Path -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task29Physical 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName; $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task29Physical 'failed to start the production engine package.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:AllEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Stop-Task29PhysicalEngine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task29Physical "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task29PhysicalMessage {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task29Physical 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task29Physical 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task29Physical "non-JSON stdout: $line" }
}

function Send-Task29Physical([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task29PhysicalMessage
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); $script:AllEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task29Physical "wrong response for $($Request.id)." }
        return $message
    }
}

function Assert-PhysicalOk($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task29Physical "$Label did not succeed at revision $Revision." }
}

function Read-Task29PhysicalEvent([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task29PhysicalMessage }
        if ($event.op -ne 'event') { Fail-Task29Physical 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task29Physical 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task29Physical "event $Name has the wrong revision." }
        return $event
    }
}

function Invoke-Task29PhysicalMutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task29Physical @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-PhysicalOk $response ($State.Current + 1) $Label; $State.Current++
    foreach ($event in $Events) { Read-Task29PhysicalEvent $event $State.Current | Out-Null }
    return $response
}

function Initialize-Task29PhysicalSession {
    Start-Task29PhysicalEngine $InstallRoot
    if ([string](Read-Task29PhysicalMessage).event -ne 'ready') { Fail-Task29Physical 'ready marker was not received.' }
    $hello = Send-Task29Physical @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }; Assert-PhysicalOk $hello 0 'session.hello'
    $required = @('replayBuffer.v1', 'replayBuffer.getConfig.v1', 'replayBuffer.configure.v1', 'replayBuffer.unconfigure.v1',
        'replayBuffer.start.v1', 'replayBuffer.stop.v1', 'replayBuffer.save.v1', 'replayBuffer.getState.v1',
        'replayBuffer.getStats.v1', 'replayBuffer.getLastFile.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name }); foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task29Physical "missing capability $name." } }
    $sub = Send-Task29Physical @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'replayBuffer.*' }, @{ pattern = 'encoder.*' }, @{ pattern = 'engine.stopping' }) } }; Assert-PhysicalOk $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0; Video = ''; Audio = ''; Output = ''; Path = '' }
}

function Invoke-Task29PhysicalSetup($State, [string] $OutputDirectory) {
    $video = Invoke-Task29PhysicalMutation $State 'video' 'encoder.create' @{
        type = 'video'; kind = 'obs_x264'; name = 'physical-replay-video'; settings = @{ bitrate = 2500 }
    } @('encoder.created') 'production video encoder.create'; $State.Video = [string]$video.data.encoder
    $audio = Invoke-Task29PhysicalMutation $State 'audio' 'encoder.create' @{
        type = 'audio'; kind = 'ffmpeg_aac'; name = 'physical-replay-audio'; audioTrack = 1; settings = @{ bitrate = 128 }
    } @('encoder.created') 'production audio encoder.create'; $State.Audio = [string]$audio.data.encoder
    $output = Invoke-Task29PhysicalMutation $State 'output' 'output.create' @{ kind = 'replay_buffer'; name = 'physical-replay'; settings = @{
            directory = $OutputDirectory; format = 'task29-%CCYY-%MM-%DD-%hh-%mm-%ss'; extension = 'mp4'; allow_spaces = $false;
            max_time_sec = 4; max_size_mb = 100 } } @('output.created') 'production replay Output create'; $State.Output = [string]$output.data.output
    $null = Invoke-Task29PhysicalMutation $State 'configure' 'replayBuffer.configure' @{ output = $State.Output } @('replayBuffer.configChanged') 'production replayBuffer.configure'
    $null = Invoke-Task29PhysicalMutation $State 'video-bind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Video } @('encoder.bindingChanged', 'output.configurationChanged') 'production video binding'
    $null = Invoke-Task29PhysicalMutation $State 'audio-bind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Audio } @('encoder.bindingChanged', 'output.configurationChanged') 'production audio binding'
}

function Invoke-Task29PhysicalReplay($State) {
    $start = Invoke-Task29PhysicalMutation $State 'start' 'replayBuffer.start' @{} @('output.started', 'encoder.activeChanged', 'encoder.activeChanged') 'production replayBuffer.start'
    if ([string]$start.data.state.state -ne 'active') { Fail-Task29Physical 'production replay buffer did not become active.' }
    Start-Sleep -Seconds 6
    $save = Invoke-Task29PhysicalMutation $State 'save' 'replayBuffer.save' @{} @() 'production replayBuffer.save'
    if (-not $save.data.pending) { Fail-Task29Physical 'production replayBuffer.save did not report pending completion.' }
    $saved = Read-Task29PhysicalEvent 'replayBuffer.saved' ($State.Current + 1); $State.Current++
    $path = [string]$saved.data.path
    if ([string]$saved.data.output -ne $State.Output -or [string]::IsNullOrWhiteSpace($path)) { Fail-Task29Physical 'replayBuffer.saved did not report a final path.' }
    $State.Path = $path
    $last = Send-Task29Physical @{ op = 'request'; id = 'last'; method = 'replayBuffer.getLastFile'; params = @{} }; Assert-PhysicalOk $last $State.Current 'production replayBuffer.getLastFile'
    if ([string]$last.data.lastFile -ne $path) { Fail-Task29Physical 'getLastFile did not match replayBuffer.saved.' }
    $null = Invoke-Task29PhysicalMutation $State 'stop' 'replayBuffer.stop' @{} @('output.stopping') 'production replayBuffer.stop'
    $next = $State.Current + 1
    Read-Task29PhysicalEvent 'output.stopped' $next | Out-Null
    Read-Task29PhysicalEvent 'encoder.activeChanged' $next | Out-Null
    Read-Task29PhysicalEvent 'encoder.activeChanged' $next | Out-Null
    $State.Current = $next
}

function Assert-Task29PhysicalFile([string] $Path) {
    $ffprobe = Get-Command ffprobe.exe -ErrorAction SilentlyContinue
    if ($null -eq $ffprobe) { Fail-Task29Physical 'ffprobe.exe is required for physical replay verification.' }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { Fail-Task29Physical "replay file was not created: $Path" }
    $file = Get-Item -LiteralPath $Path
    if ($file.Length -le 0) { Fail-Task29Physical "replay file is empty: $Path" }
    $json = & $ffprobe.Source -v error -show_entries stream=codec_type,codec_name -show_entries format=duration -of json -- $Path
    if ($LASTEXITCODE -ne 0) { Fail-Task29Physical "ffprobe could not parse replay file: $Path" }
    $probe = $json | ConvertFrom-Json
    $streams = @($probe.streams)
    if (@($streams | Where-Object { [string]$_.codec_type -eq 'video' }).Count -eq 0) { Fail-Task29Physical 'replay file has no video stream.' }
    if (@($streams | Where-Object { [string]$_.codec_type -eq 'audio' }).Count -eq 0) { Fail-Task29Physical 'replay file has no configured audio stream.' }
    $duration = [double]$probe.format.duration
    if ($duration -le 0.25) { Fail-Task29Physical "replay duration is not sensible: $duration" }
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    Write-Output ("Task 29 physical replay: PASS ({0} bytes, SHA-256 {1}, {2:N3} s)" -f $file.Length, $hash, $duration)
}

function Invoke-Task29PhysicalCleanup($State) {
    $null = Invoke-Task29PhysicalMutation $State 'video-unbind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'production video unbind'
    $null = Invoke-Task29PhysicalMutation $State 'audio-unbind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'production audio unbind'
    $null = Invoke-Task29PhysicalMutation $State 'unconfigure' 'replayBuffer.unconfigure' @{} @('replayBuffer.configChanged') 'production replayBuffer.unconfigure'
    $null = Invoke-Task29PhysicalMutation $State 'output-remove' 'output.remove' @{ output = $State.Output } @('output.removed') 'production output.remove'
    $null = Invoke-Task29PhysicalMutation $State 'video-remove' 'encoder.remove' @{ encoder = $State.Video } @('encoder.removed') 'production video remove'
    $null = Invoke-Task29PhysicalMutation $State 'audio-remove' 'encoder.remove' @{ encoder = $State.Audio } @('encoder.removed') 'production audio remove'
    $close = Send-Task29Physical @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }; Assert-PhysicalOk $close ($State.Current + 1) 'production session.close'
    Read-Task29PhysicalEvent 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task29Physical 'production engine did not exit.' }
    Stop-Task29PhysicalEngine
}

try {
    $state = Initialize-Task29PhysicalSession
    $outputDirectory = Join-Path (Get-Location).Path 'build_x64\physical-replay'
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Invoke-Task29PhysicalSetup $state $outputDirectory
    Invoke-Task29PhysicalReplay $state
    Assert-Task29PhysicalFile $state.Path
    Invoke-Task29PhysicalCleanup $state
    if (@($script:AllEvents | Where-Object { [string]$_.event -in @('replayBuffer.started', 'replayBuffer.stopped') }).Count -ne 0) { Fail-Task29Physical 'replay buffer lifecycle aliases were emitted.' }
} catch {
    try { Stop-Task29PhysicalEngine } catch { }
    throw
}

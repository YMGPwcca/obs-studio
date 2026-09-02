param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:AllEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1

function Fail-Task27Physical([string] $Message) { throw "Task 27 physical: $Message" }

function Start-Task27PhysicalEngine([string] $Root) {
    $engine = Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $Root).Path -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task27Physical 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName; $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task27Physical 'failed to start the production engine package.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:AllEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Stop-Task27PhysicalEngine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task27Physical "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task27PhysicalMessage {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task27Physical 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task27Physical 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task27Physical "non-JSON stdout: $line" }
}

function Send-Task27Physical([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task27PhysicalMessage
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); $script:AllEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task27Physical "wrong response for $($Request.id)." }
        return $message
    }
}

function Assert-PhysicalOk($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok) { Fail-Task27Physical "$Label failed: $($Response.status.code): $($Response.status.message)." }
    if ([int64]$Response.revision -ne $Revision) { Fail-Task27Physical "$Label revision mismatch." }
}

function Read-PhysicalEvent([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task27PhysicalMessage }
        if ($event.op -ne 'event') { Fail-Task27Physical 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task27Physical 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task27Physical "event $Name revision mismatch." }
        return $event
    }
}

function Invoke-PhysicalMutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task27Physical @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-PhysicalOk $response ($State.Current + 1) $Label; $State.Current++
    foreach ($event in $Events) { Read-PhysicalEvent $event $State.Current | Out-Null }
    return $response
}

function Initialize-Task27PhysicalSession {
    Start-Task27PhysicalEngine $InstallRoot
    if ([string](Read-Task27PhysicalMessage).event -ne 'ready') { Fail-Task27Physical 'ready marker was not received.' }
    $hello = Send-Task27Physical @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }
    Assert-PhysicalOk $hello 0 'session.hello'
    $sub = Send-Task27Physical @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'recording.*' }, @{ pattern = 'encoder.*' }, @{ pattern = 'engine.stopping' }) } }
    Assert-PhysicalOk $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0; Video = ''; Audio = ''; Output = '' }
}

function Invoke-PhysicalSetup($State, [string] $RecordingPath) {
	$recordingDirectory = Split-Path -Parent $RecordingPath
    $video = Invoke-PhysicalMutation $State 'video' 'encoder.create' @{
        type = 'video'; kind = 'obs_x264'; name = 'physical-video'; settings = @{ bitrate = 2500 }
    } @('encoder.created') 'production video encoder.create'
    $State.Video = [string]$video.data.encoder
    $audio = Invoke-PhysicalMutation $State 'audio' 'encoder.create' @{
        type = 'audio'; kind = 'ffmpeg_aac'; name = 'physical-audio'; audioTrack = 1; settings = @{ bitrate = 128 }
    } @('encoder.created') 'production audio encoder.create'
    $State.Audio = [string]$audio.data.encoder
    $output = Invoke-PhysicalMutation $State 'output' 'output.create' @{
        kind = 'mp4_output'; name = 'physical-recording'; settings = @{
            allow_overwrite = $true; split_file = $true; directory = $recordingDirectory;
            format = 'task27-split-%hh-%mm-%ss'; extension = 'mp4'; allow_spaces = $false }
    } @('output.created') 'production recording Output create'
    $State.Output = [string]$output.data.output
    $configured = Invoke-PhysicalMutation $State 'configure' 'recording.configure' @{
        output = $State.Output; path = $RecordingPath; overwrite = $true; createDirectory = $true
    } @('output.configurationChanged', 'recording.configChanged') 'production recording.configure'
    $null = Invoke-PhysicalMutation $State 'video-bind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Video } @('encoder.bindingChanged', 'output.configurationChanged') 'production video binding'
    $null = Invoke-PhysicalMutation $State 'audio-bind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Audio } @('encoder.bindingChanged', 'output.configurationChanged') 'production audio binding'
}

function Invoke-PhysicalRecording($State, [System.Collections.Generic.List[string]] $Paths) {
    $start = Invoke-PhysicalMutation $State 'start' 'recording.start' @{} @('output.started', 'encoder.activeChanged', 'encoder.activeChanged') 'production recording.start'
    if ([string]$start.data.state.state -ne 'active') { Fail-Task27Physical 'production recording did not become active.' }
    Start-Sleep -Seconds 3
    $activeState = Send-Task27Physical @{ op = 'request'; id = 'active-state'; method = 'recording.getState'; params = @{} }
    Assert-PhysicalOk $activeState $State.Current 'production active state'
    if (-not $activeState.data.active -or [string]$activeState.data.currentPath -eq '') { Fail-Task27Physical 'active recording state had no current path.' }
    $Paths.Add([string]$activeState.data.currentPath)
    $null = Invoke-PhysicalMutation $State 'pause' 'recording.pause' @{} @('output.paused') 'production recording.pause'
    Start-Sleep -Milliseconds 500
    $null = Invoke-PhysicalMutation $State 'resume' 'recording.resume' @{} @('output.paused') 'production recording.resume'
    $null = Invoke-PhysicalMutation $State 'chapter' 'recording.addChapter' @{ name = 'physical-chapter' } @('recording.chapterAdded') 'production recording.addChapter'
    $split = Send-Task27Physical @{ op = 'request'; id = 'split'; method = 'recording.splitFile'; params = @{} }
    Assert-PhysicalOk $split ($State.Current + 1) 'production recording.splitFile'
    $State.Current++
    Read-PhysicalEvent 'recording.fileChanged' ($State.Current + 1) | Out-Null
    $State.Current++
    $current = Send-Task27Physical @{ op = 'request'; id = 'split-current'; method = 'recording.getCurrentPath'; params = @{} }
    Assert-PhysicalOk $current $State.Current 'production current path after split'
    $Paths.Add([string]$current.data.currentPath)
    Start-Sleep -Seconds 2
    $null = Invoke-PhysicalMutation $State 'stop' 'recording.stop' @{} @('output.stopping') 'production recording.stop'
    $nextRevision = $State.Current + 1
    Read-PhysicalEvent 'output.stopped' $nextRevision | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' $nextRevision | Out-Null
    Read-PhysicalEvent 'encoder.activeChanged' $nextRevision | Out-Null
    Read-PhysicalEvent 'recording.fileFinalized' $nextRevision | Out-Null
    $State.Current = $nextRevision
    $last = Send-Task27Physical @{ op = 'request'; id = 'last'; method = 'recording.getLastFile'; params = @{} }
    Assert-PhysicalOk $last $State.Current 'production recording.getLastFile'
    if ([string]$last.data.lastFile -ne '') { $Paths.Add([string]$last.data.lastFile) }
}

function Invoke-PhysicalCleanup($State) {
    $null = Invoke-PhysicalMutation $State 'unbind-video' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'production video unbind'
    $null = Invoke-PhysicalMutation $State 'unbind-audio' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'production audio unbind'
    $null = Invoke-PhysicalMutation $State 'unconfigure' 'recording.unconfigure' @{} @('recording.configChanged') 'production recording.unconfigure'
    $null = Invoke-PhysicalMutation $State 'output-remove' 'output.remove' @{ output = $State.Output } @('output.removed') 'production output.remove'
    $null = Invoke-PhysicalMutation $State 'video-remove' 'encoder.remove' @{ encoder = $State.Video } @('encoder.removed') 'production video remove'
    $null = Invoke-PhysicalMutation $State 'audio-remove' 'encoder.remove' @{ encoder = $State.Audio } @('encoder.removed') 'production audio remove'
    $close = Send-Task27Physical @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }
    Assert-PhysicalOk $close ($State.Current + 1) 'production session.close'
    Read-PhysicalEvent 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task27Physical 'production engine did not exit.' }
    Stop-Task27PhysicalEngine
}

function Assert-PhysicalFile($Ffprobe, [string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { Fail-Task27Physical "recording file was not created: $Path" }
    if ((Get-Item -LiteralPath $Path).Length -le 0) { Fail-Task27Physical "recording file is empty: $Path" }
    $json = & $Ffprobe.Source -v error -show_entries stream=codec_type,width,height -show_entries format=duration -of json -- $Path
    if ($LASTEXITCODE -ne 0) { Fail-Task27Physical "ffprobe could not parse $Path." }
    $probe = $json | ConvertFrom-Json
    $streams = @($probe.streams)
    $hasVideo = @($streams | Where-Object { [string]$_.codec_type -eq 'video' }).Count -ne 0
    $hasAudio = @($streams | Where-Object { [string]$_.codec_type -eq 'audio' }).Count -ne 0
    if (-not $hasVideo -or -not $hasAudio) { Fail-Task27Physical "recording lacks both video and audio streams: $Path" }
    $duration = [double]$probe.format.duration
    if ($duration -le 0.25) { Fail-Task27Physical "recording duration is not reasonable: $Path ($duration)." }
}

function Assert-PhysicalFiles([System.Collections.Generic.List[string]] $Paths) {
    $ffprobe = Get-Command ffprobe.exe -ErrorAction SilentlyContinue
    if ($null -eq $ffprobe) { Fail-Task27Physical 'ffprobe.exe is required for the physical parser check.' }
    $unique = @($Paths | Where-Object { $_ } | Select-Object -Unique)
    if ($unique.Count -eq 0) { Fail-Task27Physical 'no finalized recording path was reported.' }
    foreach ($path in $unique) { Assert-PhysicalFile $ffprobe $path }
    Write-Output ("Task 27 physical recording: PASS ({0} file(s))" -f $unique.Count)
}

try {
    $state = Initialize-Task27PhysicalSession
    # The production driver does not load any CI-only module. It depends only
    # on the package's normal obs-x264, obs-ffmpeg, and obs-outputs modules.
    $outputDirectory = Join-Path (Get-Location).Path 'build_x64\physical-recordings'
    $recordingPath = Join-Path $outputDirectory ("task27-physical-$([Guid]::NewGuid().ToString('N')).mp4")
    $paths = [System.Collections.Generic.List[string]]::new()
    Invoke-PhysicalSetup $state $recordingPath
    Invoke-PhysicalRecording $state $paths
    Assert-PhysicalFiles $paths
    Invoke-PhysicalCleanup $state
    $duplicates = @($script:AllEvents | Where-Object { [string]$_.event -in @('recording.started', 'recording.stopped') })
    if ($duplicates.Count -ne 0) { Fail-Task27Physical 'recording lifecycle aliases were emitted.' }
} catch {
    try { Stop-Task27PhysicalEngine } catch { }
    throw
}

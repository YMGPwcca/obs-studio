param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1

function Fail-Task27([string] $Message) { throw "Task 27: $Message" }

function Start-Task27Engine([string] $Root) {
    $resolved = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolved -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task27 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    foreach ($plugin in @('task23-encoder', 'task27-recording')) { $info.ArgumentList.Add("--plugin=$plugin") }
    $info.UseShellExecute = $false; $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true; $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task27 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Stop-Task27Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task27 "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task27Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task27 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task27 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task27 "engine emitted non-JSON stdout: $line" }
}

function Send-Task27([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task27Message
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task27 "wrong response for $($Request.id)." }
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok) { Fail-Task27 "$Label failed: $($Response.status.code): $($Response.status.message)." }
    if ([int64]$Response.revision -ne $Revision) { Fail-Task27 "$Label revision mismatch." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    $actual = if ($null -ne $Response.status.PSObject.Properties['code']) { [string]$Response.status.code } else { '<missing>' }
    if ($Response.status.ok -or $actual -ne $Code -or [int64]$Response.revision -ne $Revision) {
        Fail-Task27 "$Label did not return $Code (actual=$actual)."
    }
}

function Read-Task27Event([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task27Message }
        if ($event.op -ne 'event') { Fail-Task27 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task27 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task27 "event $Name has the wrong revision." }
        return $event
    }
}

function Invoke-Task27Mutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task27 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-Ok $response ($State.Current + 1) $Label; $State.Current++
    foreach ($event in $Events) { Read-Task27Event $event $State.Current | Out-Null }
    return $response
}

function Initialize-Task27Session {
    Start-Task27Engine $InstallRoot
    if ([string](Read-Task27Message).event -ne 'ready') { Fail-Task27 'ready marker was not received.' }
    $hello = Send-Task27 @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $hello 0 'session.hello'
    $required = @('recording.v1', 'recording.getConfig.v1', 'recording.configure.v1', 'recording.unconfigure.v1',
        'recording.start.v1', 'recording.stop.v1', 'recording.forceStop.v1', 'recording.pause.v1',
        'recording.resume.v1', 'recording.togglePause.v1', 'recording.splitFile.v1', 'recording.addChapter.v1',
        'recording.getState.v1', 'recording.getStats.v1', 'recording.getCurrentPath.v1', 'recording.getLastFile.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task27 "missing capability $name." } }
    $sub = Send-Task27 @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'recording.*' }, @{ pattern = 'encoder.*' },
                @{ pattern = 'engine.stopping' }) } }
    Assert-Ok $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0; Video = ''; Audio = ''; Output = ''; Path = '' }
}

function Invoke-Task27CreateAndConfigure($State) {
    $video = Invoke-Task27Mutation $State 'video' 'encoder.create' @{
        type = 'video'; kind = 'task23_test_video'; name = 'task27-video'
    } @('encoder.created') 'video encoder.create'
    $State.Video = [string]$video.data.encoder
    $audio = Invoke-Task27Mutation $State 'audio' 'encoder.create' @{
        type = 'audio'; kind = 'task23_test_audio'; name = 'task27-audio'; audioTrack = 1
    } @('encoder.created') 'audio encoder.create'
    $State.Audio = [string]$audio.data.encoder
    $output = Invoke-Task27Mutation $State 'output' 'output.create' @{
        kind = 'task27_test_recording'; name = 'task27-recording'; settings = @{ split_file = $true }
    } @('output.created') 'recording Output create'
    $State.Output = [string]$output.data.output
    $badPath = Send-Task27 @{ op = 'request'; id = 'bad-path'; method = 'recording.configure'; params = @{
            output = $State.Output; path = 'https://not-a-local-file/test.mkv' } }
    Assert-Error $badPath 'bad_request' $State.Current 'URL recording path'
    $State.Path = Join-Path (Get-Location).Path ("task27-recording-$([Guid]::NewGuid().ToString('N')).mkv")
    $configured = Invoke-Task27Mutation $State 'configure' 'recording.configure' @{
        output = $State.Output; path = $State.Path; overwrite = $false; createDirectory = $false
    } @('output.configurationChanged', 'recording.configChanged') 'recording.configure'
    if (-not $configured.data.configured -or [string]$configured.data.output -ne $State.Output) { Fail-Task27 'recording config was not assigned.' }
    $config = Send-Task27 @{ op = 'request'; id = 'config'; method = 'recording.getConfig'; params = @{} }
    Assert-Ok $config $State.Current 'recording.getConfig'
    $outputView = Send-Task27 @{ op = 'request'; id = 'output-view'; method = 'output.get'; params = @{ output = $State.Output } }
    Assert-Ok $outputView $State.Current 'output.get configured role'
    if ([string]$outputView.data.state.role -ne 'recording' -or [string]$outputView.data.state.managedBy -ne 'recording') { Fail-Task27 'recording role was not visible on the Output.' }
    $remove = Send-Task27 @{ op = 'request'; id = 'role-remove'; method = 'output.remove'; params = @{ output = $State.Output } }
    Assert-Error $remove 'object_in_use' $State.Current 'remove assigned recording Output'
    $path = Send-Task27 @{ op = 'request'; id = 'current-path'; method = 'recording.getCurrentPath'; params = @{} }
    Assert-Ok $path $State.Current 'recording.getCurrentPath'
    if ([string]$path.data.currentPath -eq '' -or [string]$path.data.currentPath -match '://') { Fail-Task27 'recording current path was not canonical local state.' }
    foreach ($request in @(
        @{ op = 'request'; id = 'state'; method = 'recording.getState'; params = @{} },
        @{ op = 'request'; id = 'stats'; method = 'recording.getStats'; params = @{} },
        @{ op = 'request'; id = 'last'; method = 'recording.getLastFile'; params = @{} }
    )) { Assert-Ok (Send-Task27 $request) $State.Current $request.id }
    $null = Invoke-Task27Mutation $State 'video-bind' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Video } @('encoder.bindingChanged', 'output.configurationChanged') 'video binding'
    $null = Invoke-Task27Mutation $State 'audio-bind' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $State.Audio } @('encoder.bindingChanged', 'output.configurationChanged') 'audio binding'
}

function Invoke-Task27Lifecycle($State) {
    $start = Invoke-Task27Mutation $State 'start' 'recording.start' @{} @('output.started', 'encoder.activeChanged', 'encoder.activeChanged') 'recording.start'
    if ([string]$start.data.state.state -ne 'active') { Fail-Task27 'recording did not become active.' }
    $pause = Invoke-Task27Mutation $State 'pause' 'recording.pause' @{} @('output.paused') 'recording.pause'
    if (-not $pause.data.state.paused) { Fail-Task27 'recording pause did not set paused.' }
    $toggle = Invoke-Task27Mutation $State 'toggle' 'recording.togglePause' @{} @('output.paused') 'recording.togglePause'
    if ($toggle.data.state.paused) { Fail-Task27 'recording toggle did not resume.' }
    $resume = Send-Task27 @{ op = 'request'; id = 'resume'; method = 'recording.resume'; params = @{} }
    Assert-Ok $resume $State.Current 'recording.resume no-op'
    $chapter = Invoke-Task27Mutation $State 'chapter' 'recording.addChapter' @{ name = 'chapter-one' } @('recording.chapterAdded') 'recording.addChapter'
    if (-not $chapter.data.active) { Fail-Task27 'chapter response did not retain active state.' }
    $split = Invoke-Task27Mutation $State 'split' 'recording.splitFile' @{} @('recording.fileChanged') 'recording.splitFile'
    $current = Send-Task27 @{ op = 'request'; id = 'split-current'; method = 'recording.getCurrentPath'; params = @{} }
    Assert-Ok $current $State.Current 'recording current path after split'
    if ([string]$current.data.currentPath -notmatch '\.part2$') { Fail-Task27 'fileChanged did not update current path.' }
}

function Invoke-Task27StopAndCleanup($State) {
    $stop = Invoke-Task27Mutation $State 'stop' 'recording.stop' @{} @('output.stopping') 'recording.stop'
    Read-Task27Event 'output.stopped' ($State.Current + 1) | Out-Null
    Read-Task27Event 'encoder.activeChanged' ($State.Current + 1) | Out-Null
    Read-Task27Event 'encoder.activeChanged' ($State.Current + 1) | Out-Null
    Read-Task27Event 'recording.fileFinalized' ($State.Current + 1) | Out-Null
    $State.Current++
    $last = Send-Task27 @{ op = 'request'; id = 'last-file'; method = 'recording.getLastFile'; params = @{} }
    Assert-Ok $last $State.Current 'recording.getLastFile after stop'
    $null = Invoke-Task27Mutation $State 'unbind-video' 'output.setVideoEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'video unbind'
    $null = Invoke-Task27Mutation $State 'unbind-audio' 'output.setAudioEncoder' @{ output = $State.Output; slot = 0; encoder = $null } @('encoder.bindingChanged', 'output.configurationChanged') 'audio unbind'
    $null = Invoke-Task27Mutation $State 'unconfigure' 'recording.unconfigure' @{} @('recording.configChanged') 'recording.unconfigure'
    $null = Invoke-Task27Mutation $State 'output-remove' 'output.remove' @{ output = $State.Output } @('output.removed') 'recording Output remove'
    $null = Invoke-Task27Mutation $State 'video-remove' 'encoder.remove' @{ encoder = $State.Video } @('encoder.removed') 'video encoder remove'
    $null = Invoke-Task27Mutation $State 'audio-remove' 'encoder.remove' @{ encoder = $State.Audio } @('encoder.removed') 'audio encoder remove'
    $close = Send-Task27 @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }
    Assert-Ok $close ($State.Current + 1) 'session.close'
    Read-Task27Event 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task27 'engine did not exit.' }
    Stop-Task27Engine
    Write-Output 'Task 27 recording integration: PASS'
}

try {
    $state = Initialize-Task27Session
    Invoke-Task27CreateAndConfigure $state
    Invoke-Task27Lifecycle $state
    Invoke-Task27StopAndCleanup $state
} catch {
    try { Stop-Task27Engine } catch { }
    throw
}

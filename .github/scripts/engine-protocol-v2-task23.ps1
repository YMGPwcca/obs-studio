param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:LastResponseIndex = -1

function Fail-Task23([string] $Message) { throw "Task 23: $Message" }

function Start-Task23Engine([string] $Root) {
    $resolved = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolved -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task23 'obs-engine.exe was not found.' }
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    $info.ArgumentList.Add('--plugin=task23-encoder')
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Engine = [System.Diagnostics.Process]::new()
    $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task23 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:NextSequence = [uint64]1
    $script:LastResponseIndex = -1
}

function Stop-Task23Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) {
        $script:Engine.Kill()
        $script:Engine.WaitForExit()
    }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') {
        Fail-Task23 'legacy fallback canvas warning was emitted.'
    }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task23 "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task23Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task23 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task23 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task23 "engine emitted non-JSON stdout: $line" }
}

function Send-Task23([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task23Message
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) {
            Fail-Task23 "wrong response for $($Request.id)."
        }
        $script:LastResponseIndex = 0
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok) { Fail-Task23 "$Label failed: $($Response.status.code): $($Response.status.message)." }
    if ([int64]$Response.revision -ne $Revision) { Fail-Task23 "$Label revision=$($Response.revision), expected $Revision." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) {
        Fail-Task23 "$Label did not return $Code at revision $Revision."
    }
}

function Read-Task23Event([string] $Name, [int64] $Revision) {
    while ($true) {
        if ($script:PendingEvents.Count -gt 0) {
            $event = $script:PendingEvents[0]
            $script:PendingEvents.RemoveAt(0)
        } else {
            $event = Read-Task23Message
        }
        if ($event.op -ne 'event') { Fail-Task23 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) {
            Fail-Task23 "event '$($event.event)' seq=$($event.seq), expected $script:NextSequence."
        }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task23 "event '$Name' has the wrong revision." }
        return $event
    }
}

function Assert-Task23Capabilities($Hello) {
    $required = @(
        'encoder.v1', 'encoder.kindList.v1', 'encoder.kindGet.v1', 'encoder.kindDefaults.v1',
        'encoder.kindProperties.v1', 'encoder.kindCapabilities.v1', 'encoder.list.v1', 'encoder.get.v1',
        'encoder.create.v1', 'encoder.remove.v1', 'encoder.rename.v1', 'encoder.getSettings.v1',
        'encoder.patchSettings.v1', 'encoder.replaceSettings.v1', 'encoder.getProperties.v1',
        'encoder.getVideoInput.v1', 'encoder.setVideoInput.v1', 'encoder.getCodec.v1', 'encoder.getType.v1',
        'encoder.getDimensions.v1', 'encoder.getState.v1', 'encoder.setScaledSize.v1',
        'encoder.setScaleFilter.v1', 'encoder.roi.list.v1', 'encoder.roi.add.v1', 'encoder.roi.remove.v1',
        'encoder.roi.clear.v1')
    $caps = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task23 "missing capability $name." } }
    if ($caps -contains 'encoder.getExtraData.v1' -or $caps -contains 'encoder.getSEIData.v1') {
        Fail-Task23 'unbounded encoder binary data was advertised.'
    }
}

function Invoke-Task23Mutation($State, [string] $Id, [string] $Method, [hashtable] $Params, [string] $Event,
    [string] $Label) {
    $response = Send-Task23 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-Ok $response ($State.Current + 1) $Label
    $State.Current++
    if ($Event) { Read-Task23Event $Event $State.Current | Out-Null }
    return $response
}

function Initialize-Task23Session {
    Start-Task23Engine $InstallRoot
    $ready = Read-Task23Message
    if ([string]$ready.event -ne 'ready') { Fail-Task23 'ready marker was not received.' }
    $hello = Send-Task23 @{ op = 'request'; id = 't23-hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $hello 0 'session.hello'
    Assert-Task23Capabilities $hello
    $sub = Send-Task23 @{ op = 'request'; id = 't23-sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'encoder.*' }, @{ pattern = 'engine.stopping' }) } }
    Assert-Ok $sub 0 'session.subscribe'
    $kinds = Send-Task23 @{ op = 'request'; id = 't23-kind-list'; method = 'encoder.kindList'; params = @{} }
    Assert-Ok $kinds 0 'encoder.kindList'
    $videoKind = @($kinds.data.kinds | Where-Object { [string]$_.id -eq 'task23_test_video' }) | Select-Object -First 1
    $audioKind = @($kinds.data.kinds | Where-Object { [string]$_.id -eq 'task23_test_audio' }) | Select-Object -First 1
    if ($null -eq $videoKind -or $null -eq $audioKind) { Fail-Task23 'fixture encoder kinds were not listed.' }
    if (-not $videoKind.registered -or -not $videoKind.moduleLoaded -or [string]$videoKind.actualRuntimeCompatibility -ne 'unknown') {
        Fail-Task23 'kind registration/module/runtime compatibility truth was incorrect.'
    }
    if (-not $videoKind.capabilities.roi -or -not $videoKind.capabilities.scaling) { Fail-Task23 'fixture capabilities were incomplete.' }
    foreach ($request in @(
        @{ op = 'request'; id = 't23-kind-get'; method = 'encoder.kindGet'; params = @{ kind = 'task23_test_video' } },
        @{ op = 'request'; id = 't23-defaults'; method = 'encoder.kindDefaults'; params = @{ kind = 'task23_test_video' } },
        @{ op = 'request'; id = 't23-kind-props'; method = 'encoder.kindProperties'; params = @{ kind = 'task23_test_video' } },
        @{ op = 'request'; id = 't23-kind-caps'; method = 'encoder.kindCapabilities'; params = @{ kind = 'task23_test_video' } }
    )) { Assert-Ok (Send-Task23 $request) 0 $request.id }
    return [pscustomobject]@{ Current = [int64]0 }
}

function Invoke-Task23ObjectReads($State, [string] $Video) {
    foreach ($request in @(
        @{ op = 'request'; id = 't23-get'; method = 'encoder.get'; params = @{ encoder = $Video } },
        @{ op = 'request'; id = 't23-state'; method = 'encoder.getState'; params = @{ encoder = $Video } },
        @{ op = 'request'; id = 't23-settings'; method = 'encoder.getSettings'; params = @{ encoder = $Video } },
        @{ op = 'request'; id = 't23-properties'; method = 'encoder.getProperties'; params = @{ encoder = $Video } },
        @{ op = 'request'; id = 't23-input'; method = 'encoder.getVideoInput'; params = @{ encoder = $Video } },
        @{ op = 'request'; id = 't23-codec'; method = 'encoder.getCodec'; params = @{ encoder = $Video } },
        @{ op = 'request'; id = 't23-type'; method = 'encoder.getType'; params = @{ encoder = $Video } },
        @{ op = 'request'; id = 't23-dimensions'; method = 'encoder.getDimensions'; params = @{ encoder = $Video } }
    )) { Assert-Ok (Send-Task23 $request) $State.Current $request.id }
}

function Invoke-Task23Settings($State, [string] $Video) {
    $rename = Invoke-Task23Mutation $State 't23-rename' 'encoder.rename' @{ encoder = $Video; name = 'task23-video-renamed' } 'encoder.renamed' 'encoder.rename'
    $patch = Invoke-Task23Mutation $State 't23-patch' 'encoder.patchSettings' @{ encoder = $Video; settings = @{ bitrate = 1600 } } 'encoder.settingsChanged' 'encoder.patchSettings'
    if ([int64]$patch.data.settings.bitrate -ne 1600) { Fail-Task23 'patch settings were not canonicalized.' }
    $null = Invoke-Task23Mutation $State 't23-replace' 'encoder.replaceSettings' @{ encoder = $Video; settings = @{ bitrate = 1700; reject_update = $false } } 'encoder.settingsChanged' 'encoder.replaceSettings'
    $badSettings = Send-Task23 @{ op = 'request'; id = 't23-bad-settings'; method = 'encoder.patchSettings'; params = @{ encoder = $Video; settings = @{ bitrate = 0 } } }
    Assert-Error $badSettings 'bad_request' $State.Current 'invalid encoder settings'
}

function Invoke-Task23Scaling($State, [string] $Video) {
    $null = Invoke-Task23Mutation $State 't23-scale' 'encoder.setScaledSize' @{ encoder = $Video; width = 640; height = 360 } 'encoder.scalingChanged' 'encoder.setScaledSize'
    $null = Invoke-Task23Mutation $State 't23-filter' 'encoder.setScaleFilter' @{ encoder = $Video; filter = 'lanczos' } 'encoder.scalingChanged' 'encoder.setScaleFilter'
    $bad = Send-Task23 @{ op = 'request'; id = 't23-bad-filter'; method = 'encoder.setScaleFilter'; params = @{ encoder = $Video; filter = 'bogus' } }
    Assert-Error $bad 'bad_request' $State.Current 'invalid scale filter'
}

function Invoke-Task23Roi($State, [string] $Video) {
    $null = Invoke-Task23Mutation $State 't23-roi-add' 'encoder.roi.add' @{ encoder = $Video; left = 0; top = 0; right = 64; bottom = 64; priority = 0.5 } 'encoder.roiChanged' 'encoder.roi.add'
    $roiList = Send-Task23 @{ op = 'request'; id = 't23-roi-list'; method = 'encoder.roi.list'; params = @{ encoder = $Video } }
    Assert-Ok $roiList $State.Current 'encoder.roi.list'
    if (@($roiList.data.rois).Count -ne 1 -or [int]$roiList.data.rois[0].index -ne 0) { Fail-Task23 'ROI list/index was incorrect.' }
    $bad = Send-Task23 @{ op = 'request'; id = 't23-bad-roi'; method = 'encoder.roi.add'; params = @{ encoder = $Video; left = 64; top = 0; right = 0; bottom = 64; priority = 0 } }
    Assert-Error $bad 'bad_request' $State.Current 'invalid ROI rectangle'
    $null = Invoke-Task23Mutation $State 't23-roi-remove' 'encoder.roi.remove' @{ encoder = $Video; index = 0 } 'encoder.roiChanged' 'encoder.roi.remove'
}

function Invoke-Task23Audio($State) {
    $audio = Invoke-Task23Mutation $State 't23-create-audio' 'encoder.create' @{ type = 'audio'; kind = 'task23_test_audio'; name = 'task23-audio'; audioTrack = 1 } 'encoder.created' 'encoder.create audio'
    $audioHandle = [string]$audio.data.encoder
    $audioState = Send-Task23 @{ op = 'request'; id = 't23-audio-state'; method = 'encoder.getState'; params = @{ encoder = $audioHandle } }
    Assert-Ok $audioState $State.Current 'audio encoder state'
    if ([int]$audioState.data.audioTrack -ne 1 -or $audioState.data.PSObject.Properties.Name -contains 'videoInput') { Fail-Task23 'audio track identity was not canonical.' }
    $badTrack = Send-Task23 @{ op = 'request'; id = 't23-audio-track-bad'; method = 'encoder.create'; params = @{ type = 'audio'; kind = 'task23_test_audio'; name = 'bad-audio'; audioTrack = 0 } }
    Assert-Error $badTrack 'bad_request' $State.Current 'invalid audio track'
    $legacyMix = Send-Task23 @{ op = 'request'; id = 't23-legacy-mix'; method = 'encoder.setAudioMix'; params = @{ encoder = $audioHandle; audioTrack = 2 } }
    Assert-Error $legacyMix 'unsupported_method' $State.Current 'removed encoder.setAudioMix'
    $null = Invoke-Task23Mutation $State 't23-remove-audio' 'encoder.remove' @{ encoder = $audioHandle } 'encoder.removed' 'encoder.remove audio'
}

function Complete-Task23($State, [string] $Video) {
    $null = Invoke-Task23Mutation $State 't23-remove-video' 'encoder.remove' @{ encoder = $Video } 'encoder.removed' 'encoder.remove video'
    $list = Send-Task23 @{ op = 'request'; id = 't23-final-list'; method = 'encoder.list'; params = @{} }
    Assert-Ok $list $State.Current 'encoder.list final'
    if (@($list.data.encoders).Count -ne 0) { Fail-Task23 'encoder registry was not empty after removal.' }
    $close = Send-Task23 @{ op = 'request'; id = 't23-close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }
    Assert-Ok $close ($State.Current + 1) 'session.close'
    Read-Task23Event 'engine.stopping' ([int64]$close.revision) | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task23 'engine did not exit after session.close.' }
    Stop-Task23Engine
}

try {
    $state = Initialize-Task23Session
    $create = Invoke-Task23Mutation $state 't23-create-video' 'encoder.create' @{ type = 'video'; kind = 'task23_test_video'; name = 'task23-video'; settings = @{ bitrate = 1500 } } 'encoder.created' 'encoder.create video'
    $video = [string]$create.data.encoder
    if ([string]$create.data.videoInput.type -ne 'canvas' -or [string]$create.data.videoInput.canvas -eq '') { Fail-Task23 'video input was not semantic Canvas data.' }
    if ($create.data.PSObject.Properties.Name -contains 'videoSource' -or $create.data.PSObject.Properties.Name -contains 'video_t') { Fail-Task23 'raw video input leaked onto the wire.' }
    Invoke-Task23ObjectReads $state $video
    Invoke-Task23Settings $state $video
    Invoke-Task23Scaling $state $video
    Invoke-Task23Roi $state $video
    Invoke-Task23Audio $state
    Complete-Task23 $state $video
    Write-Output 'Task 23 encoder integration: PASS'
} catch {
    try { Stop-Task23Engine } catch { }
    throw
}

param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:Wire = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:LastResponseIndex = -1
$script:LastMessage = $null

function Fail-Task21([string] $Message) { throw "Task 21: $Message" }

function Start-Task21Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\/]bin[\/]64bit[\/]obs-engine\.exe$' } |
        Select-Object -First 1
    if ($null -eq $engine) {
        $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
    }
    if ($null -eq $engine) { Fail-Task21 'obs-engine.exe was not found.' }

    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    $info.ArgumentList.Add('--plugin=task21-audio-source')
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Engine = [System.Diagnostics.Process]::new()
    $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task21 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:Wire = [System.Collections.Generic.List[object]]::new()
    $script:NextSequence = [uint64]1
    $script:LastResponseIndex = -1
}

function Stop-Task21Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) {
        $script:Engine.Kill()
        $script:Engine.WaitForExit()
    }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') {
        Fail-Task21 'legacy fallback canvas warning was emitted.'
    }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task21 "engine exited with $($script:Engine.ExitCode)." }
}

function Read-Task21Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task21 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) {
        $exit = if ($script:Engine.HasExited) { $script:Engine.ExitCode } else { 'running' }
        Fail-Task21 "engine stdout closed unexpectedly (exit=$exit)."
    }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    $script:LastMessage = $message
    return $message
}

function Send-Task21([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 50
    $script:Engine.StandardInput.WriteLine($json)
    $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task21Message
        if ($message.op -eq 'event') {
            $script:PendingEvents.Add($message)
            continue
        }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) {
            Fail-Task21 "wrong response for $($Request.id)."
        }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok) { Fail-Task21 "$Label failed: $($Response.status.code)." }
    if ([int64]$Response.revision -ne $Revision) { Fail-Task21 "$Label revision=$($Response.revision), expected $Revision." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) {
        Fail-Task21 "$Label did not return $Code at revision $Revision."
    }
}

function Read-Task21NextEvent {
    if ($script:PendingEvents.Count -gt 0) {
        $event = $script:PendingEvents[0]
        $script:PendingEvents.RemoveAt(0)
        return $event
    }
    return Read-Task21Message
}

function Read-Task21SequencedEvent {
    $event = Read-Task21NextEvent
    if ($event.op -ne 'event') { Fail-Task21 'expected an event message.' }
    if ([uint64]$event.seq -ne $script:NextSequence) {
        Fail-Task21 "event '$($event.event)' seq=$($event.seq), expected $script:NextSequence."
    }
    $script:NextSequence++
    return $event
}

function Assert-Task21StateEvent($Event, [string] $Name, [int64] $Revision, [bool] $RequireAfterResponse = $true) {
    if ([string]$Event.event -ne $Name -or ($Revision -ge 0 -and [int64]$Event.revision -ne $Revision)) {
        Fail-Task21 "expected event '$Name' at revision $Revision, got '$($Event.event)' at $($Event.revision)."
    }
    $wire = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$Event.seq }) | Select-Object -First 1
    if ($RequireAfterResponse -and ($null -eq $wire -or $wire.Index -le $script:LastResponseIndex)) {
        Fail-Task21 "event '$Name' preceded its response."
    }
    if ($Event.PSObject.Properties.Name -contains 'telemetry') { Fail-Task21 "state event '$Name' was telemetry." }
}

function Read-Task21Event([string] $Name, [int64] $Revision, [bool] $RequireAfterResponse = $true) {
    while ($true) {
        $event = Read-Task21SequencedEvent
        if ([string]$event.event -eq 'audio.meter') { continue }
        if ([string]$event.event -in @('source.activeChanged', 'source.showingChanged') -and
            $Name -notin @('source.activeChanged', 'source.showingChanged')) { continue }
        Assert-Task21StateEvent $event $Name $Revision $RequireAfterResponse
        return $event
    }
}

function Assert-FiniteMeter($Event) {
    if (-not $Event.telemetry -or [string]$Event.event -ne 'audio.meter') { Fail-Task21 'meter event was not marked telemetry.' }
    foreach ($arrayName in @('magnitudeDb', 'peakDb', 'inputPeakDb')) {
        foreach ($value in @($Event.data.$arrayName)) {
            $number = [double]$value.value
            if ([double]::IsNaN($number) -or [double]::IsInfinity($number)) { Fail-Task21 "meter $arrayName contained a non-finite value." }
        }
    }
}

function Read-Task21Meter([string] $Subscription, [string] $Source) {
    while ($true) {
        $event = Read-Task21SequencedEvent
        if ([string]$event.event -ne 'audio.meter') { continue }
        if ([string]$event.data.meterSubscription -ne $Subscription -or [string]$event.data.source -ne $Source) {
            continue
        }
        if ([int]$event.data.channelCount -lt 1) { Fail-Task21 'meter did not report an audio channel.' }
        Assert-FiniteMeter $event
        return $event
    }
}

function Wait-Task21StereoLayout([string] $Source) {
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        $probe = Send-Task21 @{ op = 'request'; id = "t21-layout-$attempt"; method = 'audio.get'; params = @{ source = $Source } }
        if (-not $probe.status.ok) { Fail-Task21 'audio layout probe failed.' }
        if ([string]$probe.data.speakerLayout -eq 'stereo') { return $probe }
        Start-Sleep -Milliseconds 25
    }
    Fail-Task21 'deterministic audio source never reported a stereo layout.'
}

function Request([string] $Id, [string] $Method, [int64] $Revision, [hashtable] $Params, [bool] $Mutating = $false) {
    $request = @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    if ($Mutating) { $request.ifRevision = $Revision }
    $response = Send-Task21 $request
    return $response
}

function Request-Task21UnGuarded([string] $Id, [string] $Method, [hashtable] $Params) {
    return Send-Task21 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
}

function Assert-Task21Capabilities($Hello) {
    $required = @('audio.v1', 'audio.get.v1', 'audio.setMute.v1', 'audio.setVolume.v1',
        'audio.setVolumeDb.v1', 'audio.setBalance.v1', 'audio.setSyncOffset.v1', 'audio.setTracks.v1',
        'audio.setPushToTalk.v1', 'audio.setPushToMute.v1', 'audio.subscribeMeters.v1')
    $caps = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($name in $required) {
        if ($caps -notcontains $name) { Fail-Task21 "missing capability $name." }
    }
}

function Get-Task21Kinds {
    $kinds = Send-Task21 @{ op = 'request'; id = 't21-kinds'; method = 'source.kindList'; params = @{} }
    Assert-Ok $kinds 0 'source.kindList'
    $audioKind = @($kinds.data.kinds | Where-Object { $_.id -eq 'task21_audio_source' }) | Select-Object -First 1
    if ($null -eq $audioKind -or -not [bool]$audioKind.hasAudio) { Fail-Task21 'deterministic audio source was not advertised.' }
    $colorKind = @($kinds.data.kinds | Where-Object { $_.id -eq 'color_source_v3' }) | Select-Object -First 1
    if ($null -eq $colorKind) { $colorKind = @($kinds.data.kinds | Where-Object { $_.id -eq 'color_source' }) | Select-Object -First 1 }
    if ($null -eq $colorKind) { Fail-Task21 'no non-audio source kind was available.' }
    $restrictedKind = @($kinds.data.kinds | Where-Object { $_.id -eq 'task21_restricted_audio_source' }) | Select-Object -First 1
    if ($null -eq $restrictedKind -or -not [bool]$restrictedKind.hasAudio -or [bool]$restrictedKind.selfMonitorAllowed) {
        Fail-Task21 'restricted deterministic audio source was not advertised correctly.'
    }
    return [pscustomobject]@{ Audio = $audioKind; Color = $colorKind; Restricted = $restrictedKind }
}

function Initialize-Task21Session {
    $ready = Read-Task21Message
    if ([string]$ready.event -ne 'ready') { Fail-Task21 'ready marker was not received.' }
    $hello = Send-Task21 @{ op = 'request'; id = 't21-hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $hello 0 'session.hello'
    Assert-Task21Capabilities $hello
    $subscribe = Send-Task21 @{ op = 'request'; id = 't21-sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(
                @{ pattern = 'audio.*' }, @{ pattern = 'audio.meter'; telemetry = $true },
                @{ pattern = 'scene.*' }, @{ pattern = 'item.*' }, @{ pattern = 'program.*' },
                @{ pattern = 'source.*' }, @{ pattern = 'engine.stopping' })
        } }
    Assert-Ok $subscribe 0 'session.subscribe'
    return Get-Task21Kinds
}

function New-Task21Graph($Kinds) {
    $audioCreate = Request 't21-audio-create' 'source.create' 0 @{ kind = 'task21_audio_source'; name = 't21-audio'; settings = @{} } $true
    Assert-Ok $audioCreate 1 'audio source.create'
    $audio = [string]$audioCreate.data.source
    if ($audio -notmatch '^[1-9][0-9]*$') { Fail-Task21 "invalid audio source handle '$audio'." }
    Read-Task21Event 'source.created' 1 | Out-Null
    $otherCreate = Request 't21-other-create' 'source.create' 1 @{ kind = [string]$Kinds.Color.id; name = 't21-color'; settings = @{} } $true
    Assert-Ok $otherCreate 2 'non-audio source.create'
    $other = [string]$otherCreate.data.source
    Read-Task21Event 'source.created' 2 | Out-Null
    Assert-Error (Send-Task21 @{ op = 'request'; id = 't21-nonaudio'; method = 'audio.get'; params = @{ source = $other } }) 'unsupported_capability' 2 'audio.get non-audio'
    $scene = Request 't21-scene-create' 'scene.create' 2 @{ name = 'Task21 Audio Scene' } $true
    Assert-Ok $scene 3 'scene.create'
    $sceneHandle = [string]$scene.data.scene
    Read-Task21Event 'scene.created' 3 | Out-Null
    $item = Request 't21-item-create' 'item.create' 3 @{ scene = $sceneHandle; source = $audio } $true
    Assert-Ok $item 4 'item.create'
    Read-Task21Event 'item.created' 4 | Out-Null
    $program = Request 't21-program' 'program.setScene' 4 @{ scene = $sceneHandle } $true
    Assert-Ok $program 5 'program.setScene'
    Read-Task21Event 'program.sceneChanged' 5 | Out-Null
    $initial = Send-Task21 @{ op = 'request'; id = 't21-audio-get'; method = 'audio.get'; params = @{ source = $audio } }
    if (-not $initial.status.ok -or [int64]$initial.revision -lt 5) { Fail-Task21 'audio.get did not return a valid revision.' }
    $activation = Read-Task21Event 'source.activeChanged' -1 $false
    $current = [int64]$activation.revision
    Read-Task21Event 'source.showingChanged' $current $false | Out-Null
    if ([double]$initial.data.volumeMul -ne 1.0 -or [int64]$initial.data.syncOffsetNs -ne 0) { Fail-Task21 'initial audio state was not canonical.' }
    $settled = Wait-Task21StereoLayout $audio
    if ([string]$settled.data.speakerLayout -ne 'stereo') { Fail-Task21 'audio layout was not stereo.' }
    return [pscustomobject]@{ Audio = $audio; Other = $other; Scene = $sceneHandle; Current = $current; Restricted = $null; Meter = $null }
}

function Invoke-Task21VolumeChecks($State) {
    $mute = Request 't21-mute' 'audio.setMute' $State.Current @{ source = $State.Audio; muted = $true } $true
    Assert-Ok $mute ($State.Current + 1) 'audio.setMute'
    $State.Current = [int64]$mute.revision
    Read-Task21Event 'audio.muteChanged' $State.Current | Out-Null
    $toggle = Request 't21-toggle' 'audio.toggleMute' $State.Current @{ source = $State.Audio } $true
    Assert-Ok $toggle ($State.Current + 1) 'audio.toggleMute'
    $State.Current = [int64]$toggle.revision
    Read-Task21Event 'audio.muteChanged' $State.Current | Out-Null
    $volumeGet = Send-Task21 @{ op = 'request'; id = 't21-volume-get'; method = 'audio.getVolume'; params = @{ source = $State.Audio } }
    Assert-Ok $volumeGet $State.Current 'audio.getVolume'
    $volume = Request 't21-volume' 'audio.setVolume' $State.Current @{ source = $State.Audio; volumeMul = 0.5 } $true
    Assert-Ok $volume ($State.Current + 1) 'audio.setVolume'
    $State.Current = [int64]$volume.revision
    Read-Task21Event 'audio.volumeChanged' $State.Current | Out-Null
    if ([double]$volume.data.volumeMul -le 0.49 -or [double]$volume.data.volumeMul -ge 0.51) { Fail-Task21 'volume multiplier readback was wrong.' }
    $volumeDb = Request 't21-volume-db' 'audio.setVolumeDb' $State.Current @{ source = $State.Audio; volumeDb = -6.0 } $true
    Assert-Ok $volumeDb ($State.Current + 1) 'audio.setVolumeDb'
    $State.Current = [int64]$volumeDb.revision
    Read-Task21Event 'audio.volumeChanged' $State.Current | Out-Null
    $zero = Request 't21-volume-zero' 'audio.setVolume' $State.Current @{ source = $State.Audio; volumeMul = 0.0 } $true
    Assert-Ok $zero ($State.Current + 1) 'audio.setVolume zero'
    $State.Current = [int64]$zero.revision
    $zeroEvent = Read-Task21Event 'audio.volumeChanged' $State.Current
    if (-not [bool]$zeroEvent.data.volumeDbFloored) { Fail-Task21 'zero volume was not represented by the finite dB floor.' }
}

function Invoke-Task21AudioChecks($State) {
    $badBalance = Send-Task21 @{ op = 'request'; id = 't21-bad-balance'; method = 'audio.setBalance'; ifRevision = $State.Current; params = @{ source = $State.Audio; balance = 1.5 } }
    Assert-Error $badBalance 'bad_request' $State.Current 'invalid balance'
    $balance = Request 't21-balance' 'audio.setBalance' $State.Current @{ source = $State.Audio; balance = 0.25 } $true
    Assert-Ok $balance ($State.Current + 1) 'audio.setBalance'
    $State.Current = [int64]$balance.revision
    Read-Task21Event 'audio.balanceChanged' $State.Current | Out-Null
    foreach ($offset in @(1000000, -1000000)) {
        $sync = Request "t21-sync-$offset" 'audio.setSyncOffset' $State.Current @{ source = $State.Audio; syncOffsetNs = $offset } $true
        Assert-Ok $sync ($State.Current + 1) "audio.setSyncOffset $offset"
        $State.Current = [int64]$sync.revision
        Read-Task21Event 'audio.syncOffsetChanged' $State.Current | Out-Null
    }
    $monitoring = Request 't21-monitoring' 'audio.setMonitoringEnabled' $State.Current @{ source = $State.Audio; monitoringEnabled = $true } $true
    Assert-Ok $monitoring ($State.Current + 1) 'audio.setMonitoringEnabled'
    $State.Current = [int64]$monitoring.revision
    Read-Task21Event 'audio.monitoringChanged' $State.Current | Out-Null
    $monitoringGet = Send-Task21 @{ op = 'request'; id = 't21-monitoring-get'; method = 'audio.getMonitoringEnabled'; params = @{ source = $State.Audio } }
    Assert-Ok $monitoringGet $State.Current 'audio.getMonitoringEnabled'
    $tracks = Request 't21-tracks' 'audio.setTracks' $State.Current @{ source = $State.Audio; tracks = @(@{ track = 1 }, @{ track = 6 }) } $true
    Assert-Ok $tracks ($State.Current + 1) 'audio.setTracks'
    $State.Current = [int64]$tracks.revision
    Read-Task21Event 'audio.tracksChanged' $State.Current | Out-Null
    $tracksGet = Send-Task21 @{ op = 'request'; id = 't21-tracks-get'; method = 'audio.getTracks'; params = @{ source = $State.Audio } }
    Assert-Ok $tracksGet $State.Current 'audio.getTracks'
    $ptt = Request 't21-ptt' 'audio.setPushToTalk' $State.Current @{ source = $State.Audio; enabled = $true; delayMs = 200 } $true
    Assert-Ok $ptt ($State.Current + 1) 'audio.setPushToTalk'
    $State.Current = [int64]$ptt.revision
    Read-Task21Event 'audio.gatingChanged' $State.Current | Out-Null
    $pttGet = Send-Task21 @{ op = 'request'; id = 't21-ptt-get'; method = 'audio.getPushToTalk'; params = @{ source = $State.Audio } }
    Assert-Ok $pttGet $State.Current 'audio.getPushToTalk'
    $ptm = Request 't21-ptm' 'audio.setPushToMute' $State.Current @{ source = $State.Audio; enabled = $true; delayMs = 250 } $true
    Assert-Ok $ptm ($State.Current + 1) 'audio.setPushToMute'
    $State.Current = [int64]$ptm.revision
    Read-Task21Event 'audio.gatingChanged' $State.Current | Out-Null
    $ptmGet = Send-Task21 @{ op = 'request'; id = 't21-ptm-get'; method = 'audio.getPushToMute'; params = @{ source = $State.Audio } }
    Assert-Ok $ptmGet $State.Current 'audio.getPushToMute'
}

function Invoke-Task21RestrictionChecks($State, $Kinds) {
    $restrictedCreate = Request 't21-restricted-create' 'source.create' $State.Current @{ kind = [string]$Kinds.Restricted.id; name = 't21-restricted-audio'; settings = @{} } $true
    Assert-Ok $restrictedCreate ($State.Current + 1) 'restricted audio source.create'
    $State.Current = [int64]$restrictedCreate.revision
    $State.Restricted = [string]$restrictedCreate.data.source
    Read-Task21Event 'source.created' $State.Current | Out-Null
    $restrictedMonitoring = Send-Task21 @{ op = 'request'; id = 't21-restricted-monitoring'; method = 'audio.setMonitoringEnabled'; ifRevision = $State.Current; params = @{ source = $State.Restricted; monitoringEnabled = $true } }
    Assert-Error $restrictedMonitoring 'unsupported_capability' $State.Current 'self-monitor restriction'
    $badSync = Send-Task21 @{ op = 'request'; id = 't21-bad-sync'; method = 'audio.setSyncOffset'; ifRevision = $State.Current; params = @{ source = $State.Audio; syncOffsetNs = 604800000000001 } }
    Assert-Error $badSync 'bad_request' $State.Current 'sync bound'
    $badTrack = Send-Task21 @{ op = 'request'; id = 't21-bad-track'; method = 'audio.setTracks'; ifRevision = $State.Current; params = @{ source = $State.Audio; tracks = @(@{ track = 7 }) } }
    Assert-Error $badTrack 'bad_request' $State.Current 'track bound'
}

function Invoke-Task21DeviceChecks($State) {
    $deviceList = Send-Task21 @{ op = 'request'; id = 't21-devices'; method = 'audio.listMonitoringDevices'; params = @{} }
    if (-not $deviceList.status.ok) {
        if ([string]$deviceList.status.code -ne 'unsupported_capability') {
            Fail-Task21 "monitoring-device enumeration failed unexpectedly: $($deviceList.status.code)."
        }
        return
    }
    Assert-Ok $deviceList $State.Current 'audio.listMonitoringDevices'
    $device = @($deviceList.data.devices | Select-Object -First 1)[0]
    $currentDevice = Send-Task21 @{ op = 'request'; id = 't21-device-get'; method = 'audio.getMonitoringDevice'; params = @{} }
    Assert-Ok $currentDevice $State.Current 'audio.getMonitoringDevice'
    if ($null -ne $device -and [string]$device.deviceId -eq [string]$currentDevice.data.deviceId) { }
    $badDevice = Send-Task21 @{ op = 'request'; id = 't21-bad-device'; method = 'audio.setMonitoringDevice'; ifRevision = $State.Current; params = @{ deviceId = 'task21-invalid-device' } }
    Assert-Error $badDevice 'not_found' $State.Current 'invalid monitoring device'
}

function Invoke-Task21MeterChecks($State) {
    $meterSubscription = Send-Task21 @{ op = 'request'; id = 't21-meter-sub'; method = 'audio.subscribeMeters'; params = @{
            sources = @(@{ source = $State.Audio }); maxHz = 20; peakMode = 'sample' } }
    Assert-Ok $meterSubscription $State.Current 'audio.subscribeMeters'
    $meter = [string]$meterSubscription.data.meterSubscription
    if ($meter -notmatch '^[1-9][0-9]*$') { Fail-Task21 'invalid meter subscription token.' }
    $meterEvent = Read-Task21Meter $meter $State.Audio
    if ([int64]$meterEvent.revision -ne $State.Current) { Fail-Task21 'meter telemetry consumed a state revision.' }
    $State.Meter = $meter
}

function Invoke-Task21Cleanup($State) {
    $clear = Request 't21-program-clear' 'program.setScene' $State.Current @{ scene = $null } $true
    Assert-Ok $clear ($State.Current + 1) 'program.clear'
    $State.Current = [int64]$clear.revision
    Read-Task21Event 'program.sceneChanged' $State.Current | Out-Null
    $removeScene = Request 't21-scene-remove' 'scene.remove' $State.Current @{ scene = $State.Scene } $true
    Assert-Ok $removeScene ($State.Current + 1) 'scene.remove'
    $State.Current = [int64]$removeScene.revision
    Read-Task21Event 'item.removed' $State.Current | Out-Null
    Read-Task21Event 'scene.removed' $State.Current | Out-Null
    $removeAudio = Request-Task21UnGuarded 't21-audio-remove' 'source.remove' @{ source = $State.Audio }
    if (-not $removeAudio.status.ok -or [int64]$removeAudio.revision -le $State.Current) {
        Fail-Task21 'audio source.remove did not settle after lifecycle events.'
    }
    $State.Current = [int64]$removeAudio.revision
    Read-Task21Event 'source.removed' $State.Current | Out-Null
    $meterUnsub = Send-Task21 @{ op = 'request'; id = 't21-meter-unsub'; method = 'audio.unsubscribeMeters'; params = @{ meterSubscription = $State.Meter } }
    Assert-Ok $meterUnsub $State.Current 'audio.unsubscribeMeters after source removal'
    $removeRestricted = Request 't21-restricted-remove' 'source.remove' $State.Current @{ source = $State.Restricted } $true
    Assert-Ok $removeRestricted ($State.Current + 1) 'restricted audio source.remove'
    $State.Current = [int64]$removeRestricted.revision
    Read-Task21Event 'source.removed' $State.Current | Out-Null
    $removeOther = Request 't21-other-remove' 'source.remove' $State.Current @{ source = $State.Other } $true
    Assert-Ok $removeOther ($State.Current + 1) 'non-audio source.remove'
    $State.Current = [int64]$removeOther.revision
    Read-Task21Event 'source.removed' $State.Current | Out-Null
    $close = Send-Task21 @{ op = 'request'; id = 't21-close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }
    Assert-Ok $close ($State.Current + 1) 'session.close'
    Read-Task21Event 'engine.stopping' ([int64]$close.revision) | Out-Null
    $script:Engine.WaitForExit(30000) | Out-Null
    Stop-Task21Engine
}

function Invoke-Task21Scenario {
    Start-Task21Engine $InstallRoot
    $kinds = Initialize-Task21Session
    $state = New-Task21Graph $kinds
    Invoke-Task21VolumeChecks $state
    Invoke-Task21AudioChecks $state
    Invoke-Task21RestrictionChecks $state $kinds
    Invoke-Task21DeviceChecks $state
    Invoke-Task21MeterChecks $state
    Invoke-Task21Cleanup $state
    Write-Output 'Task 21 audio integration: PASS'
}
try { Invoke-Task21Scenario }
catch {
    $failure = $_.Exception
    if ($null -ne $script:Engine -and -not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    if ($null -ne $script:LastMessage) { Write-Host ('last protocol message: ' + ($script:LastMessage | ConvertTo-Json -Compress -Depth 50)) }
    if ($null -ne $script:ErrorTask) { Write-Host ('engine stderr: ' + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw $failure
}

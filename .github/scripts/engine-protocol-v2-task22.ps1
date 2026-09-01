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

function Fail-Task22([string] $Message) { throw "Task 22: $Message" }

function Start-Task22Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } |
        Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task22 'obs-engine.exe was not found.' }
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    $info.ArgumentList.Add('--plugin=task22-hotkey-source')
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Engine = [System.Diagnostics.Process]::new()
    $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task22 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:Wire = [System.Collections.Generic.List[object]]::new()
    $script:NextSequence = [uint64]1
    $script:LastResponseIndex = -1
}

function Stop-Task22Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) {
        $script:Engine.Kill()
        $script:Engine.WaitForExit()
    }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task22 "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task22Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task22 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) {
        $exit = if ($script:Engine.HasExited) { $script:Engine.ExitCode } else { 'running' }
        Fail-Task22 "engine stdout closed unexpectedly (exit=$exit)."
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

function Send-Task22([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 50
    $script:Engine.StandardInput.WriteLine($json)
    $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task22Message
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) {
            Fail-Task22 "wrong response for $($Request.id)."
        }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok) { Fail-Task22 "$Label failed: $($Response.status.code)." }
    if ([int64]$Response.revision -ne $Revision) { Fail-Task22 "$Label revision=$($Response.revision), expected $Revision." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) {
        Fail-Task22 "$Label did not return $Code at revision $Revision."
    }
}

function Read-Task22NextEvent {
    if ($script:PendingEvents.Count -gt 0) {
        $event = $script:PendingEvents[0]
        $script:PendingEvents.RemoveAt(0)
        return $event
    }
    return Read-Task22Message
}

function Read-Task22SequencedEvent {
    $event = Read-Task22NextEvent
    if ($event.op -ne 'event') { Fail-Task22 'expected an event message.' }
    if ([uint64]$event.seq -ne $script:NextSequence) {
        Fail-Task22 "event '$($event.event)' seq=$($event.seq), expected $script:NextSequence."
    }
    $script:NextSequence++
    return $event
}

function Assert-Task22Event($Event, [string] $Name, [int64] $Revision, [bool] $Telemetry) {
    if ([string]$Event.event -ne $Name) { Fail-Task22 "expected event '$Name', got '$($Event.event)'." }
    $hasTelemetry = $Event.PSObject.Properties.Name -contains 'telemetry'
    if ($hasTelemetry -ne $Telemetry) { Fail-Task22 "event '$Name' telemetry flag was incorrect." }
    if ($Revision -ge 0 -and [int64]$Event.revision -ne $Revision) {
        Fail-Task22 "event '$Name' revision=$($Event.revision), expected $Revision."
    }
    $wire = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$Event.seq }) | Select-Object -First 1
    if ($null -eq $wire -or $wire.Index -le $script:LastResponseIndex) {
        Fail-Task22 "event '$Name' preceded its response."
    }
}

function Read-Task22Event([string] $Name, [int64] $Revision, [bool] $Telemetry = $false) {
    while ($true) {
        $event = Read-Task22SequencedEvent
        if (-not $Telemetry -and [string]$event.event -eq 'hotkey.triggered') { continue }
        Assert-Task22Event $event $Name $Revision $Telemetry
        return $event
    }
}

function New-Task22Binding([string] $Key, [string[]] $Modifiers) {
    return @{ key = $Key; modifiers = @($Modifiers | ForEach-Object { @{ name = $_ } }) }
}

function Assert-Task22Capabilities($Hello) {
    $required = @('hotkey.v1', 'hotkey.list.v1', 'hotkey.get.v1', 'hotkey.getBindings.v1',
        'hotkey.setBindings.v1', 'hotkey.clearBindings.v1', 'hotkey.trigger.v1', 'hotkey.getKeyName.v1',
        'hotkey.getKeyCombinationName.v1', 'hotkey.getConflicts.v1', 'hotkey.getBackgroundCapture.v1',
        'hotkey.setBackgroundCapture.v1', 'hotkey.export.v1', 'hotkey.import.v1')
    $caps = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($name in $required) {
        if ($caps -notcontains $name) { Fail-Task22 "missing capability $name." }
    }
}

function Get-Task22HotkeyFixtures {
    $list = Send-Task22 @{ op = 'request'; id = 't22-list'; method = 'hotkey.list'; params = @{} }
    Assert-Ok $list 0 'hotkey.list'
    $frontend = @($list.data.hotkeys | Where-Object { $_.name -eq 'task22.frontend' -and $_.registerer.type -eq 'frontend' }) | Select-Object -First 1
    $source = @($list.data.hotkeys | Where-Object { $_.name -eq 'task22.source' -and $_.registerer.type -eq 'source' }) | Select-Object -First 1
    $pairStart = @($list.data.hotkeys | Where-Object { $_.name -eq 'task22.pair.start' }) | Select-Object -First 1
    if ($null -eq $frontend -or $null -eq $source -or $null -eq $pairStart) { Fail-Task22 'fixture hotkeys were not listed.' }
    if ($frontend.PSObject.Properties.Name -contains 'id' -or $source.PSObject.Properties.Name -contains 'id') {
        Fail-Task22 'hotkey list exposed a raw libobs id.'
    }
    if ($null -eq $pairStart.pairPartner -or [string]$pairStart.pairPartner.name -ne 'task22.pair.stop') {
        Fail-Task22 'hotkey pair metadata was missing.'
    }
    return [pscustomobject]@{ Frontend = $frontend; Source = $source }
}

function Invoke-Task22KeyNameChecks {
    $frontendSelector = @{ registerer = @{ type = 'frontend' }; name = 'task22.frontend' }
    $get = Send-Task22 @{ op = 'request'; id = 't22-get'; method = 'hotkey.get'; params = $frontendSelector }
    Assert-Ok $get 0 'hotkey.get'
    $keys = Send-Task22 @{ op = 'request'; id = 't22-key-name'; method = 'hotkey.getKeyName'; params = @{ key = 'OBS_KEY_F5' } }
    Assert-Ok $keys 0 'hotkey.getKeyName'
    if ([string]$keys.data.key -ne 'OBS_KEY_F5' -or [string]$keys.data.name -eq '') { Fail-Task22 'key name mapping was empty.' }
    $combo = Send-Task22 @{ op = 'request'; id = 't22-combination-name'; method = 'hotkey.getKeyCombinationName'; params = @{
            binding = (New-Task22Binding 'OBS_KEY_F5' @('shift', 'control', 'shift')) } }
    Assert-Ok $combo 0 'hotkey.getKeyCombinationName'
    $badModifier = Send-Task22 @{ op = 'request'; id = 't22-bad-modifier'; method = 'hotkey.getKeyCombinationName'; params = @{
            binding = (New-Task22Binding 'OBS_KEY_F5' @('bogus')) } }
    Assert-Error $badModifier 'bad_request' 0 'invalid modifier'
    return $frontendSelector
}

function Initialize-Task22Session {
    $ready = Read-Task22Message
    if ([string]$ready.event -ne 'ready') { Fail-Task22 'ready marker was not received.' }
    $hello = Send-Task22 @{ op = 'request'; id = 't22-hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $hello 0 'session.hello'
    Assert-Task22Capabilities $hello
    $subscribe = Send-Task22 @{ op = 'request'; id = 't22-subscribe'; method = 'session.subscribe'; params = @{
            subscriptions = @(
                @{ pattern = 'hotkey.*' }, @{ pattern = 'hotkey.triggered'; telemetry = $true },
                @{ pattern = 'audio.*' }, @{ pattern = 'source.*' }, @{ pattern = 'engine.stopping' })
        } }
    Assert-Ok $subscribe 0 'session.subscribe'
    $fixtures = Get-Task22HotkeyFixtures
    $frontendSelector = Invoke-Task22KeyNameChecks
    $sourceSelector = @{ registerer = $fixtures.Source.registerer; name = 'task22.source' }
    return [pscustomobject]@{
        Frontend = $fixtures.Frontend
        Source = $fixtures.Source
        FrontendSelector = $frontendSelector
        SourceSelector = $sourceSelector
        Current = [int64]0
        F5 = $null
        F6 = $null
        Export = $null
        AudioSource = $null
    }
}

function Invoke-Task22BindingChecks($State) {
    $State.F5 = New-Task22Binding 'OBS_KEY_F5' @('shift', 'control', 'shift')
    $setFrontend = Send-Task22 @{ op = 'request'; id = 't22-set-frontend'; method = 'hotkey.setBindings'; ifRevision = $State.Current; params = @{
            registerer = @{ type = 'frontend' }; name = 'task22.frontend'; bindings = @($State.F5, $State.F5) } }
    Assert-Ok $setFrontend ($State.Current + 1) 'hotkey.setBindings frontend'
    $State.Current++
    Read-Task22Event 'hotkey.bindingsChanged' $State.Current | Out-Null
    $frontendBindings = Send-Task22 @{ op = 'request'; id = 't22-get-bindings'; method = 'hotkey.getBindings'; params = $State.FrontendSelector }
    Assert-Ok $frontendBindings $State.Current 'hotkey.getBindings'
    if (@($frontendBindings.data.bindings).Count -ne 1 -or @($frontendBindings.data.bindings[0].modifiers).Count -ne 2) {
        Fail-Task22 'binding normalization did not remove duplicates.'
    }
    $sourceConflict = Send-Task22 @{ op = 'request'; id = 't22-source-conflict'; method = 'hotkey.setBindings'; ifRevision = $State.Current; params = @{
            registerer = $State.Source.registerer; name = 'task22.source'; bindings = @($State.F5) } }
    Assert-Error $sourceConflict 'already_exists' $State.Current 'hotkey conflict detection'
    $State.F6 = New-Task22Binding 'OBS_KEY_F6' @('alt')
    $setSource = Send-Task22 @{ op = 'request'; id = 't22-set-source'; method = 'hotkey.setBindings'; ifRevision = $State.Current; params = @{
            registerer = $State.Source.registerer; name = 'task22.source'; bindings = @($State.F6) } }
    Assert-Ok $setSource ($State.Current + 1) 'hotkey.setBindings source'
    $State.Current++
    Read-Task22Event 'hotkey.bindingsChanged' $State.Current | Out-Null
    $conflicts = Send-Task22 @{ op = 'request'; id = 't22-conflicts'; method = 'hotkey.getConflicts'; params = @{ binding = $State.F5 } }
    Assert-Ok $conflicts $State.Current 'hotkey.getConflicts'
    if ([int]$conflicts.data.count -ne 1) { Fail-Task22 'expected one F5 conflict.' }
    $export = Send-Task22 @{ op = 'request'; id = 't22-export'; method = 'hotkey.export'; params = @{} }
    Assert-Ok $export $State.Current 'hotkey.export'
    if ([int]$export.data.count -lt 3) { Fail-Task22 'hotkey export was incomplete.' }
    foreach ($entry in @($export.data.hotkeys)) {
        if ($entry.PSObject.Properties.Name -contains 'id') { Fail-Task22 'hotkey export exposed a raw libobs id.' }
    }
    $State.Export = @($export.data.hotkeys)
    $invalidImport = Send-Task22 @{ op = 'request'; id = 't22-invalid-import'; method = 'hotkey.import'; ifRevision = $State.Current; params = @{
            hotkeys = @(
                @{ registerer = @{ type = 'frontend' }; name = 'task22.frontend'; bindings = @($State.F5) },
                @{ registerer = $State.Source.registerer; name = 'task22.source'; bindings = @(New-Task22Binding 'OBS_KEY_NOT_REAL' @()) })
        } }
    Assert-Error $invalidImport 'bad_request' $State.Current 'atomic failed import'
    $sourceAfterInvalid = Send-Task22 @{ op = 'request'; id = 't22-source-after-invalid'; method = 'hotkey.getBindings'; params = $State.SourceSelector }
    Assert-Ok $sourceAfterInvalid $State.Current 'source bindings after failed import'
    if ([string]$sourceAfterInvalid.data.bindings[0].key -ne 'OBS_KEY_F6') { Fail-Task22 'failed import partially changed bindings.' }
    $clear = Send-Task22 @{ op = 'request'; id = 't22-clear'; method = 'hotkey.clearBindings'; ifRevision = $State.Current; params = $State.FrontendSelector }
    Assert-Ok $clear ($State.Current + 1) 'hotkey.clearBindings'
    $State.Current++
    Read-Task22Event 'hotkey.bindingsChanged' $State.Current | Out-Null
    $noConflicts = Send-Task22 @{ op = 'request'; id = 't22-no-conflicts'; method = 'hotkey.getConflicts'; params = @{ binding = $State.F5 } }
    Assert-Ok $noConflicts $State.Current 'hotkey.getConflicts after clear'
    if ([int]$noConflicts.data.count -ne 0) { Fail-Task22 'cleared binding still reported a conflict.' }
    $import = Send-Task22 @{ op = 'request'; id = 't22-import'; method = 'hotkey.import'; ifRevision = $State.Current; params = @{ hotkeys = @($State.Export) } }
    Assert-Ok $import ($State.Current + 1) 'hotkey.import'
    $State.Current++
    Read-Task22Event 'hotkey.bindingsChanged' $State.Current | Out-Null
}

function Invoke-Task22AudioRoutingCheck($State) {
    $audioKindList = Send-Task22 @{ op = 'request'; id = 't22-audio-kinds'; method = 'source.kindList'; params = @{} }
    Assert-Ok $audioKindList $State.Current 'audio source kind list'
    $audioKind = @($audioKindList.data.kinds | Where-Object { $_.id -eq 'audio_line' }) | Select-Object -First 1
    if ($null -eq $audioKind) { Fail-Task22 'audio_line source kind was not available for mute routing.' }
    $audioCreate = Send-Task22 @{ op = 'request'; id = 't22-audio-source'; method = 'source.create'; ifRevision = $State.Current; params = @{
            kind = 'audio_line'; name = 't22-audio-line'; settings = @{} } }
    Assert-Ok $audioCreate ($State.Current + 1) 'audio source.create for hotkey routing'
    $State.Current++
    $State.AudioSource = [string]$audioCreate.data.source
    Read-Task22Event 'source.created' $State.Current | Out-Null
    $listWithAudio = Send-Task22 @{ op = 'request'; id = 't22-list-with-audio'; method = 'hotkey.list'; params = @{} }
    Assert-Ok $listWithAudio $State.Current 'hotkey.list with Engine audio hotkey'
    $audioHotkey = @($listWithAudio.data.hotkeys | Where-Object {
            $_.registerer.type -eq 'source' -and $_.registerer.PSObject.Properties.Name -contains 'handle' -and
            [string]$_.registerer.handle -eq $State.AudioSource -and [string]$_.name -like 'engine.source.*.toggleMute'
        }) | Select-Object -First 1
    if ($null -eq $audioHotkey) { Fail-Task22 'Engine-owned source mute hotkey was not listed.' }
    $audioTrigger = Send-Task22 @{ op = 'request'; id = 't22-audio-trigger'; method = 'hotkey.trigger'; ifRevision = $State.Current; params = @{
            registerer = $audioHotkey.registerer; name = [string]$audioHotkey.name; action = 'click' } }
    Assert-Ok $audioTrigger ($State.Current + 1) 'Engine source mute hotkey trigger'
    $State.Current++
    Read-Task22Event 'audio.muteChanged' $State.Current | Out-Null
    $removeAudio = Send-Task22 @{ op = 'request'; id = 't22-audio-remove'; method = 'source.remove'; ifRevision = $State.Current; params = @{ source = $State.AudioSource } }
    Assert-Ok $removeAudio ($State.Current + 1) 'audio source.remove after hotkey trigger'
    $State.Current++
    Read-Task22Event 'source.removed' $State.Current | Out-Null
}

function Invoke-Task22TriggerChecks($State) {
    $trigger = Send-Task22 @{ op = 'request'; id = 't22-trigger'; method = 'hotkey.trigger'; ifRevision = $State.Current; params = @{
            registerer = @{ type = 'frontend' }; name = 'task22.frontend'; action = 'click' } }
    Assert-Ok $trigger $State.Current 'hotkey.trigger click'
    $triggerEvent = Read-Task22Event 'hotkey.triggered' $State.Current $true
    if ([bool]$triggerEvent.data.pressed) { Fail-Task22 'click trigger did not settle at release.' }
    $sourceTrigger = Send-Task22 @{ op = 'request'; id = 't22-source-trigger'; method = 'hotkey.trigger'; ifRevision = $State.Current; params = @{
            registerer = $State.Source.registerer; name = 'task22.source'; action = 'click' } }
    Assert-Ok $sourceTrigger $State.Current 'source hotkey trigger'
    Read-Task22Event 'hotkey.triggered' $State.Current $true | Out-Null
    $background = Send-Task22 @{ op = 'request'; id = 't22-background-get'; method = 'hotkey.getBackgroundCapture'; params = @{} }
    Assert-Ok $background $State.Current 'hotkey.getBackgroundCapture'
    if (-not [bool]$background.data.enabled) { Fail-Task22 'background capture did not default to enabled.' }
    $disable = Send-Task22 @{ op = 'request'; id = 't22-background-disable'; method = 'hotkey.setBackgroundCapture'; ifRevision = $State.Current; params = @{ enabled = $false } }
    Assert-Ok $disable ($State.Current + 1) 'hotkey.setBackgroundCapture false'
    $State.Current++
    Read-Task22Event 'hotkey.backgroundCaptureChanged' $State.Current | Out-Null
    $enable = Send-Task22 @{ op = 'request'; id = 't22-background-enable'; method = 'hotkey.setBackgroundCapture'; ifRevision = $State.Current; params = @{ enabled = $true } }
    Assert-Ok $enable ($State.Current + 1) 'hotkey.setBackgroundCapture true'
    $State.Current++
    Read-Task22Event 'hotkey.backgroundCaptureChanged' $State.Current | Out-Null
}

function Complete-Task22Scenario($State) {
    $missing = Send-Task22 @{ op = 'request'; id = 't22-missing'; method = 'hotkey.get'; params = @{ registerer = @{ type = 'frontend' }; name = 'task22.missing' } }
    Assert-Error $missing 'not_found' $State.Current 'missing hotkey'
    $close = Send-Task22 @{ op = 'request'; id = 't22-close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }
    Assert-Ok $close ($State.Current + 1) 'session.close'
    Read-Task22Event 'engine.stopping' ([int64]$close.revision) | Out-Null
    $script:Engine.WaitForExit(30000) | Out-Null
    Stop-Task22Engine
}

function Invoke-Task22Scenario {
    Start-Task22Engine $InstallRoot
    $state = Initialize-Task22Session
    Invoke-Task22BindingChecks $state
    Invoke-Task22AudioRoutingCheck $state
    Invoke-Task22TriggerChecks $state
    Complete-Task22Scenario $state
    Write-Output 'Task 22 hotkey integration: PASS'
}
try { Invoke-Task22Scenario }
catch {
    $failure = $_.Exception
    if ($null -ne $script:Engine -and -not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    if ($null -ne $script:LastMessage) { Write-Host ('last protocol message: ' + ($script:LastMessage | ConvertTo-Json -Compress -Depth 50)) }
    if ($null -ne $script:ErrorTask) { Write-Host ('engine stderr: ' + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw $failure
}

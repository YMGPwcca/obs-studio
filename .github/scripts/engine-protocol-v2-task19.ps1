param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Process = $null
$script:ErrorTask = $null
$script:Events = [System.Collections.Generic.List[object]]::new()
$script:Wire = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:LastResponseIndex = -1
$script:LastMessage = $null

function Fail-Task19([string] $Message) { throw "Task 19: $Message" }

function Start-Task19Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task19 'obs-engine.exe was not found.' }
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Process = [System.Diagnostics.Process]::new()
    $script:Process.StartInfo = $info
    if (-not $script:Process.Start()) { Fail-Task19 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task19Engine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task19 'fallback canvas warning was emitted.' }
    if ($script:Process.ExitCode -ne 0) { Fail-Task19 "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task19Message {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task19 'timed out waiting for stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task19 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task19Request([hashtable] $Request) {
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task19Message
        $script:LastMessage = $message
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task19 "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task19 "$Label failed at revision $($Response.revision)." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task19 "$Label did not return $Code at revision $Revision." }
}

function Read-Task19Event([string] $Name, [int64] $Revision) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task19Message }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) { Fail-Task19 "unexpected event; expected $Name at revision $Revision." }
    $script:NextSequence++
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task19 "event $Name preceded its response." }
    return $event
}

try {
    Start-Task19Engine $InstallRoot
    $ready = Read-Task19Message
    if ($ready.event -ne 'ready') { Fail-Task19 'ready marker was not received.' }
    Assert-Ok (Send-Task19Request @{ op = 'request'; id = 'tr-hello'; method = 'session.hello' }) 0 'hello'
    Assert-Ok (Send-Task19Request @{ op = 'request'; id = 'tr-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'transition.*' }) } }) 0 'subscribe'

    $kinds = Send-Task19Request @{ op = 'request'; id = 'tr-kind-list'; method = 'transition.kindList' }
    Assert-Ok $kinds 0 'transition.kindList'
    if ([int]$kinds.data.count -le 0) { Fail-Task19 'no dynamic transition kinds were available.' }
    $kindEntry = @($kinds.data.kinds | Where-Object { $_.kind -eq 'fade_to_color_transition' }) | Select-Object -First 1
    if ($null -eq $kindEntry) { $kindEntry = $kinds.data.kinds[0] }
    $kind = [string]$kindEntry.kind
    $defaults = Send-Task19Request @{ op = 'request'; id = 'tr-kind-defaults'; method = 'transition.kindDefaults'; params = @{ kind = $kind } }
    Assert-Ok $defaults 0 'transition.kindDefaults'
    $kindProperties = Send-Task19Request @{ op = 'request'; id = 'tr-kind-properties'; method = 'transition.kindProperties'; params = @{ kind = $kind } }
    if ($kindProperties.status.ok) {
        Assert-Ok $kindProperties 0 'transition.kindProperties'
    } elseif ([string]$kind -eq 'fade_to_color_transition') {
        Fail-Task19 'fade_to_color_transition properties unexpectedly failed.'
    }

    $create = Send-Task19Request @{ op = 'request'; id = 'tr-create'; method = 'transition.create'; params = @{ kind = $kind; name = 'Task19 Transition'; settings = $defaults.data.settings } }
    Assert-Ok $create 1 'transition.create'
    Read-Task19Event 'transition.created' 1 | Out-Null
    $transitionHandle = [string]$create.data.transition
    if ($transitionHandle -ne '2') { Fail-Task19 "unexpected transition handle $transitionHandle." }

    $list = Send-Task19Request @{ op = 'request'; id = 'tr-list'; method = 'transition.list' }
    Assert-Ok $list 1 'transition.list'
    if ([int]$list.data.count -ne 1) { Fail-Task19 'transition.list did not contain the created object.' }
    Assert-Ok (Send-Task19Request @{ op = 'request'; id = 'tr-get'; method = 'transition.get'; params = @{ transition = $transitionHandle } }) 1 'transition.get'
    $state = Send-Task19Request @{ op = 'request'; id = 'tr-state'; method = 'transition.getState'; params = @{ transition = $transitionHandle } }
    Assert-Ok $state 1 'transition.getState'
    if ([string]$state.data.state -ne 'idle' -or $state.data.active) { Fail-Task19 'new transition was not idle.' }
    Assert-Ok (Send-Task19Request @{ op = 'request'; id = 'tr-settings'; method = 'transition.getSettings'; params = @{ transition = $transitionHandle } }) 1 'transition.getSettings'
    Assert-Ok (Send-Task19Request @{ op = 'request'; id = 'tr-properties'; method = 'transition.getProperties'; params = @{ transition = $transitionHandle } }) 1 'transition.getProperties'

    $rename = Send-Task19Request @{ op = 'request'; id = 'tr-rename'; method = 'transition.rename'; params = @{ transition = $transitionHandle; name = 'Task19 Renamed' }; ifRevision = 1 }
    Assert-Ok $rename 2 'transition.rename'
    Read-Task19Event 'transition.renamed' 2 | Out-Null
    $revision = [int64]2

    $patchSettings = if ($kind -eq 'fade_to_color_transition') { @{ switch_point = 75 } } else { @{} }
    $patch = Send-Task19Request @{ op = 'request'; id = 'tr-patch'; method = 'transition.patchSettings'; params = @{ transition = $transitionHandle; settings = $patchSettings }; ifRevision = $revision }
    if ($kind -eq 'fade_to_color_transition') {
        Assert-Ok $patch ($revision + 1) 'transition.patchSettings'
        $revision++
        Read-Task19Event 'transition.settingsChanged' $revision | Out-Null
    } else {
        Assert-Ok $patch $revision 'transition.patchSettings no-op'
    }

    $setDuration = Send-Task19Request @{ op = 'request'; id = 'tr-duration'; method = 'transition.setDuration'; params = @{ transition = $transitionHandle; durationMs = 750 }; ifRevision = $revision }
    Assert-Ok $setDuration ($revision + 1) 'transition.setDuration'
    $revision++
    Read-Task19Event 'transition.durationChanged' $revision | Out-Null
    $duration = Send-Task19Request @{ op = 'request'; id = 'tr-get-duration'; method = 'transition.getDuration'; params = @{ transition = $transitionHandle } }
    Assert-Ok $duration $revision 'transition.getDuration'
    if ([int]$duration.data.durationMs -ne 750) { Fail-Task19 'transition duration readback was incorrect.' }

    $replace = Send-Task19Request @{ op = 'request'; id = 'tr-replace'; method = 'transition.replaceSettings'; params = @{ transition = $transitionHandle; settings = $defaults.data.settings }; ifRevision = $revision }
    if ($kind -eq 'fade_to_color_transition') {
        Assert-Ok $replace ($revision + 1) 'transition.replaceSettings'
        $revision++
        Read-Task19Event 'transition.settingsChanged' $revision | Out-Null
    } else {
        Assert-Ok $replace $revision 'transition.replaceSettings no-op'
    }

    Assert-Error (Send-Task19Request @{ op = 'request'; id = 'tr-invalid-kind'; method = 'transition.create'; params = @{ kind = 'not_a_transition_kind' }; ifRevision = $revision }) 'not_found' $revision 'invalid transition kind'
    $remove = Send-Task19Request @{ op = 'request'; id = 'tr-remove'; method = 'transition.remove'; params = @{ transition = $transitionHandle }; ifRevision = $revision }
    Assert-Ok $remove ($revision + 1) 'transition.remove'
    $revision++
    Read-Task19Event 'transition.removed' $revision | Out-Null
    Assert-Error (Send-Task19Request @{ op = 'request'; id = 'tr-stale'; method = 'transition.get'; params = @{ transition = $transitionHandle } }) 'not_found' $revision 'stale transition handle'

    $close = Send-Task19Request @{ op = 'request'; id = 'tr-close'; method = 'session.close'; ifRevision = $revision }
    Assert-Ok $close ($revision + 1) 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task19Engine
    Write-Output 'Task 19 transition integration: PASS'
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:ErrorTask) { Write-Host ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

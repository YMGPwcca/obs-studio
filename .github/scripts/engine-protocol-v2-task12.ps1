param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:EngineProcess = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:Wire = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:LastResponseIndex = -1

function Fail-Task12([string] $Message) {
    throw "Task 12: $Message"
}

function Start-Task12Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } |
        Select-Object -First 1
    if ($null -eq $engine) {
        $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
    }
    if ($null -eq $engine) {
        Fail-Task12 'obs-engine.exe was not found.'
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $engine.FullName
    $startInfo.WorkingDirectory = $engine.Directory.FullName
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    $script:EngineProcess = [System.Diagnostics.Process]::new()
    $script:EngineProcess.StartInfo = $startInfo
    if (-not $script:EngineProcess.Start()) {
        Fail-Task12 'failed to start obs-engine.exe.'
    }
    $script:ErrorTask = $script:EngineProcess.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:Wire = [System.Collections.Generic.List[object]]::new()
    $script:NextSequence = [uint64]1
    $script:LastResponseIndex = -1
}

function Stop-Task12Engine {
    if ($null -eq $script:EngineProcess) {
        return
    }
    if (-not $script:EngineProcess.HasExited) {
        $script:EngineProcess.Kill()
    }
    $script:EngineProcess.WaitForExit()
    if ($null -ne $script:ErrorTask) {
        $stderr = $script:ErrorTask.GetAwaiter().GetResult()
        if ($stderr -match 'Attempted to add Scene without specifying a canvas') {
            Fail-Task12 'legacy default-canvas fallback warning was emitted.'
        }
    }
    if ($script:EngineProcess.ExitCode -ne 0) {
        Fail-Task12 "engine exited with code $($script:EngineProcess.ExitCode)."
    }
}

function Read-Task12Message {
    $read = $script:EngineProcess.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) {
        Fail-Task12 'timed out waiting for engine output.'
    }
    $line = $read.Result
    if ($null -eq $line) {
        $exit = if ($script:EngineProcess.HasExited) { $script:EngineProcess.ExitCode } else { 'running' }
        Fail-Task12 "engine stdout closed unexpectedly (exit=$exit)."
    }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task12Request([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 50
    $script:EngineProcess.StandardInput.WriteLine($json)
    $script:EngineProcess.StandardInput.Flush()
    while ($true) {
        $message = Read-Task12Message
        if ($message.op -eq 'event') {
            $script:PendingEvents.Add($message)
            continue
        }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) {
            Fail-Task12 "response '$($Request.id)' was not received."
        }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Assert-Task12Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok) {
        Fail-Task12 "$Label failed with $($Response.status.code)."
    }
    if ([int64]$Response.revision -ne $Revision) {
        Fail-Task12 "$Label revision=$($Response.revision), expected $Revision."
    }
}

function Assert-Task12Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) {
        Fail-Task12 "$Label did not return $Code at revision $Revision."
    }
}

function Read-Task12Event([string] $Name, [int64] $Revision) {
    if ($script:PendingEvents.Count -gt 0) {
        $event = $script:PendingEvents[0]
        $script:PendingEvents.RemoveAt(0)
    } else {
        $event = Read-Task12Message
    }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name) {
        Fail-Task12 "expected event '$Name'."
    }
    if ([uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) {
        Fail-Task12 "event '$Name' had unexpected seq/revision."
    }
    $script:NextSequence++
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) {
        Fail-Task12 "event '$Name' was emitted before its response."
    }
    return $event
}

function Invoke-Task12Bootstrap {
    Start-Task12Engine $InstallRoot
    $ready = Read-Task12Message
    if ($ready.event -ne 'ready') { Fail-Task12 'engine did not emit its ready marker.' }
    $hello = Send-Task12Request @{ op = 'request'; id = 't12-hello'; method = 'session.hello' }
    Assert-Task12Ok $hello 0 'session.hello'
    $subscribe = Send-Task12Request @{ op = 'request'; id = 't12-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'scene.*' }, @{ pattern = 'item.*' }) } }
    Assert-Task12Ok $subscribe 0 'session.subscribe'
    $empty = Send-Task12Request @{ op = 'request'; id = 't12-list-empty'; method = 'scene.list' }
    Assert-Task12Ok $empty 0 'empty scene.list'
    if ([int]$empty.data.count -ne 0) { Fail-Task12 'initial scene list was not empty.' }
}

function Invoke-Task12SceneLifecycle {
    $created = Send-Task12Request @{ op = 'request'; id = 't12-create'; method = 'scene.create' }
    Assert-Task12Ok $created 1 'scene.create'
    if ([string]$created.data.scene -ne '2' -or [string]$created.data.canvas -ne '1') { Fail-Task12 'scene.create did not use Main Canvas handle 1 / Scene handle 2.' }
    Read-Task12Event 'scene.created' 1 | Out-Null
    $got = Send-Task12Request @{ op = 'request'; id = 't12-get'; method = 'scene.get'; params = @{ scene = '2' } }
    Assert-Task12Ok $got 1 'scene.get'
    if ([string]$got.data.canvas -ne '1') { Fail-Task12 'scene.get returned the wrong Canvas.' }
    $renamed = Send-Task12Request @{ op = 'request'; id = 't12-rename'; method = 'scene.rename'; params = @{ scene = '2'; name = 'Task12 Scene' }; ifRevision = 1 }
    Assert-Task12Ok $renamed 2 'scene.rename'
    Read-Task12Event 'scene.renamed' 2 | Out-Null
    $duplicate = Send-Task12Request @{ op = 'request'; id = 't12-duplicate'; method = 'scene.duplicate'; params = @{ scene = '2'; mode = 'references' }; ifRevision = 2 }
    Assert-Task12Ok $duplicate 3 'scene.duplicate'
    if ([string]$duplicate.data.scene -ne '3' -or [string]$duplicate.data.canvas -ne '1') { Fail-Task12 'scene.duplicate returned unexpected identity.' }
    Read-Task12Event 'scene.created' 3 | Out-Null
    $stale = Send-Task12Request @{ op = 'request'; id = 't12-stale'; method = 'scene.rename'; params = @{ scene = '2'; name = 'stale' }; ifRevision = 1 }
    Assert-Task12Error $stale 'revision_conflict' 3 'stale scene.rename'
}

function Invoke-Task12Cleanup {
    $removedCopy = Send-Task12Request @{ op = 'request'; id = 't12-remove-copy'; method = 'scene.remove'; params = @{ scene = '3' }; ifRevision = 3 }
    Assert-Task12Ok $removedCopy 4 'scene.remove copy'
    Read-Task12Event 'scene.removed' 4 | Out-Null
    $removed = Send-Task12Request @{ op = 'request'; id = 't12-remove'; method = 'scene.remove'; params = @{ scene = '2' }; ifRevision = 4 }
    Assert-Task12Ok $removed 5 'scene.remove'
    Read-Task12Event 'scene.removed' 5 | Out-Null
    $finalList = Send-Task12Request @{ op = 'request'; id = 't12-list-final'; method = 'scene.list' }
    Assert-Task12Ok $finalList 5 'final scene.list'
    if ([int]$finalList.data.count -ne 0) { Fail-Task12 'final scene list was not empty.' }
    $close = Send-Task12Request @{ op = 'request'; id = 't12-close'; method = 'session.close'; ifRevision = 5 }
    Assert-Task12Ok $close 6 'session.close'
    $script:EngineProcess.WaitForExit(30000) | Out-Null
    Stop-Task12Engine
    Write-Output 'Task 12 scene integration: PASS'
}

function Invoke-Task12Scenario {
    Invoke-Task12Bootstrap
    Invoke-Task12SceneLifecycle
    Invoke-Task12Cleanup
}

try {
    Invoke-Task12Scenario
} catch {
    if ($null -ne $script:EngineProcess -and -not $script:EngineProcess.HasExited) {
        $script:EngineProcess.Kill()
        $script:EngineProcess.WaitForExit()
    }
    throw
}

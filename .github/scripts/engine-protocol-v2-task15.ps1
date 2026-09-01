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
$script:LastRequest = ''
$script:LastMessage = $null

function Fail-Task15([string] $Message) { throw "Task 15: $Message" }

function Start-Task15Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task15 'obs-engine.exe was not found.' }
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
    if (-not $script:Process.Start()) { Fail-Task15 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task15Engine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task15 'fallback canvas warning was emitted.' }
    if ($script:Process.ExitCode -ne 0) { Fail-Task15 "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task15Message {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task15 'timed out waiting for stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task15 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task15Request([hashtable] $Request) {
    $script:LastRequest = [string]$Request.id
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task15Message
        $script:LastMessage = $message
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task15 "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Send-Task15Legacy([hashtable] $Request) {
    $script:LastRequest = [string]$Request.id
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    $message = Read-Task15Message
    $script:LastMessage = $message
    return $message
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task15 "$Label failed at revision $($Response.revision)." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task15 "$Label did not return $Code at revision $Revision." }
}

function Read-Task15Event([string] $Name, [int64] $Revision) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task15Message }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) { Fail-Task15 "unexpected event; expected $Name at revision $Revision." }
    $script:NextSequence++
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task15 "event $Name preceded its response." }
    return $event
}

try {
    Start-Task15Engine $InstallRoot
    $ready = Read-Task15Message
    if ($ready.event -ne 'ready') { Fail-Task15 'ready marker was not received.' }
    Assert-Ok (Send-Task15Request @{ op = 'request'; id = 'p-hello'; method = 'session.hello' }) 0 'hello'
    Assert-Ok (Send-Task15Request @{ op = 'request'; id = 'p-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'program.*' }, @{ pattern = 'scene.*' }) } }) 0 'subscribe'

    $initial = Send-Task15Request @{ op = 'request'; id = 'p-initial'; method = 'program.getScene' }
    Assert-Ok $initial 0 'initial program.getScene'
    if ($null -ne $initial.data.scene) { Fail-Task15 'initial Program was not null.' }

    $first = Send-Task15Request @{ op = 'request'; id = 'p-first'; method = 'scene.create'; params = @{ name = 'Program A' } }
    Assert-Ok $first 1 'first scene.create'
    if ([string]$first.data.scene -ne '2') { Fail-Task15 'unexpected first Scene handle.' }
    Read-Task15Event 'scene.created' 1 | Out-Null
    $second = Send-Task15Request @{ op = 'request'; id = 'p-second'; method = 'scene.create'; params = @{ name = 'Program B' }; ifRevision = 1 }
    Assert-Ok $second 2 'second scene.create'
    if ([string]$second.data.scene -ne '3') { Fail-Task15 'unexpected second Scene handle.' }
    Read-Task15Event 'scene.created' 2 | Out-Null

    $setFirst = Send-Task15Request @{ op = 'request'; id = 'p-set-first'; method = 'program.setScene'; params = @{ scene = '2' }; ifRevision = 2 }
    Assert-Ok $setFirst 3 'program.setScene first'
    if ([string]$setFirst.data.scene -ne '2') { Fail-Task15 'Program first route did not read back Scene 2.' }
    $firstEvent = Read-Task15Event 'program.sceneChanged' 3
    if ([string]$firstEvent.data.scene -ne '2') { Fail-Task15 'Program change event had the wrong Scene.' }
    if ($null -ne $firstEvent.data.previousScene) { Fail-Task15 'initial Program change unexpectedly had a previous Scene.' }

    $setSecond = Send-Task15Request @{ op = 'request'; id = 'p-set-second'; method = 'program.setScene'; params = @{ scene = '3' }; ifRevision = 3 }
    Assert-Ok $setSecond 4 'program.setScene second'
    $secondEvent = Read-Task15Event 'program.sceneChanged' 4
    if ([string]$secondEvent.data.scene -ne '3' -or [string]$secondEvent.data.previousScene -ne '2') { Fail-Task15 'Program switch event was incorrect.' }

    $clear = Send-Task15Request @{ op = 'request'; id = 'p-clear'; method = 'program.setScene'; params = @{ scene = $null }; ifRevision = 4 }
    Assert-Ok $clear 5 'program.setScene clear'
    $clearEvent = Read-Task15Event 'program.sceneChanged' 5
    if ($null -ne $clearEvent.data.scene -or [string]$clearEvent.data.previousScene -ne '3') { Fail-Task15 'Program clear event was incorrect.' }
    $afterClear = Send-Task15Request @{ op = 'request'; id = 'p-after-clear'; method = 'program.getScene' }
    Assert-Ok $afterClear 5 'program.getScene after clear'
    if ($null -ne $afterClear.data.scene) { Fail-Task15 'Program clear did not persist.' }

    $stale = Send-Task15Request @{ op = 'request'; id = 'p-stale'; method = 'program.setScene'; params = @{ scene = '2' }; ifRevision = 4 }
    Assert-Error $stale 'revision_conflict' 5 'stale program.setScene'

    $legacy = Send-Task15Legacy @{ id = 9001; cmd = 'program.set'; scene = 2 }
    if (-not $legacy.ok) { Fail-Task15 'legacy program.set did not succeed.' }
    $legacyRead = Send-Task15Request @{ op = 'request'; id = 'p-legacy-read'; method = 'program.getScene' }
    Assert-Ok $legacyRead 5 'program.getScene after legacy route'
    if ([string]$legacyRead.data.scene -ne '2') { Fail-Task15 'v2 Program did not observe legacy program.set routing.' }

    $removeCurrent = Send-Task15Request @{ op = 'request'; id = 'p-remove-current'; method = 'scene.remove'; params = @{ scene = '2' }; ifRevision = 5 }
    Assert-Ok $removeCurrent 6 'remove current Program Scene'
    $removeProgramEvent = Read-Task15Event 'program.sceneChanged' 6
    if ($null -ne $removeProgramEvent.data.scene -or [string]$removeProgramEvent.data.previousScene -ne '2') { Fail-Task15 'Program cleanup event on Scene removal was incorrect.' }
    Read-Task15Event 'scene.removed' 6 | Out-Null
    Assert-Error (Send-Task15Request @{ op = 'request'; id = 'p-removed'; method = 'program.setScene'; params = @{ scene = '2' }; ifRevision = 6 }) 'not_found' 6 'removed Program Scene'

    $close = Send-Task15Request @{ op = 'request'; id = 'p-close'; method = 'session.close'; ifRevision = 6 }
    Assert-Ok $close 7 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task15Engine
    Write-Output 'Task 15 program integration: PASS'
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:LastMessage) { Write-Error ("last protocol message: " + ($script:LastMessage | ConvertTo-Json -Compress -Depth 50)) }
    if ($null -ne $script:ErrorTask) { Write-Error ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

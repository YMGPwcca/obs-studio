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

function Fail-Task16([string] $Message) { throw "Task 16: $Message" }

function Start-Task16Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task16 'obs-engine.exe was not found.' }
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
    if (-not $script:Process.Start()) { Fail-Task16 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task16Engine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task16 'fallback canvas warning was emitted.' }
    if ($script:Process.ExitCode -ne 0) { Fail-Task16 "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task16Message {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task16 'timed out waiting for stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task16 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task16Request([hashtable] $Request) {
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task16Message
        $script:LastMessage = $message
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task16 "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task16 "$Label failed at revision $($Response.revision)." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task16 "$Label did not return $Code at revision $Revision." }
}

function Read-Task16Event([string] $Name, [int64] $Revision) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task16Message }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) { Fail-Task16 "unexpected event; expected $Name at revision $Revision." }
    $script:NextSequence++
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task16 "event $Name preceded its response." }
    return $event
}

try {
    Start-Task16Engine $InstallRoot
    $ready = Read-Task16Message
    if ($ready.event -ne 'ready') { Fail-Task16 'ready marker was not received.' }
    Assert-Ok (Send-Task16Request @{ op = 'request'; id = 'v-hello'; method = 'session.hello' }) 0 'hello'
    Assert-Ok (Send-Task16Request @{ op = 'request'; id = 'v-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'program.*' }, @{ pattern = 'preview.*' }, @{ pattern = 'scene.*' }) } }) 0 'subscribe'

    $programInitial = Send-Task16Request @{ op = 'request'; id = 'v-program-initial'; method = 'program.getScene' }
    Assert-Ok $programInitial 0 'initial Program'
    if ($null -ne $programInitial.data.scene) { Fail-Task16 'initial Program was not null.' }
    $previewInitial = Send-Task16Request @{ op = 'request'; id = 'v-preview-initial'; method = 'preview.getScene' }
    Assert-Ok $previewInitial 0 'initial Preview'
    if ($null -ne $previewInitial.data.scene -or $previewInitial.data.hasScene) { Fail-Task16 'initial Preview was not clear.' }
    $infoInitial = Send-Task16Request @{ op = 'request'; id = 'v-preview-info'; method = 'preview.getInfo' }
    Assert-Ok $infoInitial 0 'initial Preview info'
    if ([int]$infoInitial.data.renderWidth -le 0 -or [int]$infoInitial.data.renderHeight -le 0) { Fail-Task16 'Preview info did not report Main Canvas dimensions.' }

    $first = Send-Task16Request @{ op = 'request'; id = 'v-first'; method = 'scene.create'; params = @{ name = 'Preview A' } }
    Assert-Ok $first 1 'first scene.create'
    Read-Task16Event 'scene.created' 1 | Out-Null
    $second = Send-Task16Request @{ op = 'request'; id = 'v-second'; method = 'scene.create'; params = @{ name = 'Preview B' }; ifRevision = 1 }
    Assert-Ok $second 2 'second scene.create'
    Read-Task16Event 'scene.created' 2 | Out-Null

    $programA = Send-Task16Request @{ op = 'request'; id = 'v-program-a'; method = 'program.setScene'; params = @{ scene = '2' }; ifRevision = 2 }
    Assert-Ok $programA 3 'Program A'
    Read-Task16Event 'program.sceneChanged' 3 | Out-Null
    $previewB = Send-Task16Request @{ op = 'request'; id = 'v-preview-b'; method = 'preview.setScene'; params = @{ scene = '3' }; ifRevision = 3 }
    Assert-Ok $previewB 4 'Preview B'
    Read-Task16Event 'preview.sceneChanged' 4 | Out-Null
    $infoB = Send-Task16Request @{ op = 'request'; id = 'v-info-b'; method = 'preview.getInfo' }
    Assert-Ok $infoB 4 'Preview B info'
    if ([string]$infoB.data.scene -ne '3' -or [string]$infoB.data.canvas -ne '1' -or -not $infoB.data.hasScene) { Fail-Task16 'Preview B metadata was incorrect.' }
    if ([int]$infoB.data.renderWidth -le 0 -or [int]$infoB.data.renderHeight -le 0) { Fail-Task16 'Preview B dimensions were not reported.' }

    $clearPreview = Send-Task16Request @{ op = 'request'; id = 'v-preview-clear'; method = 'preview.setScene'; params = @{ scene = $null }; ifRevision = 4 }
    Assert-Ok $clearPreview 5 'clear Preview'
    Read-Task16Event 'preview.sceneChanged' 5 | Out-Null
    $programStillA = Send-Task16Request @{ op = 'request'; id = 'v-program-still-a'; method = 'program.getScene' }
    Assert-Ok $programStillA 5 'Program after Preview clear'
    if ([string]$programStillA.data.scene -ne '2') { Fail-Task16 'clearing Preview changed Program.' }

    $programB = Send-Task16Request @{ op = 'request'; id = 'v-program-b'; method = 'program.setScene'; params = @{ scene = '3' }; ifRevision = 5 }
    Assert-Ok $programB 6 'Program B'
    Read-Task16Event 'program.sceneChanged' 6 | Out-Null
    $previewStillClear = Send-Task16Request @{ op = 'request'; id = 'v-preview-still-clear'; method = 'preview.getScene' }
    Assert-Ok $previewStillClear 6 'Preview after Program switch'
    if ($null -ne $previewStillClear.data.scene) { Fail-Task16 'changing Program changed Preview.' }

    $previewA = Send-Task16Request @{ op = 'request'; id = 'v-preview-a'; method = 'preview.setScene'; params = @{ scene = '2' }; ifRevision = 6 }
    Assert-Ok $previewA 7 'Preview A'
    Read-Task16Event 'preview.sceneChanged' 7 | Out-Null
    $removePreviewScene = Send-Task16Request @{ op = 'request'; id = 'v-remove-preview'; method = 'scene.remove'; params = @{ scene = '2' }; ifRevision = 7 }
    Assert-Ok $removePreviewScene 8 'remove Preview Scene'
    $previewCleanup = Read-Task16Event 'preview.sceneChanged' 8
    if ($null -ne $previewCleanup.data.scene -or [string]$previewCleanup.data.previousScene -ne '2') { Fail-Task16 'Preview cleanup event was incorrect.' }
    Read-Task16Event 'scene.removed' 8 | Out-Null
    $programStillB = Send-Task16Request @{ op = 'request'; id = 'v-program-still-b'; method = 'program.getScene' }
    Assert-Ok $programStillB 8 'Program after Preview Scene removal'
    if ([string]$programStillB.data.scene -ne '3') { Fail-Task16 'removing Preview Scene changed Program.' }

    $removeProgramScene = Send-Task16Request @{ op = 'request'; id = 'v-remove-program'; method = 'scene.remove'; params = @{ scene = '3' }; ifRevision = 8 }
    Assert-Ok $removeProgramScene 9 'remove Program Scene'
    $programCleanup = Read-Task16Event 'program.sceneChanged' 9
    if ($null -ne $programCleanup.data.scene -or [string]$programCleanup.data.previousScene -ne '3') { Fail-Task16 'Program cleanup event was incorrect.' }
    Read-Task16Event 'scene.removed' 9 | Out-Null
    Assert-Error (Send-Task16Request @{ op = 'request'; id = 'v-preview-stale'; method = 'preview.setScene'; params = @{ scene = '2' }; ifRevision = 9 }) 'not_found' 9 'stale Preview Scene'

    $close = Send-Task16Request @{ op = 'request'; id = 'v-close'; method = 'session.close'; ifRevision = 9 }
    Assert-Ok $close 10 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task16Engine
    Write-Output 'Task 16 preview integration: PASS'
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:LastMessage) { Write-Error ("last protocol message: " + ($script:LastMessage | ConvertTo-Json -Compress -Depth 50)) }
    if ($null -ne $script:ErrorTask) { Write-Error ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

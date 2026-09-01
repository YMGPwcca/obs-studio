param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot,
    [Parameter(Mandatory = $true)]
    [string] $ConsumerPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Process = $null
$script:ErrorTask = $null
$script:Consumer = $null
$script:ConsumerOutputTask = $null
$script:ConsumerErrorTask = $null
$script:Events = [System.Collections.Generic.List[object]]::new()
$script:Wire = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:LastResponseIndex = -1
$script:LastMessage = $null
$script:ResponseTrace = [System.Collections.Generic.List[object]]::new()

function Fail-Task17([string] $Message) { throw "Task 17: $Message" }

function Start-Task17Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task17 'obs-engine.exe was not found.' }
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
    if (-not $script:Process.Start()) { Fail-Task17 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task17Engine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task17 'fallback canvas warning was emitted.' }
    if ($script:Process.ExitCode -ne 0) { Fail-Task17 "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task17Message {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task17 'timed out waiting for stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task17 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task17Request([hashtable] $Request) {
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task17Message
        $script:LastMessage = $message
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task17 "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        $code = if ($null -ne $message.status.PSObject.Properties['code']) { [string]$message.status.code } else { '' }
        $script:ResponseTrace.Add([pscustomobject]@{ Id = [string]$Request.id; Revision = [int64]$message.revision; Ok = [bool]$message.status.ok; Code = $code })
        return $message
    }
}

function Send-Task17GuardedRequest([string] $Id, [string] $Method, [hashtable] $Params, [int64] $Revision) {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $attemptId = if ($attempt -eq 0) { $Id } else { "$Id-retry$attempt" }
        $request = @{ op = 'request'; id = $attemptId; method = $Method; ifRevision = $Revision }
        if ($null -ne $Params) { $request.params = $Params }
        $response = Send-Task17Request $request
        if ($response.status.ok -or [string]$response.status.code -ne 'revision_conflict') {
            $response | Add-Member -NotePropertyName GuardRevision -NotePropertyValue $Revision -Force
            return $response
        }
        $Revision = [int64]$response.revision
    }
    return $response
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task17 "$Label failed at revision $($Response.revision)." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task17 "$Label did not return $Code at revision $Revision." }
}

function Read-Task17Event([string] $Name, [int64] $Revision) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task17Message }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) { Fail-Task17 "unexpected event; expected $Name at revision $Revision." }
    $script:NextSequence++
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task17 "event $Name preceded its response." }
    return $event
}

function Start-Task17Consumer([string] $Path, $Descriptor, [int] $Frames) {
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $resolvedPath
    $info.Arguments = "--shared-handle=$($Descriptor.sharedTexture.handle) --width=$($Descriptor.width) --height=$($Descriptor.height) --adapter-luid=$($Descriptor.adapterLuid) --frames=$Frames --timeout-ms=250"
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Consumer = [System.Diagnostics.Process]::new()
    $script:Consumer.StartInfo = $info
    if (-not $script:Consumer.Start()) { Fail-Task17 'failed to start the D3D11 consumer.' }
    $script:ConsumerOutputTask = $script:Consumer.StandardOutput.ReadToEndAsync()
    $script:ConsumerErrorTask = $script:Consumer.StandardError.ReadToEndAsync()
}

function Wait-Task17Consumer {
    if ($null -eq $script:Consumer) { Fail-Task17 'consumer was not started.' }
    if (-not $script:Consumer.WaitForExit(30000)) {
        $script:Consumer.Kill()
        $script:Consumer.WaitForExit()
        Fail-Task17 'D3D11 consumer timed out.'
    }
    $stdout = $script:ConsumerOutputTask.GetAwaiter().GetResult().Trim()
    $stderr = $script:ConsumerErrorTask.GetAwaiter().GetResult()
    if ($script:Consumer.ExitCode -ne 0) { Fail-Task17 "D3D11 consumer failed: $stdout $stderr" }
    if ([string]::IsNullOrWhiteSpace($stdout)) { Fail-Task17 'D3D11 consumer returned no evidence.' }
    return ($stdout | ConvertFrom-Json)
}

function Assert-ColorEvidence($Evidence, [bool] $ExpectChanged, [bool] $ExpectBlue, [string] $Label) {
    if ([int]$Evidence.frames -lt 2 -or [int]$Evidence.timeouts -ge [int]$Evidence.frames) { Fail-Task17 "$Label did not acquire enough synchronized frames." }
    if ($ExpectChanged -and [int]$Evidence.uniqueChecksums -lt 2) { Fail-Task17 "$Label did not observe content changing after the route switch." }
    if ($ExpectBlue) {
        if ([int]$Evidence.lastCenterB -lt 200 -or [int]$Evidence.lastCenterR -gt 50) { Fail-Task17 "$Label did not observe the expected blue output." }
    } else {
        if ([int]$Evidence.firstCenterR -lt 200 -or [int]$Evidence.firstCenterB -gt 50) { Fail-Task17 "$Label did not observe the expected red output." }
    }
}

function Invoke-Task17Bootstrap {
    Start-Task17Engine $InstallRoot
    $ready = Read-Task17Message
    if ($ready.event -ne 'ready') { Fail-Task17 'ready marker was not received.' }
    $script:T17Hello = Send-Task17Request @{ op = 'request'; id = 't17-hello'; method = 'session.hello' }
    Assert-Ok $script:T17Hello 0 'hello'
    $transportCapability = @($script:T17Hello.data.capabilities | Where-Object { $_.name -eq 'preview.d3d11SharedTexture.v1' }) | Select-Object -First 1
    if ($null -eq $transportCapability) { Fail-Task17 'D3D11 shared-texture capability was not advertised on the physical D3D11 host.' }
    Assert-Ok (Send-Task17Request @{ op = 'request'; id = 't17-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'scene.*' }, @{ pattern = 'item.*' }, @{ pattern = 'program.*' }, @{ pattern = 'preview.*' }, @{ pattern = 'previewOutput.*' }) } }) 0 'subscribe'
}

function Invoke-Task17ColorSetup {
    $script:T17RedScene = Send-Task17Request @{ op = 'request'; id = 't17-red-scene'; method = 'scene.create'; params = @{ name = 'Task17 Red' } }
    Assert-Ok $script:T17RedScene 1 'red scene.create'
    Read-Task17Event 'scene.created' 1 | Out-Null
    $redSource = Send-Task17Request @{ op = 'request'; id = 't17-red-source'; method = 'source.create'; params = @{ kind = 'color_source_v3'; name = 'Task17 Red Source'; settings = @{ width = 1920; height = 1080; color = 4278190335 } } }
    Assert-Ok $redSource 2 'red source.create'
    $redItem = Send-Task17Request @{ op = 'request'; id = 't17-red-item'; method = 'item.create'; params = @{ scene = [string]$script:T17RedScene.data.scene; source = [string]$redSource.data.source }; ifRevision = 2 }
    Assert-Ok $redItem 3 'red item.create'
    Read-Task17Event 'item.created' 3 | Out-Null
    $script:T17BlueScene = Send-Task17Request @{ op = 'request'; id = 't17-blue-scene'; method = 'scene.create'; params = @{ name = 'Task17 Blue' }; ifRevision = 3 }
    Assert-Ok $script:T17BlueScene 4 'blue scene.create'
    Read-Task17Event 'scene.created' 4 | Out-Null
    $blueSource = Send-Task17Request @{ op = 'request'; id = 't17-blue-source'; method = 'source.create'; params = @{ kind = 'color_source_v3'; name = 'Task17 Blue Source'; settings = @{ width = 1920; height = 1080; color = 4294901760 } } }
    Assert-Ok $blueSource 5 'blue source.create'
    $blueItem = Send-Task17Request @{ op = 'request'; id = 't17-blue-item'; method = 'item.create'; params = @{ scene = [string]$script:T17BlueScene.data.scene; source = [string]$blueSource.data.source }; ifRevision = 5 }
    Assert-Ok $blueItem 6 'blue item.create'
    Read-Task17Event 'item.created' 6 | Out-Null
    $programRed = Send-Task17Request @{ op = 'request'; id = 't17-program-red'; method = 'program.setScene'; params = @{ scene = [string]$script:T17RedScene.data.scene }; ifRevision = 6 }
    Assert-Ok $programRed 7 'program red'
    Read-Task17Event 'program.sceneChanged' 7 | Out-Null
    $programSettled = Send-Task17Request @{ op = 'request'; id = 't17-program-settled'; method = 'program.getScene' }
    if (-not $programSettled.status.ok -or [int64]$programSettled.revision -lt [int64]$programRed.revision) { Fail-Task17 'program state query regressed the revision after the red route.' }
    if ([string]$programSettled.data.scene -ne [string]$script:T17RedScene.data.scene) { Fail-Task17 'Program route did not settle on the red Scene.' }
    $script:T17Revision = [int64]$programSettled.revision
    $previewBlue = Send-Task17GuardedRequest 't17-preview-blue' 'preview.setScene' @{ scene = [string]$script:T17BlueScene.data.scene } $script:T17Revision
    $script:T17Revision = [int64]$previewBlue.GuardRevision + 1
    Assert-Ok $previewBlue $script:T17Revision 'preview blue'
    Read-Task17Event 'preview.sceneChanged' $script:T17Revision | Out-Null
}

function Invoke-Task17OutputSetup {
    $programOutput = Send-Task17GuardedRequest 't17-output-program' 'previewOutput.create' @{ target = @{ type = 'program' }; width = 320; height = 180; enabled = $true } $script:T17Revision
    $programOutputRevision = [int64]$programOutput.GuardRevision + 1
    Assert-Ok $programOutput $programOutputRevision 'program PreviewOutput create'
    Read-Task17Event 'previewOutput.created' $programOutputRevision | Out-Null
    $script:T17Revision = $programOutputRevision
    $script:T17ProgramOutputHandle = [string]$programOutput.data.previewOutput
    $script:T17PreviewOutput = Send-Task17GuardedRequest 't17-output-preview' 'previewOutput.create' @{ target = @{ type = 'preview' }; width = 320; height = 180; enabled = $false } $script:T17Revision
    $previewOutputRevision = [int64]$script:T17PreviewOutput.GuardRevision + 1
    Assert-Ok $script:T17PreviewOutput $previewOutputRevision 'preview PreviewOutput create'
    Read-Task17Event 'previewOutput.created' $previewOutputRevision | Out-Null
    $script:T17Revision = $previewOutputRevision
    $script:T17ProgramDescriptor = Send-Task17Request @{ op = 'request'; id = 't17-get-shared'; method = 'previewOutput.getSharedTexture'; params = @{ previewOutput = $script:T17ProgramOutputHandle } }
    if (-not $script:T17ProgramDescriptor.status.ok -or [int64]$script:T17ProgramDescriptor.revision -lt $script:T17Revision) { Fail-Task17 'get program shared texture failed or regressed the revision.' }
    if ([string]$script:T17ProgramDescriptor.data.sharedTexture.type -ne 'd3d11LegacySharedHandle' -or [string]$script:T17ProgramDescriptor.data.sharedTexture.openApi -ne 'ID3D11Device::OpenSharedResource' -or -not $script:T17ProgramDescriptor.data.sharedTexture.controllerMustNotClose) { Fail-Task17 'shared texture descriptor did not preserve the documented legacy handle contract.' }
    if ([string]$script:T17ProgramDescriptor.data.synchronization.type -ne 'keyedMutex' -or [int]$script:T17ProgramDescriptor.data.synchronization.consumerAcquireKey -ne 1 -or [int]$script:T17ProgramDescriptor.data.synchronization.consumerReleaseKey -ne 0) { Fail-Task17 'keyed mutex synchronization descriptor was incorrect.' }
    $script:T17InitialGeneration = [string]$script:T17ProgramDescriptor.data.resourceGeneration
    $script:T17Revision = [int64]$script:T17ProgramDescriptor.revision
    Start-Task17Consumer $ConsumerPath $script:T17ProgramDescriptor.data 90
    Start-Sleep -Milliseconds 700
}

function Invoke-Task17RouteSwitch {
    $programBeforeSwitch = Send-Task17Request @{ op = 'request'; id = 't17-program-before-switch'; method = 'program.getScene' }
    if (-not $programBeforeSwitch.status.ok -or [int64]$programBeforeSwitch.revision -lt $script:T17Revision) { Fail-Task17 'program state query regressed the revision before the route switch.' }
    if ([string]$programBeforeSwitch.data.scene -ne [string]$script:T17RedScene.data.scene) { Fail-Task17 'Program changed before the explicit route switch.' }
    $script:T17Revision = [int64]$programBeforeSwitch.revision
    $programBlue = Send-Task17GuardedRequest 't17-program-blue' 'program.setScene' @{ scene = [string]$script:T17BlueScene.data.scene } $script:T17Revision
    $programBlueRevision = [int64]$programBlue.GuardRevision + 1
    Assert-Ok $programBlue $programBlueRevision 'program blue'
    Read-Task17Event 'program.sceneChanged' $programBlueRevision | Out-Null
    $script:T17Revision = $programBlueRevision
    $script:T17FirstEvidence = Wait-Task17Consumer
    Assert-ColorEvidence $script:T17FirstEvidence $true $false 'first shared-texture consumer'
    if ([int]$script:T17FirstEvidence.lastCenterB -lt 200 -or [int]$script:T17FirstEvidence.lastCenterR -gt 50) { Fail-Task17 'first consumer did not observe the post-switch blue frame.' }
    $programBlueSettled = Send-Task17Request @{ op = 'request'; id = 't17-program-blue-settled'; method = 'program.getScene' }
    if (-not $programBlueSettled.status.ok -or [int64]$programBlueSettled.revision -lt $programBlueRevision) { Fail-Task17 'program state query regressed the revision after the route switch.' }
    if ([string]$programBlueSettled.data.scene -ne [string]$script:T17BlueScene.data.scene) { Fail-Task17 'Program route did not settle on the blue Scene.' }
    $script:T17Revision = [int64]$programBlueSettled.revision
}

function Invoke-Task17ResourceChecks {
    $release = Send-Task17Request @{ op = 'request'; id = 't17-release'; method = 'previewOutput.releaseSharedTexture'; params = @{ previewOutput = $script:T17ProgramOutputHandle } }
    Assert-Ok $release $script:T17Revision 'release shared texture'
    if (-not $release.data.released) { Fail-Task17 'releaseSharedTexture did not acknowledge release.' }
    $resize = Send-Task17GuardedRequest 't17-resize' 'previewOutput.resize' @{ previewOutput = $script:T17ProgramOutputHandle; width = 160; height = 90 } $script:T17Revision
    $resizeRevision = [int64]$resize.GuardRevision + 1
    Assert-Ok $resize $resizeRevision 'resize shared texture'
    $resourceEvent = Read-Task17Event 'previewOutput.resourceChanged' $resizeRevision
    $script:T17Revision = $resizeRevision
    if ([string]$resize.data.resourceGeneration -eq $script:T17InitialGeneration -or [int]$resize.data.width -ne 160 -or [int]$resize.data.height -ne 90) { Fail-Task17 'resize did not replace the resource or bump generation.' }
    if ([string]$resourceEvent.data.resourceGeneration -ne [string]$resize.data.resourceGeneration) { Fail-Task17 'resourceChanged event disagreed with resize response.' }
    $resizedDescriptor = Send-Task17Request @{ op = 'request'; id = 't17-get-resized'; method = 'previewOutput.getSharedTexture'; params = @{ previewOutput = $script:T17ProgramOutputHandle } }
    Assert-Ok $resizedDescriptor $script:T17Revision 'get resized shared texture'
    Start-Task17Consumer $ConsumerPath $resizedDescriptor.data 12
    $script:T17SecondEvidence = Wait-Task17Consumer
    Assert-ColorEvidence $script:T17SecondEvidence $false $true 'resized shared-texture consumer'
    Assert-Ok (Send-Task17Request @{ op = 'request'; id = 't17-release-2'; method = 'previewOutput.releaseSharedTexture'; params = @{ previewOutput = $script:T17ProgramOutputHandle } }) $script:T17Revision 'release resized shared texture'
    $disable = Send-Task17GuardedRequest 't17-disable' 'previewOutput.setEnabled' @{ previewOutput = $script:T17ProgramOutputHandle; enabled = $false } $script:T17Revision
    $disableRevision = [int64]$disable.GuardRevision + 1
    Assert-Ok $disable $disableRevision 'disable PreviewOutput'
    Read-Task17Event 'previewOutput.enabledChanged' $disableRevision | Out-Null
    $script:T17Revision = $disableRevision
    $enable = Send-Task17GuardedRequest 't17-enable' 'previewOutput.setEnabled' @{ previewOutput = $script:T17ProgramOutputHandle; enabled = $true } $script:T17Revision
    $enableRevision = [int64]$enable.GuardRevision + 1
    Assert-Ok $enable $enableRevision 'enable PreviewOutput'
    Read-Task17Event 'previewOutput.enabledChanged' $enableRevision | Out-Null
    $script:T17Revision = $enableRevision
}

function Invoke-Task17Cleanup {
    $destroyPreview = Send-Task17GuardedRequest 't17-destroy-preview' 'previewOutput.destroy' @{ previewOutput = [string]$script:T17PreviewOutput.data.previewOutput } $script:T17Revision
    $destroyPreviewRevision = [int64]$destroyPreview.GuardRevision + 1
    Assert-Ok $destroyPreview $destroyPreviewRevision 'destroy preview PreviewOutput'
    Read-Task17Event 'previewOutput.destroyed' $destroyPreviewRevision | Out-Null
    $script:T17Revision = $destroyPreviewRevision
    $destroyProgram = Send-Task17GuardedRequest 't17-destroy-program' 'previewOutput.destroy' @{ previewOutput = $script:T17ProgramOutputHandle } $script:T17Revision
    $destroyProgramRevision = [int64]$destroyProgram.GuardRevision + 1
    Assert-Ok $destroyProgram $destroyProgramRevision 'destroy program PreviewOutput'
    Read-Task17Event 'previewOutput.destroyed' $destroyProgramRevision | Out-Null
    $script:T17Revision = $destroyProgramRevision
    Write-Output ("Task 17 first consumer evidence: " + ($script:T17FirstEvidence | ConvertTo-Json -Compress -Depth 20))
    Write-Output ("Task 17 resized consumer evidence: " + ($script:T17SecondEvidence | ConvertTo-Json -Compress -Depth 20))
    $close = Send-Task17GuardedRequest 't17-close' 'session.close' $null $script:T17Revision
    $closeRevision = [int64]$close.GuardRevision + 1
    Assert-Ok $close $closeRevision 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task17Engine
    Write-Output 'Task 17 D3D11 shared-texture integration: PASS'
}

function Invoke-Task17Scenario {
    Invoke-Task17Bootstrap
    Invoke-Task17ColorSetup
    Invoke-Task17OutputSetup
    Invoke-Task17RouteSwitch
    Invoke-Task17ResourceChecks
    Invoke-Task17Cleanup
}

function Write-Task17FailureDetails {
    if ($null -ne $script:LastMessage) {
        $lastOp = if ($null -ne $script:LastMessage.PSObject.Properties['op']) { [string]$script:LastMessage.op } else { '' }
        $lastEvent = if ($null -ne $script:LastMessage.PSObject.Properties['event']) { [string]$script:LastMessage.event } else { '' }
        $lastCode = if ($null -ne $script:LastMessage.PSObject.Properties['status'] -and $null -ne $script:LastMessage.status.PSObject.Properties['code']) { [string]$script:LastMessage.status.code } else { '' }
        Write-Host ("last protocol summary: op=$lastOp id=$($script:LastMessage.id) event=$lastEvent revision=$($script:LastMessage.revision) status=$lastCode")
    }
    if ($script:ResponseTrace.Count -gt 0) { Write-Host ("response trace: " + ($script:ResponseTrace | ConvertTo-Json -Compress -Depth 50)) }
    if ($null -ne $script:ErrorTask) { Write-Host ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
}

function Invoke-Task17FailureCleanup {
    if ($null -ne $script:Consumer -and -not $script:Consumer.HasExited) { $script:Consumer.Kill(); $script:Consumer.WaitForExit() }
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    Write-Task17FailureDetails
}

try {
    Invoke-Task17Scenario
} catch {
    Invoke-Task17FailureCleanup
    throw
}

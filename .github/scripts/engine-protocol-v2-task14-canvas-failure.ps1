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
$script:Events = [System.Collections.Generic.List[object]]::new()
$script:Wire = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:LastResponseIndex = -1
$script:Revision = [int64]0

function Fail-Task14CanvasFailure([string] $Message) { throw "Task 14 Canvas failure: $Message" }

function Start-Task14CanvasFailureEngine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task14CanvasFailure 'obs-engine.exe was not found.' }
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName
    $info.Arguments = '--test-fail-next-canvas-reset'
    $info.WorkingDirectory = $engine.Directory.FullName
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Process = [System.Diagnostics.Process]::new()
    $script:Process.StartInfo = $info
    if (-not $script:Process.Start()) { Fail-Task14CanvasFailure 'failed to start test-hook engine.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task14CanvasFailureEngine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task14CanvasFailure 'fallback Canvas warning was emitted.' }
    if ($script:Process.ExitCode -ne 0) { Fail-Task14CanvasFailure "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task14CanvasFailureMessage {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task14CanvasFailure 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task14CanvasFailure 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task14CanvasFailureRequest([hashtable] $Request) {
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task14CanvasFailureMessage
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task14CanvasFailure "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Send-Task14CanvasFailureGuarded([string] $Id, [string] $Method, $Params) {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $requestId = if ($attempt -eq 0) { $Id } else { "$Id-retry$attempt" }
        $request = @{ op = 'request'; id = $requestId; method = $Method; ifRevision = $script:Revision }
        if ($null -ne $Params) { $request.params = $Params }
        $response = Send-Task14CanvasFailureRequest $request
        if ($response.status.ok -or [string]$response.status.code -ne 'revision_conflict') { return $response }
        $script:Revision = [int64]$response.revision
    }
    return $response
}

function Assert-Task14CanvasFailureOk($Response, [int64] $ExpectedRevision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $ExpectedRevision) { Fail-Task14CanvasFailure "$Label failed at revision $($Response.revision)." }
}

function Assert-Task14CanvasFailureError($Response, [string] $Code, [int64] $ExpectedRevision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $ExpectedRevision) { Fail-Task14CanvasFailure "$Label did not return $Code at revision $ExpectedRevision." }
}

function Assert-Task14CanvasFailureReadOk($Response, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -lt $script:Revision) { Fail-Task14CanvasFailure "$Label returned an invalid revision $($Response.revision)." }
    $script:Revision = [int64]$Response.revision
}

function Read-Task14CanvasFailureEvent([string] $Name, [int64] $Revision) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task14CanvasFailureMessage }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) { Fail-Task14CanvasFailure "unexpected event; expected $Name at revision $Revision." }
    $script:NextSequence++
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task14CanvasFailure "event $Name preceded its response." }
    return $event
}

function Read-Task14CanvasFailureEventsThrough([string] $FinalName, [int64] $Revision) {
    $names = [System.Collections.Generic.List[string]]::new()
    while ($true) {
        if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task14CanvasFailureMessage }
        if ($event.op -ne 'event' -or [int64]$event.revision -ne $Revision -or [uint64]$event.seq -ne $script:NextSequence) { Fail-Task14CanvasFailure "unexpected event while waiting for $FinalName." }
        $script:NextSequence++
        $names.Add([string]$event.event)
        if ([string]$event.event -eq $FinalName) { return $names }
    }
}

function Assert-Task14CanvasFailureNoEvents([string[]] $Names) {
    foreach ($event in $script:Events) {
        if ([string]$event.event -in $Names) { Fail-Task14CanvasFailure "unexpected event $($event.event) was emitted." }
    }
}

function Start-Task14CanvasFailureConsumer($Descriptor, [int] $Frames) {
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = (Resolve-Path -LiteralPath $ConsumerPath).Path
    $info.Arguments = "--shared-handle=$($Descriptor.sharedTexture.handle) --width=$($Descriptor.width) --height=$($Descriptor.height) --adapter-luid=$($Descriptor.adapterLuid) --frames=$Frames --timeout-ms=250"
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { Fail-Task14CanvasFailure 'failed to start D3D11 consumer.' }
    return [pscustomobject]@{ Process = $process; Output = $process.StandardOutput.ReadToEndAsync(); Error = $process.StandardError.ReadToEndAsync() }
}

function Read-Task14CanvasFailureConsumer($Runner, [string] $Label) {
    if (-not $Runner.Process.WaitForExit(30000)) { $Runner.Process.Kill(); $Runner.Process.WaitForExit(); Fail-Task14CanvasFailure "$Label consumer timed out." }
    $stdout = $Runner.Output.GetAwaiter().GetResult().Trim()
    $stderr = $Runner.Error.GetAwaiter().GetResult()
    if ($Runner.Process.ExitCode -ne 0) { Fail-Task14CanvasFailure "$Label consumer failed: $stdout $stderr" }
    $evidence = $stdout | ConvertFrom-Json
    if ([int]$evidence.timeouts -ge [int]$evidence.frames -or [int]$evidence.lastCenterG -lt 200) { Fail-Task14CanvasFailure "$Label did not retain the routed Canvas pixels." }
    return $evidence
}

function Assert-Task14CanvasFailureLeaseShape($Info, [string] $Label) {
    if ($Info.data.PSObject.Properties['consumerAttached']) { Fail-Task14CanvasFailure "$Label exposed ephemeral consumerAttached state." }
}

function Invoke-Task14CanvasFailureScenario {
    Start-Task14CanvasFailureEngine $InstallRoot
    $ready = Read-Task14CanvasFailureMessage
    if ($ready.event -ne 'ready') { Fail-Task14CanvasFailure 'ready marker was not received.' }
    $hello = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-hello'; method = 'session.hello' }
    Assert-Task14CanvasFailureOk $hello 0 'hello'
    $subscribe = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-subscribe'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'canvas.*' }, @{ pattern = 'scene.*' }, @{ pattern = 'item.*' }, @{ pattern = 'previewOutput.*' }) } }
    Assert-Task14CanvasFailureOk $subscribe 0 'subscribe'

    $canvas = Send-Task14CanvasFailureGuarded 'cf-canvas' 'canvas.create' @{ name = 'Failure Canvas'; videoSettings = @{ width = 320; height = 180; format = 'bgra'; colorSpace = 'srgb'; range = 'full'; scaleType = 'bilinear'; fpsNumerator = 30; fpsDenominator = 1 } }
    Assert-Task14CanvasFailureOk $canvas ($script:Revision + 1) 'Canvas create'
    $script:Revision = [int64]$canvas.revision
    Read-Task14CanvasFailureEvent 'canvas.created' $script:Revision | Out-Null
    $canvasHandle = [string]$canvas.data.canvas
    $scene = Send-Task14CanvasFailureGuarded 'cf-scene' 'scene.create' @{ name = 'Failure Scene'; canvas = $canvasHandle }
    Assert-Task14CanvasFailureOk $scene ($script:Revision + 1) 'Scene create'
    $script:Revision = [int64]$scene.revision
    Read-Task14CanvasFailureEvent 'scene.created' $script:Revision | Out-Null
    $source = Send-Task14CanvasFailureGuarded 'cf-source' 'source.create' @{ kind = 'color_source_v3'; name = 'Failure Green'; settings = @{ width = 320; height = 180; color = 4278255360 } }
    Assert-Task14CanvasFailureOk $source ($script:Revision + 1) 'Source create'
    $script:Revision = [int64]$source.revision
    $item = Send-Task14CanvasFailureGuarded 'cf-item' 'item.create' @{ scene = [string]$scene.data.scene; source = [string]$source.data.source }
    Assert-Task14CanvasFailureOk $item ($script:Revision + 1) 'Item create'
    $script:Revision = [int64]$item.revision
    Read-Task14CanvasFailureEvent 'item.created' $script:Revision | Out-Null
    $route = Send-Task14CanvasFailureGuarded 'cf-route' 'canvas.setChannel' @{ canvas = $canvasHandle; channel = 0; target = @{ type = 'scene'; scene = [string]$scene.data.scene } }
    Assert-Task14CanvasFailureOk $route ($script:Revision + 1) 'Canvas route'
    $script:Revision = [int64]$route.revision
    Read-Task14CanvasFailureEvent 'canvas.channelChanged' $script:Revision | Out-Null
    $output = Send-Task14CanvasFailureGuarded 'cf-output' 'previewOutput.create' @{ target = @{ type = 'canvas'; canvas = $canvasHandle }; width = 160; height = 90; enabled = $true; scale = 'fit' }
    Assert-Task14CanvasFailureOk $output ($script:Revision + 1) 'PreviewOutput create'
    $script:Revision = [int64]$output.revision
    Read-Task14CanvasFailureEvent 'previewOutput.created' $script:Revision | Out-Null
    $outputHandle = [string]$output.data.previewOutput
    $beforeInfo = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-info-before'; method = 'previewOutput.getInfo'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $beforeInfo 'PreviewOutput initial info'
    Assert-Task14CanvasFailureLeaseShape $beforeInfo 'initial PreviewOutput'
    $beforeGeneration = [string]$beforeInfo.data.resourceGeneration

    $beforeDescriptor = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-shared-before'; method = 'previewOutput.getSharedTexture'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $beforeDescriptor 'initial shared texture'
    Assert-Task14CanvasFailureLeaseShape $beforeDescriptor 'initial shared texture'
    $beforeRunner = Start-Task14CanvasFailureConsumer $beforeDescriptor.data 12
    Read-Task14CanvasFailureConsumer $beforeRunner 'initial Canvas route' | Out-Null
    $beforeRelease = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-release-before'; method = 'previewOutput.releaseSharedTexture'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $beforeRelease 'initial shared-texture release'

    $failedReset = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-reset-fail'; method = 'canvas.setVideoSettings'; ifRevision = $script:Revision; params = @{ canvas = $canvasHandle; videoSettings = @{ width = 400; height = 200 } } }
    Assert-Task14CanvasFailureError $failedReset 'obs_error' $script:Revision 'injected Canvas reset failure'
    Assert-Task14CanvasFailureNoEvents @('canvas.videoSettingsChanged', 'previewOutput.resourceChanged')
    $afterFailureCanvas = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-canvas-after-failure'; method = 'canvas.getVideoSettings'; params = @{ canvas = $canvasHandle } }
    Assert-Task14CanvasFailureReadOk $afterFailureCanvas 'Canvas after failed reset'
    if ([int]$afterFailureCanvas.data.width -ne 320 -or [int]$afterFailureCanvas.data.height -ne 180) { Fail-Task14CanvasFailure 'failed reset changed the Canvas video settings.' }
    $afterFailureInfo = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-info-after-failure'; method = 'previewOutput.getInfo'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $afterFailureInfo 'PreviewOutput after failed reset'
    Assert-Task14CanvasFailureLeaseShape $afterFailureInfo 'failed-reset PreviewOutput'
    if ([string]$afterFailureInfo.data.resourceGeneration -ne $beforeGeneration -or -not $afterFailureInfo.data.targetAvailable) { Fail-Task14CanvasFailure 'failed reset invalidated or replaced the PreviewOutput resource.' }
    $afterFailureDescriptor = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-shared-after-failure'; method = 'previewOutput.getSharedTexture'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $afterFailureDescriptor 'shared texture after failed reset'
    Assert-Task14CanvasFailureLeaseShape $afterFailureDescriptor 'failed-reset shared texture'
    if ([string]$afterFailureDescriptor.data.sharedTexture.handle -ne [string]$beforeDescriptor.data.sharedTexture.handle) { Fail-Task14CanvasFailure 'failed reset replaced the existing PreviewOutput share descriptor.' }
    $afterFailureRunner = Start-Task14CanvasFailureConsumer $afterFailureDescriptor.data 12
    Read-Task14CanvasFailureConsumer $afterFailureRunner 'Canvas route after failed reset' | Out-Null
    $afterFailureRelease = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-release-after-failure'; method = 'previewOutput.releaseSharedTexture'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $afterFailureRelease 'failed-reset shared-texture release'
    Write-Output 'Canvas reset injected failure: atomic/no event/no generation change PASS'

    $successfulReset = Send-Task14CanvasFailureGuarded 'cf-reset-success' 'canvas.setVideoSettings' @{ canvas = $canvasHandle; videoSettings = @{ width = 400; height = 200 } }
    Assert-Task14CanvasFailureOk $successfulReset ($script:Revision + 1) 'successful Canvas reset retry'
    $script:Revision = [int64]$successfulReset.revision
    Read-Task14CanvasFailureEvent 'canvas.videoSettingsChanged' $script:Revision | Out-Null
    $resourceChanged = Read-Task14CanvasFailureEvent 'previewOutput.resourceChanged' $script:Revision
    if ([uint64]$resourceChanged.data.resourceGeneration -ne ([uint64]$beforeGeneration + 1)) { Fail-Task14CanvasFailure 'successful reset did not increment PreviewOutput generation exactly once.' }
    $afterSuccessInfo = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-info-after-success'; method = 'previewOutput.getInfo'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $afterSuccessInfo 'PreviewOutput after successful reset'
    Assert-Task14CanvasFailureLeaseShape $afterSuccessInfo 'successful-reset PreviewOutput'
    if ([string]$afterSuccessInfo.data.resourceGeneration -ne [string]$resourceChanged.data.resourceGeneration -or -not $afterSuccessInfo.data.targetAvailable) { Fail-Task14CanvasFailure 'successful reset generation/readback disagreed.' }
    Assert-Task14CanvasFailureNoEvents @('canvas.videoSettingsChanged', 'previewOutput.resourceChanged')
    $afterSuccessDescriptor = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-shared-after-success'; method = 'previewOutput.getSharedTexture'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $afterSuccessDescriptor 'shared texture after successful reset'
    Assert-Task14CanvasFailureLeaseShape $afterSuccessDescriptor 'successful-reset shared texture'
    $afterSuccessRunner = Start-Task14CanvasFailureConsumer $afterSuccessDescriptor.data 12
    Read-Task14CanvasFailureConsumer $afterSuccessRunner 'Canvas route after successful reset' | Out-Null
    $afterSuccessRelease = Send-Task14CanvasFailureRequest @{ op = 'request'; id = 'cf-release-after-success'; method = 'previewOutput.releaseSharedTexture'; params = @{ previewOutput = $outputHandle } }
    Assert-Task14CanvasFailureReadOk $afterSuccessRelease 'successful-reset shared-texture release'
    Write-Output 'Canvas reset successful retry: one revision/one event/working replacement PASS'

    $clear = Send-Task14CanvasFailureGuarded 'cf-clear-route' 'canvas.setChannel' @{ canvas = $canvasHandle; channel = 0; target = $null }
    Assert-Task14CanvasFailureOk $clear ($script:Revision + 1) 'clear Canvas route'
    $script:Revision = [int64]$clear.revision
    Read-Task14CanvasFailureEvent 'canvas.channelChanged' $script:Revision | Out-Null
    $destroy = Send-Task14CanvasFailureGuarded 'cf-destroy-output' 'previewOutput.destroy' @{ previewOutput = $outputHandle }
    Assert-Task14CanvasFailureOk $destroy ($script:Revision + 1) 'destroy PreviewOutput'
    $script:Revision = [int64]$destroy.revision
    Read-Task14CanvasFailureEvent 'previewOutput.destroyed' $script:Revision | Out-Null
    $removeScene = Send-Task14CanvasFailureGuarded 'cf-remove-scene' 'scene.remove' @{ scene = [string]$scene.data.scene }
    Assert-Task14CanvasFailureOk $removeScene ($script:Revision + 1) 'remove Scene'
    $script:Revision = [int64]$removeScene.revision
    Read-Task14CanvasFailureEventsThrough 'scene.removed' $script:Revision | Out-Null
    $removeSource = Send-Task14CanvasFailureGuarded 'cf-remove-source' 'source.remove' @{ source = [string]$source.data.source }
    Assert-Task14CanvasFailureOk $removeSource ($script:Revision + 1) 'remove Source'
    $script:Revision = [int64]$removeSource.revision
    $removeCanvas = Send-Task14CanvasFailureGuarded 'cf-remove-canvas' 'canvas.remove' @{ canvas = $canvasHandle }
    Assert-Task14CanvasFailureOk $removeCanvas ($script:Revision + 1) 'remove Canvas'
    $script:Revision = [int64]$removeCanvas.revision
    Read-Task14CanvasFailureEvent 'canvas.removed' $script:Revision | Out-Null
    $close = Send-Task14CanvasFailureGuarded 'cf-close' 'session.close' $null
    Assert-Task14CanvasFailureOk $close ($script:Revision + 1) 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task14CanvasFailureEngine
    Write-Output 'Task 14 Canvas failure-atomicity integration: PASS'
}

try {
    Invoke-Task14CanvasFailureScenario
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:ErrorTask) { Write-Host ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

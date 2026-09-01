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

function Fail-Task20([string] $Message) { throw "Task 20: $Message" }

function Start-Task20Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task20 'obs-engine.exe was not found.' }
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
    if (-not $script:Process.Start()) { Fail-Task20 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task20Engine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task20 'fallback canvas warning was emitted.' }
    if ($script:Process.ExitCode -ne 0) { Fail-Task20 "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task20Message {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task20 'timed out waiting for stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task20 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task20Request([hashtable] $Request) {
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task20Message
        $script:LastMessage = $message
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task20 "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Send-Task20Guarded([string] $Id, [string] $Method, [hashtable] $Params, [int64] $Revision) {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $attemptId = if ($attempt -eq 0) { $Id } else { "$Id-retry$attempt" }
        $request = @{ op = 'request'; id = $attemptId; method = $Method; ifRevision = $Revision }
        if ($null -ne $Params) { $request.params = $Params }
        $response = Send-Task20Request $request
        if ($response.status.ok -or [string]$response.status.code -ne 'revision_conflict') {
            $response | Add-Member -NotePropertyName GuardRevision -NotePropertyValue $Revision -Force
            return $response
        }
        $Revision = [int64]$response.revision
    }
    return $response
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task20 "$Label failed at revision $($Response.revision)." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task20 "$Label did not return $Code at revision $Revision." }
}

function Read-Task20Event([string] $Name, [int64] $Revision, [bool] $AllowBeforeResponse = $false) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task20Message }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) { Fail-Task20 "unexpected event; expected $Name at revision $Revision." }
    $script:NextSequence++
    if (-not $AllowBeforeResponse) {
        $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
        if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task20 "event $Name preceded its response." }
    }
    return $event
}

function Read-Task20EventsThrough([string] $FinalName, [int64] $Revision, [System.Collections.Generic.List[string]] $Seen) {
    while ($true) {
        if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task20Message }
        if ($event.op -ne 'event' -or [int64]$event.revision -ne $Revision -or [uint64]$event.seq -ne $script:NextSequence) { Fail-Task20 "unexpected event while waiting for $FinalName." }
        $script:NextSequence++
        $Seen.Add([string]$event.event)
        if ([string]$event.event -eq $FinalName) { return }
    }
}

try {
    Start-Task20Engine $InstallRoot
    $ready = Read-Task20Message
    if ($ready.event -ne 'ready') { Fail-Task20 'ready marker was not received.' }
    $hello = Send-Task20Request @{ op = 'request'; id = 'o-hello'; method = 'session.hello' }
    Assert-Ok $hello 0 'hello'
    $requiredCapabilities = @('previewOutput.v1', 'previewOutput.list.v1', 'previewOutput.get.v1', 'previewOutput.setTarget.v1')
    foreach ($capability in $requiredCapabilities) {
        if (@($hello.data.capabilities | Where-Object { $_.name -eq $capability }).Count -eq 0) { Fail-Task20 "capability $capability was not advertised." }
    }
    Assert-Ok (Send-Task20Request @{ op = 'request'; id = 'o-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'canvas.*' }, @{ pattern = 'scene.*' }, @{ pattern = 'item.*' }, @{ pattern = 'source.*' }, @{ pattern = 'filter.*' }, @{ pattern = 'previewOutput.*' }) } }) 0 'subscribe'

    $canvasOne = Send-Task20Request @{ op = 'request'; id = 'o-canvas-one'; method = 'canvas.create'; params = @{ name = 'Task20 Canvas One'; videoSettings = @{ width = 640; height = 360; format = 'bgra'; colorSpace = 'srgb'; range = 'full'; scaleType = 'bilinear'; fpsNumerator = 30; fpsDenominator = 1 } } }
    Assert-Ok $canvasOne 1 'Canvas One'
    Read-Task20Event 'canvas.created' 1 | Out-Null
    $revision = 1
    $canvasTwo = Send-Task20Guarded 'o-canvas-two' 'canvas.create' @{ name = 'Task20 Canvas Two'; videoSettings = @{ width = 320; height = 180; format = 'bgra'; colorSpace = 'srgb'; range = 'full'; scaleType = 'bilinear'; fpsNumerator = 30; fpsDenominator = 1 } } $revision
    Assert-Ok $canvasTwo ($canvasTwo.GuardRevision + 1) 'Canvas Two'
    $revision = [int64]$canvasTwo.revision
    Read-Task20Event 'canvas.created' $revision | Out-Null

    $mainScene = Send-Task20Guarded 'o-main-scene' 'scene.create' @{ name = 'Task20 Main Scene' } $revision
    Assert-Ok $mainScene ($mainScene.GuardRevision + 1) 'main Scene'
    $revision = [int64]$mainScene.revision
    Read-Task20Event 'scene.created' $revision | Out-Null
    $targetScene = Send-Task20Guarded 'o-target-scene' 'scene.create' @{ name = 'Task20 Target Scene' } $revision
    Assert-Ok $targetScene ($targetScene.GuardRevision + 1) 'target Scene'
    $revision = [int64]$targetScene.revision
    Read-Task20Event 'scene.created' $revision | Out-Null
    $privateScene = Send-Task20Guarded 'o-private-scene' 'scene.create' @{ name = 'Task20 Private Scene'; canvas = [string]$canvasTwo.data.canvas } $revision
    Assert-Ok $privateScene ($privateScene.GuardRevision + 1) 'private Scene'
    $revision = [int64]$privateScene.revision
    Read-Task20Event 'scene.created' $revision | Out-Null

    $source = Send-Task20Guarded 'o-source' 'source.create' @{ kind = 'color_source_v3'; name = 'Task20 Filtered Color'; settings = @{ width = 640; height = 360; color = 4278190335 } } $revision
    Assert-Ok $source ($source.GuardRevision + 1) 'color source'
    $revision = [int64]$source.revision
    Read-Task20Event 'source.created' $revision | Out-Null
    $item = Send-Task20Guarded 'o-item' 'item.create' @{ scene = [string]$mainScene.data.scene; source = [string]$source.data.source } $revision
    Assert-Ok $item ($item.GuardRevision + 1) 'scene item'
    $revision = [int64]$item.revision
    Read-Task20Event 'item.created' $revision | Out-Null
    $transform = Send-Task20Guarded 'o-transform' 'item.setTransform' @{ item = [string]$item.data.item; transform = @{ position = @{ x = 20.0; y = 30.0 }; scale = @{ x = 1.5; y = 1.25 }; rotation = 15.0; alignment = 5; bounds = @{ type = 'none' }; crop = @{ left = 0; top = 0; right = 0; bottom = 0 }; cropToBounds = $false } } $revision
    Assert-Ok $transform ($transform.GuardRevision + 1) 'transformed scene item'
    $revision = [int64]$transform.revision
    Read-Task20Event 'item.transformChanged' $revision | Out-Null

    $filterKinds = Send-Task20Request @{ op = 'request'; id = 'o-filter-kinds'; method = 'filter.kindList' }
    Assert-Ok $filterKinds $revision 'filter kind list'
    $filterKindEntry = @($filterKinds.data.kinds | Where-Object { $_.id -eq 'color_filter' }) | Select-Object -First 1
    if ($null -eq $filterKindEntry) { $filterKindEntry = $filterKinds.data.kinds[0] }
    $filterKind = [string]$filterKindEntry.id
    $filterDefaults = Send-Task20Request @{ op = 'request'; id = 'o-filter-defaults'; method = 'filter.kindDefaults'; params = @{ kind = $filterKind } }
    Assert-Ok $filterDefaults $revision 'filter defaults'
    $filter = Send-Task20Guarded 'o-filter' 'filter.create' @{ source = [string]$source.data.source; kind = $filterKind; name = 'Task20 Filter'; settings = $filterDefaults.data.settings } $revision
    Assert-Ok $filter ($filter.GuardRevision + 1) 'source filter'
    $revision = [int64]$filter.revision
    Read-Task20Event 'filter.created' $revision | Out-Null

    $programOutput = Send-Task20Guarded 'o-output-program' 'previewOutput.create' @{ target = @{ type = 'program' }; width = 160; height = 90; enabled = $false } $revision
    Assert-Ok $programOutput ($programOutput.GuardRevision + 1) 'Program output'
    $revision = [int64]$programOutput.revision
    Read-Task20Event 'previewOutput.created' $revision | Out-Null
    $previewOutput = Send-Task20Guarded 'o-output-preview' 'previewOutput.create' @{ target = @{ type = 'preview' }; width = 160; height = 90; enabled = $false } $revision
    Assert-Ok $previewOutput ($previewOutput.GuardRevision + 1) 'Preview output'
    $revision = [int64]$previewOutput.revision
    Read-Task20Event 'previewOutput.created' $revision | Out-Null
    $sceneOutput = Send-Task20Guarded 'o-output-scene' 'previewOutput.create' @{ target = @{ type = 'scene'; scene = [string]$mainScene.data.scene }; width = 160; height = 90; enabled = $false; scale = 'fit' } $revision
    Assert-Ok $sceneOutput ($sceneOutput.GuardRevision + 1) 'Scene output'
    $revision = [int64]$sceneOutput.revision
    Read-Task20Event 'previewOutput.created' $revision | Out-Null
    $sourceOutput = Send-Task20Guarded 'o-output-source' 'previewOutput.create' @{ target = @{ type = 'source'; source = [string]$source.data.source }; width = 160; height = 90; enabled = $false; scale = 'fill' } $revision
    Assert-Ok $sourceOutput ($sourceOutput.GuardRevision + 1) 'Source output'
    $revision = [int64]$sourceOutput.revision
    Read-Task20Event 'previewOutput.created' $revision | Out-Null
    $canvasOutput = Send-Task20Guarded 'o-output-canvas' 'previewOutput.create' @{ target = @{ type = 'canvas'; canvas = [string]$canvasTwo.data.canvas }; width = 160; height = 90; enabled = $false; scale = 'oneToOne' } $revision
    Assert-Ok $canvasOutput ($canvasOutput.GuardRevision + 1) 'Canvas output'
    $revision = [int64]$canvasOutput.revision
    Read-Task20Event 'previewOutput.created' $revision | Out-Null

    $list = Send-Task20Request @{ op = 'request'; id = 'o-list'; method = 'previewOutput.list' }
    Assert-Ok $list $revision 'previewOutput.list'
    if ([int]$list.data.count -ne 5) { Fail-Task20 'previewOutput.list did not enumerate all five target types.' }
    foreach ($output in @($programOutput, $previewOutput, $sceneOutput, $sourceOutput, $canvasOutput)) {
        $got = Send-Task20Request @{ op = 'request'; id = "o-get-$($output.data.previewOutput)"; method = 'previewOutput.get'; params = @{ previewOutput = [string]$output.data.previewOutput } }
        Assert-Ok $got $revision 'previewOutput.get'
        if (-not $got.data.hasSharedTexture -or [int]$got.data.width -ne 160 -or [int]$got.data.height -ne 90) { Fail-Task20 'previewOutput.get returned incomplete resource metadata.' }
    }

    $programHandle = [string]$programOutput.data.previewOutput
    $retargetSource = Send-Task20Guarded 'o-retarget-source' 'previewOutput.setTarget' @{ previewOutput = $programHandle; target = @{ type = 'source'; source = [string]$source.data.source }; scale = 'stretch' } $revision
    Assert-Ok $retargetSource ($retargetSource.GuardRevision + 1) 'retarget to Source'
    $revision = [int64]$retargetSource.revision
    Read-Task20Event 'previewOutput.targetChanged' $revision | Out-Null
    $retargetScene = Send-Task20Guarded 'o-retarget-scene' 'previewOutput.setTarget' @{ previewOutput = $programHandle; target = @{ type = 'scene'; scene = [string]$mainScene.data.scene }; scale = 'fill' } $revision
    Assert-Ok $retargetScene ($retargetScene.GuardRevision + 1) 'retarget to Scene'
    $revision = [int64]$retargetScene.revision
    Read-Task20Event 'previewOutput.targetChanged' $revision | Out-Null
    $retargetCanvas = Send-Task20Guarded 'o-retarget-canvas' 'previewOutput.setTarget' @{ previewOutput = $programHandle; target = @{ type = 'canvas'; canvas = [string]$canvasTwo.data.canvas }; scale = 'oneToOne' } $revision
    Assert-Ok $retargetCanvas ($retargetCanvas.GuardRevision + 1) 'retarget to Canvas'
    $revision = [int64]$retargetCanvas.revision
    Read-Task20Event 'previewOutput.targetChanged' $revision | Out-Null
    $retargetInfo = Send-Task20Request @{ op = 'request'; id = 'o-retarget-info'; method = 'previewOutput.getInfo'; params = @{ previewOutput = $programHandle } }
    Assert-Ok $retargetInfo $revision 'retarget info'
    if ([string]$retargetInfo.data.target.type -ne 'canvas' -or [string]$retargetInfo.data.scale -ne 'oneToOne') { Fail-Task20 'rapid retargeting or semantic scale readback was incorrect.' }

    $canvasReset = Send-Task20Guarded 'o-canvas-reset' 'canvas.setVideoSettings' @{ canvas = [string]$canvasTwo.data.canvas; videoSettings = @{ width = 400; height = 200 } } $revision
    Assert-Ok $canvasReset ($canvasReset.GuardRevision + 1) 'Canvas video reset with output'
    $revision = [int64]$canvasReset.revision
    Read-Task20Event 'canvas.videoSettingsChanged' $revision | Out-Null
    $resourceReset = Read-Task20Event 'previewOutput.resourceChanged' $revision
    $resourceResetSecond = Read-Task20Event 'previewOutput.resourceChanged' $revision
    if ([string]$resourceReset.data.resourceGeneration -eq [string]$canvasOutput.data.resourceGeneration -or [string]$resourceResetSecond.data.resourceGeneration -eq [string]$canvasOutput.data.resourceGeneration) { Fail-Task20 'Canvas video reset did not bump PreviewOutput resource generation.' }

    $removeSource = Send-Task20Guarded 'o-remove-source' 'source.remove' @{ source = [string]$source.data.source } $revision
    Assert-Ok $removeSource ($removeSource.GuardRevision + 1) 'remove Source target'
    $revision = [int64]$removeSource.revision
    $sourceRemovalEvents = [System.Collections.Generic.List[string]]::new()
    Read-Task20EventsThrough 'source.removed' $revision $sourceRemovalEvents
    if (-not ($sourceRemovalEvents -contains 'previewOutput.targetChanged')) { Fail-Task20 'Source target removal did not invalidate its PreviewOutput.' }
    $sourceInfo = Send-Task20Request @{ op = 'request'; id = 'o-source-invalidated'; method = 'previewOutput.getInfo'; params = @{ previewOutput = [string]$sourceOutput.data.previewOutput } }
    Assert-Ok $sourceInfo $revision 'invalidated Source output info'
    if ($sourceInfo.data.targetAvailable) { Fail-Task20 'Source-targeted PreviewOutput remained available after source removal.' }

    $removeMainScene = Send-Task20Guarded 'o-remove-scene' 'scene.remove' @{ scene = [string]$mainScene.data.scene } $revision
    Assert-Ok $removeMainScene ($removeMainScene.GuardRevision + 1) 'remove Scene target'
    $revision = [int64]$removeMainScene.revision
    $sceneRemovalEvents = [System.Collections.Generic.List[string]]::new()
    Read-Task20EventsThrough 'scene.removed' $revision $sceneRemovalEvents
    if (-not ($sceneRemovalEvents -contains 'previewOutput.targetChanged')) { Fail-Task20 'Scene target removal did not invalidate its PreviewOutput.' }

    $removePrivateScene = Send-Task20Guarded 'o-remove-private-scene' 'scene.remove' @{ scene = [string]$privateScene.data.scene } $revision
    Assert-Ok $removePrivateScene ($removePrivateScene.GuardRevision + 1) 'remove private Scene'
    $revision = [int64]$removePrivateScene.revision
    Read-Task20Event 'scene.removed' $revision | Out-Null
    $removeCanvas = Send-Task20Guarded 'o-remove-canvas' 'canvas.remove' @{ canvas = [string]$canvasTwo.data.canvas } $revision
    Assert-Ok $removeCanvas ($removeCanvas.GuardRevision + 1) 'remove Canvas target'
    $revision = [int64]$removeCanvas.revision
    Read-Task20Event 'previewOutput.targetChanged' $revision | Out-Null
    Read-Task20Event 'previewOutput.targetChanged' $revision | Out-Null
    Read-Task20Event 'canvas.removed' $revision | Out-Null
    $canvasInfo = Send-Task20Request @{ op = 'request'; id = 'o-canvas-invalidated'; method = 'previewOutput.getInfo'; params = @{ previewOutput = [string]$canvasOutput.data.previewOutput } }
    Assert-Ok $canvasInfo $revision 'invalidated Canvas output info'
    if ($canvasInfo.data.targetAvailable) { Fail-Task20 'Canvas-targeted PreviewOutput remained available after Canvas removal.' }

    $rebindPreview = Send-Task20Guarded 'o-rebind-preview' 'previewOutput.setTarget' @{ previewOutput = $programHandle; target = @{ type = 'preview' }; scale = 'fit' } $revision
    Assert-Ok $rebindPreview ($rebindPreview.GuardRevision + 1) 'rebind after target disappearance'
    $revision = [int64]$rebindPreview.revision
    Read-Task20Event 'previewOutput.targetChanged' $revision | Out-Null
    $remaining = Send-Task20Request @{ op = 'request'; id = 'o-list-final'; method = 'previewOutput.list' }
    Assert-Ok $remaining $revision 'final previewOutput.list'
    if ([int]$remaining.data.count -ne 5) { Fail-Task20 'PreviewOutput list lost a live output during target invalidation.' }

    foreach ($output in @($programOutput, $previewOutput, $sceneOutput, $sourceOutput, $canvasOutput)) {
        $destroy = Send-Task20Guarded "o-destroy-$($output.data.previewOutput)" 'previewOutput.destroy' @{ previewOutput = [string]$output.data.previewOutput } $revision
        Assert-Ok $destroy ($destroy.GuardRevision + 1) 'destroy PreviewOutput'
        $revision = [int64]$destroy.revision
        Read-Task20Event 'previewOutput.destroyed' $revision | Out-Null
    }

    $close = Send-Task20Guarded 'o-close' 'session.close' $null $revision
    Assert-Ok $close ($close.GuardRevision + 1) 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task20Engine
    Write-Output 'Task 20 previewOutput integration: PASS'
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:ErrorTask) { Write-Host ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

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

function Invoke-Task20Bootstrap {
    Start-Task20Engine $InstallRoot
    $ready = Read-Task20Message
    if ($ready.event -ne 'ready') { Fail-Task20 'ready marker was not received.' }
    $script:T20Hello = Send-Task20Request @{ op = 'request'; id = 'o-hello'; method = 'session.hello' }
    Assert-Ok $script:T20Hello 0 'hello'
    $requiredCapabilities = @('previewOutput.v1', 'previewOutput.list.v1', 'previewOutput.get.v1', 'previewOutput.setTarget.v1')
    foreach ($capability in $requiredCapabilities) {
        if (@($script:T20Hello.data.capabilities | Where-Object { $_.name -eq $capability }).Count -eq 0) { Fail-Task20 "capability $capability was not advertised." }
    }
    Assert-Ok (Send-Task20Request @{ op = 'request'; id = 'o-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'canvas.*' }, @{ pattern = 'scene.*' }, @{ pattern = 'item.*' }, @{ pattern = 'source.*' }, @{ pattern = 'filter.*' }, @{ pattern = 'previewOutput.*' }) } }) 0 'subscribe'
}

function Invoke-Task20GraphSetup {
    $canvasOne = Send-Task20Request @{ op = 'request'; id = 'o-canvas-one'; method = 'canvas.create'; params = @{ name = 'Task20 Canvas One'; videoSettings = @{ width = 640; height = 360; format = 'bgra'; colorSpace = 'srgb'; range = 'full'; scaleType = 'bilinear'; fpsNumerator = 30; fpsDenominator = 1 } } }
    Assert-Ok $canvasOne 1 'Canvas One'
    Read-Task20Event 'canvas.created' 1 | Out-Null
    $script:T20Revision = 1
    $script:T20CanvasTwo = Send-Task20Guarded 'o-canvas-two' 'canvas.create' @{ name = 'Task20 Canvas Two'; videoSettings = @{ width = 320; height = 180; format = 'bgra'; colorSpace = 'srgb'; range = 'full'; scaleType = 'bilinear'; fpsNumerator = 30; fpsDenominator = 1 } } $script:T20Revision
    Assert-Ok $script:T20CanvasTwo ($script:T20CanvasTwo.GuardRevision + 1) 'Canvas Two'
    $script:T20Revision = [int64]$script:T20CanvasTwo.revision
    Read-Task20Event 'canvas.created' $script:T20Revision | Out-Null
    $script:T20MainScene = Send-Task20Guarded 'o-main-scene' 'scene.create' @{ name = 'Task20 Main Scene' } $script:T20Revision
    Assert-Ok $script:T20MainScene ($script:T20MainScene.GuardRevision + 1) 'main Scene'
    $script:T20Revision = [int64]$script:T20MainScene.revision
    Read-Task20Event 'scene.created' $script:T20Revision | Out-Null
    $script:T20TargetScene = Send-Task20Guarded 'o-target-scene' 'scene.create' @{ name = 'Task20 Target Scene' } $script:T20Revision
    Assert-Ok $script:T20TargetScene ($script:T20TargetScene.GuardRevision + 1) 'target Scene'
    $script:T20Revision = [int64]$script:T20TargetScene.revision
    Read-Task20Event 'scene.created' $script:T20Revision | Out-Null
    $script:T20PrivateScene = Send-Task20Guarded 'o-private-scene' 'scene.create' @{ name = 'Task20 Private Scene'; canvas = [string]$script:T20CanvasTwo.data.canvas } $script:T20Revision
    Assert-Ok $script:T20PrivateScene ($script:T20PrivateScene.GuardRevision + 1) 'private Scene'
    $script:T20Revision = [int64]$script:T20PrivateScene.revision
    Read-Task20Event 'scene.created' $script:T20Revision | Out-Null
}

function Invoke-Task20SourceSetup {
    $script:T20Source = Send-Task20Guarded 'o-source' 'source.create' @{ kind = 'color_source_v3'; name = 'Task20 Filtered Color'; settings = @{ width = 640; height = 360; color = 4278190335 } } $script:T20Revision
    Assert-Ok $script:T20Source ($script:T20Source.GuardRevision + 1) 'color source'
    $script:T20Revision = [int64]$script:T20Source.revision
    Read-Task20Event 'source.created' $script:T20Revision | Out-Null
    $script:T20Item = Send-Task20Guarded 'o-item' 'item.create' @{ scene = [string]$script:T20MainScene.data.scene; source = [string]$script:T20Source.data.source } $script:T20Revision
    Assert-Ok $script:T20Item ($script:T20Item.GuardRevision + 1) 'scene item'
    $script:T20Revision = [int64]$script:T20Item.revision
    Read-Task20Event 'item.created' $script:T20Revision | Out-Null
    $transform = Send-Task20Guarded 'o-transform' 'item.setTransform' @{ item = [string]$script:T20Item.data.item; transform = @{ position = @{ x = 20.0; y = 30.0 }; scale = @{ x = 1.5; y = 1.25 }; rotation = 15.0; alignment = 5; bounds = @{ type = 'none' }; crop = @{ left = 0; top = 0; right = 0; bottom = 0 }; cropToBounds = $false } } $script:T20Revision
    Assert-Ok $transform ($transform.GuardRevision + 1) 'transformed scene item'
    $script:T20Revision = [int64]$transform.revision
    Read-Task20Event 'item.transformChanged' $script:T20Revision | Out-Null
    $filterKinds = Send-Task20Request @{ op = 'request'; id = 'o-filter-kinds'; method = 'filter.kindList' }
    Assert-Ok $filterKinds $script:T20Revision 'filter kind list'
    $filterKindEntry = @($filterKinds.data.kinds | Where-Object { $_.id -eq 'color_filter' }) | Select-Object -First 1
    if ($null -eq $filterKindEntry) { $filterKindEntry = $filterKinds.data.kinds[0] }
    $filterKind = [string]$filterKindEntry.id
    $filterDefaults = Send-Task20Request @{ op = 'request'; id = 'o-filter-defaults'; method = 'filter.kindDefaults'; params = @{ kind = $filterKind } }
    Assert-Ok $filterDefaults $script:T20Revision 'filter defaults'
    $filter = Send-Task20Guarded 'o-filter' 'filter.create' @{ source = [string]$script:T20Source.data.source; kind = $filterKind; name = 'Task20 Filter'; settings = $filterDefaults.data.settings } $script:T20Revision
    Assert-Ok $filter ($filter.GuardRevision + 1) 'source filter'
    $script:T20Revision = [int64]$filter.revision
    Read-Task20Event 'filter.created' $script:T20Revision | Out-Null
}

function Invoke-Task20OutputSetup {
    $script:T20ProgramOutput = Send-Task20Guarded 'o-output-program' 'previewOutput.create' @{ target = @{ type = 'program' }; width = 160; height = 90; enabled = $false } $script:T20Revision
    Assert-Ok $script:T20ProgramOutput ($script:T20ProgramOutput.GuardRevision + 1) 'Program output'
    $script:T20Revision = [int64]$script:T20ProgramOutput.revision
    Read-Task20Event 'previewOutput.created' $script:T20Revision | Out-Null
    $script:T20PreviewOutput = Send-Task20Guarded 'o-output-preview' 'previewOutput.create' @{ target = @{ type = 'preview' }; width = 160; height = 90; enabled = $false } $script:T20Revision
    Assert-Ok $script:T20PreviewOutput ($script:T20PreviewOutput.GuardRevision + 1) 'Preview output'
    $script:T20Revision = [int64]$script:T20PreviewOutput.revision
    Read-Task20Event 'previewOutput.created' $script:T20Revision | Out-Null
    $script:T20SceneOutput = Send-Task20Guarded 'o-output-scene' 'previewOutput.create' @{ target = @{ type = 'scene'; scene = [string]$script:T20MainScene.data.scene }; width = 160; height = 90; enabled = $false; scale = 'fit' } $script:T20Revision
    Assert-Ok $script:T20SceneOutput ($script:T20SceneOutput.GuardRevision + 1) 'Scene output'
    $script:T20Revision = [int64]$script:T20SceneOutput.revision
    Read-Task20Event 'previewOutput.created' $script:T20Revision | Out-Null
    $script:T20SourceOutput = Send-Task20Guarded 'o-output-source' 'previewOutput.create' @{ target = @{ type = 'source'; source = [string]$script:T20Source.data.source }; width = 160; height = 90; enabled = $false; scale = 'fill' } $script:T20Revision
    Assert-Ok $script:T20SourceOutput ($script:T20SourceOutput.GuardRevision + 1) 'Source output'
    $script:T20Revision = [int64]$script:T20SourceOutput.revision
    Read-Task20Event 'previewOutput.created' $script:T20Revision | Out-Null
    $script:T20CanvasOutput = Send-Task20Guarded 'o-output-canvas' 'previewOutput.create' @{ target = @{ type = 'canvas'; canvas = [string]$script:T20CanvasTwo.data.canvas }; width = 160; height = 90; enabled = $false; scale = 'oneToOne' } $script:T20Revision
    Assert-Ok $script:T20CanvasOutput ($script:T20CanvasOutput.GuardRevision + 1) 'Canvas output'
    $script:T20Revision = [int64]$script:T20CanvasOutput.revision
    Read-Task20Event 'previewOutput.created' $script:T20Revision | Out-Null
    $list = Send-Task20Request @{ op = 'request'; id = 'o-list'; method = 'previewOutput.list' }
    Assert-Ok $list $script:T20Revision 'previewOutput.list'
    if ([int]$list.data.count -ne 5) { Fail-Task20 'previewOutput.list did not enumerate all five target types.' }
    foreach ($output in @($script:T20ProgramOutput, $script:T20PreviewOutput, $script:T20SceneOutput, $script:T20SourceOutput, $script:T20CanvasOutput)) {
        $got = Send-Task20Request @{ op = 'request'; id = "o-get-$($output.data.previewOutput)"; method = 'previewOutput.get'; params = @{ previewOutput = [string]$output.data.previewOutput } }
        Assert-Ok $got $script:T20Revision 'previewOutput.get'
        if (-not $got.data.hasSharedTexture -or [int]$got.data.width -ne 160 -or [int]$got.data.height -ne 90) { Fail-Task20 'previewOutput.get returned incomplete resource metadata.' }
    }
    $script:T20ProgramHandle = [string]$script:T20ProgramOutput.data.previewOutput
}

function Invoke-Task20RetargetAndReset {
    $retargetSource = Send-Task20Guarded 'o-retarget-source' 'previewOutput.setTarget' @{ previewOutput = $script:T20ProgramHandle; target = @{ type = 'source'; source = [string]$script:T20Source.data.source }; scale = 'stretch' } $script:T20Revision
    Assert-Ok $retargetSource ($retargetSource.GuardRevision + 1) 'retarget to Source'
    $script:T20Revision = [int64]$retargetSource.revision
    Read-Task20Event 'previewOutput.targetChanged' $script:T20Revision | Out-Null
    $retargetScene = Send-Task20Guarded 'o-retarget-scene' 'previewOutput.setTarget' @{ previewOutput = $script:T20ProgramHandle; target = @{ type = 'scene'; scene = [string]$script:T20MainScene.data.scene }; scale = 'fill' } $script:T20Revision
    Assert-Ok $retargetScene ($retargetScene.GuardRevision + 1) 'retarget to Scene'
    $script:T20Revision = [int64]$retargetScene.revision
    Read-Task20Event 'previewOutput.targetChanged' $script:T20Revision | Out-Null
    $retargetCanvas = Send-Task20Guarded 'o-retarget-canvas' 'previewOutput.setTarget' @{ previewOutput = $script:T20ProgramHandle; target = @{ type = 'canvas'; canvas = [string]$script:T20CanvasTwo.data.canvas }; scale = 'oneToOne' } $script:T20Revision
    Assert-Ok $retargetCanvas ($retargetCanvas.GuardRevision + 1) 'retarget to Canvas'
    $script:T20Revision = [int64]$retargetCanvas.revision
    Read-Task20Event 'previewOutput.targetChanged' $script:T20Revision | Out-Null
    $retargetInfo = Send-Task20Request @{ op = 'request'; id = 'o-retarget-info'; method = 'previewOutput.getInfo'; params = @{ previewOutput = $script:T20ProgramHandle } }
    Assert-Ok $retargetInfo $script:T20Revision 'retarget info'
    if ([string]$retargetInfo.data.target.type -ne 'canvas' -or [string]$retargetInfo.data.scale -ne 'oneToOne') { Fail-Task20 'rapid retargeting or semantic scale readback was incorrect.' }
    $canvasReset = Send-Task20Guarded 'o-canvas-reset' 'canvas.setVideoSettings' @{ canvas = [string]$script:T20CanvasTwo.data.canvas; videoSettings = @{ width = 400; height = 200 } } $script:T20Revision
    Assert-Ok $canvasReset ($canvasReset.GuardRevision + 1) 'Canvas video reset with output'
    $script:T20Revision = [int64]$canvasReset.revision
    Read-Task20Event 'canvas.videoSettingsChanged' $script:T20Revision | Out-Null
    $resourceReset = Read-Task20Event 'previewOutput.resourceChanged' $script:T20Revision
    $resourceResetSecond = Read-Task20Event 'previewOutput.resourceChanged' $script:T20Revision
    if ([string]$resourceReset.data.resourceGeneration -eq [string]$script:T20CanvasOutput.data.resourceGeneration -or [string]$resourceResetSecond.data.resourceGeneration -eq [string]$script:T20CanvasOutput.data.resourceGeneration) { Fail-Task20 'Canvas video reset did not bump PreviewOutput resource generation.' }
}

function Invoke-Task20InvalidationChecks {
    $removeSource = Send-Task20Guarded 'o-remove-source' 'source.remove' @{ source = [string]$script:T20Source.data.source } $script:T20Revision
    Assert-Ok $removeSource ($removeSource.GuardRevision + 1) 'remove Source target'
    $script:T20Revision = [int64]$removeSource.revision
    $sourceRemovalEvents = [System.Collections.Generic.List[string]]::new()
    Read-Task20EventsThrough 'source.removed' $script:T20Revision $sourceRemovalEvents
    if (-not ($sourceRemovalEvents -contains 'previewOutput.targetChanged')) { Fail-Task20 'Source target removal did not invalidate its PreviewOutput.' }
    $sourceInfo = Send-Task20Request @{ op = 'request'; id = 'o-source-invalidated'; method = 'previewOutput.getInfo'; params = @{ previewOutput = [string]$script:T20SourceOutput.data.previewOutput } }
    Assert-Ok $sourceInfo $script:T20Revision 'invalidated Source output info'
    if ($sourceInfo.data.targetAvailable) { Fail-Task20 'Source-targeted PreviewOutput remained available after source removal.' }
    $removeMainScene = Send-Task20Guarded 'o-remove-scene' 'scene.remove' @{ scene = [string]$script:T20MainScene.data.scene } $script:T20Revision
    Assert-Ok $removeMainScene ($removeMainScene.GuardRevision + 1) 'remove Scene target'
    $script:T20Revision = [int64]$removeMainScene.revision
    $sceneRemovalEvents = [System.Collections.Generic.List[string]]::new()
    Read-Task20EventsThrough 'scene.removed' $script:T20Revision $sceneRemovalEvents
    if (-not ($sceneRemovalEvents -contains 'previewOutput.targetChanged')) { Fail-Task20 'Scene target removal did not invalidate its PreviewOutput.' }
    $removePrivateScene = Send-Task20Guarded 'o-remove-private-scene' 'scene.remove' @{ scene = [string]$script:T20PrivateScene.data.scene } $script:T20Revision
    Assert-Ok $removePrivateScene ($removePrivateScene.GuardRevision + 1) 'remove private Scene'
    $script:T20Revision = [int64]$removePrivateScene.revision
    Read-Task20Event 'scene.removed' $script:T20Revision | Out-Null
}

function Invoke-Task20CanvasCleanup {
    $removeCanvas = Send-Task20Guarded 'o-remove-canvas' 'canvas.remove' @{ canvas = [string]$script:T20CanvasTwo.data.canvas } $script:T20Revision
    Assert-Ok $removeCanvas ($removeCanvas.GuardRevision + 1) 'remove Canvas target'
    $script:T20Revision = [int64]$removeCanvas.revision
    Read-Task20Event 'previewOutput.targetChanged' $script:T20Revision | Out-Null
    Read-Task20Event 'previewOutput.targetChanged' $script:T20Revision | Out-Null
    Read-Task20Event 'canvas.removed' $script:T20Revision | Out-Null
    $canvasInfo = Send-Task20Request @{ op = 'request'; id = 'o-canvas-invalidated'; method = 'previewOutput.getInfo'; params = @{ previewOutput = [string]$script:T20CanvasOutput.data.previewOutput } }
    Assert-Ok $canvasInfo $script:T20Revision 'invalidated Canvas output info'
    if ($canvasInfo.data.targetAvailable) { Fail-Task20 'Canvas-targeted PreviewOutput remained available after Canvas removal.' }
    $rebindPreview = Send-Task20Guarded 'o-rebind-preview' 'previewOutput.setTarget' @{ previewOutput = $script:T20ProgramHandle; target = @{ type = 'preview' }; scale = 'fit' } $script:T20Revision
    Assert-Ok $rebindPreview ($rebindPreview.GuardRevision + 1) 'rebind after target disappearance'
    $script:T20Revision = [int64]$rebindPreview.revision
    Read-Task20Event 'previewOutput.targetChanged' $script:T20Revision | Out-Null
    $remaining = Send-Task20Request @{ op = 'request'; id = 'o-list-final'; method = 'previewOutput.list' }
    Assert-Ok $remaining $script:T20Revision 'final previewOutput.list'
    if ([int]$remaining.data.count -ne 5) { Fail-Task20 'PreviewOutput list lost a live output during target invalidation.' }
}

function Invoke-Task20FinalCleanup {
    foreach ($output in @($script:T20ProgramOutput, $script:T20PreviewOutput, $script:T20SceneOutput, $script:T20SourceOutput, $script:T20CanvasOutput)) {
        $destroy = Send-Task20Guarded "o-destroy-$($output.data.previewOutput)" 'previewOutput.destroy' @{ previewOutput = [string]$output.data.previewOutput } $script:T20Revision
        Assert-Ok $destroy ($destroy.GuardRevision + 1) 'destroy PreviewOutput'
        $script:T20Revision = [int64]$destroy.revision
        Read-Task20Event 'previewOutput.destroyed' $script:T20Revision | Out-Null
    }
    $close = Send-Task20Guarded 'o-close' 'session.close' $null $script:T20Revision
    Assert-Ok $close ($close.GuardRevision + 1) 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task20Engine
    Write-Output 'Task 20 previewOutput integration: PASS'
}

function Invoke-Task20Scenario {
    Invoke-Task20Bootstrap
    Invoke-Task20GraphSetup
    Invoke-Task20SourceSetup
    Invoke-Task20OutputSetup
    Invoke-Task20RetargetAndReset
    Invoke-Task20InvalidationChecks
    Invoke-Task20CanvasCleanup
    Invoke-Task20FinalCleanup
}

try {
    Invoke-Task20Scenario
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:ErrorTask) { Write-Host ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot,
    [Parameter(Mandatory = $true)]
    [string] $ConsumerPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:P2Process = $null
$script:P2ErrorTask = $null
$script:P2Events = [System.Collections.Generic.List[object]]::new()
$script:P2Wire = [System.Collections.Generic.List[object]]::new()
$script:P2NextSequence = [uint64]1
$script:P2LastResponseIndex = -1
$script:P2Revision = [int64]0
$script:P2CurrentRequest = $null
$script:P2Outputs = @{}
$script:P2Graph = @{}

function Fail-P2Physical([string] $Message) {
    throw "Phase 2 physical acceptance: $Message"
}

function Start-P2PhysicalEngine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } |
        Select-Object -First 1
    if ($null -eq $engine) { Fail-P2Physical 'obs-engine.exe was not found.' }
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:P2Process = [System.Diagnostics.Process]::new()
    $script:P2Process.StartInfo = $info
    if (-not $script:P2Process.Start()) { Fail-P2Physical 'failed to start obs-engine.exe.' }
    $script:P2ErrorTask = $script:P2Process.StandardError.ReadToEndAsync()
}

function Stop-P2PhysicalEngine {
    if ($null -eq $script:P2Process) { return }
    if (-not $script:P2Process.HasExited) { $script:P2Process.Kill(); $script:P2Process.WaitForExit() }
    $stderr = if ($null -ne $script:P2ErrorTask) { $script:P2ErrorTask.GetAwaiter().GetResult() } else { '' }
    Assert-P2PhysicalStderr $stderr
    if ($script:P2Process.ExitCode -ne 0) { Fail-P2Physical "engine exited with $($script:P2Process.ExitCode)." }
    Write-Host 'Physical stderr classification: PASS'
}

function Read-P2PhysicalMessage {
    $read = $script:P2Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) {
        $request = if ($null -eq $script:P2CurrentRequest) { '<bootstrap>' } else { "$($script:P2CurrentRequest.id) ($($script:P2CurrentRequest.method))" }
        Fail-P2Physical "timed out waiting for engine stdout while waiting for $request."
    }
    $line = $read.Result
    if ($null -eq $line) { Fail-P2Physical 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:P2Wire.Add([pscustomobject]@{ Index = $script:P2Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-P2PhysicalRequest([hashtable] $Request) {
    $script:P2CurrentRequest = $Request
    $script:P2Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60))
    $script:P2Process.StandardInput.Flush()
    while ($true) {
        $message = Read-P2PhysicalMessage
        if ($message.op -eq 'event') { $script:P2Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-P2Physical "wrong response for $($Request.id)." }
        $script:P2LastResponseIndex = $script:P2Wire.Count - 1
        $script:P2CurrentRequest = $null
        return $message
    }
}

function Send-P2PhysicalGuarded([string] $Id, [string] $Method, $Params, [int64] $Revision) {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $requestId = if ($attempt -eq 0) { $Id } else { "$Id-retry$attempt" }
        $request = @{ op = 'request'; id = $requestId; method = $Method; ifRevision = $Revision }
        if ($null -ne $Params) { $request.params = $Params }
        $response = Send-P2PhysicalRequest $request
        if ($response.status.ok -or [string]$response.status.code -ne 'revision_conflict') {
            $response | Add-Member -NotePropertyName GuardRevision -NotePropertyValue $Revision -Force
            return $response
        }
        $Revision = [int64]$response.revision
    }
    return $response
}

function Assert-P2PhysicalOk($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) {
        $code = if ($null -ne $Response.status.PSObject.Properties['code']) { [string]$Response.status.code } else { '' }
        $message = if ($null -ne $Response.status.PSObject.Properties['message']) { [string]$Response.status.message } else { '' }
        Fail-P2Physical "$Label failed: status=$($Response.status.ok) code=$code message=$message expectedRevision=$Revision actualRevision=$($Response.revision)."
    }
}

function Test-P2PhysicalAllowedDiagnostic([string] $Line) {
    $allowed = @(
        'Hardware-Accelerated GPU Scheduling enabled on adapter!',
        'No AJA devices found, skipping loading AJA plugin',
        "Failed to initialize module 'aja.dll'",
        'CoreAudio AAC encoder not installed on the system or couldn''t be loaded',
        'A DeckLink iterator could not be created.  The DeckLink drivers may not be installed',
        "Failed to initialize module 'decklink.dll'",
        'Failed to get C:\Program Files\NVIDIA Corporation\NVIDIA Audio Effects SDK\NVAudioEffects.dll version info size',
        'NVIDIA denoiser disabled, redistributable not found or could not be loaded.',
        'Failed to get C:\Program Files\NVIDIA Corporation\NVIDIA Video Effects\NVVideoEffects.dll version info size',
        'NVIDIA VIDEO FX]: FX disabled, redistributable not found or could not be loaded.',
        "LoadLibrary failed for 'nvEncodeAPI64.dll': The specified module could not be found.",
        'NVENC not supported',
        "Failed to initialize module 'obs-nvenc.dll'",
        "Couldn't find VLC installation, VLC video source disabled"
    )
    foreach ($fragment in $allowed) {
        if ($Line.Contains($fragment)) { return $true }
    }
    return $false
}

function Assert-P2PhysicalStderr([string] $Stderr) {
    $unexpected = [System.Collections.Generic.List[string]]::new()
    foreach ($line in ($Stderr -split '\r?\n')) {
        if ($line -match '\[libobs:(warning|error)\]' -and -not (Test-P2PhysicalAllowedDiagnostic $line)) {
            $unexpected.Add($line.Trim())
        }
    }
    if ($Stderr -match 'Attempted to add Scene without specifying a canvas') {
        Fail-P2Physical 'legacy fallback warning was emitted.'
    }
    if ($unexpected.Count -gt 0) {
        Fail-P2Physical "unexpected engine diagnostics: $($unexpected -join ' | ')"
    }
}

function Test-P2PhysicalAmbientEvent($Event, [string] $ExpectedName) {
    if ([string]$Event.event -eq $ExpectedName) { return $false }
    return [string]$Event.event -eq 'scene.itemsChanged' -or [string]$Event.event -like 'source.*'
}

function Take-P2PhysicalEvent {
    if ($script:P2Events.Count -gt 0) {
        $event = $script:P2Events[0]
        $script:P2Events.RemoveAt(0)
        return $event
    }
    return Read-P2PhysicalMessage
}

function Test-P2PhysicalEventSequence($Event) {
    if ($Event.op -ne 'event') { return $false }
    return [uint64]$Event.seq -eq $script:P2NextSequence
}

function Test-P2PhysicalExpectedEvent($Event, [string] $Name, [int64] $Revision) {
    if (-not (Test-P2PhysicalEventSequence $Event)) { return $false }
    if ([string]$Event.event -ne $Name) { return $false }
    return [int64]$Event.revision -eq $Revision
}

function Assert-P2PhysicalAmbientSequence($Event) {
    if (-not (Test-P2PhysicalEventSequence $Event)) {
        Fail-P2Physical "invalid ambient event sequence: op=$($Event.op) event=$($Event.event) seq=$($Event.seq) nextSeq=$script:P2NextSequence."
    }
    $script:P2NextSequence++
}

function Assert-P2PhysicalEventOrder($Event, [string] $Name, [bool] $AllowBeforeResponse) {
    if ($AllowBeforeResponse) { return }
    $wireEvent = @($script:P2Wire | Where-Object { $_.Seq -eq [uint64]$Event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:P2LastResponseIndex) { Fail-P2Physical "event $Name preceded its response." }
}

function Read-P2PhysicalEvent([string] $Name, [int64] $Revision, [bool] $AllowBeforeResponse = $false) {
    while ($true) {
        $event = Take-P2PhysicalEvent
        if (Test-P2PhysicalAmbientEvent $event $Name) {
            Assert-P2PhysicalAmbientSequence $event
            continue
        }
        if (-not (Test-P2PhysicalExpectedEvent $event $Name $Revision)) {
            Fail-P2Physical "unexpected event; expected $Name at revision $Revision, got op=$($event.op) event=$($event.event) seq=$($event.seq) revision=$($event.revision) nextSeq=$script:P2NextSequence pending=$($script:P2Events.Count)."
        }
        break
    }
    $script:P2NextSequence++
    Assert-P2PhysicalEventOrder $event $Name $AllowBeforeResponse
    return $event
}

function Sync-P2PhysicalReadRevision($Response, [string] $Label, [bool] $DrainEvents = $true) {
    if (-not $Response.status.ok -or [int64]$Response.revision -lt $script:P2Revision) {
        Fail-P2Physical "$Label returned an invalid read revision $($Response.revision) from $script:P2Revision."
    }
    if ($DrainEvents) {
        while ($script:P2Events.Count -gt 0 -and [int64]$script:P2Events[0].revision -le [int64]$Response.revision) {
            $event = $script:P2Events[0]
            $script:P2Events.RemoveAt(0)
            if ($event.op -ne 'event' -or [uint64]$event.seq -ne $script:P2NextSequence) {
                Fail-P2Physical "$Label left an invalid event sequence at seq=$($event.seq), nextSeq=$script:P2NextSequence."
            }
            $script:P2NextSequence++
        }
    }
    $script:P2Revision = [int64]$Response.revision
}

function Assert-P2PhysicalReadOk($Response, [string] $Label, [bool] $DrainEvents = $true) {
    Sync-P2PhysicalReadRevision $Response $Label $DrainEvents
}

function Read-P2PhysicalEventsThrough([string] $FinalName, [int64] $Revision) {
    $seen = [System.Collections.Generic.List[string]]::new()
    while ($true) {
        $event = Take-P2PhysicalEvent
        if (Test-P2PhysicalAmbientEvent $event $FinalName) {
            Assert-P2PhysicalAmbientSequence $event
            continue
        }
        if ($event.op -ne 'event' -or [int64]$event.revision -ne $Revision -or [uint64]$event.seq -ne $script:P2NextSequence) {
            Fail-P2Physical "unexpected event while waiting for $FinalName at revision $Revision, got op=$($event.op) event=$($event.event) seq=$($event.seq) revision=$($event.revision) nextSeq=$script:P2NextSequence pending=$($script:P2Events.Count)."
        }
        $script:P2NextSequence++
        $seen.Add([string]$event.event)
        if ([string]$event.event -eq $FinalName) { return $seen }
    }
}

function New-P2PhysicalConsumer($Descriptor, [int] $Frames, [string] $AdapterLuid = '') {
    $path = (Resolve-Path -LiteralPath $ConsumerPath).Path
    $luid = if ([string]::IsNullOrWhiteSpace($AdapterLuid)) { [string]$Descriptor.adapterLuid } else { $AdapterLuid }
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $path
    $info.Arguments = "--shared-handle=$($Descriptor.sharedTexture.handle) --width=$($Descriptor.width) --height=$($Descriptor.height) --adapter-luid=$luid --frames=$Frames --timeout-ms=250"
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { Fail-P2Physical 'failed to start D3D11 consumer.' }
    return [pscustomobject]@{ Process = $process; Output = $process.StandardOutput.ReadToEndAsync(); Error = $process.StandardError.ReadToEndAsync() }
}

function Wait-P2PhysicalConsumer($Runner, [string] $Label) {
    if (-not $Runner.Process.WaitForExit(30000)) { $Runner.Process.Kill(); $Runner.Process.WaitForExit(); Fail-P2Physical "$Label consumer timed out." }
    $stdout = $Runner.Output.GetAwaiter().GetResult().Trim()
    $stderr = $Runner.Error.GetAwaiter().GetResult()
    if ($Runner.Process.ExitCode -ne 0) { Fail-P2Physical "$Label consumer failed: $stdout $stderr" }
    if ([string]::IsNullOrWhiteSpace($stdout)) { Fail-P2Physical "$Label consumer returned no evidence." }
    return ($stdout | ConvertFrom-Json)
}

function Assert-P2PhysicalAdapterMismatch($Descriptor) {
    $runner = New-P2PhysicalConsumer $Descriptor 1 'FFFFFFFF-FFFFFFFF'
    if (-not $runner.Process.WaitForExit(10000)) { $runner.Process.Kill(); $runner.Process.WaitForExit(); Fail-P2Physical 'adapter-mismatch consumer did not terminate.' }
    if ($runner.Process.ExitCode -eq 0) { Fail-P2Physical 'adapter-mismatch consumer unexpectedly accepted the wrong adapter.' }
    Write-Host 'D3D11 adapter-mismatch rejection: PASS'
}

function Assert-P2PhysicalColor($Evidence, [string] $Label, [int] $Blue, [int] $Green, [int] $Red, [bool] $Changed = $false) {
    if ([int]$Evidence.frames -lt 2 -or [int]$Evidence.timeouts -ge [int]$Evidence.frames) { Fail-P2Physical "$Label did not acquire synchronized frames." }
    if ($Changed -and [int]$Evidence.uniqueChecksums -lt 2) { Fail-P2Physical "$Label did not observe the expected content change." }
    if ([int]$Evidence.lastCenterB -lt ($Blue - 30) -or [int]$Evidence.lastCenterG -lt ($Green - 30) -or [int]$Evidence.lastCenterR -lt ($Red - 30)) {
        Fail-P2Physical "$Label pixel evidence was not the expected BGRA color: $($Evidence | ConvertTo-Json -Compress -Depth 10)."
    }
}

function New-P2GraphCanvas {
    $response = Send-P2PhysicalGuarded 'p-canvas' 'canvas.create' @{ name = 'Physical Canvas'; videoSettings = @{ width = 320; height = 180; format = 'bgra'; colorSpace = 'srgb'; range = 'full'; scaleType = 'bilinear'; fpsNumerator = 30; fpsDenominator = 1 } } $script:P2Revision
    Assert-P2PhysicalOk $response ($response.GuardRevision + 1) 'private Canvas'
    $script:P2Revision = [int64]$response.revision
    Read-P2PhysicalEvent 'canvas.created' $script:P2Revision | Out-Null
    return [string]$response.data.canvas
}

function New-P2GraphScene([string] $Id, [string] $Name, [string] $Canvas = '') {
    $params = @{ name = $Name }
    if ($Canvas) { $params.canvas = $Canvas }
    $response = Send-P2PhysicalGuarded $Id 'scene.create' $params $script:P2Revision
    Assert-P2PhysicalOk $response ($response.GuardRevision + 1) $Name
    $script:P2Revision = [int64]$response.revision
    Read-P2PhysicalEvent 'scene.created' $script:P2Revision | Out-Null
    return [string]$response.data.scene
}

function New-P2GraphSource([string] $Id, [string] $Name, [uint64] $Color) {
    $response = Send-P2PhysicalGuarded $Id 'source.create' @{ kind = 'color_source_v3'; name = $Name; settings = @{ width = 1920; height = 1080; color = $Color } } $script:P2Revision
    Assert-P2PhysicalOk $response ($response.GuardRevision + 1) $Name
    $script:P2Revision = [int64]$response.revision
    Read-P2PhysicalEvent 'source.created' $script:P2Revision | Out-Null
    return [string]$response.data.source
}

function New-P2GraphItem([string] $Id, [string] $Scene, [string] $Source) {
    $response = Send-P2PhysicalGuarded $Id 'item.create' @{ scene = $Scene; source = $Source } $script:P2Revision
    Assert-P2PhysicalOk $response ($response.GuardRevision + 1) 'scene Item'
    $script:P2Revision = [int64]$response.revision
    Read-P2PhysicalEvent 'item.created' $script:P2Revision | Out-Null
    return [string]$response.data.item
}

function Invoke-P2PhysicalItemMutation([string] $Id, [string] $Method, $Params, [string] $EventName) {
    $response = Send-P2PhysicalGuarded $Id $Method $Params $script:P2Revision
    Assert-P2PhysicalOk $response ($response.GuardRevision + 1) $Method
    $script:P2Revision = [int64]$response.revision
    Read-P2PhysicalEvent $EventName $script:P2Revision | Out-Null
}

function Invoke-P2PhysicalItemCoverage([string] $Item, [string] $Scene, [string] $Source) {
    $initial = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-item-initial'; method = 'item.get'; params = @{ item = $Item } }
    Assert-P2PhysicalReadOk $initial 'initial physical Item'
    $orderItem = New-P2GraphItem 'p-private-order-item' $Scene $Source
    Invoke-P2PhysicalItemMutation 'p-item-move-top' 'item.moveTop' @{ item = $Item } 'item.orderChanged'
    Invoke-P2PhysicalItemMutation 'p-item-set-order' 'item.setOrder' @{ item = $Item; index = 0 } 'item.orderChanged'
    $mutations = @(
        @{ id = 'p-item-position'; method = 'item.setPosition'; params = @{ item = $Item; position = @{ x = 24.0; y = 18.0 } }; event = 'item.transformChanged' },
        @{ id = 'p-item-scale'; method = 'item.setScale'; params = @{ item = $Item; scale = @{ x = 1.1; y = 0.9 } }; event = 'item.transformChanged' },
        @{ id = 'p-item-rotation'; method = 'item.setRotation'; params = @{ item = $Item; rotation = 7.0 }; event = 'item.transformChanged' },
        @{ id = 'p-item-alignment'; method = 'item.setAlignment'; params = @{ item = $Item; alignment = 10 }; event = 'item.transformChanged' },
        @{ id = 'p-item-bounds'; method = 'item.setBounds'; params = @{ item = $Item; bounds = @{ type = 'stretch'; width = 160.0; height = 90.0 } }; event = 'item.transformChanged' },
        @{ id = 'p-item-bounds-alignment'; method = 'item.setBoundsAlignment'; params = @{ item = $Item; alignment = 5 }; event = 'item.transformChanged' },
        @{ id = 'p-item-crop'; method = 'item.setCrop'; params = @{ item = $Item; crop = @{ left = 1; top = 1; right = 1; bottom = 1 } }; event = 'item.transformChanged' },
        @{ id = 'p-item-crop-to-bounds'; method = 'item.setCropToBounds'; params = @{ item = $Item; cropToBounds = $true }; event = 'item.transformChanged' },
        @{ id = 'p-item-visible'; method = 'item.setVisible'; params = @{ item = $Item; visible = $false }; event = 'item.visibilityChanged' },
        @{ id = 'p-item-locked'; method = 'item.setLocked'; params = @{ item = $Item; locked = $true }; event = 'item.lockedChanged' },
        @{ id = 'p-item-scale-filter'; method = 'item.setScaleFilter'; params = @{ item = $Item; scaleFilter = 'point' }; event = 'item.transformChanged' },
        @{ id = 'p-item-blend-mode'; method = 'item.setBlendMode'; params = @{ item = $Item; blendMode = 'additive' }; event = 'item.blendChanged' },
        @{ id = 'p-item-blend-method'; method = 'item.setBlendMethod'; params = @{ item = $Item; blendMethod = 'srgbOff' }; event = 'item.blendChanged' }
    )
    foreach ($mutation in $mutations) {
        Invoke-P2PhysicalItemMutation $mutation.id $mutation.method $mutation.params $mutation.event
    }
    Invoke-P2PhysicalItemMutation 'p-item-restore-transform' 'item.setTransform' @{ item = $Item; transform = $initial.data.transform } 'item.transformChanged'
    Invoke-P2PhysicalItemMutation 'p-item-restore-visible' 'item.setVisible' @{ item = $Item; visible = [bool]$initial.data.visible } 'item.visibilityChanged'
    Invoke-P2PhysicalItemMutation 'p-item-restore-locked' 'item.setLocked' @{ item = $Item; locked = [bool]$initial.data.locked } 'item.lockedChanged'
    Invoke-P2PhysicalItemMutation 'p-item-restore-filter' 'item.setScaleFilter' @{ item = $Item; scaleFilter = [string]$initial.data.scaleFilter } 'item.transformChanged'
    Invoke-P2PhysicalItemMutation 'p-item-restore-blend-mode' 'item.setBlendMode' @{ item = $Item; blendMode = [string]$initial.data.blendMode } 'item.blendChanged'
    Invoke-P2PhysicalItemMutation 'p-item-restore-blend-method' 'item.setBlendMethod' @{ item = $Item; blendMethod = [string]$initial.data.blendMethod } 'item.blendChanged'
    Write-Host 'Physical Item transform/crop/bounds/order/visibility/lock/blend coverage: PASS'
    return $orderItem
}

function Invoke-P2GraphComposition {
    $canvas = New-P2GraphCanvas
    $main = New-P2GraphScene 'p-main' 'Physical Main'
    $preview = New-P2GraphScene 'p-preview' 'Physical Preview'
    $scene = New-P2GraphScene 'p-scene' 'Physical Scene'
    $private = New-P2GraphScene 'p-private' 'Physical Private' $canvas
    $red = New-P2GraphSource 'p-red' 'Physical Red' 4278190335
    $blue = New-P2GraphSource 'p-blue' 'Physical Blue' 4294901760
    $green = New-P2GraphSource 'p-green' 'Physical Green' 4278255360
    $mainItem = New-P2GraphItem 'p-main-item' $main $red
    $previewItem = New-P2GraphItem 'p-preview-item' $preview $blue
    $sceneItem = New-P2GraphItem 'p-scene-item' $scene $green
    $privateItem = New-P2GraphItem 'p-private-item' $private $green
    $transform = Send-P2PhysicalGuarded 'p-transform' 'item.setTransform' @{ item = $mainItem; transform = @{ position = @{ x = 20.0; y = 30.0 }; scale = @{ x = 1.0; y = 1.0 }; rotation = 5.0; alignment = 5; bounds = @{ type = 'none'; width = 0.0; height = 0.0 }; crop = @{ left = 0; top = 0; right = 0; bottom = 0 }; cropToBounds = $false } } $script:P2Revision
    Assert-P2PhysicalOk $transform ($transform.GuardRevision + 1) 'item transform'
    $script:P2Revision = [int64]$transform.revision
    Read-P2PhysicalEvent 'item.transformChanged' $script:P2Revision | Out-Null
    $channel = Send-P2PhysicalGuarded 'p-private-channel' 'canvas.setChannel' @{ canvas = $canvas; channel = 0; target = @{ type = 'scene'; scene = $private } } $script:P2Revision
    Assert-P2PhysicalOk $channel ($channel.GuardRevision + 1) 'private Canvas channel'
    $script:P2Revision = [int64]$channel.revision
    Read-P2PhysicalEvent 'canvas.channelChanged' $script:P2Revision | Out-Null
    $program = Send-P2PhysicalGuarded 'p-program' 'program.setScene' @{ scene = $main } $script:P2Revision
    Assert-P2PhysicalOk $program ($program.GuardRevision + 1) 'Program route'
    $script:P2Revision = [int64]$program.revision
    Read-P2PhysicalEvent 'program.sceneChanged' $script:P2Revision | Out-Null
    $previewRoute = Send-P2PhysicalGuarded 'p-preview-route' 'preview.setScene' @{ scene = $preview } $script:P2Revision
    Assert-P2PhysicalOk $previewRoute ($previewRoute.GuardRevision + 1) 'Preview route'
    $script:P2Revision = [int64]$previewRoute.revision
    Read-P2PhysicalEvent 'preview.sceneChanged' $script:P2Revision | Out-Null
    $script:P2Graph = @{ Canvas = $canvas; Main = $main; Preview = $preview; Scene = $scene; Private = $private; Red = $red; Blue = $blue; Green = $green; MainItem = $mainItem; PreviewItem = $previewItem; SceneItem = $sceneItem; PrivateItem = $privateItem; OrderItem = '' }
    $script:P2Graph.OrderItem = Invoke-P2PhysicalItemCoverage $privateItem $private $green
}

function New-P2PhysicalOutput([string] $Id, [string] $Type, [string] $Handle = '') {
    $target = @{ type = $Type }
    if ($Handle) { $target[$Type] = $Handle }
    $response = Send-P2PhysicalGuarded $Id 'previewOutput.create' @{ target = $target; width = 160; height = 90; enabled = $true; scale = 'fit' } $script:P2Revision
    Assert-P2PhysicalOk $response ($response.GuardRevision + 1) "PreviewOutput $Type"
    $script:P2Revision = [int64]$response.revision
    Read-P2PhysicalEvent 'previewOutput.created' $script:P2Revision | Out-Null
    return $response.data
}

function Test-P2PhysicalOutput($Output, [string] $Label, [int] $Blue, [int] $Green, [int] $Red, [bool] $CheckMismatch = $false) {
    $descriptor = Send-P2PhysicalRequest @{ op = 'request'; id = "p-get-$Label"; method = 'previewOutput.getSharedTexture'; params = @{ previewOutput = [string]$Output.previewOutput } }
    Assert-P2PhysicalReadOk $descriptor "$Label shared texture"
    if ($CheckMismatch) { Assert-P2PhysicalAdapterMismatch $descriptor.data }
    $runner = New-P2PhysicalConsumer $descriptor.data 24
    $evidence = Wait-P2PhysicalConsumer $runner $Label
    $evidence | Add-Member -NotePropertyName adapterLuid -NotePropertyValue ([string]$descriptor.data.adapterLuid) -Force
    $evidence | Add-Member -NotePropertyName resourceGeneration -NotePropertyValue ([string]$descriptor.data.resourceGeneration) -Force
    Assert-P2PhysicalColor $evidence $Label $Blue $Green $Red
    $release = Send-P2PhysicalRequest @{ op = 'request'; id = "p-release-$Label"; method = 'previewOutput.releaseSharedTexture'; params = @{ previewOutput = [string]$Output.previewOutput } }
    Assert-P2PhysicalReadOk $release "$Label release"
    return $evidence
}

function Invoke-P2StaticOutputs {
    $script:P2Outputs.Program = New-P2PhysicalOutput 'p-output-program' 'program'
    $script:P2Outputs.Preview = New-P2PhysicalOutput 'p-output-preview' 'preview'
    $script:P2Outputs.Scene = New-P2PhysicalOutput 'p-output-scene' 'scene' $script:P2Graph.Scene
    $script:P2Outputs.Source = New-P2PhysicalOutput 'p-output-source' 'source' $script:P2Graph.Green
    $script:P2Outputs.Canvas = New-P2PhysicalOutput 'p-output-canvas' 'canvas' $script:P2Graph.Canvas
    $script:P2StaticEvidence = @{}
    $script:P2StaticEvidence.Program = Test-P2PhysicalOutput $script:P2Outputs.Program 'program' 0 0 255 $true
    $script:P2StaticEvidence.Preview = Test-P2PhysicalOutput $script:P2Outputs.Preview 'preview' 255 0 0
    $script:P2StaticEvidence.Scene = Test-P2PhysicalOutput $script:P2Outputs.Scene 'scene' 0 255 0
    $script:P2StaticEvidence.Source = Test-P2PhysicalOutput $script:P2Outputs.Source 'source' 0 255 0
    $script:P2StaticEvidence.Canvas = Test-P2PhysicalOutput $script:P2Outputs.Canvas 'canvas' 0 255 0
    Write-Output ("Physical static output evidence: " + ($script:P2StaticEvidence | ConvertTo-Json -Compress -Depth 10))
}

function Invoke-P2TransitionPhysical {
    $kinds = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-transition-kinds'; method = 'transition.kindList' }
    Assert-P2PhysicalReadOk $kinds 'transition.kindList'
    $fade = @($kinds.data.kinds | Where-Object { $_.kind -eq 'fade_transition' }) | Select-Object -First 1
    if ($null -eq $fade) { Fail-P2Physical 'fade_transition was not available.' }
    $create = Send-P2PhysicalGuarded 'p-transition-create' 'transition.create' @{ kind = 'fade_transition'; name = 'Physical Fade' } $script:P2Revision
    Assert-P2PhysicalOk $create ($create.GuardRevision + 1) 'transition.create'
    $script:P2Revision = [int64]$create.revision
    Read-P2PhysicalEvent 'transition.created' $script:P2Revision | Out-Null
    $transition = [string]$create.data.transition
    $select = Send-P2PhysicalGuarded 'p-transition-select' 'studio.setTransition' @{ transition = $transition } $script:P2Revision
    Assert-P2PhysicalOk $select ($select.GuardRevision + 1) 'studio.setTransition'
    $script:P2Revision = [int64]$select.revision
    Read-P2PhysicalEvent 'studio.transitionChanged' $script:P2Revision | Out-Null
    $duration = Send-P2PhysicalGuarded 'p-transition-duration' 'studio.setTransitionDuration' @{ durationMs = 750 } $script:P2Revision
    Assert-P2PhysicalOk $duration ($duration.GuardRevision + 1) 'studio duration'
    $script:P2Revision = [int64]$duration.revision
    Read-P2PhysicalEvent 'studio.transitionDurationChanged' $script:P2Revision | Out-Null
    $enable = Send-P2PhysicalGuarded 'p-studio-enable' 'studio.setEnabled' @{ enabled = $true } $script:P2Revision
    Assert-P2PhysicalOk $enable ($enable.GuardRevision + 1) 'studio.setEnabled'
    $script:P2Revision = [int64]$enable.revision
    Read-P2PhysicalEvent 'studio.enabledChanged' $script:P2Revision | Out-Null
    $programDescriptor = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-transition-program-get'; method = 'previewOutput.getSharedTexture'; params = @{ previewOutput = [string]$script:P2Outputs.Program.previewOutput } }
    $previewDescriptor = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-transition-preview-get'; method = 'previewOutput.getSharedTexture'; params = @{ previewOutput = [string]$script:P2Outputs.Preview.previewOutput } }
    Assert-P2PhysicalOk $programDescriptor $script:P2Revision 'transition Program shared texture'
    Assert-P2PhysicalOk $previewDescriptor $script:P2Revision 'transition Preview shared texture'
    $programRunner = New-P2PhysicalConsumer $programDescriptor.data 90
    $previewRunner = New-P2PhysicalConsumer $previewDescriptor.data 90
    Start-Sleep -Milliseconds 350
    $start = Send-P2PhysicalGuarded 'p-transition-start' 'studio.transition' $null $script:P2Revision
    Assert-P2PhysicalOk $start ($start.GuardRevision + 1) 'studio.transition'
    $script:P2Revision = [int64]$start.revision
    Read-P2PhysicalEvent 'transition.started' $script:P2Revision | Out-Null
    $programEvidence = Wait-P2PhysicalConsumer $programRunner 'transition Program'
    $previewEvidence = Wait-P2PhysicalConsumer $previewRunner 'transition Preview'
    Assert-P2PhysicalColor $programEvidence 'transition Program' 255 0 0 $true
    Assert-P2PhysicalColor $previewEvidence 'transition Preview' 255 0 0
    Write-Output ("Physical transition Program evidence: " + ($programEvidence | ConvertTo-Json -Compress -Depth 10))
    Write-Output ("Physical transition Preview evidence: " + ($previewEvidence | ConvertTo-Json -Compress -Depth 10))
    $settled = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-transition-settled'; method = 'studio.getEnabled' }
    Assert-P2PhysicalReadOk $settled 'transition settled state' $false
    $endRevision = [int64]$settled.revision
    Read-P2PhysicalEvent 'program.sceneChanged' $endRevision $true | Out-Null
    Read-P2PhysicalEvent 'transition.ended' $endRevision $true | Out-Null
    $script:P2Revision = $endRevision
    $releaseProgram = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-transition-program-release'; method = 'previewOutput.releaseSharedTexture'; params = @{ previewOutput = [string]$script:P2Outputs.Program.previewOutput } }
    $releasePreview = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-transition-preview-release'; method = 'previewOutput.releaseSharedTexture'; params = @{ previewOutput = [string]$script:P2Outputs.Preview.previewOutput } }
    Assert-P2PhysicalReadOk $releaseProgram 'transition Program release'
    Assert-P2PhysicalReadOk $releasePreview 'transition Preview release'
    Write-Output 'Physical Scene -> Canvas -> Preview -> Transition -> Program flow: PASS'
}

function Invoke-P2ResourceRecreation {
    $toCanvas = Send-P2PhysicalGuarded 'p-retarget-source-canvas' 'previewOutput.setTarget' @{ previewOutput = [string]$script:P2Outputs.Source.previewOutput; target = @{ type = 'canvas'; canvas = $script:P2Graph.Canvas }; scale = 'stretch' } $script:P2Revision
    Assert-P2PhysicalOk $toCanvas ($toCanvas.GuardRevision + 1) 'Source output retarget Canvas'
    $script:P2Revision = [int64]$toCanvas.revision
    Read-P2PhysicalEvent 'previewOutput.targetChanged' $script:P2Revision | Out-Null
    Test-P2PhysicalOutput $script:P2Outputs.Source 'retargeted-canvas' 0 255 0 | Out-Null
    $reset = Send-P2PhysicalGuarded 'p-canvas-reset' 'canvas.setVideoSettings' @{ canvas = $script:P2Graph.Canvas; videoSettings = @{ width = 400; height = 200 } } $script:P2Revision
    Assert-P2PhysicalOk $reset ($reset.GuardRevision + 1) 'Canvas video reset'
    $script:P2Revision = [int64]$reset.revision
    Read-P2PhysicalEvent 'canvas.videoSettingsChanged' $script:P2Revision | Out-Null
    Read-P2PhysicalEvent 'previewOutput.resourceChanged' $script:P2Revision | Out-Null
    Read-P2PhysicalEvent 'previewOutput.resourceChanged' $script:P2Revision | Out-Null
    $resize = Send-P2PhysicalGuarded 'p-source-resize' 'previewOutput.resize' @{ previewOutput = [string]$script:P2Outputs.Source.previewOutput; width = 96; height = 54 } $script:P2Revision
    Assert-P2PhysicalOk $resize ($resize.GuardRevision + 1) 'PreviewOutput resize'
    $script:P2Revision = [int64]$resize.revision
    Read-P2PhysicalEvent 'previewOutput.resourceChanged' $script:P2Revision | Out-Null
    $resizedEvidence = Test-P2PhysicalOutput $script:P2Outputs.Source 'resized-canvas' 0 255 0
    Write-Output ("Physical resized Canvas evidence: " + ($resizedEvidence | ConvertTo-Json -Compress -Depth 10))
    $remove = Send-P2PhysicalGuarded 'p-remove-current-scene-target' 'scene.remove' @{ scene = $script:P2Graph.Scene } $script:P2Revision
    Assert-P2PhysicalOk $remove ($remove.GuardRevision + 1) 'remove current Scene target'
    $script:P2Revision = [int64]$remove.revision
    $removalEvents = Read-P2PhysicalEventsThrough 'scene.removed' $script:P2Revision
    if (-not ($removalEvents -contains 'previewOutput.targetChanged')) { Fail-P2Physical 'current Scene target removal did not invalidate its PreviewOutput.' }
    $sceneInfo = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-current-scene-invalidated'; method = 'previewOutput.getInfo'; params = @{ previewOutput = [string]$script:P2Outputs.Scene.previewOutput } }
    Assert-P2PhysicalReadOk $sceneInfo 'invalidated current Scene target'
    if ($sceneInfo.data.targetAvailable) { Fail-P2Physical 'removed current Scene target remained available.' }
    $script:P2Graph.Scene = ''
    Write-Output 'Physical current Scene target removal/invalidation: PASS'
    Write-Output 'Physical PreviewOutput retarget/resize/resource recreation: PASS'
}

function Remove-P2GraphSource([string] $Id, [string] $Handle) {
    $remove = Send-P2PhysicalGuarded $Id 'source.remove' @{ source = $Handle } $script:P2Revision
    Assert-P2PhysicalOk $remove ($remove.GuardRevision + 1) 'remove graph Source'
    $script:P2Revision = [int64]$remove.revision
    Read-P2PhysicalEventsThrough 'source.removed' $script:P2Revision | Out-Null
}

function Remove-P2GraphScene([string] $Id, [string] $Handle) {
    $remove = Send-P2PhysicalGuarded $Id 'scene.remove' @{ scene = $Handle } $script:P2Revision
    Assert-P2PhysicalOk $remove ($remove.GuardRevision + 1) 'remove graph Scene'
    $script:P2Revision = [int64]$remove.revision
    Read-P2PhysicalEventsThrough 'scene.removed' $script:P2Revision | Out-Null
}

function Invoke-P2PhysicalCleanup {
    $clear = Send-P2PhysicalGuarded 'p-clear-private-channel' 'canvas.setChannel' @{ canvas = $script:P2Graph.Canvas; channel = 0; target = $null } $script:P2Revision
    Assert-P2PhysicalOk $clear ($clear.GuardRevision + 1) 'clear private Canvas channel'
    $script:P2Revision = [int64]$clear.revision
    Read-P2PhysicalEvent 'canvas.channelChanged' $script:P2Revision | Out-Null
    foreach ($entry in @($script:P2Outputs.Program, $script:P2Outputs.Preview, $script:P2Outputs.Scene, $script:P2Outputs.Source, $script:P2Outputs.Canvas)) {
        $destroy = Send-P2PhysicalGuarded "p-destroy-$($entry.previewOutput)" 'previewOutput.destroy' @{ previewOutput = [string]$entry.previewOutput } $script:P2Revision
        Assert-P2PhysicalOk $destroy ($destroy.GuardRevision + 1) 'destroy PreviewOutput'
        $script:P2Revision = [int64]$destroy.revision
        Read-P2PhysicalEvent 'previewOutput.destroyed' $script:P2Revision | Out-Null
    }
    Remove-P2GraphSource 'p-remove-red' $script:P2Graph.Red
    Remove-P2GraphSource 'p-remove-blue' $script:P2Graph.Blue
    Remove-P2GraphSource 'p-remove-green' $script:P2Graph.Green
    Remove-P2GraphScene 'p-remove-main' $script:P2Graph.Main
    Remove-P2GraphScene 'p-remove-preview' $script:P2Graph.Preview
    if ($script:P2Graph.Scene) { Remove-P2GraphScene 'p-remove-scene' $script:P2Graph.Scene }
    Remove-P2GraphScene 'p-remove-private' $script:P2Graph.Private
    $removeCanvas = Send-P2PhysicalGuarded 'p-remove-canvas' 'canvas.remove' @{ canvas = $script:P2Graph.Canvas } $script:P2Revision
    Assert-P2PhysicalOk $removeCanvas ($removeCanvas.GuardRevision + 1) 'remove private Canvas'
    $script:P2Revision = [int64]$removeCanvas.revision
    Read-P2PhysicalEvent 'canvas.removed' $script:P2Revision | Out-Null
    $close = Send-P2PhysicalGuarded 'p-close' 'session.close' $null $script:P2Revision
    Assert-P2PhysicalOk $close ($close.GuardRevision + 1) 'session.close'
    $script:P2Process.WaitForExit(30000) | Out-Null
    Stop-P2PhysicalEngine
    Write-Output 'Phase 2 integrated physical acceptance: PASS'
}

function Invoke-P2PhysicalScenario {
    Start-P2PhysicalEngine $InstallRoot
    $ready = Read-P2PhysicalMessage
    if ($ready.event -ne 'ready') { Fail-P2Physical 'ready marker was not received.' }
    $hello = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-hello'; method = 'session.hello' }
    Assert-P2PhysicalOk $hello 0 'session.hello'
    if (@($hello.data.capabilities | Where-Object { $_.name -eq 'preview.d3d11SharedTexture.v1' }).Count -eq 0) { Fail-P2Physical 'D3D11 shared-texture capability was not advertised.' }
    $subscribe = Send-P2PhysicalRequest @{ op = 'request'; id = 'p-subscribe'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'canvas.*' }, @{ pattern = 'scene.*' }, @{ pattern = 'item.*' }, @{ pattern = 'source.*' }, @{ pattern = 'preview.*' }, @{ pattern = 'program.*' }, @{ pattern = 'previewOutput.*' }, @{ pattern = 'transition.*' }, @{ pattern = 'studio.*' }) } }
    Assert-P2PhysicalOk $subscribe 0 'session.subscribe'
    Invoke-P2GraphComposition
    Invoke-P2StaticOutputs
    Invoke-P2TransitionPhysical
    Invoke-P2ResourceRecreation
    Invoke-P2PhysicalCleanup
}

try {
    Invoke-P2PhysicalScenario
} catch {
    if ($null -ne $script:P2Process -and -not $script:P2Process.HasExited) { $script:P2Process.Kill(); $script:P2Process.WaitForExit() }
    if ($null -ne $script:P2ErrorTask) { Write-Host ("engine stderr: " + $script:P2ErrorTask.GetAwaiter().GetResult()) }
    throw
}

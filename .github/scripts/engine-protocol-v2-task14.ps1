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

function Fail-Task14([string] $Message) { throw "Task 14: $Message" }

function Start-Task14Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task14 'obs-engine.exe was not found.' }
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
    if (-not $script:Process.Start()) { Fail-Task14 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task14Engine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task14 'fallback canvas warning was emitted.' }
    if ($script:Process.ExitCode -ne 0) { Fail-Task14 "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task14Message {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task14 'timed out waiting for stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task14 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task14Request([hashtable] $Request) {
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task14Message
        $script:LastMessage = $message
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task14 "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task14 "$Label failed at revision $($Response.revision)." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task14 "$Label did not return $Code at revision $Revision." }
}

function Read-Task14Event([string] $Name, [int64] $Revision) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task14Message }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) { Fail-Task14 "unexpected event; expected $Name at revision $Revision." }
    $script:NextSequence++
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task14 "event $Name preceded its response." }
    return $event
}

function Invoke-Task14Bootstrap {
    Start-Task14Engine $InstallRoot
    $ready = Read-Task14Message
    if ($ready.event -ne 'ready') { Fail-Task14 'ready marker was not received.' }
    Assert-Ok (Send-Task14Request @{ op = 'request'; id = 'c-hello'; method = 'session.hello' }) 0 'hello'
    Assert-Ok (Send-Task14Request @{ op = 'request'; id = 'c-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'canvas.*' }, @{ pattern = 'scene.*' }) } }) 0 'subscribe'
    $main = Send-Task14Request @{ op = 'request'; id = 'c-main'; method = 'canvas.getMain' }
    Assert-Ok $main 0 'canvas.getMain'
    if ([string]$main.data.canvas -ne '1' -or -not $main.data.isMain) { Fail-Task14 'Main Canvas identity was not stable.' }
    $mainList = Send-Task14Request @{ op = 'request'; id = 'c-list'; method = 'canvas.list' }
    Assert-Ok $mainList 0 'canvas.list'
    if ([int]$mainList.data.count -ne 1) { Fail-Task14 'initial Canvas list did not contain only Main.' }
}

function Invoke-Task14CanvasSetup {
    $script:T14Private = Send-Task14Request @{ op = 'request'; id = 'c-create'; method = 'canvas.create'; params = @{ name = 'Task14 Canvas'; videoSettings = @{ width = 640; height = 360; format = 'bgra'; colorSpace = 'srgb'; range = 'full'; scaleType = 'bilinear'; fpsNumerator = 30; fpsDenominator = 1 } } }
    Assert-Ok $script:T14Private 1 'canvas.create'
    if ([string]$script:T14Private.data.canvas -ne '2') { Fail-Task14 'unexpected private Canvas handle.' }
    Read-Task14Event 'canvas.created' 1 | Out-Null
    $privateGet = Send-Task14Request @{ op = 'request'; id = 'c-get'; method = 'canvas.get'; params = @{ canvas = '2' } }
    Assert-Ok $privateGet 1 'canvas.get'
    if ([int]$privateGet.data.video.width -ne 640 -or [int]$privateGet.data.video.height -ne 360) { Fail-Task14 'private Canvas video settings were not applied.' }
    $script:T14Scene = Send-Task14Request @{ op = 'request'; id = 'c-scene'; method = 'scene.create'; params = @{ name = 'Private Scene'; canvas = '2' }; ifRevision = 1 }
    Assert-Ok $script:T14Scene 2 'scene.create on private Canvas'
    if ([string]$script:T14Scene.data.canvas -ne '2') { Fail-Task14 'Scene was not owned by private Canvas.' }
    Read-Task14Event 'scene.created' 2 | Out-Null
    $scenes = Send-Task14Request @{ op = 'request'; id = 'c-scenes'; method = 'canvas.listScenes'; params = @{ canvas = '2' } }
    Assert-Ok $scenes 2 'canvas.listScenes'
    if ([int]$scenes.data.count -ne 1 -or [string]$scenes.data.scenes[0].scene -ne '3') { Fail-Task14 'canvas.listScenes disagreed with Scene ownership.' }
}

function Invoke-Task14CanvasOperations {
    Assert-Error (Send-Task14Request @{ op = 'request'; id = 'c-busy-remove'; method = 'canvas.remove'; params = @{ canvas = '2' }; ifRevision = 2 }) 'object_in_use' 2 'remove Canvas with Scene'
    $rename = Send-Task14Request @{ op = 'request'; id = 'c-rename'; method = 'canvas.rename'; params = @{ canvas = '2'; name = 'Renamed Canvas' }; ifRevision = 2 }
    Assert-Ok $rename 3 'canvas.rename'
    Read-Task14Event 'canvas.renamed' 3 | Out-Null
    Assert-Error (Send-Task14Request @{ op = 'request'; id = 'c-invalid-video'; method = 'canvas.setVideoSettings'; params = @{ canvas = '2'; videoSettings = @{ width = 0 } }; ifRevision = 3 }) 'bad_request' 3 'invalid video settings'
    $reset = Send-Task14Request @{ op = 'request'; id = 'c-reset-video'; method = 'canvas.setVideoSettings'; params = @{ canvas = '2'; videoSettings = @{ width = 800; height = 450 } }; ifRevision = 3 }
    Assert-Ok $reset 4 'idle Canvas video reset'
    if ([int]$reset.data.width -ne 800 -or [int]$reset.data.height -ne 450) { Fail-Task14 'Canvas video reset did not read back its new dimensions.' }
    Read-Task14Event 'canvas.videoSettingsChanged' 4 | Out-Null
    $channel = Send-Task14Request @{ op = 'request'; id = 'c-channel'; method = 'canvas.setChannel'; params = @{ canvas = '2'; channel = 0; target = @{ type = 'scene'; scene = '3' } }; ifRevision = 4 }
    Assert-Ok $channel 5 'canvas.setChannel'
    Read-Task14Event 'canvas.channelChanged' 5 | Out-Null
    $channelGet = Send-Task14Request @{ op = 'request'; id = 'c-channel-get'; method = 'canvas.getChannel'; params = @{ canvas = '2'; channel = 0 } }
    Assert-Ok $channelGet 5 'canvas.getChannel'
    if ([string]$channelGet.data.target.type -ne 'scene' -or [string]$channelGet.data.target.scene -ne '3') { Fail-Task14 'Canvas channel target readback was incorrect.' }
    Assert-Error (Send-Task14Request @{ op = 'request'; id = 'c-channel-busy'; method = 'canvas.remove'; params = @{ canvas = '2' }; ifRevision = 5 }) 'object_in_use' 5 'remove Canvas with routed channel'
}

function Invoke-Task14Cleanup {
    $clear = Send-Task14Request @{ op = 'request'; id = 'c-channel-clear'; method = 'canvas.setChannel'; params = @{ canvas = '2'; channel = 0; target = $null }; ifRevision = 5 }
    Assert-Ok $clear 6 'clear Canvas channel'
    Read-Task14Event 'canvas.channelChanged' 6 | Out-Null
    $removeScene = Send-Task14Request @{ op = 'request'; id = 'c-scene-remove'; method = 'scene.remove'; params = @{ scene = '3' }; ifRevision = 6 }
    Assert-Ok $removeScene 7 'remove private Scene'
    Read-Task14Event 'scene.removed' 7 | Out-Null
    $removeCanvas = Send-Task14Request @{ op = 'request'; id = 'c-remove'; method = 'canvas.remove'; params = @{ canvas = '2' }; ifRevision = 7 }
    Assert-Ok $removeCanvas 8 'remove private Canvas'
    Read-Task14Event 'canvas.removed' 8 | Out-Null
    Assert-Error (Send-Task14Request @{ op = 'request'; id = 'c-main-remove'; method = 'canvas.remove'; params = @{ canvas = '1' }; ifRevision = 8 }) 'invalid_state' 8 'remove Main Canvas'
    $close = Send-Task14Request @{ op = 'request'; id = 'c-close'; method = 'session.close'; ifRevision = 8 }
    Assert-Ok $close 9 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task14Engine
    Write-Output 'Task 14 canvas integration: PASS'
}

function Invoke-Task14Scenario {
    Invoke-Task14Bootstrap
    Invoke-Task14CanvasSetup
    Invoke-Task14CanvasOperations
    Invoke-Task14Cleanup
}

try {
    Invoke-Task14Scenario
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:LastMessage) { Write-Error ("last protocol message: " + ($script:LastMessage | ConvertTo-Json -Compress -Depth 50)) }
    if ($null -ne $script:ErrorTask) { Write-Error ("engine stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

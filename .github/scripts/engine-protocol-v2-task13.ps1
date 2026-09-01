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

function Fail-Task13([string] $Message) {
    throw "Task 13: $Message"
}

function Start-Task13Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } |
        Select-Object -First 1
    if ($null -eq $engine) { $engine = Get-ChildItem -LiteralPath $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1 }
    if ($null -eq $engine) { Fail-Task13 'obs-engine.exe was not found.' }
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
    if (-not $script:Process.Start()) { Fail-Task13 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Stop-Task13Engine {
    if ($null -eq $script:Process) { return }
    if (-not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:ErrorTask) {
        $stderr = $script:ErrorTask.GetAwaiter().GetResult()
        if ($stderr -match 'Attempted to add Scene without specifying a canvas') { Fail-Task13 'fallback canvas warning was emitted.' }
    }
    if ($script:Process.ExitCode -ne 0) { Fail-Task13 "engine exited with $($script:Process.ExitCode)." }
}

function Read-Task13Message {
    $read = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task13 'timed out waiting for stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task13 'engine stdout closed unexpectedly.' }
    $message = $line | ConvertFrom-Json
    $op = if ($null -ne $message.PSObject.Properties['op']) { [string]$message.op } else { '' }
    $id = if ($null -ne $message.PSObject.Properties['id']) { [string]$message.id } else { '' }
    $event = if ($null -ne $message.PSObject.Properties['event']) { [string]$message.event } else { '' }
    $seq = if ($null -ne $message.PSObject.Properties['seq']) { [uint64]$message.seq } else { [uint64]0 }
    $script:Wire.Add([pscustomobject]@{ Index = $script:Wire.Count; Op = $op; Id = $id; Event = $event; Seq = $seq })
    return $message
}

function Send-Task13Request([hashtable] $Request) {
    $script:LastRequest = [string]$Request.id
    $script:Process.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50))
    $script:Process.StandardInput.Flush()
    while ($true) {
        $message = Read-Task13Message
        if ($message.op -eq 'event') { $script:Events.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task13 "wrong response for $($Request.id)." }
        $script:LastResponseIndex = $script:Wire.Count - 1
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task13 "$Label failed at revision $($Response.revision)." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task13 "$Label did not return $Code at revision $Revision." }
}

function Read-Task13Event([string] $Name, [int64] $Revision) {
    if ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0) } else { $event = Read-Task13Message }
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name -or [uint64]$event.seq -ne $script:NextSequence -or [int64]$event.revision -ne $Revision) {
        Fail-Task13 "unexpected event; expected $Name at revision $Revision."
    }
    $script:NextSequence++
    $wireEvent = @($script:Wire | Where-Object { $_.Seq -eq [uint64]$event.seq }) | Select-Object -First 1
    if ($null -eq $wireEvent -or $wireEvent.Index -le $script:LastResponseIndex) { Fail-Task13 "event $Name preceded its response." }
    return $event
}

try {
    Start-Task13Engine $InstallRoot
    $ready = Read-Task13Message
    if ($ready.event -ne 'ready') { Fail-Task13 'ready marker was not received.' }
    Assert-Ok (Send-Task13Request @{ op = 'request'; id = 'i-hello'; method = 'session.hello' }) 0 'hello'
    Assert-Ok (Send-Task13Request @{ op = 'request'; id = 'i-sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'scene.*' }, @{ pattern = 'item.*' }) } }) 0 'subscribe'

    $scene = Send-Task13Request @{ op = 'request'; id = 'i-scene'; method = 'scene.create' }
    Assert-Ok $scene 1 'scene.create'
    if ([string]$scene.data.scene -ne '2') { Fail-Task13 'unexpected Scene handle.' }
    Read-Task13Event 'scene.created' 1 | Out-Null

    $source = Send-Task13Request @{ op = 'request'; id = 'i-source'; method = 'source.create'; params = @{ kind = 'color_source_v3'; name = 'Task13 Color'; settings = @{ width = 320; height = 180 } } }
    Assert-Ok $source 2 'source.create'
    $sourceHandle = [string]$source.data.source

    $item = Send-Task13Request @{ op = 'request'; id = 'i-item'; method = 'item.create'; params = @{ scene = '2'; source = $sourceHandle }; ifRevision = 2 }
    Assert-Ok $item 3 'item.create'
    $itemHandle = [string]$item.data.item
    if ($itemHandle -ne '4') { Fail-Task13 "unexpected Item handle $itemHandle." }
    Read-Task13Event 'item.created' 3 | Out-Null

    $got = Send-Task13Request @{ op = 'request'; id = 'i-get'; method = 'item.get'; params = @{ item = $itemHandle } }
    Assert-Ok $got 3 'item.get'
    if ([int]$got.data.order -ne 0 -or -not $got.data.visible -or $got.data.locked) { Fail-Task13 'initial Item state was incorrect.' }

    $transform = @{ position = @{ x = 40.0; y = 20.0 }; scale = @{ x = 1.5; y = 0.75 }; rotation = 12.0; alignment = 5; bounds = @{ type = 'none' }; crop = @{ left = 0; top = 0; right = 0; bottom = 0 }; cropToBounds = $false }
    $set = Send-Task13Request @{ op = 'request'; id = 'i-transform'; method = 'item.setTransform'; params = @{ item = $itemHandle; transform = $transform }; ifRevision = 3 }
    Assert-Ok $set 4 'item.setTransform'
    Read-Task13Event 'item.transformChanged' 4 | Out-Null
    if ([double]$set.data.transform.position.x -ne 40.0) { Fail-Task13 'canonical transform readback was incorrect.' }

    $invalid = Send-Task13Request @{ op = 'request'; id = 'i-invalid'; method = 'item.setTransform'; params = @{ item = $itemHandle; transform = @{ position = @{ x = 99.0 }; bounds = @{ type = 'invalid' } } }; ifRevision = 4 }
    Assert-Error $invalid 'bad_request' 4 'invalid compound transform'
    $unchanged = Send-Task13Request @{ op = 'request'; id = 'i-unchanged'; method = 'item.getTransform'; params = @{ item = $itemHandle } }
    Assert-Ok $unchanged 4 'getTransform after rejected update'
    if ([double]$unchanged.data.transform.position.x -ne 40.0) { Fail-Task13 'rejected compound update partially changed the Item.' }

    $commands = @(
        @{ id = 'i-pos'; method = 'item.setPosition'; params = @{ item = $itemHandle; position = @{ x = 50.0; y = 25.0 } } },
        @{ id = 'i-scale'; method = 'item.setScale'; params = @{ item = $itemHandle; scale = @{ x = 2.0; y = 2.0 } } },
        @{ id = 'i-rot'; method = 'item.setRotation'; params = @{ item = $itemHandle; rotation = 20.0 } },
        @{ id = 'i-align'; method = 'item.setAlignment'; params = @{ item = $itemHandle; alignment = 10 } },
        @{ id = 'i-bounds'; method = 'item.setBounds'; params = @{ item = $itemHandle; bounds = @{ type = 'stretch'; width = 160.0; height = 90.0 } } },
        @{ id = 'i-balign'; method = 'item.setBoundsAlignment'; params = @{ item = $itemHandle; alignment = 5 } },
        @{ id = 'i-crop'; method = 'item.setCrop'; params = @{ item = $itemHandle; crop = @{ left = 1; top = 1; right = 1; bottom = 1 } } },
        @{ id = 'i-ctb'; method = 'item.setCropToBounds'; params = @{ item = $itemHandle; cropToBounds = $true } },
        @{ id = 'i-visible'; method = 'item.setVisible'; params = @{ item = $itemHandle; visible = $false } },
        @{ id = 'i-locked'; method = 'item.setLocked'; params = @{ item = $itemHandle; locked = $true } },
        @{ id = 'i-filter'; method = 'item.setScaleFilter'; params = @{ item = $itemHandle; scaleFilter = 'point' } },
        @{ id = 'i-blend-mode'; method = 'item.setBlendMode'; params = @{ item = $itemHandle; blendMode = 'additive' } },
        @{ id = 'i-blend-method'; method = 'item.setBlendMethod'; params = @{ item = $itemHandle; blendMethod = 'srgbOff' } }
    )
    $revision = [int64]4
    foreach ($command in $commands) {
        $revision++
        $request = @{ op = 'request'; id = $command.id; method = $command.method; params = $command.params; ifRevision = $revision - 1 }
        $response = Send-Task13Request $request
        Assert-Ok $response $revision $command.method
        $eventName = if ($command.method -in @('item.setVisible')) { 'item.visibilityChanged' } elseif ($command.method -eq 'item.setLocked') { 'item.lockedChanged' } elseif ($command.method -in @('item.setBlendMode','item.setBlendMethod')) { 'item.blendChanged' } else { 'item.transformChanged' }
        Read-Task13Event $eventName $revision | Out-Null
    }

    $second = Send-Task13Request @{ op = 'request'; id = 'i-second'; method = 'item.create'; params = @{ scene = '2'; source = $sourceHandle }; ifRevision = $revision }
    $revision++
    Assert-Ok $second $revision 'second item.create'
    $secondHandle = [string]$second.data.item
    Read-Task13Event 'item.created' $revision | Out-Null

    $moveTop = Send-Task13Request @{ op = 'request'; id = 'i-top'; method = 'item.moveTop'; params = @{ item = $itemHandle }; ifRevision = $revision }
    $revision++
    Assert-Ok $moveTop $revision 'item.moveTop'
    Read-Task13Event 'item.orderChanged' $revision | Out-Null
    $moveBottom = Send-Task13Request @{ op = 'request'; id = 'i-bottom'; method = 'item.setOrder'; params = @{ item = $itemHandle; index = 0 }; ifRevision = $revision }
    $revision++
    Assert-Ok $moveBottom $revision 'item.setOrder'
    Read-Task13Event 'item.orderChanged' $revision | Out-Null

    $duplicate = Send-Task13Request @{ op = 'request'; id = 'i-duplicate'; method = 'item.duplicate'; params = @{ item = $itemHandle }; ifRevision = $revision }
    $revision++
    Assert-Ok $duplicate $revision 'item.duplicate'
    $duplicateHandle = [string]$duplicate.data.item
    if ($duplicateHandle -eq $itemHandle -or $duplicateHandle -eq $secondHandle) { Fail-Task13 'item.duplicate reused an Item handle.' }
    Read-Task13Event 'item.created' $revision | Out-Null

    $group = Send-Task13Request @{ op = 'request'; id = 'i-group'; method = 'item.createGroup'; params = @{ scene = '2'; name = 'Task13 Group'; items = @(@{ item = $itemHandle }, @{ item = $secondHandle }) }; ifRevision = $revision }
    $revision++
    Assert-Ok $group $revision 'item.createGroup'
    $groupHandle = [string]$group.data.item
    Read-Task13Event 'item.created' $revision | Out-Null
    Read-Task13Event 'scene.itemsChanged' $revision | Out-Null

    $children = Send-Task13Request @{ op = 'request'; id = 'i-children'; method = 'item.getChildren'; params = @{ item = $groupHandle } }
    Assert-Ok $children $revision 'item.getChildren'
    if ([int]$children.data.count -ne 2) { Fail-Task13 'group child enumeration was incorrect.' }

    $ungroup = Send-Task13Request @{ op = 'request'; id = 'i-ungroup'; method = 'item.ungroup'; params = @{ item = $groupHandle }; ifRevision = $revision }
    $revision++
    Assert-Ok $ungroup $revision 'item.ungroup'
    Read-Task13Event 'item.removed' $revision | Out-Null
    Read-Task13Event 'item.removed' $revision | Out-Null
    Read-Task13Event 'item.removed' $revision | Out-Null
    Read-Task13Event 'item.created' $revision | Out-Null
    Read-Task13Event 'item.created' $revision | Out-Null
    Read-Task13Event 'scene.itemsChanged' $revision | Out-Null

    $removeScene = Send-Task13Request @{ op = 'request'; id = 'i-remove-scene'; method = 'scene.remove'; params = @{ scene = '2' }; ifRevision = $revision }
    $revision++
    Assert-Ok $removeScene $revision 'scene.remove with Items'
    while ($script:Events.Count -gt 0) { $event = $script:Events[0]; $script:Events.RemoveAt(0); if ($event.op -ne 'event' -or [int64]$event.revision -ne $revision) { Fail-Task13 'scene removal event had the wrong revision.' }; if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task13 'scene removal event sequence was not monotonic.' }; $script:NextSequence++ }

    $close = Send-Task13Request @{ op = 'request'; id = 'i-close'; method = 'session.close'; ifRevision = $revision }
    Assert-Ok $close ($revision + 1) 'session.close'
    $script:Process.WaitForExit(30000) | Out-Null
    Stop-Task13Engine
    Write-Output 'Task 13 item integration: PASS'
} catch {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) { $script:Process.Kill(); $script:Process.WaitForExit() }
    if ($null -ne $script:Process) { Write-Host "last request: $($script:LastRequest); engine exit code: $($script:Process.ExitCode)" }
    if ($null -ne $script:ErrorTask) { Write-Host ("stderr: " + $script:ErrorTask.GetAwaiter().GetResult()) }
    throw
}

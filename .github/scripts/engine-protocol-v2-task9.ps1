param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'

$script:Process = $null
$script:ErrorTask = $null

function Start-Task9Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path $Root).Path
    $engine = Get-ChildItem -Path $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
    if ($null -eq $engine) {
        throw 'obs-engine.exe was not found in the installed runtime.'
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $engine.FullName
    $startInfo.WorkingDirectory = $engine.Directory.FullName
    $startInfo.Arguments = '--plugin=task9-interaction-source'
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    $script:Process = [System.Diagnostics.Process]::new()
    $script:Process.StartInfo = $startInfo
    if (-not $script:Process.Start()) {
        throw 'Failed to start obs-engine.exe.'
    }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()
}

function Read-EngineMessage {
    $ReadTask = $Process.StandardOutput.ReadLineAsync()
    if (-not $ReadTask.Wait(30000)) {
        throw 'Timed out waiting 30 seconds for obs-engine stdout.'
    }
    $Line = $ReadTask.Result
    if ($null -eq $Line) {
        $ExitText = if ($Process.HasExited) { "exit=$($Process.ExitCode)" } else { 'process still running' }
        throw "obs-engine closed stdout unexpectedly ($ExitText)."
    }
    Write-Host "obs-engine stdout: $Line"
    return ($Line | ConvertFrom-Json)
}

function Send-V2Request([hashtable] $Request) {
    $Json = $Request | ConvertTo-Json -Compress -Depth 50
    Write-Host "obs-engine stdin:  $Json"
    $Process.StandardInput.WriteLine($Json)
    $Process.StandardInput.Flush()
    $Response = Read-EngineMessage
    if ($Response.op -ne 'response' -or [string]$Response.id -ne [string]$Request.id) {
        throw "Expected response for '$($Request.id)' but received a different message."
    }
    return $Response
}

function Assert-OkAtRevision($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) {
        throw "$Label failed or returned revision=$($Response.revision), expected $Revision."
    }
}

function Assert-ErrorAtRevision($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) {
        throw "$Label did not return $Code at revision $Revision."
    }
}

function Assert-Task9Capabilities($Hello) {
    $capabilityNames = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($required in @(
        'interaction.v1',
        'interaction.focus.v1',
        'interaction.mouseMove.v1',
        'interaction.mouseButton.v1',
        'interaction.mouseWheel.v1',
        'interaction.key.v1',
        'interaction.text.v1',
        'interaction.reset.v1'
    )) {
        if ($capabilityNames -notcontains $required) {
            throw "Task 9 capability was not advertised: $required"
        }
    }
}

function Initialize-Task9Session {
    $ready = Read-EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        throw 'Migration bootstrap ready event changed unexpectedly.'
    }

    $hello = Send-V2Request @{ op = 'request'; id = 'task9.hello'; method = 'session.hello'; params = @{} }
    Assert-OkAtRevision $hello 0 'session.hello'
    Assert-Task9Capabilities $hello
}

function Invoke-Task9InitialInteraction {
    $create = Send-V2Request @{
        op = 'request'; id = 'task9.create'; method = 'source.create'; ifRevision = 0
        params = @{ kind = 'task9_interaction_source'; name = 'task9-interactive' }
    }
    Assert-OkAtRevision $create 1 'Task 9 interaction source.create'
    if ([string]$create.data.source -ne '1') {
        throw "Fresh Task 9 engine expected interaction source handle '1', got '$($create.data.source)'."
    }

    $focus = Send-V2Request @{
        op = 'request'; id = 'task9.focus'; method = 'interaction.focus'
        params = @{ source = '1'; focused = $true }
    }
    Assert-OkAtRevision $focus 1 'interaction.focus'

    $move = Send-V2Request @{
        op = 'request'; id = 'task9.move'; method = 'interaction.mouseMove'
        params = @{
            source = '1'; x = 100; y = 50; leave = $false
            modifiers = @{ shift = $true; mouseLeft = $true }
        }
    }
    Assert-OkAtRevision $move 1 'interaction.mouseMove'

    $button = Send-V2Request @{
        op = 'request'; id = 'task9.button'; method = 'interaction.mouseButton'
        params = @{
            source = '1'; x = 100; y = 50; button = 'left'; state = 'down'; clickCount = 1
            modifiers = @{ mouseLeft = $true }
        }
    }
    Assert-OkAtRevision $button 1 'interaction.mouseButton left down'

    $rightButton = Send-V2Request @{
        op = 'request'; id = 'task9.button-right'; method = 'interaction.mouseButton'
        params = @{
            source = '1'; x = 100; y = 50; button = 'right'; state = 'down'; clickCount = 1
            modifiers = @{ mouseLeft = $true; mouseRight = $true }
        }
    }
    Assert-OkAtRevision $rightButton 1 'interaction.mouseButton right down'

    # OBS wheel events carry keyboard modifiers, not mouse-button bits. Keeping
    # both buttons held across this wheel forces reset to reconstruct the button
    # mask from tracked state instead of trusting this latest modifier snapshot.
    $wheel = Send-V2Request @{
        op = 'request'; id = 'task9.wheel'; method = 'interaction.mouseWheel'
        params = @{
            source = '1'; x = 100; y = 50; deltaX = 0; deltaY = 120
            modifiers = @{ control = $true }
        }
    }
    Assert-OkAtRevision $wheel 1 'interaction.mouseWheel'

    $key = Send-V2Request @{
        op = 'request'; id = 'task9.key'; method = 'interaction.key'
        params = @{
            source = '1'; state = 'down'; text = 'a'; nativeModifiers = 0; nativeScanCode = 30; nativeVirtualKey = 65
            modifiers = @{ shift = $true }
        }
    }
    Assert-OkAtRevision $key 1 'interaction.key'

    $textRequest = Send-V2Request @{
        op = 'request'; id = 'task9.text'; method = 'interaction.text'
        params = @{ source = '1'; text = 'Hi'; modifiers = @{} }
    }
    Assert-OkAtRevision $textRequest 1 'interaction.text'

    $reset = Send-V2Request @{
        op = 'request'; id = 'task9.reset'; method = 'interaction.reset'; params = @{ source = '1' }
    }
    Assert-OkAtRevision $reset 1 'interaction.reset'
    if ([int]$reset.data.releasedButtons -ne 2 -or [int]$reset.data.releasedKeys -ne 1) {
        throw "interaction.reset releasedButtons=$($reset.data.releasedButtons), releasedKeys=$($reset.data.releasedKeys); expected 2 and 1."
    }
    return [int64]1
}

function Invoke-Task9PruneScenario([int64] $Revision) {
    # Seed live interaction state on source 1, then deliberately leave a large
    # held-key state behind on a removed source. The next interaction on source
    # 1 executes the bounded stale-state pruning path.
    $pruneSeed = Send-V2Request @{
        op = 'request'; id = 'task9.prune-seed'; method = 'interaction.focus'
        params = @{ source = '1'; focused = $true }
    }
    Assert-OkAtRevision $pruneSeed $Revision 'interaction prune seed'

    $tempCreate = Send-V2Request @{
        op = 'request'; id = 'task9.temp-create'; method = 'source.create'; ifRevision = $Revision
        params = @{ kind = 'task9_interaction_source'; name = 'task9-held-key-bound' }
    }
    Assert-OkAtRevision $tempCreate ($Revision + 1) 'held-key bound source.create'
    if ([string]$tempCreate.data.source -ne '2') {
        throw "Fresh Task 9 engine expected temporary interaction source handle '2', got '$($tempCreate.data.source)'."
    }

    for ($keyIndex = 1; $keyIndex -le 256; $keyIndex++) {
        $heldKey = Send-V2Request @{
            op = 'request'; id = "task9.held-key.$keyIndex"; method = 'interaction.key'
            params = @{
                source = '2'; state = 'down'; nativeModifiers = 0; nativeScanCode = 0; nativeVirtualKey = $keyIndex
                modifiers = @{}
            }
        }
        Assert-OkAtRevision $heldKey ($Revision + 1) "interaction.key held-key bound entry $keyIndex"
    }

    $heldKeyOverflow = Send-V2Request @{
        op = 'request'; id = 'task9.held-key.overflow'; method = 'interaction.key'
        params = @{
            source = '2'; state = 'down'; nativeModifiers = 0; nativeScanCode = 0; nativeVirtualKey = 257
            modifiers = @{}
        }
    }
    Assert-ErrorAtRevision $heldKeyOverflow 'busy' ($Revision + 1) 'interaction.key held-key overflow'

    $tempRemove = Send-V2Request @{
        op = 'request'; id = 'task9.temp-remove'; method = 'source.remove'; ifRevision = ($Revision + 1)
        params = @{ source = '2' }
    }
    Assert-OkAtRevision $tempRemove ($Revision + 2) 'remove held-key bound source'

    $pruneTrigger = Send-V2Request @{
        op = 'request'; id = 'task9.prune-trigger'; method = 'interaction.mouseMove'
        params = @{ source = '1'; x = 1; y = 1; leave = $false; modifiers = @{} }
    }
    Assert-OkAtRevision $pruneTrigger ($Revision + 2) 'interaction stale-state prune trigger'

    $pruneReset = Send-V2Request @{
        op = 'request'; id = 'task9.prune-reset'; method = 'interaction.reset'; params = @{ source = '1' }
    }
    Assert-OkAtRevision $pruneReset ($Revision + 2) 'interaction reset after stale-state prune'
    if ([int]$pruneReset.data.releasedButtons -ne 0 -or [int]$pruneReset.data.releasedKeys -ne 0) {
        throw 'Stale-source input state leaked into the live source reset state.'
    }
    return [int64]($Revision + 2)
}

function Invoke-Task9ValidationScenario([int64] $Revision) {
    $badCoordinates = Send-V2Request @{
        op = 'request'; id = 'task9.bad-coordinates'; method = 'interaction.mouseMove'
        params = @{ source = '1'; x = 5000; y = 5000; leave = $false }
    }
    Assert-ErrorAtRevision $badCoordinates 'bad_request' $Revision 'out-of-bounds interaction.mouseMove'

    $malformedHandle = Send-V2Request @{
        op = 'request'; id = 'task9.malformed-handle'; method = 'interaction.focus'
        params = @{ source = '01'; focused = $true }
    }
    Assert-ErrorAtRevision $malformedHandle 'bad_request' $Revision 'non-canonical interaction source handle'

    $missingHandle = Send-V2Request @{
        op = 'request'; id = 'task9.missing-handle'; method = 'interaction.focus'
        params = @{ source = '999'; focused = $true }
    }
    Assert-ErrorAtRevision $missingHandle 'not_found' $Revision 'missing interaction source handle'

    $badRevision = Send-V2Request @{
        op = 'request'; id = 'task9.bad-revision'; method = 'interaction.focus'; ifRevision = $Revision
        params = @{ source = '1'; focused = $true }
    }
    Assert-ErrorAtRevision $badRevision 'bad_request' $Revision 'interaction ifRevision guard'

    $kinds = Send-V2Request @{ op = 'request'; id = 'task9.kinds'; method = 'source.kindList'; params = @{} }
    Assert-OkAtRevision $kinds $Revision 'source.kindList after interactions'
    $colorKind = $kinds.data.kinds | Where-Object { $_.id -eq 'color_source_v3' } | Select-Object -First 1
    if ($null -eq $colorKind) {
        $colorKind = $kinds.data.kinds | Where-Object { $_.id -eq 'color_source' } | Select-Object -First 1
    }
    if ($null -eq $colorKind) {
        throw 'No Color Source kind was registered for unsupported-capability coverage.'
    }
    return $colorKind
}

function Invoke-Task9UnsupportedCapability([object] $ColorKind, [int64] $Revision) {
    $unsupportedCreate = Send-V2Request @{
        op = 'request'; id = 'task9.unsupported-create'; method = 'source.create'; ifRevision = $Revision
        params = @{ kind = [string]$ColorKind.id; name = 'task9-noninteractive'; settings = @{ width = 320; height = 180 } }
    }
    Assert-OkAtRevision $unsupportedCreate ($Revision + 1) 'non-interactive source.create'
    if ([string]$unsupportedCreate.data.source -ne '3') {
        throw "Fresh Task 9 engine expected non-interactive source handle '3', got '$($unsupportedCreate.data.source)'."
    }

    $unsupported = Send-V2Request @{
        op = 'request'; id = 'task9.unsupported'; method = 'interaction.focus'
        params = @{ source = '3'; focused = $true }
    }
    Assert-ErrorAtRevision $unsupported 'unsupported_capability' ($Revision + 1) 'non-interactive source interaction'

    $removeUnsupported = Send-V2Request @{
        op = 'request'; id = 'task9.remove-unsupported'; method = 'source.remove'; ifRevision = ($Revision + 1)
        params = @{ source = '3' }
    }
    Assert-OkAtRevision $removeUnsupported ($Revision + 2) 'remove non-interactive source'

    $removeInteractive = Send-V2Request @{
        op = 'request'; id = 'task9.remove-interactive'; method = 'source.remove'; ifRevision = ($Revision + 2)
        params = @{ source = '1' }
    }
    Assert-OkAtRevision $removeInteractive ($Revision + 3) 'remove interaction source'
    return [int64]($Revision + 3)
}

function Assert-Task9ExpectedLogs([string] $Stderr) {
    $expectedLogs = @(
        '[task9-interaction] focus focused=1',
        '[task9-interaction] mouseMove x=100 y=50 modifiers=18 leave=0',
        '[task9-interaction] mouseButton x=100 y=50 modifiers=16 button=0 up=0 clickCount=1',
        '[task9-interaction] mouseButton x=100 y=50 modifiers=80 button=2 up=0 clickCount=1',
        '[task9-interaction] mouseWheel x=100 y=50 modifiers=4 deltaX=0 deltaY=120',
        '[task9-interaction] key up=0 modifiers=2 text=a nativeModifiers=0 nativeScanCode=30 nativeVirtualKey=65',
        '[task9-interaction] key up=0 modifiers=0 text=H nativeModifiers=0 nativeScanCode=0 nativeVirtualKey=0',
        '[task9-interaction] key up=1 modifiers=0 text=H nativeModifiers=0 nativeScanCode=0 nativeVirtualKey=0',
        '[task9-interaction] key up=0 modifiers=0 text=i nativeModifiers=0 nativeScanCode=0 nativeVirtualKey=0',
        '[task9-interaction] key up=1 modifiers=0 text=i nativeModifiers=0 nativeScanCode=0 nativeVirtualKey=0',
        '[task9-interaction] mouseButton x=100 y=50 modifiers=68 button=0 up=1 clickCount=1',
        '[task9-interaction] mouseButton x=100 y=50 modifiers=4 button=2 up=1 clickCount=1',
        '[task9-interaction] key up=1 modifiers=2 text=a nativeModifiers=0 nativeScanCode=30 nativeVirtualKey=65',
        '[task9-interaction] mouseMove x=100 y=50 modifiers=0 leave=1',
        '[task9-interaction] focus focused=0',
        '[task9-interaction] key up=0 modifiers=0 text= nativeModifiers=0 nativeScanCode=0 nativeVirtualKey=256',
        '[task9-interaction] mouseMove x=1 y=1 modifiers=0 leave=0'
    )
    foreach ($expected in $expectedLogs) {
        if (-not $Stderr.Contains($expected)) {
            throw "Task 9 source did not receive expected libobs callback: $expected"
        }
    }

    $overflowLog = '[task9-interaction] key up=0 modifiers=0 text= nativeModifiers=0 nativeScanCode=0 nativeVirtualKey=257'
    if ($Stderr.Contains($overflowLog)) {
        throw 'Held-key overflow was delivered to libobs even though interaction.key returned busy.'
    }
}

function Complete-Task9Session([int64] $Revision) {
    $close = Send-V2Request @{
        op = 'request'; id = 'task9.close'; method = 'session.close'; ifRevision = $Revision; params = @{}
    }
    Assert-OkAtRevision $close ($Revision + 1) 'session.close'

    $script:Process.StandardInput.Close()
    if (-not $script:Process.WaitForExit(30000)) {
        $script:Process.Kill($true)
        throw 'obs-engine did not exit within 30 seconds after session.close.'
    }
    if ($script:Process.ExitCode -ne 0) {
        throw "obs-engine exited with code $($script:Process.ExitCode)."
    }

    $stderr = $script:ErrorTask.Result
    Write-Host '=== obs-engine stderr ==='
    Write-Host $stderr
    Assert-Task9ExpectedLogs $stderr
    Write-Host 'Task 9 interaction namespace: PASS'
}

function Invoke-Task9Scenario {
    Initialize-Task9Session
    $revision = Invoke-Task9InitialInteraction
    $revision = Invoke-Task9PruneScenario $revision
    $colorKind = Invoke-Task9ValidationScenario $revision
    $revision = Invoke-Task9UnsupportedCapability $colorKind $revision
    Complete-Task9Session $revision
}

function Stop-Task9Engine {
    if ($null -eq $script:Process) {
        return
    }
    if (-not $script:Process.HasExited) {
        try { $script:Process.StandardInput.Close() } catch {}
        try { $script:Process.Kill($true) } catch {}
        try { $script:Process.WaitForExit(5000) | Out-Null } catch {}
    }
    $script:Process.Dispose()
    $script:Process = $null
}

try {
    Start-Task9Engine $InstallRoot
    Invoke-Task9Scenario
}
finally {
    Stop-Task9Engine
}

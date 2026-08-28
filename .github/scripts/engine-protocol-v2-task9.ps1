param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'

$InstallRoot = (Resolve-Path $InstallRoot).Path
$Engine = Get-ChildItem -Path $InstallRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
if ($null -eq $Engine) {
    throw 'obs-engine.exe was not found in the installed runtime.'
}

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Engine.FullName
$StartInfo.WorkingDirectory = $Engine.Directory.FullName
$StartInfo.Arguments = '--plugin=task9-interaction-source'
$StartInfo.UseShellExecute = $false
$StartInfo.RedirectStandardInput = $true
$StartInfo.RedirectStandardOutput = $true
$StartInfo.RedirectStandardError = $true
$StartInfo.CreateNoWindow = $true

$Process = [System.Diagnostics.Process]::new()
$Process.StartInfo = $StartInfo
if (-not $Process.Start()) {
    throw 'Failed to start obs-engine.exe.'
}
$ErrorTask = $Process.StandardError.ReadToEndAsync()

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

try {
    $Ready = Read-EngineMessage
    if ($Ready.event -ne 'ready' -or [int]$Ready.protocol -ne 1) {
        throw 'Migration bootstrap ready event changed unexpectedly.'
    }

    $Hello = Send-V2Request @{ op = 'request'; id = 'task9.hello'; method = 'session.hello'; params = @{} }
    Assert-OkAtRevision $Hello 0 'session.hello'
    $CapabilityNames = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($Required in @(
        'interaction.v1',
        'interaction.focus.v1',
        'interaction.mouseMove.v1',
        'interaction.mouseButton.v1',
        'interaction.mouseWheel.v1',
        'interaction.key.v1',
        'interaction.text.v1',
        'interaction.reset.v1'
    )) {
        if ($CapabilityNames -notcontains $Required) {
            throw "Task 9 capability was not advertised: $Required"
        }
    }

    $Create = Send-V2Request @{
        op = 'request'; id = 'task9.create'; method = 'source.create'; ifRevision = 0
        params = @{ kind = 'task9_interaction_source'; name = 'task9-interactive' }
    }
    Assert-OkAtRevision $Create 1 'Task 9 interaction source.create'
    if ([string]$Create.data.source -ne '1') {
        throw "Fresh Task 9 engine expected interaction source handle '1', got '$($Create.data.source)'."
    }

    $Focus = Send-V2Request @{
        op = 'request'; id = 'task9.focus'; method = 'interaction.focus'
        params = @{ source = '1'; focused = $true }
    }
    Assert-OkAtRevision $Focus 1 'interaction.focus'

    $Move = Send-V2Request @{
        op = 'request'; id = 'task9.move'; method = 'interaction.mouseMove'
        params = @{
            source = '1'; x = 100; y = 50; leave = $false
            modifiers = @{ shift = $true; mouseLeft = $true }
        }
    }
    Assert-OkAtRevision $Move 1 'interaction.mouseMove'

    $Button = Send-V2Request @{
        op = 'request'; id = 'task9.button'; method = 'interaction.mouseButton'
        params = @{
            source = '1'; x = 100; y = 50; button = 'left'; state = 'down'; clickCount = 1
            modifiers = @{ mouseLeft = $true }
        }
    }
    Assert-OkAtRevision $Button 1 'interaction.mouseButton left down'

    $RightButton = Send-V2Request @{
        op = 'request'; id = 'task9.button-right'; method = 'interaction.mouseButton'
        params = @{
            source = '1'; x = 100; y = 50; button = 'right'; state = 'down'; clickCount = 1
            modifiers = @{ mouseLeft = $true; mouseRight = $true }
        }
    }
    Assert-OkAtRevision $RightButton 1 'interaction.mouseButton right down'

    # OBS wheel events carry keyboard modifiers, not mouse-button bits. Keeping
    # both buttons held across this wheel forces reset to reconstruct the button
    # mask from tracked state instead of trusting this latest modifier snapshot.
    $Wheel = Send-V2Request @{
        op = 'request'; id = 'task9.wheel'; method = 'interaction.mouseWheel'
        params = @{
            source = '1'; x = 100; y = 50; deltaX = 0; deltaY = 120
            modifiers = @{ control = $true }
        }
    }
    Assert-OkAtRevision $Wheel 1 'interaction.mouseWheel'

    $Key = Send-V2Request @{
        op = 'request'; id = 'task9.key'; method = 'interaction.key'
        params = @{
            source = '1'; state = 'down'; text = 'a'; nativeModifiers = 0; nativeScanCode = 30; nativeVirtualKey = 65
            modifiers = @{ shift = $true }
        }
    }
    Assert-OkAtRevision $Key 1 'interaction.key'

    $Text = Send-V2Request @{
        op = 'request'; id = 'task9.text'; method = 'interaction.text'
        params = @{ source = '1'; text = 'Hi'; modifiers = @{} }
    }
    Assert-OkAtRevision $Text 1 'interaction.text'

    $Reset = Send-V2Request @{
        op = 'request'; id = 'task9.reset'; method = 'interaction.reset'; params = @{ source = '1' }
    }
    Assert-OkAtRevision $Reset 1 'interaction.reset'
    if ([int]$Reset.data.releasedButtons -ne 2 -or [int]$Reset.data.releasedKeys -ne 1) {
        throw "interaction.reset releasedButtons=$($Reset.data.releasedButtons), releasedKeys=$($Reset.data.releasedKeys); expected 2 and 1."
    }

    # Seed live interaction state on source 1, then deliberately leave a large
    # held-key state behind on a removed source. The next interaction on source
    # 1 executes the bounded stale-state pruning path.
    $PruneSeed = Send-V2Request @{
        op = 'request'; id = 'task9.prune-seed'; method = 'interaction.focus'
        params = @{ source = '1'; focused = $true }
    }
    Assert-OkAtRevision $PruneSeed 1 'interaction prune seed'

    $TempCreate = Send-V2Request @{
        op = 'request'; id = 'task9.temp-create'; method = 'source.create'; ifRevision = 1
        params = @{ kind = 'task9_interaction_source'; name = 'task9-held-key-bound' }
    }
    Assert-OkAtRevision $TempCreate 2 'held-key bound source.create'
    if ([string]$TempCreate.data.source -ne '2') {
        throw "Fresh Task 9 engine expected temporary interaction source handle '2', got '$($TempCreate.data.source)'."
    }

    for ($KeyIndex = 1; $KeyIndex -le 256; $KeyIndex++) {
        $HeldKey = Send-V2Request @{
            op = 'request'; id = "task9.held-key.$KeyIndex"; method = 'interaction.key'
            params = @{
                source = '2'; state = 'down'; nativeModifiers = 0; nativeScanCode = 0; nativeVirtualKey = $KeyIndex
                modifiers = @{}
            }
        }
        Assert-OkAtRevision $HeldKey 2 "interaction.key held-key bound entry $KeyIndex"
    }

    $HeldKeyOverflow = Send-V2Request @{
        op = 'request'; id = 'task9.held-key.overflow'; method = 'interaction.key'
        params = @{
            source = '2'; state = 'down'; nativeModifiers = 0; nativeScanCode = 0; nativeVirtualKey = 257
            modifiers = @{}
        }
    }
    Assert-ErrorAtRevision $HeldKeyOverflow 'busy' 2 'interaction.key held-key overflow'

    $TempRemove = Send-V2Request @{
        op = 'request'; id = 'task9.temp-remove'; method = 'source.remove'; ifRevision = 2
        params = @{ source = '2' }
    }
    Assert-OkAtRevision $TempRemove 3 'remove held-key bound source'

    $PruneTrigger = Send-V2Request @{
        op = 'request'; id = 'task9.prune-trigger'; method = 'interaction.mouseMove'
        params = @{ source = '1'; x = 1; y = 1; leave = $false; modifiers = @{} }
    }
    Assert-OkAtRevision $PruneTrigger 3 'interaction stale-state prune trigger'

    $PruneReset = Send-V2Request @{
        op = 'request'; id = 'task9.prune-reset'; method = 'interaction.reset'; params = @{ source = '1' }
    }
    Assert-OkAtRevision $PruneReset 3 'interaction reset after stale-state prune'
    if ([int]$PruneReset.data.releasedButtons -ne 0 -or [int]$PruneReset.data.releasedKeys -ne 0) {
        throw 'Stale-source input state leaked into the live source reset state.'
    }

    $BadCoordinates = Send-V2Request @{
        op = 'request'; id = 'task9.bad-coordinates'; method = 'interaction.mouseMove'
        params = @{ source = '1'; x = 5000; y = 5000; leave = $false }
    }
    Assert-ErrorAtRevision $BadCoordinates 'bad_request' 3 'out-of-bounds interaction.mouseMove'

    $MalformedHandle = Send-V2Request @{
        op = 'request'; id = 'task9.malformed-handle'; method = 'interaction.focus'
        params = @{ source = '01'; focused = $true }
    }
    Assert-ErrorAtRevision $MalformedHandle 'bad_request' 3 'non-canonical interaction source handle'

    $MissingHandle = Send-V2Request @{
        op = 'request'; id = 'task9.missing-handle'; method = 'interaction.focus'
        params = @{ source = '999'; focused = $true }
    }
    Assert-ErrorAtRevision $MissingHandle 'not_found' 3 'missing interaction source handle'

    $BadRevision = Send-V2Request @{
        op = 'request'; id = 'task9.bad-revision'; method = 'interaction.focus'; ifRevision = 3
        params = @{ source = '1'; focused = $true }
    }
    Assert-ErrorAtRevision $BadRevision 'bad_request' 3 'interaction ifRevision guard'

    $Kinds = Send-V2Request @{ op = 'request'; id = 'task9.kinds'; method = 'source.kindList'; params = @{} }
    Assert-OkAtRevision $Kinds 3 'source.kindList after interactions'
    $ColorKind = $Kinds.data.kinds | Where-Object { $_.id -eq 'color_source_v3' } | Select-Object -First 1
    if ($null -eq $ColorKind) {
        $ColorKind = $Kinds.data.kinds | Where-Object { $_.id -eq 'color_source' } | Select-Object -First 1
    }
    if ($null -eq $ColorKind) {
        throw 'No Color Source kind was registered for unsupported-capability coverage.'
    }

    $UnsupportedCreate = Send-V2Request @{
        op = 'request'; id = 'task9.unsupported-create'; method = 'source.create'; ifRevision = 3
        params = @{ kind = [string]$ColorKind.id; name = 'task9-noninteractive'; settings = @{ width = 320; height = 180 } }
    }
    Assert-OkAtRevision $UnsupportedCreate 4 'non-interactive source.create'
    if ([string]$UnsupportedCreate.data.source -ne '3') {
        throw "Fresh Task 9 engine expected non-interactive source handle '3', got '$($UnsupportedCreate.data.source)'."
    }

    $Unsupported = Send-V2Request @{
        op = 'request'; id = 'task9.unsupported'; method = 'interaction.focus'
        params = @{ source = '3'; focused = $true }
    }
    Assert-ErrorAtRevision $Unsupported 'unsupported_capability' 4 'non-interactive source interaction'

    $RemoveUnsupported = Send-V2Request @{
        op = 'request'; id = 'task9.remove-unsupported'; method = 'source.remove'; ifRevision = 4
        params = @{ source = '3' }
    }
    Assert-OkAtRevision $RemoveUnsupported 5 'remove non-interactive source'

    $RemoveInteractive = Send-V2Request @{
        op = 'request'; id = 'task9.remove-interactive'; method = 'source.remove'; ifRevision = 5
        params = @{ source = '1' }
    }
    Assert-OkAtRevision $RemoveInteractive 6 'remove interaction source'

    $Close = Send-V2Request @{
        op = 'request'; id = 'task9.close'; method = 'session.close'; ifRevision = 6; params = @{}
    }
    Assert-OkAtRevision $Close 7 'session.close'

    $Process.StandardInput.Close()
    if (-not $Process.WaitForExit(30000)) {
        $Process.Kill($true)
        throw 'obs-engine did not exit within 30 seconds after session.close.'
    }
    if ($Process.ExitCode -ne 0) {
        throw "obs-engine exited with code $($Process.ExitCode)."
    }

    $Stderr = $ErrorTask.Result
    Write-Host '=== obs-engine stderr ==='
    Write-Host $Stderr

    $ExpectedLogs = @(
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
    foreach ($Expected in $ExpectedLogs) {
        if (-not $Stderr.Contains($Expected)) {
            throw "Task 9 source did not receive expected libobs callback: $Expected"
        }
    }

    $OverflowLog = '[task9-interaction] key up=0 modifiers=0 text= nativeModifiers=0 nativeScanCode=0 nativeVirtualKey=257'
    if ($Stderr.Contains($OverflowLog)) {
        throw 'Held-key overflow was delivered to libobs even though interaction.key returned busy.'
    }

    Write-Host 'Task 9 interaction namespace: PASS'
}
finally {
    if (-not $Process.HasExited) {
        try { $Process.StandardInput.Close() } catch {}
        try { $Process.Kill($true) } catch {}
        try { $Process.WaitForExit(5000) | Out-Null } catch {}
    }
    $Process.Dispose()
}

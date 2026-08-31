param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Process = $null
$script:ErrorTask = $null
$script:NextSeq = [uint64]1
$script:CaseName = ''

function Fail([string] $Message) {
    throw "[$script:CaseName] $Message"
}

function Read-EngineMessage {
    $readTask = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $readTask.Wait(30000)) {
        Fail 'Timed out waiting for obs-engine stdout.'
    }
    $line = $readTask.Result
    if ($null -eq $line) {
        $exitText = if ($script:Process.HasExited) { "exit=$($script:Process.ExitCode)" } else { 'process still running' }
        Fail "obs-engine closed stdout unexpectedly ($exitText)."
    }
    Write-Host "[$script:CaseName] stdout: $line"
    return ($line | ConvertFrom-Json)
}

function Send-V2Request([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 50
    Write-Host "[$script:CaseName] stdin:  $json"
    $script:Process.StandardInput.WriteLine($json)
    $script:Process.StandardInput.Flush()
    $response = Read-EngineMessage
    if ($response.op -ne 'response' -or [string]$response.id -ne [string]$Request.id) {
        Fail "Expected response '$($Request.id)' but received a different message."
    }
    return $response
}

function Assert-Ok($Response, [int64] $Revision) {
    if (-not $Response.status.ok) {
        Fail "Request '$($Response.id)' failed: $($Response.status.code) $($Response.status.message)"
    }
    if ([int64]$Response.revision -ne $Revision) {
        Fail "Request '$($Response.id)' revision=$($Response.revision), expected $Revision."
    }
}

function Read-StateEvent([string] $Name, [int64] $Revision, [string] $Source = '') {
    $event = Read-EngineMessage
    if ($event.op -ne 'event' -or [string]$event.event -ne $Name) {
        Fail "Expected event '$Name' but received '$($event.event)'."
    }
    if ([uint64]$event.seq -ne $script:NextSeq) {
        Fail "Event '$Name' seq=$($event.seq), expected $script:NextSeq."
    }
    if ([int64]$event.revision -ne $Revision) {
        Fail "Event '$Name' revision=$($event.revision), expected $Revision."
    }
    if ($Source -and [string]$event.data.source -ne $Source) {
        Fail "Event '$Name' source=$($event.data.source), expected $Source."
    }
    $script:NextSeq++
    return $event
}

function Start-EngineCase([string] $Name) {
    $script:CaseName = $Name
    $script:NextSeq = [uint64]1

    $engine = Get-ChildItem -Path $InstallRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
    if ($null -eq $engine) {
        Fail 'obs-engine.exe was not found.'
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $engine.FullName
    $startInfo.WorkingDirectory = $engine.Directory.FullName
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true
    $startInfo.ArgumentList.Add('--plugin=task8-concurrency-source')

    $script:Process = [System.Diagnostics.Process]::new()
    $script:Process.StartInfo = $startInfo
    if (-not $script:Process.Start()) {
        Fail 'Failed to start obs-engine.exe.'
    }
    $script:ErrorTask = $script:Process.StandardError.ReadToEndAsync()

    $ready = Read-EngineMessage
    if ($ready.event -ne 'ready') {
        Fail 'Engine did not emit the ready bootstrap event.'
    }

    $hello = Send-V2Request @{
        op = 'request'; id = "$Name.hello"; method = 'session.hello'; params = @{}
    }
    Assert-Ok $hello 0

    $subscribe = Send-V2Request @{
        op = 'request'; id = "$Name.subscribe"; method = 'session.subscribe'
        params = @{ subscriptions = @(@{ pattern = 'source.*' }, @{ pattern = 'session.*' }) }
    }
    Assert-Ok $subscribe 0

    $kinds = Send-V2Request @{
        op = 'request'; id = "$Name.kinds"; method = 'source.kindList'; params = @{}
    }
    Assert-Ok $kinds 0
    $ids = @($kinds.data.kinds | ForEach-Object { [string]$_.id })
    if ($ids -notcontains 'task8_concurrency_video' -or $ids -notcontains 'task8_concurrency_peer') {
        Fail 'CI-only deterministic concurrency source kinds were not registered.'
    }
}

function New-TestSource([string] $Kind, [string] $Name, [string] $Label, [int64] $Revision) {
    $next = $Revision + 1
    $response = Send-V2Request @{
        op = 'request'; id = "$script:CaseName.create.$Label"; method = 'source.create'; ifRevision = $Revision
        params = @{
            kind = $Kind
            name = $Name
            settings = @{ label = $Label; width = 320; height = 180; marker = 0 }
        }
    }
    Assert-Ok $response $next
    $handle = [string]$response.data.source
    if ($handle -notmatch '^[1-9][0-9]*$') {
        Fail "Created source '$Label' returned non-canonical handle '$handle'."
    }
    $null = Read-StateEvent 'source.created' $next $handle
    return [pscustomobject]@{ Handle = $handle; Revision = $next }
}

function Assert-AEvents([string] $Handle, [int64] $Revision) {
    $settings = Read-StateEvent 'source.settingsChanged' $Revision $Handle
    $dimensions = Read-StateEvent 'source.dimensionsChanged' $Revision $Handle
    if ([int]$dimensions.data.width -ne 640 -or [int]$dimensions.data.height -ne 360) {
        Fail "A dimensions were $($dimensions.data.width)x$($dimensions.data.height), expected 640x360."
    }
    if ([int]$settings.data.settings.width -ne 640 -or [int]$settings.data.settings.height -ne 360) {
        Fail 'A settings event did not contain the canonical 640x360 settings.'
    }
}

function Assert-Ping([int64] $Revision) {
    $ping = Send-V2Request @{
        op = 'request'; id = "$script:CaseName.ping"; method = 'session.ping'; params = @{}
    }
    Assert-Ok $ping $Revision
}

function Finish-EngineCase([int64] $Revision) {
    $close = Send-V2Request @{
        op = 'request'; id = "$script:CaseName.close"; method = 'session.close'; ifRevision = $Revision; params = @{}
    }
    Assert-Ok $close ($Revision + 1)
    $script:Process.StandardInput.Close()
    if (-not $script:Process.WaitForExit(30000)) {
        Fail 'Engine did not exit after session.close.'
    }
    $stderr = $script:ErrorTask.GetAwaiter().GetResult()
    if ($script:Process.ExitCode -ne 0) {
        Fail "Engine exited with code $($script:Process.ExitCode).`n$stderr"
    }
    if ($stderr) {
        Write-Host "[$script:CaseName] stderr:`n$stderr"
    }
    $script:Process.Dispose()
    $script:Process = $null
    $script:ErrorTask = $null
    Write-Host "[$script:CaseName] PASS" -ForegroundColor Green
}

function Run-CaseA {
    Start-EngineCase 'A'
    $a = New-TestSource 'task8_concurrency_video' 'A-video' 'A' 0
    $replace = Send-V2Request @{
        op = 'request'; id = 'A.replace'; method = 'source.replaceSettings'; ifRevision = 1
        params = @{ source = $a.Handle; settings = @{ label = 'A'; scenario = 'A'; width = 640; height = 360; marker = 0 } }
    }
    Assert-Ok $replace 2
    Assert-AEvents $a.Handle 2
    Assert-Ping 2
    Finish-EngineCase 2
}

function Run-CaseB {
    Start-EngineCase 'B'
    $b = New-TestSource 'task8_concurrency_peer' 'B-peer' 'B' 0
    $a = New-TestSource 'task8_concurrency_video' 'A-video' 'A' 1
    $replace = Send-V2Request @{
        op = 'request'; id = 'B.replace'; method = 'source.replaceSettings'; ifRevision = 2
        params = @{ source = $a.Handle; settings = @{ label = 'A'; scenario = 'B'; width = 640; height = 360; marker = 0 } }
    }
    Assert-Ok $replace 3
    Assert-AEvents $a.Handle 3
    $peer = Read-StateEvent 'source.settingsChanged' 4 $b.Handle
    if ([int]$peer.data.settings.marker -ne 1) { Fail 'B peer marker was not updated.' }
    Assert-Ping 4
    Finish-EngineCase 4
}

function Run-CaseC {
    Start-EngineCase 'C'
    $b = New-TestSource 'task8_concurrency_peer' 'B-peer' 'B' 0
    $c = New-TestSource 'task8_concurrency_peer' 'C-peer' 'C' 1
    $a = New-TestSource 'task8_concurrency_video' 'A-video' 'A' 2
    $replace = Send-V2Request @{
        op = 'request'; id = 'C.replace'; method = 'source.replaceSettings'; ifRevision = 3
        params = @{ source = $a.Handle; settings = @{ label = 'A'; scenario = 'BC'; width = 640; height = 360; marker = 0 } }
    }
    Assert-Ok $replace 4
    Assert-AEvents $a.Handle 4
    $null = Read-StateEvent 'source.settingsChanged' 5 $b.Handle
    $null = Read-StateEvent 'source.settingsChanged' 6 $c.Handle
    Assert-Ping 6
    Finish-EngineCase 6
}

function Run-CaseD {
    Start-EngineCase 'D'
    $b = New-TestSource 'task8_concurrency_peer' 'B-peer' 'B' 0
    $a = New-TestSource 'task8_concurrency_video' 'A-video' 'A' 1

    $arm = Send-V2Request @{
        op = 'request'; id = 'D.arm'; method = 'source.replaceSettings'; ifRevision = 2
        params = @{ source = $b.Handle; settings = @{ label = 'B'; scenario = 'ARM_DESTROY'; width = 320; height = 180; marker = 0 } }
    }
    Assert-Ok $arm 3
    $null = Read-StateEvent 'source.settingsChanged' 3 $b.Handle

    $remove = Send-V2Request @{
        op = 'request'; id = 'D.removeA'; method = 'source.remove'; ifRevision = 3; params = @{ source = $a.Handle }
    }
    Assert-Ok $remove 4
    $null = Read-StateEvent 'source.removed' 4 $a.Handle
    $peer = Read-StateEvent 'source.settingsChanged' 5 $b.Handle
    if ([int]$peer.data.settings.marker -ne 1) { Fail 'B callback was lost while A was removed.' }
    Assert-Ping 5
    Finish-EngineCase 5
}

function Run-CaseE {
    Start-EngineCase 'E'
    $b = New-TestSource 'task8_concurrency_peer' 'B-peer' 'B' 0
    $a = New-TestSource 'task8_concurrency_video' 'A-video' 'A' 1

    $arm = Send-V2Request @{
        op = 'request'; id = 'E.arm'; method = 'source.replaceSettings'; ifRevision = 2
        params = @{ source = $b.Handle; settings = @{ label = 'B'; scenario = 'DELAYED'; delayMs = 250; width = 320; height = 180; marker = 0 } }
    }
    Assert-Ok $arm 3
    $null = Read-StateEvent 'source.settingsChanged' 3 $b.Handle

    $async = Read-StateEvent 'source.settingsChanged' 4 $b.Handle
    if ([int]$async.data.settings.marker -ne 1) { Fail 'Delayed unrelated B update did not commit revision 4.' }

    $stale = Send-V2Request @{
        op = 'request'; id = 'E.stale'; method = 'source.rename'; ifRevision = 3
        params = @{ source = $a.Handle; name = 'must-not-apply' }
    }
    if ($stale.status.ok -or [string]$stale.status.code -ne 'revision_conflict' -or [int64]$stale.revision -ne 4 -or
        [int64]$stale.status.details.actualRevision -ne 4) {
        Fail 'Stale guarded request was not rejected after unrelated B advanced the revision.'
    }
    Assert-Ping 4
    Finish-EngineCase 4
}

function Run-CaseF {
    Start-EngineCase 'F'
    $b = New-TestSource 'task8_concurrency_peer' 'B-peer' 'B' 0
    $a = New-TestSource 'task8_concurrency_video' 'A-video' 'A' 1

    $replace = Send-V2Request @{
        op = 'request'; id = 'F.overflow'; method = 'source.replaceSettings'; ifRevision = 2
        params = @{ source = $a.Handle; settings = @{ label = 'A'; scenario = 'OVERFLOW'; width = 640; height = 360; marker = 0 } }
    }
    Assert-Ok $replace 3

    $resync = Read-StateEvent 'session.resyncRequired' 4
    if ([string]$resync.data.reason -ne 'event_queue_overflow') {
        Fail "Unexpected resync reason '$($resync.data.reason)'."
    }
    Assert-Ping 4
    Finish-EngineCase 4
}

function Invoke-Task8Cases {
    Run-CaseA
    Run-CaseB
    Run-CaseC
    Run-CaseD
    Run-CaseE
    Run-CaseF
    Write-Host 'Task 8 deterministic concurrency A-F: PASS' -ForegroundColor Green
}

function Stop-Task8AfterFailure {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) {
        try { $script:Process.Kill($true) } catch {}
        try { $script:Process.WaitForExit(5000) | Out-Null } catch {}
    }
    if ($null -ne $script:ErrorTask) {
        try {
            $stderr = $script:ErrorTask.GetAwaiter().GetResult()
            if ($stderr) { Write-Host "[$script:CaseName] stderr:`n$stderr" }
        } catch {}
    }
}

try {
    Invoke-Task8Cases
}
catch {
    Stop-Task8AfterFailure
    throw
}

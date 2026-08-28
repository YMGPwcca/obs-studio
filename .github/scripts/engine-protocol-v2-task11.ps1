param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$InstallRoot = (Resolve-Path $InstallRoot).Path
$Engine = Get-ChildItem -Path $InstallRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
if ($null -eq $Engine) {
    throw 'obs-engine.exe was not found in the runtime root.'
}

$StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName = $Engine.FullName
$StartInfo.WorkingDirectory = $Engine.Directory.FullName
$StartInfo.ArgumentList.Add('--plugin=task11-filter-source')
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
$script:Events = [System.Collections.Generic.List[object]]::new()
$script:NextSeq = [uint64]1

function Fail([string] $Message) {
    throw "Task 11: $Message"
}

function Read-EngineMessage {
    $ReadTask = $Process.StandardOutput.ReadLineAsync()
    if (-not $ReadTask.Wait(30000)) {
        Fail 'timed out waiting 30 seconds for obs-engine stdout.'
    }
    $Line = $ReadTask.Result
    if ($null -eq $Line) {
        $ExitText = if ($Process.HasExited) { "exit=$($Process.ExitCode)" } else { 'process still running' }
        Fail "obs-engine closed stdout unexpectedly ($ExitText)."
    }
    Write-Host "stdout: $Line"
    return ($Line | ConvertFrom-Json)
}

function Send-V2Request([hashtable] $Request) {
    $Json = $Request | ConvertTo-Json -Compress -Depth 50
    Write-Host "stdin:  $Json"
    $Process.StandardInput.WriteLine($Json)
    $Process.StandardInput.Flush()

    while ($true) {
        $Message = Read-EngineMessage
        if ($Message.op -eq 'event') {
            $script:Events.Add($Message)
            continue
        }
        if ($Message.op -ne 'response' -or [string]$Message.id -ne [string]$Request.id) {
            Fail "expected response '$($Request.id)' but received a different message."
        }
        return $Message
    }
}

function Assert-Ok($Response, [int64] $ExpectedRevision, [string] $Label) {
    if (-not $Response.status.ok) {
        Fail "$Label failed: $($Response.status.code) $($Response.status.message)"
    }
    if ([int64]$Response.revision -ne $ExpectedRevision) {
        Fail "$Label returned revision=$($Response.revision), expected $ExpectedRevision."
    }
}

function Assert-Error($Response, [string] $Code, [int64] $ExpectedRevision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or
        [int64]$Response.revision -ne $ExpectedRevision) {
        Fail "$Label did not return $Code at revision $ExpectedRevision."
    }
}

function Read-Event([string] $Name, [int64] $ExpectedRevision, [string] $Filter = '', [string] $Source = '') {
    while ($true) {
        if ($script:Events.Count -gt 0) {
            $Event = $script:Events[0]
            $script:Events.RemoveAt(0)
        } else {
            $Event = Read-EngineMessage
        }
        if ($Event.op -ne 'event') {
            Fail "expected event '$Name' but received response '$($Event.id)'."
        }
        if ([string]$Event.event -ne $Name) {
            Fail "expected event '$Name' but received '$($Event.event)'."
        }
        if ([uint64]$Event.seq -ne $script:NextSeq) {
            Fail "event '$Name' seq=$($Event.seq), expected $script:NextSeq."
        }
        if ([int64]$Event.revision -ne $ExpectedRevision) {
            Fail "event '$Name' revision=$($Event.revision), expected $ExpectedRevision."
        }
        if ($Filter -and [string]$Event.data.filter -ne $Filter) {
            Fail "event '$Name' filter=$($Event.data.filter), expected $Filter."
        }
        if ($Source -and [string]$Event.data.source -ne $Source) {
            Fail "event '$Name' source=$($Event.data.source), expected $Source."
        }
        $script:NextSeq++
        return $Event
    }
}

function Assert-Order($Data, [string[]] $Expected, [string] $Label) {
    $Actual = @($Data.filters | ForEach-Object { [string]$_.filter })
    if ($Actual.Count -ne $Expected.Count) {
        Fail "$Label returned $($Actual.Count) filters, expected $($Expected.Count)."
    }
    for ($Index = 0; $Index -lt $Expected.Count; $Index++) {
        if ($Actual[$Index] -ne $Expected[$Index]) {
            Fail "$Label order[$Index]=$($Actual[$Index]), expected $($Expected[$Index])."
        }
    }
}

try {
    $Ready = Read-EngineMessage
    if ($Ready.event -ne 'ready' -or [int]$Ready.protocol -ne 1) {
        Fail 'migration bootstrap ready event changed unexpectedly.'
    }

    $Hello = Send-V2Request @{ op = 'request'; id = 'task11.hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $Hello 0 'session.hello'
    $Capabilities = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($Required in @(
        'filter.v1', 'filter.kindList.v1', 'filter.kindDefaults.v1', 'filter.kindProperties.v1',
        'filter.list.v1', 'filter.get.v1', 'filter.create.v1', 'filter.remove.v1',
        'filter.rename.v1', 'filter.duplicate.v1', 'filter.getSettings.v1',
        'filter.patchSettings.v1', 'filter.replaceSettings.v1', 'filter.setEnabled.v1',
        'filter.getEnabled.v1', 'filter.setOrder.v1', 'filter.moveUp.v1',
        'filter.moveDown.v1', 'filter.moveTop.v1', 'filter.moveBottom.v1'
    )) {
        if ($Capabilities -notcontains $Required) {
            Fail "missing capability: $Required"
        }
    }

    $Subscribe = Send-V2Request @{
        op = 'request'; id = 'task11.subscribe'; method = 'session.subscribe';
        params = @{ subscriptions = @(@{ pattern = 'filter.*' }, @{ pattern = 'source.*' }) }
    }
    Assert-Ok $Subscribe 0 'session.subscribe'

    $Kinds = Send-V2Request @{ op = 'request'; id = 'task11.kindList'; method = 'filter.kindList'; params = @{} }
    Assert-Ok $Kinds 0 'filter.kindList'
    $KindEntry = @($Kinds.data.kinds | Where-Object { [string]$_.id -eq 'task11_filter' })
    if ($KindEntry.Count -ne 1 -or [string]$KindEntry[0].id -ne 'task11_filter') {
        Fail 'deterministic Task 11 filter kind was not discovered through filter.kindList.'
    }

    $Defaults = Send-V2Request @{ op = 'request'; id = 'task11.defaults'; method = 'filter.kindDefaults'; params = @{ kind = 'task11_filter' } }
    Assert-Ok $Defaults 0 'filter.kindDefaults'
    if ([string]$Defaults.data.kind -ne 'task11_filter') {
        Fail 'filter.kindDefaults returned the wrong kind.'
    }

    $KindProperties = Send-V2Request @{ op = 'request'; id = 'task11.kindProperties'; method = 'filter.kindProperties'; params = @{ kind = 'task11_filter' } }
    Assert-Ok $KindProperties 0 'filter.kindProperties'
    if ([string]$KindProperties.data.target.type -ne 'filterKind') {
        Fail 'filter.kindProperties returned the wrong target type.'
    }

    $CreateSource = Send-V2Request @{
        op = 'request'; id = 'task11.source'; method = 'source.create'; ifRevision = 0
        params = @{ kind = 'task11_filter_source'; name = 'task11-parent' }
    }
    Assert-Ok $CreateSource 1 'source.create'
    if ([string]$CreateSource.data.source -ne '1') {
        Fail "fresh Task 11 engine expected source handle '1', got '$($CreateSource.data.source)'."
    }
    Read-Event 'source.created' 1 '' '1' | Out-Null

    $Empty = Send-V2Request @{ op = 'request'; id = 'task11.empty'; method = 'filter.list'; params = @{ source = '1' } }
    Assert-Ok $Empty 1 'empty filter.list'
    if ([int]$Empty.data.count -ne 0) {
        Fail 'new Task 11 parent unexpectedly contained filters.'
    }

    $Create = Send-V2Request @{
        op = 'request'; id = 'task11.create'; method = 'filter.create'; ifRevision = 1
        params = @{ source = '1'; kind = 'task11_filter'; name = 'first-filter'; settings = @{ value = 10 } }
    }
    Assert-Ok $Create 2 'filter.create'
    if ([string]$Create.data.filter -ne '2' -or [int]$Create.data.index -ne 0) {
        Fail 'first filter did not receive deterministic handle/index.'
    }
    Read-Event 'filter.created' 2 '2' '1' | Out-Null

    $Get = Send-V2Request @{ op = 'request'; id = 'task11.get'; method = 'filter.get'; params = @{ filter = '2' } }
    Assert-Ok $Get 2 'filter.get'
    if ([string]$Get.data.kind -ne 'task11_filter' -or [string]$Get.data.name -ne 'first-filter') {
        Fail 'filter.get returned an incorrect summary.'
    }

    $FilterProperties = Send-V2Request @{
        op = 'request'; id = 'task11.properties'; method = 'properties.get';
        params = @{ target = @{ type = 'filter'; filter = '2' } }
    }
    Assert-Ok $FilterProperties 2 'properties.get(filter)'
    if ([string]$FilterProperties.data.target.type -ne 'filter' -or
        [int64]$FilterProperties.data.settings.value -ne 10) {
        Fail 'generic property bridge did not resolve the live filter target.'
    }

    $Settings = Send-V2Request @{ op = 'request'; id = 'task11.settings'; method = 'filter.getSettings'; params = @{ filter = '2' } }
    Assert-Ok $Settings 2 'filter.getSettings'
    if ([int64]$Settings.data.settings.value -ne 10) {
        Fail 'filter.getSettings returned the wrong value.'
    }

    $Patch = Send-V2Request @{
        op = 'request'; id = 'task11.patch'; method = 'filter.patchSettings'; ifRevision = 2
        params = @{ filter = '2'; settings = @{ value = 20 } }
    }
    Assert-Ok $Patch 3 'filter.patchSettings'
    if ([int64]$Patch.data.settings.value -ne 20) {
        Fail 'filter.patchSettings did not return settled settings.'
    }
    Read-Event 'filter.settingsChanged' 3 '2' '1' | Out-Null

    $Replace = Send-V2Request @{
        op = 'request'; id = 'task11.replace'; method = 'filter.replaceSettings'; ifRevision = 3
        params = @{ filter = '2'; settings = @{ value = 30 } }
    }
    Assert-Ok $Replace 4 'filter.replaceSettings'
    if ([int64]$Replace.data.settings.value -ne 30) {
        Fail 'filter.replaceSettings did not return settled settings.'
    }
    Read-Event 'filter.settingsChanged' 4 '2' '1' | Out-Null

    $Disable = Send-V2Request @{
        op = 'request'; id = 'task11.disable'; method = 'filter.setEnabled'; ifRevision = 4
        params = @{ filter = '2'; enabled = $false }
    }
    Assert-Ok $Disable 5 'filter.setEnabled(false)'
    Read-Event 'filter.enabledChanged' 5 '2' '1' | Out-Null
    if ($Disable.data.enabled) {
        Fail 'filter.setEnabled(false) returned enabled=true.'
    }

    $Enabled = Send-V2Request @{ op = 'request'; id = 'task11.enabled'; method = 'filter.getEnabled'; params = @{ filter = '2' } }
    Assert-Ok $Enabled 5 'filter.getEnabled'
    if ($Enabled.data.enabled) {
        Fail 'filter.getEnabled returned enabled=true after disable.'
    }

    $Rename = Send-V2Request @{
        op = 'request'; id = 'task11.rename'; method = 'filter.rename'; ifRevision = 5
        params = @{ filter = '2'; name = 'renamed-filter' }
    }
    Assert-Ok $Rename 6 'filter.rename'
    Read-Event 'filter.renamed' 6 '2' '1' | Out-Null

    $Create3 = Send-V2Request @{
        op = 'request'; id = 'task11.create3'; method = 'filter.create'; ifRevision = 6
        params = @{ source = '1'; kind = 'task11_filter'; name = 'third-filter'; settings = @{ value = 3 } }
    }
    Assert-Ok $Create3 7 'second filter.create'
    if ([string]$Create3.data.filter -ne '3') { Fail 'second filter handle was not 3.' }
    Read-Event 'filter.created' 7 '3' '1' | Out-Null

    $Create4 = Send-V2Request @{
        op = 'request'; id = 'task11.create4'; method = 'filter.create'; ifRevision = 7
        params = @{ source = '1'; kind = 'task11_filter'; name = 'fourth-filter'; settings = @{ value = 4 } }
    }
    Assert-Ok $Create4 8 'third filter.create'
    if ([string]$Create4.data.filter -ne '4') { Fail 'third filter handle was not 4.' }
    Read-Event 'filter.created' 8 '4' '1' | Out-Null

    $List1 = Send-V2Request @{ op = 'request'; id = 'task11.list1'; method = 'filter.list'; params = @{ source = '1' } }
    Assert-Ok $List1 8 'filter.list after creates'
    Assert-Order $List1.data @('4', '3', '2') 'initial filter order'

    $SetOrder = Send-V2Request @{
        op = 'request'; id = 'task11.setOrder'; method = 'filter.setOrder'; ifRevision = 8
        params = @{ filter = '2'; index = 0 }
    }
    Assert-Ok $SetOrder 9 'filter.setOrder'
    Read-Event 'filter.orderChanged' 9 '' '1' | Out-Null
    Assert-Order $SetOrder.data @('2', '4', '3') 'setOrder result'

    $MoveUp = Send-V2Request @{ op = 'request'; id = 'task11.moveUp'; method = 'filter.moveUp'; ifRevision = 9; params = @{ filter = '2' } }
    Assert-Ok $MoveUp 10 'filter.moveUp'
    Read-Event 'filter.orderChanged' 10 '' '1' | Out-Null
    Assert-Order $MoveUp.data @('4', '2', '3') 'moveUp result'

    $MoveTop = Send-V2Request @{ op = 'request'; id = 'task11.moveTop'; method = 'filter.moveTop'; ifRevision = 10; params = @{ filter = '2' } }
    Assert-Ok $MoveTop 11 'filter.moveTop'
    Read-Event 'filter.orderChanged' 11 '' '1' | Out-Null
    Assert-Order $MoveTop.data @('4', '3', '2') 'moveTop result'

    $MoveBottom = Send-V2Request @{ op = 'request'; id = 'task11.moveBottom'; method = 'filter.moveBottom'; ifRevision = 11; params = @{ filter = '2' } }
    Assert-Ok $MoveBottom 12 'filter.moveBottom'
    Read-Event 'filter.orderChanged' 12 '' '1' | Out-Null
    Assert-Order $MoveBottom.data @('2', '4', '3') 'moveBottom result'

    $InvalidIndex = Send-V2Request @{
        op = 'request'; id = 'task11.invalidIndex'; method = 'filter.setOrder'; ifRevision = 12
        params = @{ filter = '2'; index = 99 }
    }
    Assert-Error $InvalidIndex 'bad_request' 12 'out-of-range filter index'

    $Stale = Send-V2Request @{
        op = 'request'; id = 'task11.stale'; method = 'filter.rename'; ifRevision = 11
        params = @{ filter = '2'; name = 'must-not-apply' }
    }
    Assert-Error $Stale 'revision_conflict' 12 'stale filter guard'

    $Duplicate = Send-V2Request @{
        op = 'request'; id = 'task11.duplicate'; method = 'filter.duplicate'; ifRevision = 12
        params = @{ filter = '2'; name = 'duplicated-filter' }
    }
    Assert-Ok $Duplicate 13 'filter.duplicate'
    if ([string]$Duplicate.data.filter -ne '5' -or [string]$Duplicate.data.duplicateOf -ne '2') {
        Fail 'filter.duplicate returned the wrong handle or duplicateOf.'
    }
    Read-Event 'filter.created' 13 '5' '1' | Out-Null

    $RemoveOne = Send-V2Request @{ op = 'request'; id = 'task11.remove'; method = 'filter.remove'; ifRevision = 13; params = @{ filter = '3' } }
    Assert-Ok $RemoveOne 14 'filter.remove'
    Read-Event 'filter.removed' 14 '3' '1' | Out-Null

    $SourceRemove = Send-V2Request @{ op = 'request'; id = 'task11.sourceRemove'; method = 'source.remove'; ifRevision = 14; params = @{ source = '1' } }
    Assert-Ok $SourceRemove 15 'source.remove with filters'
    Read-Event 'filter.removed' 15 '5' '1' | Out-Null
    Read-Event 'filter.removed' 15 '2' '1' | Out-Null
    Read-Event 'filter.removed' 15 '4' '1' | Out-Null
    Read-Event 'source.removed' 15 '' '1' | Out-Null

    $StaleFilter = Send-V2Request @{ op = 'request'; id = 'task11.staleFilter'; method = 'filter.get'; params = @{ filter = '2' } }
    Assert-Error $StaleFilter 'not_found' 15 'removed filter handle'

    $Close = Send-V2Request @{ op = 'request'; id = 'task11.close'; method = 'session.close'; ifRevision = 15; params = @{} }
    Assert-Ok $Close 16 'session.close'
    $Process.StandardInput.Close()
    if (-not $Process.WaitForExit(30000)) {
        Fail 'obs-engine did not exit after session.close.'
    }
    if ($Process.ExitCode -ne 0) {
        Fail "obs-engine exited with code $($Process.ExitCode)."
    }
} finally {
    if ($null -ne $Process -and -not $Process.HasExited) {
        $Process.Kill()
        $Process.WaitForExit()
    }
    if ($null -ne $ErrorTask) {
        $ErrorText = $ErrorTask.Result
        if ($ErrorText) {
            Write-Host "stderr: $ErrorText"
        }
    }
}

Write-Host 'Task 11 filter integration passed.'

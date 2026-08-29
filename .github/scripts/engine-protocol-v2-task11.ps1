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
$script:WireLog = [System.Collections.Generic.List[object]]::new()
$script:LastResponseWireIndex = -1
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
    $Message = $Line | ConvertFrom-Json
    $Op = if ($null -ne $Message.PSObject.Properties['op']) { [string]$Message.op } else { '' }
    $Id = if ($null -ne $Message.PSObject.Properties['id']) { [string]$Message.id } else { '' }
    $EventName = if ($null -ne $Message.PSObject.Properties['event']) { [string]$Message.event } else { '' }
    $Sequence = if ($null -ne $Message.PSObject.Properties['seq']) { [uint64]$Message.seq } else { [uint64]0 }
    $script:WireLog.Add([pscustomobject]@{
        Index = $script:WireLog.Count
        Op = $Op
        Id = $Id
        Event = $EventName
        Seq = $Sequence
    })
    return $Message
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
        $script:LastResponseWireIndex = $script:WireLog.Count - 1
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
        $WireEvent = @($script:WireLog | Where-Object { $_.Seq -eq [uint64]$Event.seq }) | Select-Object -First 1
        if ($null -eq $WireEvent -or $WireEvent.Index -le $script:LastResponseWireIndex) {
            Fail "command-owned event '$Name' was emitted before its response."
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

function Assert-NoQueuedEvents([string] $Label) {
    if ($script:Events.Count -ne 0) {
        $Names = @($script:Events | ForEach-Object { [string]$_.event }) -join ', '
        Fail "$Label unexpectedly left queued events: $Names"
    }
}

function Read-Until-Resync([int64] $MinimumRevision) {
    $script:LastResyncBatch = [System.Collections.Generic.List[object]]::new()
    while ($true) {
        if ($script:Events.Count -gt 0) {
            $Event = $script:Events[0]
            $script:Events.RemoveAt(0)
        } else {
            $Event = Read-EngineMessage
        }
        if ($Event.op -ne 'event') {
            Fail 'received a response while waiting for session.resyncRequired.'
        }
        if ([uint64]$Event.seq -ne $script:NextSeq) {
            Fail "resync wait saw seq=$($Event.seq), expected $script:NextSeq."
        }
        $script:NextSeq++
        $null = $script:LastResyncBatch.Add($Event)
        if ([string]$Event.event -eq 'session.resyncRequired') {
            if ([int64]$Event.revision -lt $MinimumRevision -or
                [string]$Event.data.reason -ne 'event_queue_overflow') {
                Fail 'session.resyncRequired had an invalid revision or reason.'
            }
            return $Event
        }
    }
}

function Assert-NoLateSettingsEvent([string] $Filter, [string] $Label) {
    $Late = @($script:LastResyncBatch | Where-Object {
        [string]$_.event -eq 'filter.settingsChanged' -and [string]$_.data.filter -eq $Filter
    })
    if ($Late.Count -ne 0) {
        Fail "$Label claimed a late filter.settingsChanged callback."
    }
}

function Read-SafeSettingsEvent([string] $Filter, [int64] $Value, [int64] $ExpectedRevision) {
    while ($true) {
        if ($script:Events.Count -gt 0) {
            $Event = $script:Events[0]
            $script:Events.RemoveAt(0)
        } else {
            $Event = Read-EngineMessage
        }
        if ($Event.op -ne 'event') {
            Fail 'safe-after-late received a response while waiting for its settings event.'
        }
        if ([uint64]$Event.seq -ne $script:NextSeq) {
            Fail "safe-after-late saw seq=$($Event.seq), expected $script:NextSeq."
        }
        $WireEvent = @($script:WireLog | Where-Object { $_.Seq -eq [uint64]$Event.seq }) | Select-Object -First 1
        if ($null -ne $WireEvent -and $WireEvent.Index -le $script:LastResponseWireIndex -and
            [string]$Event.event -ne 'session.resyncRequired') {
            Fail "safe-after-late observed a normal event before its response: $($Event.event)."
        }
        $script:NextSeq++
        if ([string]$Event.event -eq 'session.resyncRequired') {
            continue
        }
        if ([string]$Event.event -ne 'filter.settingsChanged' -or
            [string]$Event.data.filter -ne $Filter -or
            [int64]$Event.data.settings.value -ne $Value -or
            [int64]$Event.revision -ne $ExpectedRevision) {
            Fail 'safe-after-late did not receive its exact command-owned settings event.'
        }
        return $Event
    }
}

function Assert-CanonicalHandle([string] $Value, [string] $Label) {
    if ($Value -notmatch '^[1-9][0-9]*$') {
        Fail "$Label returned a non-canonical handle '$Value'."
    }
}

function New-TestSource([string] $Id, [string] $Kind, [string] $Name, [hashtable] $Settings, [int64] $Revision) {
    $Params = @{ kind = $Kind; name = $Name }
    if ($null -ne $Settings) {
        $Params.settings = $Settings
    }
    $Response = Send-V2Request @{
        op = 'request'; id = $Id; method = 'source.create'; ifRevision = $Revision
        params = $Params
    }
    Assert-Ok $Response ($Revision + 1) $Id
    $Handle = [string]$Response.data.source
    Assert-CanonicalHandle $Handle $Id
    $null = Read-Event 'source.created' ($Revision + 1) '' $Handle
    return [pscustomobject]@{ Handle = $Handle; Revision = $Revision + 1 }
}

function New-TestFilter([string] $Id, [string] $Source, [string] $Name, [int64] $Value, [int64] $Revision) {
    $Response = Send-V2Request @{
        op = 'request'; id = $Id; method = 'filter.create'; ifRevision = $Revision
        params = @{
            source = $Source; kind = 'task11_filter'; name = $Name
            settings = @{ value = $Value }
        }
    }
    Assert-Ok $Response ($Revision + 1) $Id
    $Handle = [string]$Response.data.filter
    Assert-CanonicalHandle $Handle $Id
    if ([string]$Response.data.source -ne $Source -or [string]$Response.data.name -ne $Name) {
        Fail "$Id returned the wrong filter identity."
    }
    $null = Read-Event 'filter.created' ($Revision + 1) $Handle $Source
    return [pscustomobject]@{ Handle = $Handle; Revision = $Revision + 1 }
}

try {
    $Ready = Read-EngineMessage
    if ($Ready.event -ne 'ready' -or [int]$Ready.protocol -ne 1) {
        Fail 'migration bootstrap ready event changed unexpectedly.'
    }

    $Hello = Send-V2Request @{ op = 'request'; id = 'task11.hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $Hello 0 'session.hello'
    $Capabilities = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
    $RequiredFilterCapabilities = @(
        'filter.v1', 'filter.kindList.v1', 'filter.kindDefaults.v1', 'filter.kindProperties.v1',
        'filter.list.v1', 'filter.get.v1', 'filter.create.v1', 'filter.remove.v1',
        'filter.rename.v1', 'filter.duplicate.v1', 'filter.getSettings.v1',
        'filter.patchSettings.v1', 'filter.replaceSettings.v1', 'filter.setEnabled.v1',
        'filter.getEnabled.v1', 'filter.setOrder.v1', 'filter.moveUp.v1',
        'filter.moveDown.v1', 'filter.moveTop.v1', 'filter.moveBottom.v1'
    )
    foreach ($Required in $RequiredFilterCapabilities) {
        if ($Capabilities -notcontains $Required) {
            Fail "session.hello is missing capability '$Required'."
        }
    }
    if (@($Capabilities | Where-Object { $_ -eq 'filter.v1' }).Count -ne 1) {
        Fail 'session.hello advertised filter.v1 more than once.'
    }

    $CapabilitiesQuery = Send-V2Request @{
        op = 'request'; id = 'task11.capabilities'; method = 'engine.getCapabilities'; params = @{}
    }
    Assert-Ok $CapabilitiesQuery 0 'engine.getCapabilities'
    $CapabilityQueryNames = @($CapabilitiesQuery.data.capabilities | ForEach-Object { [string]$_.name })
    foreach ($Required in $RequiredFilterCapabilities) {
        if ($CapabilityQueryNames -notcontains $Required) {
            Fail "engine.getCapabilities is missing capability '$Required'."
        }
    }
    if (@($CapabilityQueryNames | Where-Object { $_ -eq 'filter.v1' }).Count -ne 1) {
        Fail 'engine.getCapabilities advertised filter.v1 more than once.'
    }

    $Subscribe = Send-V2Request @{
        op = 'request'; id = 'task11.subscribe'; method = 'session.subscribe'
        params = @{
            subscriptions = @(
                @{ pattern = 'filter.*' }
                @{ pattern = 'source.*' }
                @{ pattern = 'session.*' }
            )
        }
    }
    Assert-Ok $Subscribe 0 'session.subscribe'

    $Kinds = Send-V2Request @{ op = 'request'; id = 'task11.kindList'; method = 'filter.kindList'; params = @{} }
    Assert-Ok $Kinds 0 'filter.kindList'
    $KindEntry = @($Kinds.data.kinds | Where-Object { [string]$_.id -eq 'task11_filter' })
    if ($KindEntry.Count -ne 1) {
        Fail 'deterministic Task 11 filter kind was not discovered exactly once.'
    }
    if (-not [bool]$KindEntry[0].moduleLoadState) {
        Fail 'deterministic Task 11 filter kind did not report a loaded module.'
    }

    $Defaults = Send-V2Request @{
        op = 'request'; id = 'task11.defaults'; method = 'filter.kindDefaults'
        params = @{ kind = 'task11_filter' }
    }
    Assert-Ok $Defaults 0 'filter.kindDefaults'
    if ($null -eq $Defaults.data.settings) {
        Fail 'filter.kindDefaults did not return a settings object.'
    }

    $KindProperties = Send-V2Request @{
        op = 'request'; id = 'task11.kindProperties'; method = 'filter.kindProperties'
        params = @{ kind = 'task11_filter' }
    }
    Assert-Ok $KindProperties 0 'filter.kindProperties'
    if ([string]$KindProperties.data.target.type -ne 'filterKind' -or
        [string]$KindProperties.data.target.kind -ne 'task11_filter') {
        Fail 'filter.kindProperties returned the wrong target.'
    }
    $KindPropertyNames = @($KindProperties.data.properties | ForEach-Object { [string]$_.name })
    foreach ($PropertyName in @('value', 'blockMs', 'triggerOther', 'burst')) {
        if ($KindPropertyNames -notcontains $PropertyName) {
            Fail "filter.kindProperties is missing '$PropertyName'."
        }
    }

    $Revision = [int64]0
    $Parent = New-TestSource 'task11.source' 'task11_filter_source' 'task11-parent' $null $Revision
    $Revision = $Parent.Revision
    if ($Parent.Handle -ne '1') {
        Fail "fresh Task 11 engine expected source handle 1, got $($Parent.Handle)."
    }

    $Empty = Send-V2Request @{
        op = 'request'; id = 'task11.empty'; method = 'filter.list'
        params = @{ source = $Parent.Handle }
    }
    Assert-Ok $Empty $Revision 'empty filter.list'
    if ([int]$Empty.data.count -ne 0) {
        Fail 'new parent unexpectedly contained filters.'
    }

    $FilterA = New-TestFilter 'task11.filter-a' $Parent.Handle 'first-filter' 10 $Revision
    $Revision = $FilterA.Revision
    if ($FilterA.Handle -ne '2') {
        Fail "fresh Task 11 engine expected first filter handle 2, got $($FilterA.Handle)."
    }
    $FilterB = New-TestFilter 'task11.filter-b' $Parent.Handle 'second-filter' 11 $Revision
    $Revision = $FilterB.Revision
    if ($FilterB.Handle -ne '3') {
        Fail "fresh Task 11 engine expected second filter handle 3, got $($FilterB.Handle)."
    }
    $FilterC = New-TestFilter 'task11.filter-c' $Parent.Handle 'third-filter' 12 $Revision
    $Revision = $FilterC.Revision
    if ($FilterC.Handle -ne '4') {
        Fail "fresh Task 11 engine expected third filter handle 4, got $($FilterC.Handle)."
    }

    $BadHandle = Send-V2Request @{
        op = 'request'; id = 'task11.bad-handle'; method = 'filter.get'
        params = @{ filter = '01' }
    }
    Assert-Error $BadHandle 'bad_request' $Revision 'non-canonical filter handle'
    $NumericHandle = Send-V2Request @{
        op = 'request'; id = 'task11.numeric-handle'; method = 'filter.get'
        params = @{ filter = 2 }
    }
    Assert-Error $NumericHandle 'bad_request' $Revision 'numeric filter handle'
    $MissingFilter = Send-V2Request @{
        op = 'request'; id = 'task11.missing-filter'; method = 'filter.get'
        params = @{ filter = '999' }
    }
    Assert-Error $MissingFilter 'not_found' $Revision 'unknown filter handle'

    $Get = Send-V2Request @{
        op = 'request'; id = 'task11.get'; method = 'filter.get'
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $Get $Revision 'filter.get'
    if ([string]$Get.data.kind -ne 'task11_filter' -or [string]$Get.data.name -ne 'first-filter' -or
        [string]$Get.data.source -ne $Parent.Handle -or -not [bool]$Get.data.enabled) {
        Fail 'filter.get returned an incorrect summary.'
    }

    $FilterSettings = Send-V2Request @{
        op = 'request'; id = 'task11.get-settings'; method = 'filter.getSettings'
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $FilterSettings $Revision 'filter.getSettings'
    if ([int64]$FilterSettings.data.settings.value -ne 10) {
        Fail 'filter.getSettings did not return the creation settings.'
    }

    $FilterProperties = Send-V2Request @{
        op = 'request'; id = 'task11.properties-live'; method = 'properties.get'
        params = @{ target = @{ type = 'filter'; filter = $FilterA.Handle } }
    }
    Assert-Ok $FilterProperties $Revision 'properties.get(filter)'
    if ([string]$FilterProperties.data.target.type -ne 'filter' -or
        [string]$FilterProperties.data.target.filter -ne $FilterA.Handle -or
        [string]$FilterProperties.data.target.source -ne $Parent.Handle -or
        [int64]$FilterProperties.data.settings.value -ne 10) {
        Fail 'generic property bridge did not resolve the live filter.'
    }

    $GuardedRead = Send-V2Request @{
        op = 'request'; id = 'task11.guarded-read'; method = 'filter.get'
        ifRevision = $Revision; params = @{ filter = $FilterA.Handle }
    }
    Assert-Error $GuardedRead 'bad_request' $Revision 'ifRevision on filter.get'

    $Patch = Send-V2Request @{
        op = 'request'; id = 'task11.patch'; method = 'filter.patchSettings'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; settings = @{ value = 20 } }
    }
    Assert-Ok $Patch ($Revision + 1) 'filter.patchSettings'
    $Revision++
    if ([int64]$Patch.data.settings.value -ne 20) {
        Fail 'filter.patchSettings did not settle at value 20.'
    }
    $PatchEvent = Read-Event 'filter.settingsChanged' $Revision $FilterA.Handle $Parent.Handle
    if ([int64]$PatchEvent.data.settings.value -ne 20) {
        Fail 'filter.patchSettings event did not contain value 20.'
    }

    $PatchNoop = Send-V2Request @{
        op = 'request'; id = 'task11.patch-noop'; method = 'filter.patchSettings'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; settings = @{ value = 20 } }
    }
    Assert-Ok $PatchNoop $Revision 'idempotent filter.patchSettings'
    Assert-NoQueuedEvents 'idempotent filter.patchSettings'

    $Replace = Send-V2Request @{
        op = 'request'; id = 'task11.replace'; method = 'filter.replaceSettings'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; settings = @{ value = 30 } }
    }
    Assert-Ok $Replace ($Revision + 1) 'filter.replaceSettings'
    $Revision++
    if ([int64]$Replace.data.settings.value -ne 30) {
        Fail 'filter.replaceSettings did not settle at value 30.'
    }
    $ReplaceEvent = Read-Event 'filter.settingsChanged' $Revision $FilterA.Handle $Parent.Handle
    if ([int64]$ReplaceEvent.data.settings.value -ne 30) {
        Fail 'filter.replaceSettings event did not contain value 30.'
    }

    $Disable = Send-V2Request @{
        op = 'request'; id = 'task11.disable'; method = 'filter.setEnabled'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; enabled = $false }
    }
    Assert-Ok $Disable ($Revision + 1) 'filter.setEnabled(false)'
    $Revision++
    if ($Disable.data.enabled) {
        Fail 'filter.setEnabled(false) returned enabled=true.'
    }
    $null = Read-Event 'filter.enabledChanged' $Revision $FilterA.Handle $Parent.Handle

    $DisableNoop = Send-V2Request @{
        op = 'request'; id = 'task11.disable-noop'; method = 'filter.setEnabled'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; enabled = $false }
    }
    Assert-Ok $DisableNoop $Revision 'idempotent filter.setEnabled(false)'
    Assert-NoQueuedEvents 'idempotent filter.setEnabled'

    $Enabled = Send-V2Request @{
        op = 'request'; id = 'task11.enabled'; method = 'filter.getEnabled'
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $Enabled $Revision 'filter.getEnabled'
    if ($Enabled.data.enabled) {
        Fail 'filter.getEnabled reported enabled after disable.'
    }

    $Rename = Send-V2Request @{
        op = 'request'; id = 'task11.rename'; method = 'filter.rename'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; name = 'renamed-filter' }
    }
    Assert-Ok $Rename ($Revision + 1) 'filter.rename'
    $Revision++
    $RenameEvent = Read-Event 'filter.renamed' $Revision $FilterA.Handle $Parent.Handle
    if ([string]$RenameEvent.data.name -ne 'renamed-filter' -or
        [string]$RenameEvent.data.previousName -ne 'first-filter') {
        Fail 'filter.rename event did not contain both names.'
    }

    $RenameNoop = Send-V2Request @{
        op = 'request'; id = 'task11.rename-noop'; method = 'filter.rename'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; name = 'renamed-filter' }
    }
    Assert-Ok $RenameNoop $Revision 'idempotent filter.rename'
    Assert-NoQueuedEvents 'idempotent filter.rename'

    $List = Send-V2Request @{
        op = 'request'; id = 'task11.list'; method = 'filter.list'
        params = @{ source = $Parent.Handle }
    }
    Assert-Ok $List $Revision 'filter.list after creates'
    Assert-Order $List.data @($FilterC.Handle, $FilterB.Handle, $FilterA.Handle) 'initial filter order'

    $SetOrder = Send-V2Request @{
        op = 'request'; id = 'task11.set-order'; method = 'filter.setOrder'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; index = 0 }
    }
    Assert-Ok $SetOrder ($Revision + 1) 'filter.setOrder'
    $Revision++
    Assert-Order $SetOrder.data @($FilterA.Handle, $FilterC.Handle, $FilterB.Handle) 'filter.setOrder result'
    $null = Read-Event 'filter.orderChanged' $Revision '' $Parent.Handle

    $SetOrderNoop = Send-V2Request @{
        op = 'request'; id = 'task11.set-order-noop'; method = 'filter.setOrder'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; index = 0 }
    }
    Assert-Ok $SetOrderNoop $Revision 'idempotent filter.setOrder'
    Assert-Order $SetOrderNoop.data @($FilterA.Handle, $FilterC.Handle, $FilterB.Handle) 'filter.setOrder no-op result'
    Assert-NoQueuedEvents 'idempotent filter.setOrder'

    $MoveUp = Send-V2Request @{
        op = 'request'; id = 'task11.move-up'; method = 'filter.moveUp'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $MoveUp ($Revision + 1) 'filter.moveUp'
    $Revision++
    Assert-Order $MoveUp.data @($FilterC.Handle, $FilterA.Handle, $FilterB.Handle) 'filter.moveUp result'
    $null = Read-Event 'filter.orderChanged' $Revision '' $Parent.Handle

    $MoveDown = Send-V2Request @{
        op = 'request'; id = 'task11.move-down'; method = 'filter.moveDown'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $MoveDown ($Revision + 1) 'filter.moveDown'
    $Revision++
    Assert-Order $MoveDown.data @($FilterA.Handle, $FilterC.Handle, $FilterB.Handle) 'filter.moveDown result'
    $null = Read-Event 'filter.orderChanged' $Revision '' $Parent.Handle

    $MoveTop = Send-V2Request @{
        op = 'request'; id = 'task11.move-top'; method = 'filter.moveTop'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $MoveTop ($Revision + 1) 'filter.moveTop'
    $Revision++
    Assert-Order $MoveTop.data @($FilterC.Handle, $FilterB.Handle, $FilterA.Handle) 'filter.moveTop result'
    $null = Read-Event 'filter.orderChanged' $Revision '' $Parent.Handle

    $MoveBottom = Send-V2Request @{
        op = 'request'; id = 'task11.move-bottom'; method = 'filter.moveBottom'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $MoveBottom ($Revision + 1) 'filter.moveBottom'
    $Revision++
    Assert-Order $MoveBottom.data @($FilterA.Handle, $FilterC.Handle, $FilterB.Handle) 'filter.moveBottom result'
    $null = Read-Event 'filter.orderChanged' $Revision '' $Parent.Handle

    $OutOfRange = Send-V2Request @{
        op = 'request'; id = 'task11.order-out-of-range'; method = 'filter.setOrder'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; index = 3 }
    }
    Assert-Error $OutOfRange 'bad_request' $Revision 'out-of-range filter index'
    $NegativeIndex = Send-V2Request @{
        op = 'request'; id = 'task11.order-negative'; method = 'filter.setOrder'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; index = -1 }
    }
    Assert-Error $NegativeIndex 'bad_request' $Revision 'negative filter index'
    $StringIndex = Send-V2Request @{
        op = 'request'; id = 'task11.order-string'; method = 'filter.setOrder'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; index = '0' }
    }
    Assert-Error $StringIndex 'bad_request' $Revision 'string filter index'

    $Stale = Send-V2Request @{
        op = 'request'; id = 'task11.stale'; method = 'filter.rename'; ifRevision = ($Revision - 1)
        params = @{ filter = $FilterA.Handle; name = 'must-not-apply' }
    }
    Assert-Error $Stale 'revision_conflict' $Revision 'stale filter guard'
    $AfterStale = Send-V2Request @{
        op = 'request'; id = 'task11.after-stale'; method = 'filter.get'
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $AfterStale $Revision 'filter.get after stale guard'
    if ([string]$AfterStale.data.name -ne 'renamed-filter') {
        Fail 'stale filter.rename changed the filter name.'
    }
    Assert-NoQueuedEvents 'stale filter guard'

    $Duplicate = Send-V2Request @{
        op = 'request'; id = 'task11.filter-duplicate'; method = 'filter.duplicate'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; name = 'duplicated-filter' }
    }
    Assert-Ok $Duplicate ($Revision + 1) 'filter.duplicate'
    $Revision++
    $FilterD = [pscustomobject]@{ Handle = [string]$Duplicate.data.filter }
    Assert-CanonicalHandle $FilterD.Handle 'filter.duplicate'
    if ([string]$Duplicate.data.duplicateOf -ne $FilterA.Handle -or
        [string]$Duplicate.data.source -ne $Parent.Handle -or [bool]$Duplicate.data.enabled) {
        Fail 'filter.duplicate did not preserve identity or disabled state.'
    }
    $null = Read-Event 'filter.created' $Revision $FilterD.Handle $Parent.Handle
    $DuplicateSettings = Send-V2Request @{
        op = 'request'; id = 'task11.duplicate-settings'; method = 'filter.getSettings'
        params = @{ filter = $FilterD.Handle }
    }
    Assert-Ok $DuplicateSettings $Revision 'duplicated filter settings'
    if ([int64]$DuplicateSettings.data.settings.value -ne 30) {
        Fail 'filter.duplicate did not copy settings.'
    }

    $RemoveB = Send-V2Request @{
        op = 'request'; id = 'task11.filter-remove'; method = 'filter.remove'; ifRevision = $Revision
        params = @{ filter = $FilterB.Handle }
    }
    Assert-Ok $RemoveB ($Revision + 1) 'filter.remove'
    $Revision++
    $null = Read-Event 'filter.removed' $Revision $FilterB.Handle $Parent.Handle
    $RemovedB = Send-V2Request @{
        op = 'request'; id = 'task11.removed-filter-get'; method = 'filter.get'
        params = @{ filter = $FilterB.Handle }
    }
    Assert-Error $RemovedB 'not_found' $Revision 'removed filter handle'
    Assert-NoQueuedEvents 'filter.remove'

    $SourceDuplicate = Send-V2Request @{
        op = 'request'; id = 'task11.source-duplicate'; method = 'source.duplicate'; ifRevision = $Revision
        params = @{ source = $Parent.Handle; name = 'task11-parent-copy' }
    }
    Assert-Ok $SourceDuplicate ($Revision + 1) 'source.duplicate with filters'
    $Revision++
    $DuplicateSource = [string]$SourceDuplicate.data.source
    Assert-CanonicalHandle $DuplicateSource 'source.duplicate'
    if ($DuplicateSource -eq $Parent.Handle) {
        Fail 'source.duplicate reused the parent source handle.'
    }
    $null = Read-Event 'source.created' $Revision '' $DuplicateSource
    Assert-NoQueuedEvents 'source.duplicate synthesized filter events'

    $Inherited = Send-V2Request @{
        op = 'request'; id = 'task11.inherited-list'; method = 'filter.list'
        params = @{ source = $DuplicateSource }
    }
    Assert-Ok $Inherited $Revision 'filter.list on duplicated source'
    $InheritedEntries = @($Inherited.data.filters)
    if ($InheritedEntries.Count -ne 3) {
        Fail "duplicated source inherited $($InheritedEntries.Count) filters, expected 3."
    }
    $OriginalHandles = @($FilterA.Handle, $FilterC.Handle, $FilterD.Handle)
    $InheritedHandles = @($InheritedEntries | ForEach-Object { [string]$_.filter })
    foreach ($InheritedHandle in $InheritedHandles) {
        Assert-CanonicalHandle $InheritedHandle 'inherited filter'
        if ($OriginalHandles -contains $InheritedHandle -or $InheritedHandle -eq $FilterB.Handle) {
            Fail 'source.duplicate reused an existing filter handle.'
        }
    }
    $InheritedGet = Send-V2Request @{
        op = 'request'; id = 'task11.inherited-get'; method = 'filter.get'
        params = @{ filter = $InheritedHandles[0] }
    }
    Assert-Ok $InheritedGet $Revision 'filter.get inherited filter'
    if ([string]$InheritedGet.data.source -ne $DuplicateSource) {
        Fail 'inherited filter has the wrong parent source handle.'
    }

    # The fixture asks a peer filter to update while A is settling. The peer
    # callback must remain an independent deferred batch/revision.
    $Concurrent = Send-V2Request @{
        op = 'request'; id = 'task11.unrelated-update'; method = 'filter.patchSettings'; ifRevision = $Revision
        params = @{
            filter = $FilterA.Handle
            settings = @{ value = 40; triggerOther = $true }
        }
    }
    Assert-Ok $Concurrent ($Revision + 1) 'filter.patchSettings with unrelated peer callback'
    $Revision++
    $null = Read-Event 'filter.settingsChanged' $Revision $FilterA.Handle $Parent.Handle
    $PeerEvent = Read-Event 'filter.settingsChanged' ($Revision + 1) $FilterC.Handle $Parent.Handle
    $Revision = [int64]$PeerEvent.revision
    if ([int64]$PeerEvent.data.settings.value -ne 777) {
        Fail 'unrelated peer callback did not preserve its own settings.'
    }
    $PeerSettings = Send-V2Request @{
        op = 'request'; id = 'task11.peer-settings'; method = 'filter.getSettings'
        params = @{ filter = $FilterC.Handle }
    }
    Assert-Ok $PeerSettings $Revision 'peer filter settings after unrelated callback'
    if ([int64]$PeerSettings.data.settings.value -ne 777) {
        Fail 'unrelated peer callback was not committed to the peer filter.'
    }

    $ResetA = Send-V2Request @{
        op = 'request'; id = 'task11.reset-a'; method = 'filter.replaceSettings'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; settings = @{ value = 50 } }
    }
    Assert-Ok $ResetA ($Revision + 1) 'filter.replaceSettings after unrelated callback'
    $Revision++
    $null = Read-Event 'filter.settingsChanged' $Revision $FilterA.Handle $Parent.Handle

    # A deliberately bounded blocking callback must settle through the
    # permanent observer without a temporary connect/disconnect waiter.
    $Blocking = [System.Diagnostics.Stopwatch]::StartNew()
    $BlockResponse = Send-V2Request @{
        op = 'request'; id = 'task11.blocking-update'; method = 'filter.patchSettings'; ifRevision = $Revision
        params = @{ filter = $FilterC.Handle; settings = @{ value = 60; blockMs = 1000 } }
    }
    $Blocking.Stop()
    Assert-Ok $BlockResponse ($Revision + 1) 'blocking filter update'
    $Revision++
    if ($Blocking.Elapsed.TotalSeconds -ge 5.0) {
        Fail "blocking filter callback did not settle before the bounded deadline ($($Blocking.Elapsed.TotalSeconds) seconds)."
    }
    $null = Read-Event 'filter.settingsChanged' $Revision $FilterC.Handle $Parent.Handle

    # The first timeout leaves the A handle uncertain. The following request
    # changes the settings while the old callback is still blocked; it must
    # not be settled by that late callback.
    $TimeoutWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $TimeoutResponse = Send-V2Request @{
        op = 'request'; id = 'task11.timeout'; method = 'filter.patchSettings'; ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; settings = @{ value = 99; blockMs = 12000 } }
    }
    $TimeoutWatch.Stop()
    Write-Host "initial timeout elapsed=$($TimeoutWatch.Elapsed.TotalSeconds) seconds"
    Assert-Error $TimeoutResponse 'timeout' $Revision 'timed-out filter update'
    $FirstResync = Read-Until-Resync ($Revision + 1)
    $Revision = [int64]$FirstResync.revision

    $AfterTimeoutSettings = Send-V2Request @{
        op = 'request'; id = 'task11.after-timeout-settings'; method = 'filter.getSettings'
        params = @{ filter = $FilterA.Handle }
    }
    Assert-Ok $AfterTimeoutSettings $Revision 'filter.getSettings after timeout'
    if ([int64]$AfterTimeoutSettings.data.settings.value -ne 99 -or
        [int64]$AfterTimeoutSettings.data.settings.blockMs -ne 12000) {
        Fail 'timed-out filter update did not apply the canonical settings object.'
    }

    $NewerWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $NewerDuringLate = Send-V2Request @{
        op = 'request'; id = 'task11.newer-during-late'; method = 'filter.patchSettings'
        ifRevision = $Revision
        params = @{ filter = $FilterA.Handle; settings = @{ value = 100; blockMs = 0 } }
    }
    $NewerWatch.Stop()
    Write-Host "newer timeout elapsed=$($NewerWatch.Elapsed.TotalSeconds) seconds"
    Assert-Error $NewerDuringLate 'timeout' $Revision 'new filter update during late completion'
    $SecondResync = Read-Until-Resync ($Revision + 1)
    $Revision = [int64]$SecondResync.revision

    # A and the newer timed-out request each have an independent private
    # deferred-update identity. Their eventual completions must produce
    # resynchronization boundaries and no normal settings event before a later
    # request is allowed to settle normally.
    $LateResync = Read-Until-Resync ($Revision + 1)
    Assert-NoLateSettingsEvent $FilterA.Handle 'late completion'
    $Revision = [int64]$LateResync.revision

    # A timed-out newer request may still have its own late callback after A's
    # boundary. The safe follow-up deliberately omits ifRevision and retries
    # only across an observed resync, so an asynchronous boundary cannot turn
    # into a false revision conflict.
    $SafeAfterLate = $null
    for ($Attempt = 1; $Attempt -le 3 -and $null -eq $SafeAfterLate; $Attempt++) {
        $SafeWatch = [System.Diagnostics.Stopwatch]::StartNew()
        $SafeCandidate = Send-V2Request @{
            op = 'request'; id = "task11.safe-after-late.$Attempt"; method = 'filter.patchSettings'
            params = @{ filter = $FilterA.Handle; settings = @{ value = 101; blockMs = 0 } }
        }
        $SafeWatch.Stop()
        Write-Host "safe update attempt=$Attempt elapsed=$($SafeWatch.Elapsed.TotalSeconds) seconds"
        if ($SafeCandidate.status.ok) {
            $SafeAfterLate = $SafeCandidate
            break
        }
        Assert-Error $SafeCandidate 'timeout' $Revision "filter update after late uncertainty attempt $Attempt"
        $SafeRetryBatch = [System.Collections.Generic.List[object]]::new()
        $SafeRetryResync = Read-Until-Resync ($Revision + 1) $SafeRetryBatch
        $Revision = [int64]$SafeRetryResync.revision
    }
    if ($null -eq $SafeAfterLate) {
        Fail 'filter update after late uncertainty did not recover within three bounded attempts.'
    }
    $Revision = [int64]$SafeAfterLate.revision
    $null = Read-SafeSettingsEvent $FilterA.Handle 101 $Revision

    # The fixture emits 1100 peer update observations during D's update,
    # exceeding the bounded deferred bridge and forcing a resync.
    $Burst = Send-V2Request @{
        op = 'request'; id = 'task11.overflow'; method = 'filter.patchSettings'; ifRevision = $Revision
        params = @{ filter = $FilterD.Handle; settings = @{ value = 102; burst = $true } }
    }
    Assert-Error $Burst 'timeout' $Revision 'deferred filter queue overflow'
    $OverflowResync = Read-Until-Resync ($Revision + 1)
    Assert-NoLateSettingsEvent $FilterD.Handle 'overflow'
    $Revision = [int64]$OverflowResync.revision

    $OriginalList = Send-V2Request @{
        op = 'request'; id = 'task11.parent-list-before-remove'; method = 'filter.list'
        params = @{ source = $Parent.Handle }
    }
    Assert-Ok $OriginalList $Revision 'parent filter list before source removal'
    $OriginalHandles = @($OriginalList.data.filters | ForEach-Object { [string]$_.filter })
    if ($OriginalHandles.Count -ne 3) {
        Fail 'parent filter list changed unexpectedly before source removal.'
    }

    $SourceRemove = Send-V2Request @{
        op = 'request'; id = 'task11.source-remove'; method = 'source.remove'; ifRevision = $Revision
        params = @{ source = $Parent.Handle }
    }
    Assert-Ok $SourceRemove ($Revision + 1) 'source.remove with filters'
    $Revision++
    foreach ($Handle in $OriginalHandles) {
        $null = Read-Event 'filter.removed' $Revision $Handle $Parent.Handle
    }
    $null = Read-Event 'source.removed' $Revision '' $Parent.Handle

    foreach ($Handle in $OriginalHandles) {
        $StaleFilter = Send-V2Request @{
            op = 'request'; id = "task11.stale.$Handle"; method = 'filter.get'
            params = @{ filter = $Handle }
        }
        Assert-Error $StaleFilter 'not_found' $Revision "stale removed filter $Handle"
    }

    $DuplicateSourceRemove = Send-V2Request @{
        op = 'request'; id = 'task11.duplicate-source-remove'; method = 'source.remove'
        ifRevision = $Revision; params = @{ source = $DuplicateSource }
    }
    Assert-Ok $DuplicateSourceRemove ($Revision + 1) 'source.remove duplicated source'
    $Revision++
    foreach ($Handle in $InheritedHandles) {
        $null = Read-Event 'filter.removed' $Revision $Handle $DuplicateSource
    }
    $null = Read-Event 'source.removed' $Revision '' $DuplicateSource

    $InheritedStale = Send-V2Request @{
        op = 'request'; id = 'task11.inherited-stale'; method = 'filter.get'
        params = @{ filter = $InheritedHandles[0] }
    }
    Assert-Error $InheritedStale 'not_found' $Revision 'stale inherited filter handle'

    Assert-NoQueuedEvents 'final filter event queue'

    $Close = Send-V2Request @{
        op = 'request'; id = 'task11.close'; method = 'session.close'; ifRevision = $Revision; params = @{}
    }
    Assert-Ok $Close ($Revision + 1) 'session.close'
    $Process.StandardInput.Close()
    if (-not $Process.WaitForExit(30000)) {
        Fail 'obs-engine did not exit after session.close.'
    }
    if ($Process.ExitCode -ne 0) {
        Fail "obs-engine exited with code $($Process.ExitCode)."
    }
    $Stderr = $ErrorTask.GetAwaiter().GetResult()
    if ($Stderr -notmatch '\[task11-filter\] deterministic parent/filter module loaded') {
        Fail 'deterministic Task 11 module-load evidence was missing from stderr.'
    }
    Write-Host 'Task 11 filter integration: PASS' -ForegroundColor Green
}
catch {
    if ($null -ne $Process -and -not $Process.HasExited) {
        try { $Process.Kill($true) } catch {}
        try { $Process.WaitForExit(5000) | Out-Null } catch {}
    }
    if ($null -ne $ErrorTask) {
        try {
            $Stderr = $ErrorTask.GetAwaiter().GetResult()
            if ($Stderr) {
                Write-Host '=== obs-engine stderr ==='
                Write-Host $Stderr
            }
        } catch {}
    }
    throw
}
finally {
    if ($null -ne $Process) {
        if (-not $Process.HasExited) {
            try { $Process.StandardInput.Close() } catch {}
            try { $Process.Kill($true) } catch {}
            try { $Process.WaitForExit(5000) | Out-Null } catch {}
        }
        $Process.Dispose()
    }
}

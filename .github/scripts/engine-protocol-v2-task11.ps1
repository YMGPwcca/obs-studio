param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Process = $null
$script:ErrorTask = $null
$script:Events = [System.Collections.Generic.List[object]]::new()
$script:WireLog = [System.Collections.Generic.List[object]]::new()
$script:LastResponseWireIndex = -1
$script:NextSeq = [uint64]1

function Start-Task11Engine([string] $Root) {
    $resolvedRoot = (Resolve-Path $Root).Path
    $engine = Get-ChildItem -Path $resolvedRoot -Filter 'obs-engine.exe' -File -Recurse | Select-Object -First 1
    if ($null -eq $engine) {
        throw 'obs-engine.exe was not found in the runtime root.'
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $engine.FullName
    $startInfo.WorkingDirectory = $engine.Directory.FullName
    $startInfo.ArgumentList.Add('--plugin=task11-filter-source')
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
    $script:Events = [System.Collections.Generic.List[object]]::new()
    $script:WireLog = [System.Collections.Generic.List[object]]::new()
    $script:LastResponseWireIndex = -1
    $script:NextSeq = [uint64]1
}

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

function Read-PendingEvent {
    if ($script:Events.Count -gt 0) {
        $Event = $script:Events[0]
        $script:Events.RemoveAt(0)
    } else {
        $Event = Read-EngineMessage
    }
    return $Event
}

function Assert-EventSequence($Event, [string] $Label) {
    if ($Event.op -ne 'event') {
        Fail "$Label expected an event but received response '$($Event.id)'."
    }
    if ([uint64]$Event.seq -ne $script:NextSeq) {
        Fail "$Label seq=$($Event.seq), expected $script:NextSeq."
    }
    $script:NextSeq++
}

function Assert-CommandEventAfterResponse($Event, [string] $Name) {
    $WireEvent = @($script:WireLog | Where-Object { $_.Seq -eq [uint64]$Event.seq }) | Select-Object -First 1
    if ($null -eq $WireEvent -or $WireEvent.Index -le $script:LastResponseWireIndex) {
        Fail "command-owned event '$Name' was emitted before its response."
    }
}

function Assert-EventTargets($Event, [string] $Name, [string] $Filter, [string] $Source) {
    if ($Filter -and [string]$Event.data.filter -ne $Filter) {
        Fail "event '$Name' filter=$($Event.data.filter), expected $Filter."
    }
    if ($Source -and [string]$Event.data.source -ne $Source) {
        Fail "event '$Name' source=$($Event.data.source), expected $Source."
    }
}

function Read-Event([string] $Name, [int64] $ExpectedRevision, [string] $Filter = '', [string] $Source = '') {
    $Event = Read-PendingEvent
    Assert-EventSequence $Event "event '$Name'"
    if ([string]$Event.event -ne $Name) {
        Fail "expected event '$Name' but received '$($Event.event)'."
    }
    if ([int64]$Event.revision -ne $ExpectedRevision) {
        Fail "event '$Name' revision=$($Event.revision), expected $ExpectedRevision."
    }
    Assert-CommandEventAfterResponse $Event $Name
    Assert-EventTargets $Event $Name $Filter $Source
    return $Event
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
        $Event = Read-PendingEvent
        Assert-EventSequence $Event 'resync wait'
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

function Assert-SafeEventWireOrder($Event) {
    $WireEvent = @($script:WireLog | Where-Object { $_.Seq -eq [uint64]$Event.seq }) | Select-Object -First 1
    if ($null -ne $WireEvent -and $WireEvent.Index -le $script:LastResponseWireIndex -and
        [string]$Event.event -ne 'session.resyncRequired') {
        Fail "safe-after-late observed a normal event before its response: $($Event.event)."
    }
}

function Assert-SafeSettingsPayload($Event, [string] $Filter, [int64] $Value, [int64] $ExpectedRevision) {
    if ([string]$Event.event -ne 'filter.settingsChanged' -or
        [string]$Event.data.filter -ne $Filter -or
        [int64]$Event.data.settings.value -ne $Value -or
        [int64]$Event.revision -ne $ExpectedRevision) {
        Fail 'safe-after-late did not receive its exact command-owned settings event.'
    }
}

function Read-SafeSettingsEvent([string] $Filter, [int64] $Value, [int64] $ExpectedRevision) {
    while ($true) {
        $Event = Read-PendingEvent
        Assert-EventSequence $Event 'safe-after-late'
        Assert-SafeEventWireOrder $Event
        if ([string]$Event.event -eq 'session.resyncRequired') {
            continue
        }
        Assert-SafeSettingsPayload $Event $Filter $Value $ExpectedRevision
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

function Assert-Task11CapabilitySet([object[]] $Values, [object[]] $Required, [string] $Label) {
    foreach ($requiredName in $Required) {
        if ($Values -notcontains $requiredName) {
            Fail "$Label is missing capability '$requiredName'."
        }
    }
    if (@($Values | Where-Object { $_ -eq 'filter.v1' }).Count -ne 1) {
        Fail "$Label advertised filter.v1 more than once."
    }
}

function Assert-Task11KindContract($Kinds) {
    $kindEntry = @($Kinds.data.kinds | Where-Object { [string]$_.id -eq 'task11_filter' })
    if ($kindEntry.Count -ne 1) {
        Fail 'deterministic Task 11 filter kind was not discovered exactly once.'
    }
    if (-not [bool]$kindEntry[0].moduleLoadState) {
        Fail 'deterministic Task 11 filter kind did not report a loaded module.'
    }
}

function Assert-Task11PropertyContract($KindProperties) {
    if ([string]$KindProperties.data.target.type -ne 'filterKind' -or
        [string]$KindProperties.data.target.kind -ne 'task11_filter') {
        Fail 'filter.kindProperties returned the wrong target.'
    }
    $propertyNames = @($KindProperties.data.properties | ForEach-Object { [string]$_.name })
    foreach ($propertyName in @('value', 'blockMs', 'triggerOther', 'burst')) {
        if ($propertyNames -notcontains $propertyName) {
            Fail "filter.kindProperties is missing '$propertyName'."
        }
    }
}

function Initialize-Task11Session {
    $ready = Read-EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        Fail 'migration bootstrap ready event changed unexpectedly.'
    }
    $required = @(
        'filter.v1', 'filter.kindList.v1', 'filter.kindDefaults.v1', 'filter.kindProperties.v1',
        'filter.list.v1', 'filter.get.v1', 'filter.create.v1', 'filter.remove.v1',
        'filter.rename.v1', 'filter.duplicate.v1', 'filter.getSettings.v1',
        'filter.patchSettings.v1', 'filter.replaceSettings.v1', 'filter.setEnabled.v1',
        'filter.getEnabled.v1', 'filter.setOrder.v1', 'filter.moveUp.v1',
        'filter.moveDown.v1', 'filter.moveTop.v1', 'filter.moveBottom.v1'
    )
    $hello = Send-V2Request @{ op = 'request'; id = 'task11.hello'; method = 'session.hello'; params = @{} }
    Assert-Ok $hello 0 'session.hello'
    $capabilities = @($hello.data.capabilities | ForEach-Object { [string]$_.name })
    Assert-Task11CapabilitySet $capabilities $required 'session.hello'

    $capabilitiesQuery = Send-V2Request @{
        op = 'request'; id = 'task11.capabilities'; method = 'engine.getCapabilities'; params = @{}
    }
    Assert-Ok $capabilitiesQuery 0 'engine.getCapabilities'
    $capabilityQueryNames = @($capabilitiesQuery.data.capabilities | ForEach-Object { [string]$_.name })
    Assert-Task11CapabilitySet $capabilityQueryNames $required 'engine.getCapabilities'

    $subscribe = Send-V2Request @{
        op = 'request'; id = 'task11.subscribe'; method = 'session.subscribe'
        params = @{
            subscriptions = @(
                @{ pattern = 'filter.*' }
                @{ pattern = 'source.*' }
                @{ pattern = 'session.*' }
            )
        }
    }
    Assert-Ok $subscribe 0 'session.subscribe'

    $kinds = Send-V2Request @{ op = 'request'; id = 'task11.kindList'; method = 'filter.kindList'; params = @{} }
    Assert-Ok $kinds 0 'filter.kindList'
    Assert-Task11KindContract $kinds
    $defaults = Send-V2Request @{
        op = 'request'; id = 'task11.defaults'; method = 'filter.kindDefaults'
        params = @{ kind = 'task11_filter' }
    }
    Assert-Ok $defaults 0 'filter.kindDefaults'
    if ($null -eq $defaults.data.settings) {
        Fail 'filter.kindDefaults did not return a settings object.'
    }
    $kindProperties = Send-V2Request @{
        op = 'request'; id = 'task11.kindProperties'; method = 'filter.kindProperties'
        params = @{ kind = 'task11_filter' }
    }
    Assert-Ok $kindProperties 0 'filter.kindProperties'
    Assert-Task11PropertyContract $kindProperties
}

function New-Task11State {
    return [pscustomobject]@{
        Revision = [int64]0
        Parent = $null
        FilterA = $null
        FilterB = $null
        FilterC = $null
        FilterD = $null
        DuplicateSource = ''
        InheritedHandles = @()
        OriginalHandles = @()
    }
}

function Initialize-Task11FilterGraph([object] $State) {
    $State.Parent = New-TestSource 'task11.source' 'task11_filter_source' 'task11-parent' $null $State.Revision
    $State.Revision = $State.Parent.Revision
    if ($State.Parent.Handle -ne '1') {
        Fail "fresh Task 11 engine expected source handle 1, got $($State.Parent.Handle)."
    }
    $empty = Send-V2Request @{
        op = 'request'; id = 'task11.empty'; method = 'filter.list'
        params = @{ source = $State.Parent.Handle }
    }
    Assert-Ok $empty $State.Revision 'empty filter.list'
    if ([int]$empty.data.count -ne 0) {
        Fail 'new parent unexpectedly contained filters.'
    }
    $State.FilterA = New-TestFilter 'task11.filter-a' $State.Parent.Handle 'first-filter' 10 $State.Revision
    $State.Revision = $State.FilterA.Revision
    if ($State.FilterA.Handle -ne '2') {
        Fail "fresh Task 11 engine expected first filter handle 2, got $($State.FilterA.Handle)."
    }
    $State.FilterB = New-TestFilter 'task11.filter-b' $State.Parent.Handle 'second-filter' 11 $State.Revision
    $State.Revision = $State.FilterB.Revision
    if ($State.FilterB.Handle -ne '3') {
        Fail "fresh Task 11 engine expected second filter handle 3, got $($State.FilterB.Handle)."
    }
    $State.FilterC = New-TestFilter 'task11.filter-c' $State.Parent.Handle 'third-filter' 12 $State.Revision
    $State.Revision = $State.FilterC.Revision
    if ($State.FilterC.Handle -ne '4') {
        Fail "fresh Task 11 engine expected third filter handle 4, got $($State.FilterC.Handle)."
    }
}

function Assert-Task11LiveFilterReads([object] $State) {
    $badHandle = Send-V2Request @{
        op = 'request'; id = 'task11.bad-handle'; method = 'filter.get'
        params = @{ filter = '01' }
    }
    Assert-Error $badHandle 'bad_request' $State.Revision 'non-canonical filter handle'
    $numericHandle = Send-V2Request @{
        op = 'request'; id = 'task11.numeric-handle'; method = 'filter.get'
        params = @{ filter = 2 }
    }
    Assert-Error $numericHandle 'bad_request' $State.Revision 'numeric filter handle'
    $missingFilter = Send-V2Request @{
        op = 'request'; id = 'task11.missing-filter'; method = 'filter.get'
        params = @{ filter = '999' }
    }
    Assert-Error $missingFilter 'not_found' $State.Revision 'unknown filter handle'
    $get = Send-V2Request @{
        op = 'request'; id = 'task11.get'; method = 'filter.get'
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $get $State.Revision 'filter.get'
    if ([string]$get.data.kind -ne 'task11_filter' -or [string]$get.data.name -ne 'first-filter' -or
        [string]$get.data.source -ne $State.Parent.Handle -or -not [bool]$get.data.enabled) {
        Fail 'filter.get returned an incorrect summary.'
    }
    $filterSettings = Send-V2Request @{
        op = 'request'; id = 'task11.get-settings'; method = 'filter.getSettings'
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $filterSettings $State.Revision 'filter.getSettings'
    if ([int64]$filterSettings.data.settings.value -ne 10) {
        Fail 'filter.getSettings did not return the creation settings.'
    }
    $filterProperties = Send-V2Request @{
        op = 'request'; id = 'task11.properties-live'; method = 'properties.get'
        params = @{ target = @{ type = 'filter'; filter = $State.FilterA.Handle } }
    }
    Assert-Ok $filterProperties $State.Revision 'properties.get(filter)'
    if ([string]$filterProperties.data.target.type -ne 'filter' -or
        [string]$filterProperties.data.target.filter -ne $State.FilterA.Handle -or
        [string]$filterProperties.data.target.source -ne $State.Parent.Handle -or
        [int64]$filterProperties.data.settings.value -ne 10) {
        Fail 'generic property bridge did not resolve the live filter.'
    }
    $guardedRead = Send-V2Request @{
        op = 'request'; id = 'task11.guarded-read'; method = 'filter.get'
        ifRevision = $State.Revision; params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Error $guardedRead 'bad_request' $State.Revision 'ifRevision on filter.get'
}

function Invoke-Task11FilterMutations([object] $State) {
    $patch = Send-V2Request @{
        op = 'request'; id = 'task11.patch'; method = 'filter.patchSettings'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; settings = @{ value = 20 } }
    }
    Assert-Ok $patch ($State.Revision + 1) 'filter.patchSettings'
    $State.Revision++
    if ([int64]$patch.data.settings.value -ne 20) {
        Fail 'filter.patchSettings did not settle at value 20.'
    }
    $patchEvent = Read-Event 'filter.settingsChanged' $State.Revision $State.FilterA.Handle $State.Parent.Handle
    if ([int64]$patchEvent.data.settings.value -ne 20) {
        Fail 'filter.patchSettings event did not contain value 20.'
    }
    $patchNoop = Send-V2Request @{
        op = 'request'; id = 'task11.patch-noop'; method = 'filter.patchSettings'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; settings = @{ value = 20 } }
    }
    Assert-Ok $patchNoop $State.Revision 'idempotent filter.patchSettings'
    Assert-NoQueuedEvents 'idempotent filter.patchSettings'

    $replace = Send-V2Request @{
        op = 'request'; id = 'task11.replace'; method = 'filter.replaceSettings'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; settings = @{ value = 30 } }
    }
    Assert-Ok $replace ($State.Revision + 1) 'filter.replaceSettings'
    $State.Revision++
    if ([int64]$replace.data.settings.value -ne 30) {
        Fail 'filter.replaceSettings did not settle at value 30.'
    }
    $replaceEvent = Read-Event 'filter.settingsChanged' $State.Revision $State.FilterA.Handle $State.Parent.Handle
    if ([int64]$replaceEvent.data.settings.value -ne 30) {
        Fail 'filter.replaceSettings event did not contain value 30.'
    }

    $disable = Send-V2Request @{
        op = 'request'; id = 'task11.disable'; method = 'filter.setEnabled'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; enabled = $false }
    }
    Assert-Ok $disable ($State.Revision + 1) 'filter.setEnabled(false)'
    $State.Revision++
    if ($disable.data.enabled) {
        Fail 'filter.setEnabled(false) returned enabled=true.'
    }
    $null = Read-Event 'filter.enabledChanged' $State.Revision $State.FilterA.Handle $State.Parent.Handle
    $disableNoop = Send-V2Request @{
        op = 'request'; id = 'task11.disable-noop'; method = 'filter.setEnabled'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; enabled = $false }
    }
    Assert-Ok $disableNoop $State.Revision 'idempotent filter.setEnabled(false)'
    Assert-NoQueuedEvents 'idempotent filter.setEnabled'
    $enabled = Send-V2Request @{
        op = 'request'; id = 'task11.enabled'; method = 'filter.getEnabled'
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $enabled $State.Revision 'filter.getEnabled'
    if ($enabled.data.enabled) {
        Fail 'filter.getEnabled reported enabled after disable.'
    }

    $rename = Send-V2Request @{
        op = 'request'; id = 'task11.rename'; method = 'filter.rename'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; name = 'renamed-filter' }
    }
    Assert-Ok $rename ($State.Revision + 1) 'filter.rename'
    $State.Revision++
    $renameEvent = Read-Event 'filter.renamed' $State.Revision $State.FilterA.Handle $State.Parent.Handle
    if ([string]$renameEvent.data.name -ne 'renamed-filter' -or
        [string]$renameEvent.data.previousName -ne 'first-filter') {
        Fail 'filter.rename event did not contain both names.'
    }
    $renameNoop = Send-V2Request @{
        op = 'request'; id = 'task11.rename-noop'; method = 'filter.rename'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; name = 'renamed-filter' }
    }
    Assert-Ok $renameNoop $State.Revision 'idempotent filter.rename'
    Assert-NoQueuedEvents 'idempotent filter.rename'
}

function Assert-Task11InitialOrder([object] $State) {
    $list = Send-V2Request @{
        op = 'request'; id = 'task11.list'; method = 'filter.list'
        params = @{ source = $State.Parent.Handle }
    }
    Assert-Ok $list $State.Revision 'filter.list after creates'
    Assert-Order $list.data @($State.FilterC.Handle, $State.FilterB.Handle, $State.FilterA.Handle) 'initial filter order'
}

function Invoke-Task11OrderMutations([object] $State) {
    $setOrder = Send-V2Request @{
        op = 'request'; id = 'task11.set-order'; method = 'filter.setOrder'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; index = 0 }
    }
    Assert-Ok $setOrder ($State.Revision + 1) 'filter.setOrder'
    $State.Revision++
    Assert-Order $setOrder.data @($State.FilterA.Handle, $State.FilterC.Handle, $State.FilterB.Handle) 'filter.setOrder result'
    $null = Read-Event 'filter.orderChanged' $State.Revision '' $State.Parent.Handle
    $setOrderNoop = Send-V2Request @{
        op = 'request'; id = 'task11.set-order-noop'; method = 'filter.setOrder'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; index = 0 }
    }
    Assert-Ok $setOrderNoop $State.Revision 'idempotent filter.setOrder'
    Assert-Order $setOrderNoop.data @($State.FilterA.Handle, $State.FilterC.Handle, $State.FilterB.Handle) 'filter.setOrder no-op result'
    Assert-NoQueuedEvents 'idempotent filter.setOrder'

    $moveUp = Send-V2Request @{
        op = 'request'; id = 'task11.move-up'; method = 'filter.moveUp'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $moveUp ($State.Revision + 1) 'filter.moveUp'
    $State.Revision++
    Assert-Order $moveUp.data @($State.FilterC.Handle, $State.FilterA.Handle, $State.FilterB.Handle) 'filter.moveUp result'
    $null = Read-Event 'filter.orderChanged' $State.Revision '' $State.Parent.Handle
    $moveDown = Send-V2Request @{
        op = 'request'; id = 'task11.move-down'; method = 'filter.moveDown'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $moveDown ($State.Revision + 1) 'filter.moveDown'
    $State.Revision++
    Assert-Order $moveDown.data @($State.FilterA.Handle, $State.FilterC.Handle, $State.FilterB.Handle) 'filter.moveDown result'
    $null = Read-Event 'filter.orderChanged' $State.Revision '' $State.Parent.Handle
    $moveTop = Send-V2Request @{
        op = 'request'; id = 'task11.move-top'; method = 'filter.moveTop'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $moveTop ($State.Revision + 1) 'filter.moveTop'
    $State.Revision++
    Assert-Order $moveTop.data @($State.FilterC.Handle, $State.FilterB.Handle, $State.FilterA.Handle) 'filter.moveTop result'
    $null = Read-Event 'filter.orderChanged' $State.Revision '' $State.Parent.Handle
    $moveBottom = Send-V2Request @{
        op = 'request'; id = 'task11.move-bottom'; method = 'filter.moveBottom'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $moveBottom ($State.Revision + 1) 'filter.moveBottom'
    $State.Revision++
    Assert-Order $moveBottom.data @($State.FilterA.Handle, $State.FilterC.Handle, $State.FilterB.Handle) 'filter.moveBottom result'
    $null = Read-Event 'filter.orderChanged' $State.Revision '' $State.Parent.Handle
}

function Invoke-Task11OrderValidation([object] $State) {
    foreach ($case in @(
        @{ Id = 'task11.order-out-of-range'; Index = 3; Code = 'bad_request'; Label = 'out-of-range filter index' },
        @{ Id = 'task11.order-negative'; Index = -1; Code = 'bad_request'; Label = 'negative filter index' },
        @{ Id = 'task11.order-string'; Index = '0'; Code = 'bad_request'; Label = 'string filter index' }
    )) {
        $response = Send-V2Request @{
            op = 'request'; id = $case.Id; method = 'filter.setOrder'; ifRevision = $State.Revision
            params = @{ filter = $State.FilterA.Handle; index = $case.Index }
        }
        Assert-Error $response $case.Code $State.Revision $case.Label
    }
    $stale = Send-V2Request @{
        op = 'request'; id = 'task11.stale'; method = 'filter.rename'; ifRevision = ($State.Revision - 1)
        params = @{ filter = $State.FilterA.Handle; name = 'must-not-apply' }
    }
    Assert-Error $stale 'revision_conflict' $State.Revision 'stale filter guard'
    $afterStale = Send-V2Request @{
        op = 'request'; id = 'task11.after-stale'; method = 'filter.get'
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $afterStale $State.Revision 'filter.get after stale guard'
    if ([string]$afterStale.data.name -ne 'renamed-filter') {
        Fail 'stale filter.rename changed the filter name.'
    }
    Assert-NoQueuedEvents 'stale filter guard'
}

function Invoke-Task11DuplicateAndRemove([object] $State) {
    $duplicate = Send-V2Request @{
        op = 'request'; id = 'task11.filter-duplicate'; method = 'filter.duplicate'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; name = 'duplicated-filter' }
    }
    Assert-Ok $duplicate ($State.Revision + 1) 'filter.duplicate'
    $State.Revision++
    $State.FilterD = [pscustomobject]@{ Handle = [string]$duplicate.data.filter }
    Assert-CanonicalHandle $State.FilterD.Handle 'filter.duplicate'
    if ([string]$duplicate.data.duplicateOf -ne $State.FilterA.Handle -or
        [string]$duplicate.data.source -ne $State.Parent.Handle -or [bool]$duplicate.data.enabled) {
        Fail 'filter.duplicate did not preserve identity or disabled state.'
    }
    $null = Read-Event 'filter.created' $State.Revision $State.FilterD.Handle $State.Parent.Handle
    $duplicateSettings = Send-V2Request @{
        op = 'request'; id = 'task11.duplicate-settings'; method = 'filter.getSettings'
        params = @{ filter = $State.FilterD.Handle }
    }
    Assert-Ok $duplicateSettings $State.Revision 'duplicated filter settings'
    if ([int64]$duplicateSettings.data.settings.value -ne 30) {
        Fail 'filter.duplicate did not copy settings.'
    }

    $removeB = Send-V2Request @{
        op = 'request'; id = 'task11.filter-remove'; method = 'filter.remove'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterB.Handle }
    }
    Assert-Ok $removeB ($State.Revision + 1) 'filter.remove'
    $State.Revision++
    $null = Read-Event 'filter.removed' $State.Revision $State.FilterB.Handle $State.Parent.Handle
    $removedB = Send-V2Request @{
        op = 'request'; id = 'task11.removed-filter-get'; method = 'filter.get'
        params = @{ filter = $State.FilterB.Handle }
    }
    Assert-Error $removedB 'not_found' $State.Revision 'removed filter handle'
    Assert-NoQueuedEvents 'filter.remove'
}

function Invoke-Task11SourceDuplicate([object] $State) {
    $sourceDuplicate = Send-V2Request @{
        op = 'request'; id = 'task11.source-duplicate'; method = 'source.duplicate'; ifRevision = $State.Revision
        params = @{ source = $State.Parent.Handle; name = 'task11-parent-copy' }
    }
    Assert-Ok $sourceDuplicate ($State.Revision + 1) 'source.duplicate with filters'
    $State.Revision++
    $State.DuplicateSource = [string]$sourceDuplicate.data.source
    Assert-CanonicalHandle $State.DuplicateSource 'source.duplicate'
    if ($State.DuplicateSource -eq $State.Parent.Handle) {
        Fail 'source.duplicate reused the parent source handle.'
    }
    $null = Read-Event 'source.created' $State.Revision '' $State.DuplicateSource
    Assert-NoQueuedEvents 'source.duplicate synthesized filter events'
    $inherited = Send-V2Request @{
        op = 'request'; id = 'task11.inherited-list'; method = 'filter.list'
        params = @{ source = $State.DuplicateSource }
    }
    Assert-Ok $inherited $State.Revision 'filter.list on duplicated source'
    $inheritedEntries = @($inherited.data.filters)
    if ($inheritedEntries.Count -ne 3) {
        Fail "duplicated source inherited $($inheritedEntries.Count) filters, expected 3."
    }
    $State.OriginalHandles = @($State.FilterA.Handle, $State.FilterC.Handle, $State.FilterD.Handle)
    $State.InheritedHandles = @($inheritedEntries | ForEach-Object { [string]$_.filter })
    foreach ($inheritedHandle in $State.InheritedHandles) {
        Assert-CanonicalHandle $inheritedHandle 'inherited filter'
        if ($State.OriginalHandles -contains $inheritedHandle -or $inheritedHandle -eq $State.FilterB.Handle) {
            Fail 'source.duplicate reused an existing filter handle.'
        }
    }
    $inheritedGet = Send-V2Request @{
        op = 'request'; id = 'task11.inherited-get'; method = 'filter.get'
        params = @{ filter = $State.InheritedHandles[0] }
    }
    Assert-Ok $inheritedGet $State.Revision 'filter.get inherited filter'
    if ([string]$inheritedGet.data.source -ne $State.DuplicateSource) {
        Fail 'inherited filter has the wrong parent source handle.'
    }
}

function Invoke-Task11UnrelatedUpdate([object] $State) {
    # The fixture asks a peer filter to update while A is settling. The peer
    # callback must remain an independent deferred batch/revision.
    $concurrent = Send-V2Request @{
        op = 'request'; id = 'task11.unrelated-update'; method = 'filter.patchSettings'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; settings = @{ value = 40; triggerOther = $true } }
    }
    Assert-Ok $concurrent ($State.Revision + 1) 'filter.patchSettings with unrelated peer callback'
    $State.Revision++
    $null = Read-Event 'filter.settingsChanged' $State.Revision $State.FilterA.Handle $State.Parent.Handle
    $peerEvent = Read-Event 'filter.settingsChanged' ($State.Revision + 1) $State.FilterC.Handle $State.Parent.Handle
    $State.Revision = [int64]$peerEvent.revision
    if ([int64]$peerEvent.data.settings.value -ne 777) {
        Fail 'unrelated peer callback did not preserve its own settings.'
    }
    $peerSettings = Send-V2Request @{
        op = 'request'; id = 'task11.peer-settings'; method = 'filter.getSettings'
        params = @{ filter = $State.FilterC.Handle }
    }
    Assert-Ok $peerSettings $State.Revision 'peer filter settings after unrelated callback'
    if ([int64]$peerSettings.data.settings.value -ne 777) {
        Fail 'unrelated peer callback was not committed to the peer filter.'
    }
    $resetA = Send-V2Request @{
        op = 'request'; id = 'task11.reset-a'; method = 'filter.replaceSettings'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; settings = @{ value = 50 } }
    }
    Assert-Ok $resetA ($State.Revision + 1) 'filter.replaceSettings after unrelated callback'
    $State.Revision++
    $null = Read-Event 'filter.settingsChanged' $State.Revision $State.FilterA.Handle $State.Parent.Handle
}

function Invoke-Task11BlockingUpdate([object] $State) {
    # A deliberately bounded blocking callback must settle through the
    # permanent observer without a temporary connect/disconnect waiter.
    $blocking = [System.Diagnostics.Stopwatch]::StartNew()
    $blockResponse = Send-V2Request @{
        op = 'request'; id = 'task11.blocking-update'; method = 'filter.patchSettings'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterC.Handle; settings = @{ value = 60; blockMs = 1000 } }
    }
    $blocking.Stop()
    Assert-Ok $blockResponse ($State.Revision + 1) 'blocking filter update'
    $State.Revision++
    if ($blocking.Elapsed.TotalSeconds -ge 5.0) {
        Fail "blocking filter callback did not settle before the bounded deadline ($($blocking.Elapsed.TotalSeconds) seconds)."
    }
    $null = Read-Event 'filter.settingsChanged' $State.Revision $State.FilterC.Handle $State.Parent.Handle
}

function Invoke-Task11TimeoutStart([object] $State) {
    # The first timeout leaves the A handle uncertain. The following request
    # changes the settings while the old callback is still blocked; it must
    # not be settled by that late callback.
    $timeoutWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $timeoutResponse = Send-V2Request @{
        op = 'request'; id = 'task11.timeout'; method = 'filter.patchSettings'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; settings = @{ value = 99; blockMs = 12000 } }
    }
    $timeoutWatch.Stop()
    Write-Host "initial timeout elapsed=$($timeoutWatch.Elapsed.TotalSeconds) seconds"
    Assert-Error $timeoutResponse 'timeout' $State.Revision 'timed-out filter update'
    $firstResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$firstResync.revision
    $afterTimeoutSettings = Send-V2Request @{
        op = 'request'; id = 'task11.after-timeout-settings'; method = 'filter.getSettings'
        params = @{ filter = $State.FilterA.Handle }
    }
    Assert-Ok $afterTimeoutSettings $State.Revision 'filter.getSettings after timeout'
    if ([int64]$afterTimeoutSettings.data.settings.value -ne 99 -or
        [int64]$afterTimeoutSettings.data.settings.blockMs -ne 12000) {
        Fail 'timed-out filter update did not apply the canonical settings object.'
    }
}

function Invoke-Task11LateTimeouts([object] $State) {
    $newerWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $newerDuringLate = Send-V2Request @{
        op = 'request'; id = 'task11.newer-during-late'; method = 'filter.patchSettings'
        ifRevision = $State.Revision
        params = @{ filter = $State.FilterA.Handle; settings = @{ value = 100; blockMs = 0 } }
    }
    $newerWatch.Stop()
    Write-Host "newer timeout elapsed=$($newerWatch.Elapsed.TotalSeconds) seconds"
    Assert-Error $newerDuringLate 'timeout' $State.Revision 'new filter update during late completion'
    $secondResync = Read-Until-Resync ($State.Revision + 1)
    $State.Revision = [int64]$secondResync.revision
    $lateResync = Read-Until-Resync ($State.Revision + 1)
    Assert-NoLateSettingsEvent $State.FilterA.Handle 'late completion'
    $State.Revision = [int64]$lateResync.revision
}

function Invoke-Task11SafeAfterLate([object] $State) {
    # A timed-out newer request may still have its own late callback after A's
    # boundary. The safe follow-up deliberately omits ifRevision and retries
    # only across an observed resync, so an asynchronous boundary cannot turn
    # into a false revision conflict.
    $safeAfterLate = $null
    for ($attempt = 1; $attempt -le 3 -and $null -eq $safeAfterLate; $attempt++) {
        $safeWatch = [System.Diagnostics.Stopwatch]::StartNew()
        $safeCandidate = Send-V2Request @{
            op = 'request'; id = "task11.safe-after-late.$attempt"; method = 'filter.patchSettings'
            params = @{ filter = $State.FilterA.Handle; settings = @{ value = 101; blockMs = 0 } }
        }
        $safeWatch.Stop()
        Write-Host "safe update attempt=$attempt elapsed=$($safeWatch.Elapsed.TotalSeconds) seconds"
        if ($safeCandidate.status.ok) {
            $safeAfterLate = $safeCandidate
            break
        }
        Assert-Error $safeCandidate 'timeout' $State.Revision "filter update after late uncertainty attempt $attempt"
        $safeRetryBatch = [System.Collections.Generic.List[object]]::new()
        $safeRetryResync = Read-Until-Resync ($State.Revision + 1) $safeRetryBatch
        $State.Revision = [int64]$safeRetryResync.revision
    }
    if ($null -eq $safeAfterLate) {
        Fail 'filter update after late uncertainty did not recover within three bounded attempts.'
    }
    $State.Revision = [int64]$safeAfterLate.revision
    $null = Read-SafeSettingsEvent $State.FilterA.Handle 101 $State.Revision
}

function Invoke-Task11Overflow([object] $State) {
    # The fixture emits 1100 peer update observations during D's update,
    # exceeding the bounded deferred bridge and forcing a resync.
    $burst = Send-V2Request @{
        op = 'request'; id = 'task11.overflow'; method = 'filter.patchSettings'; ifRevision = $State.Revision
        params = @{ filter = $State.FilterD.Handle; settings = @{ value = 102; burst = $true } }
    }
    Assert-Error $burst 'timeout' $State.Revision 'deferred filter queue overflow'
    $overflowResync = Read-Until-Resync ($State.Revision + 1)
    Assert-NoLateSettingsEvent $State.FilterD.Handle 'overflow'
    $State.Revision = [int64]$overflowResync.revision
    $originalList = Send-V2Request @{
        op = 'request'; id = 'task11.parent-list-before-remove'; method = 'filter.list'
        params = @{ source = $State.Parent.Handle }
    }
    Assert-Ok $originalList $State.Revision 'parent filter list before source removal'
    $State.OriginalHandles = @($originalList.data.filters | ForEach-Object { [string]$_.filter })
    if ($State.OriginalHandles.Count -ne 3) {
        Fail 'parent filter list changed unexpectedly before source removal.'
    }
}

function Remove-Task11ParentSource([object] $State) {
    $sourceRemove = Send-V2Request @{
        op = 'request'; id = 'task11.source-remove'; method = 'source.remove'; ifRevision = $State.Revision
        params = @{ source = $State.Parent.Handle }
    }
    Assert-Ok $sourceRemove ($State.Revision + 1) 'source.remove with filters'
    $State.Revision++
    foreach ($handle in $State.OriginalHandles) {
        $null = Read-Event 'filter.removed' $State.Revision $handle $State.Parent.Handle
    }
    $null = Read-Event 'source.removed' $State.Revision '' $State.Parent.Handle
    foreach ($handle in $State.OriginalHandles) {
        $staleFilter = Send-V2Request @{
            op = 'request'; id = "task11.stale.$handle"; method = 'filter.get'
            params = @{ filter = $handle }
        }
        Assert-Error $staleFilter 'not_found' $State.Revision "stale removed filter $handle"
    }
}

function Remove-Task11DuplicateSource([object] $State) {
    $duplicateSourceRemove = Send-V2Request @{
        op = 'request'; id = 'task11.duplicate-source-remove'; method = 'source.remove'
        ifRevision = $State.Revision; params = @{ source = $State.DuplicateSource }
    }
    Assert-Ok $duplicateSourceRemove ($State.Revision + 1) 'source.remove duplicated source'
    $State.Revision++
    foreach ($handle in $State.InheritedHandles) {
        $null = Read-Event 'filter.removed' $State.Revision $handle $State.DuplicateSource
    }
    $null = Read-Event 'source.removed' $State.Revision '' $State.DuplicateSource
    $inheritedStale = Send-V2Request @{
        op = 'request'; id = 'task11.inherited-stale'; method = 'filter.get'
        params = @{ filter = $State.InheritedHandles[0] }
    }
    Assert-Error $inheritedStale 'not_found' $State.Revision 'stale inherited filter handle'
    Assert-NoQueuedEvents 'final filter event queue'
}

function Complete-Task11Scenario([object] $State) {
    Invoke-Task11Overflow $State
    Remove-Task11ParentSource $State
    Remove-Task11DuplicateSource $State
    $close = Send-V2Request @{
        op = 'request'; id = 'task11.close'; method = 'session.close'; ifRevision = $State.Revision; params = @{}
    }
    Assert-Ok $close ($State.Revision + 1) 'session.close'
    $script:Process.StandardInput.Close()
    if (-not $script:Process.WaitForExit(30000)) {
        Fail 'obs-engine did not exit after session.close.'
    }
    if ($script:Process.ExitCode -ne 0) {
        Fail "obs-engine exited with code $($script:Process.ExitCode)."
    }
    $stderr = $script:ErrorTask.GetAwaiter().GetResult()
    if ($stderr -notmatch '\[task11-filter\] deterministic parent/filter module loaded') {
        Fail 'deterministic Task 11 module-load evidence was missing from stderr.'
    }
    Write-Host 'Task 11 filter integration: PASS' -ForegroundColor Green
}

function Invoke-Task11Scenario {
    Initialize-Task11Session
    $state = New-Task11State
    Initialize-Task11FilterGraph $state
    Assert-Task11LiveFilterReads $state
    Invoke-Task11FilterMutations $state
    Assert-Task11InitialOrder $state
    Invoke-Task11OrderMutations $state
    Invoke-Task11OrderValidation $state
    Invoke-Task11DuplicateAndRemove $state
    Invoke-Task11SourceDuplicate $state
    Invoke-Task11UnrelatedUpdate $state
    Invoke-Task11BlockingUpdate $state
    Invoke-Task11TimeoutStart $state
    Invoke-Task11LateTimeouts $state
    Invoke-Task11SafeAfterLate $state
    Complete-Task11Scenario $state
}

function Stop-Task11AfterFailure {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) {
        try { $script:Process.Kill($true) } catch {}
        try { $script:Process.WaitForExit(5000) | Out-Null } catch {}
    }
    if ($null -ne $script:ErrorTask) {
        try {
            $stderr = $script:ErrorTask.GetAwaiter().GetResult()
            if ($stderr) {
                Write-Host '=== obs-engine stderr ==='
                Write-Host $stderr
            }
        } catch {}
    }
}

function Stop-Task11Engine {
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
    Start-Task11Engine $InstallRoot
    Invoke-Task11Scenario
}
catch {
    Stop-Task11AfterFailure
    throw
}
finally {
    Stop-Task11Engine
}

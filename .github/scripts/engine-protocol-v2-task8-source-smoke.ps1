$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Task8InstallRoot = $null
$script:Task8DiagnosticFile = $null
$script:Task8Process = $null
$script:Task8ErrorTask = $null
$script:Task8Engine = $null
$script:Task8FailureText = $null
$script:Task8NextSeq = [uint64]1

function Initialize-Task8Runtime {
  $script:Task8InstallRoot = Resolve-Path 'build_x64/install'
  $DiagnosticDir = Join-Path $script:Task8InstallRoot '_task8-diagnostics'
  New-Item -ItemType Directory -Force -Path $DiagnosticDir | Out-Null
  $script:Task8DiagnosticFile = Join-Path $DiagnosticDir 'v2-source-smoke.txt'
  $script:Task8Engine = Get-ChildItem -Path $script:Task8InstallRoot -Filter 'obs-engine.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -eq $script:Task8Engine ) { throw 'obs-engine.exe was not found in the installed runtime.' }
  $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
  $StartInfo.FileName = $script:Task8Engine.FullName
  $StartInfo.WorkingDirectory = $script:Task8Engine.Directory.FullName
  $StartInfo.UseShellExecute = $false
  $StartInfo.RedirectStandardInput = $true
  $StartInfo.RedirectStandardOutput = $true
  $StartInfo.RedirectStandardError = $true
  $StartInfo.CreateNoWindow = $true
  $script:Task8Process = [System.Diagnostics.Process]::new()
  $script:Task8Process.StartInfo = $StartInfo
  if ( -not $script:Task8Process.Start() ) { throw 'Failed to start obs-engine.exe.' }
  $script:Task8ErrorTask = $script:Task8Process.StandardError.ReadToEndAsync()
}

function Read-Task8EngineMessage {
  $ReadTask = $script:Task8Process.StandardOutput.ReadLineAsync()
  if ( -not $ReadTask.Wait(30000) ) { throw 'Timed out waiting 30 seconds for obs-engine stdout.' }
  $Line = $ReadTask.Result
  if ( $null -eq $Line ) {
    $ExitText = if ( $script:Task8Process.HasExited ) { "exit=$($script:Task8Process.ExitCode)" } else { 'process still running' }
    throw "obs-engine closed stdout unexpectedly ($ExitText)."
  }
  Write-Host "obs-engine stdout: $Line"
  return ($Line | ConvertFrom-Json)
}

function Send-Task8Request([hashtable] $Request) {
  $Json = $Request | ConvertTo-Json -Compress -Depth 50
  Write-Host "obs-engine stdin:  $Json"
  $script:Task8Process.StandardInput.WriteLine($Json)
  $script:Task8Process.StandardInput.Flush()
  $Response = Read-Task8EngineMessage
  if ( $Response.op -ne 'response' -or [string]$Response.id -ne [string]$Request.id ) {
    throw "Expected response for '$($Request.id)' but received a different message."
  }
  return $Response
}

function Read-Task8StateEvent([string] $Name, [int64] $Revision) {
  $Event = Read-Task8EngineMessage
  if ( $Event.op -ne 'event' -or [string]$Event.event -ne $Name ) {
    throw "Expected event '$Name' but received '$($Event.event)'."
  }
  if ( [uint64]$Event.seq -ne $script:Task8NextSeq ) {
    throw "Event '$Name' had seq=$($Event.seq), expected $script:Task8NextSeq."
  }
  if ( [int64]$Event.revision -ne $Revision ) {
    throw "Event '$Name' had revision=$($Event.revision), expected $Revision."
  }
  $script:Task8NextSeq++
  return $Event
}

function Assert-Task8CanonicalHandle([string] $Handle, [string] $Label) {
  if ( $Handle -notmatch '^[1-9][0-9]*$' ) { throw "$Label was not a canonical decimal string handle: '$Handle'" }
}

function Assert-Task8Capabilities([object] $Hello) {
  $Required = @(
    'source.v1'; 'source.kindList.v1'; 'source.kindGet.v1'; 'source.kindDefaults.v1'; 'source.kindProperties.v1'
    'source.list.v1'; 'source.get.v1'; 'source.create.v1'; 'source.duplicate.v1'; 'source.remove.v1'
    'source.rename.v1'; 'source.getSettings.v1'; 'source.patchSettings.v1'; 'source.replaceSettings.v1'
    'source.resetSettings.v1'; 'source.getProperties.v1'; 'source.getFlags.v1'; 'source.getDimensions.v1'
    'source.getState.v1'; 'source.getActive.v1'; 'source.getShowing.v1'; 'source.getMissingFiles.v1'
    'source.refresh.v1'; 'source.saveState.v1'; 'source.loadState.v1'
  )
  $CapabilityNames = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
  foreach ( $RequiredName in $Required ) {
    if ( $CapabilityNames -notcontains $RequiredName ) { throw "Task 8 capability was not advertised: $RequiredName" }
  }
}

function Get-Task8ColorKind([object] $Kinds) {
  $ColorKind = $Kinds.data.kinds | Where-Object { $_.id -eq 'color_source_v3' } | Select-Object -First 1
  if ( $null -eq $ColorKind ) { $ColorKind = $Kinds.data.kinds | Where-Object { $_.id -eq 'color_source' } | Select-Object -First 1 }
  if ( $null -eq $ColorKind ) { throw 'No Color Source kind was registered.' }
  return [string]$ColorKind.id
}

function Invoke-Task8Bootstrap {
  $Ready = Read-Task8EngineMessage
  if ( $Ready.event -ne 'ready' -or [int]$Ready.protocol -ne 1 ) { throw 'Migration bootstrap ready event changed unexpectedly.' }
  $Hello = Send-Task8Request @{ op = 'request'; id = 'task8.hello'; method = 'session.hello'; params = @{} }
  if ( -not $Hello.status.ok -or [int64]$Hello.revision -ne 0 ) { throw 'session.hello failed or a new engine did not begin at revision 0.' }
  Assert-Task8Capabilities $Hello
}

function Initialize-Task8Protocol {
  Invoke-Task8Bootstrap
  $Subscribe = Send-Task8Request @{
    op = 'request'; id = 'task8.subscribe'; method = 'session.subscribe'
    params = @{ subscriptions = @(@{ pattern = 'source.*' }) }
  }
  if ( -not $Subscribe.status.ok -or [int64]$Subscribe.revision -ne 0 ) { throw 'Task 8 source event subscription failed.' }
  $Kinds = Send-Task8Request @{ op = 'request'; id = 'task8.kinds'; method = 'source.kindList'; params = @{} }
  return Get-Task8ColorKind $Kinds
}

function Invoke-Task8KindQueries([string] $KindId) {
  Invoke-Task8KindMetadata $KindId
  Invoke-Task8InitialSourceList
}

function Invoke-Task8KindMetadata([string] $KindId) {
  Invoke-Task8KindGet $KindId
  Invoke-Task8KindDefaults $KindId
  Invoke-Task8KindProperties $KindId
}

function Invoke-Task8KindGet([string] $KindId) {
  $Kind = Send-Task8Request @{ op = 'request'; id = 'task8.kind'; method = 'source.kindGet'; params = @{ kind = $KindId } }
  if ( -not $Kind.status.ok -or [int64]$Kind.revision -ne 0 -or [string]$Kind.data.id -ne $KindId -or
       -not [bool]$Kind.data.hasVideo -or [bool]$Kind.data.hasAudio ) {
    throw 'source.kindGet did not expose correct semantic Color Source flags.'
  }
}

function Invoke-Task8KindDefaults([string] $KindId) {
  $KindDefaults = Send-Task8Request @{ op = 'request'; id = 'task8.kind-defaults'; method = 'source.kindDefaults'; params = @{ kind = $KindId } }
  if ( -not $KindDefaults.status.ok -or [int64]$KindDefaults.revision -ne 0 ) { throw 'source.kindDefaults failed or mutated revision state.' }
}

function Invoke-Task8KindProperties([string] $KindId) {
  $KindProperties = Send-Task8Request @{ op = 'request'; id = 'task8.kind-properties'; method = 'source.kindProperties'; params = @{ kind = $KindId } }
  if ( -not $KindProperties.status.ok -or [int64]$KindProperties.revision -ne 0 -or
       [string]$KindProperties.data.target.type -ne 'sourceKind' -or [string]$KindProperties.data.target.kind -ne $KindId ) {
    throw 'source.kindProperties did not delegate to the generic source-kind property form.'
  }
}

function Invoke-Task8InitialSourceList {
  $EmptyList = Send-Task8Request @{ op = 'request'; id = 'task8.empty-list'; method = 'source.list'; params = @{} }
  if ( -not $EmptyList.status.ok -or @($EmptyList.data.sources).Count -ne 0 ) { throw 'source.list was not empty before Task 8 created a source.' }
}

function Invoke-Task8SourceCreation([string] $KindId) {
  $Create = Send-Task8Request @{
    op = 'request'; id = 'task8.create'; method = 'source.create'; ifRevision = 0
    params = @{ kind = $KindId; name = 'task8-color'; settings = @{ width = 320; height = 180; color = 4294901760 } }
  }
  if ( -not $Create.status.ok -or [int64]$Create.revision -ne 1 ) { throw 'source.create did not commit revision 1.' }
  $Source = [string]$Create.data.source
  Assert-Task8CanonicalHandle $Source 'source.create handle'
  $CreatedEvent = Read-Task8StateEvent 'source.created' 1
  if ( [string]$CreatedEvent.data.source -ne $Source ) { throw 'source.created identified the wrong source.' }
  return $Source
}

function Invoke-Task8SourceIdentity([string] $Source, [string] $KindId) {
  $List = Send-Task8Request @{ op = 'request'; id = 'task8.list'; method = 'source.list'; params = @{} }
  if ( @($List.data.sources).Count -ne 1 -or [string]$List.data.sources[0].source -ne $Source ) { throw 'source.list did not expose the engine-managed source.' }
  $Get = Send-Task8Request @{ op = 'request'; id = 'task8.get'; method = 'source.get'; params = @{ source = $Source } }
  if ( [string]$Get.data.name -ne 'task8-color' -or [string]$Get.data.kind -ne $KindId ) { throw 'source.get returned incorrect source identity.' }
  $Flags = Send-Task8Request @{ op = 'request'; id = 'task8.flags'; method = 'source.getFlags'; params = @{ source = $Source } }
  if ( -not [bool]$Flags.data.hasVideo -or [bool]$Flags.data.hasAudio ) { throw 'source.getFlags returned incorrect semantic Color Source flags.' }
  $Dimensions = Send-Task8Request @{ op = 'request'; id = 'task8.dimensions'; method = 'source.getDimensions'; params = @{ source = $Source } }
  if ( [int]$Dimensions.data.width -ne 320 -or [int]$Dimensions.data.height -ne 180 ) { throw 'source.getDimensions did not return initial Color Source dimensions.' }
}

function Invoke-Task8ActivityChecks([string] $Source) {
  Invoke-Task8ActivityState $Source
  Invoke-Task8LiveProperties $Source
}

function Invoke-Task8ActivityState([string] $Source) {
  $Active = Send-Task8Request @{ op = 'request'; id = 'task8.active'; method = 'source.getActive'; params = @{ source = $Source } }
  $Showing = Send-Task8Request @{ op = 'request'; id = 'task8.showing'; method = 'source.getShowing'; params = @{ source = $Source } }
  if ( [bool]$Active.data.active -or [bool]$Showing.data.showing ) { throw 'A newly-created private Color Source unexpectedly began active/showing.' }
  $State = Send-Task8Request @{ op = 'request'; id = 'task8.state'; method = 'source.getState'; params = @{ source = $Source } }
  if ( [string]$State.data.source -ne $Source -or [int]$State.data.dimensions.width -ne 320 -or [int]$State.data.settings.width -ne 320 ) { throw 'source.getState did not return the combined live source snapshot.' }
}

function Invoke-Task8LiveProperties([string] $Source) {
  $LiveProperties = Send-Task8Request @{ op = 'request'; id = 'task8.properties'; method = 'source.getProperties'; params = @{ source = $Source } }
  if ( -not $LiveProperties.status.ok -or [string]$LiveProperties.data.target.type -ne 'source' -or [string]$LiveProperties.data.target.source -ne $Source ) { throw 'source.getProperties did not delegate to the live-source generic property form.' }
  $Missing = Send-Task8Request @{ op = 'request'; id = 'task8.missing'; method = 'source.getMissingFiles'; params = @{ source = $Source } }
  if ( -not $Missing.status.ok -or [int]$Missing.data.count -ne 0 ) { throw 'Color Source unexpectedly reported missing files.' }
}

function Invoke-Task8Rename([string] $Source) {
  $StaleRename = Send-Task8Request @{
    op = 'request'; id = 'task8.stale-rename'; method = 'source.rename'; ifRevision = 0
    params = @{ source = $Source; name = 'must-not-apply' }
  }
  if ( $StaleRename.status.ok -or $StaleRename.status.code -ne 'revision_conflict' -or [int64]$StaleRename.revision -ne 1 ) { throw 'A stale source.rename was not rejected at revision 1.' }
  $Rename = Send-Task8Request @{
    op = 'request'; id = 'task8.rename'; method = 'source.rename'; ifRevision = 1
    params = @{ source = $Source; name = 'task8-renamed' }
  }
  if ( -not $Rename.status.ok -or [int64]$Rename.revision -ne 2 ) { throw 'source.rename did not commit revision 2.' }
  $RenamedEvent = Read-Task8StateEvent 'source.renamed' 2
  if ( [string]$RenamedEvent.data.previousName -ne 'task8-color' -or [string]$RenamedEvent.data.name -ne 'task8-renamed' ) { throw 'source.renamed payload was incorrect.' }
}

function Invoke-Task8Duplicate([string] $Source) {
  $Duplicate = Send-Task8Request @{
    op = 'request'; id = 'task8.duplicate'; method = 'source.duplicate'; ifRevision = 2
    params = @{ source = $Source; name = 'task8-copy' }
  }
  if ( -not $Duplicate.status.ok -or [int64]$Duplicate.revision -ne 3 ) { throw 'source.duplicate did not commit revision 3.' }
  $Copy = [string]$Duplicate.data.source
  Assert-Task8CanonicalHandle $Copy 'source.duplicate handle'
  if ( $Copy -eq $Source -or [string]$Duplicate.data.duplicateOf -ne $Source ) { throw 'source.duplicate did not create an independent engine object.' }
  $DuplicateEvent = Read-Task8StateEvent 'source.created' 3
  if ( [string]$DuplicateEvent.data.source -ne $Copy -or [string]$DuplicateEvent.data.duplicateOf -ne $Source ) { throw 'Duplicated source.created payload was incorrect.' }
  return $Copy
}

function Invoke-Task8ReplaceAndSave([string] $Source, [string] $KindId) {
  Invoke-Task8ReplaceSettings $Source
  return Invoke-Task8SaveState $Source $KindId
}

function Invoke-Task8ReplaceSettings([string] $Source) {
  $Replace = Send-Task8Request @{
    op = 'request'; id = 'task8.replace'; method = 'source.replaceSettings'; ifRevision = 3
    params = @{ source = $Source; settings = @{ width = 640; height = 360; color = 4278255360 } }
  }
  if ( -not $Replace.status.ok -or [int64]$Replace.revision -ne 4 -or [int]$Replace.data.settings.width -ne 640 -or [int]$Replace.data.settings.height -ne 360 ) { throw 'source.replaceSettings did not commit/read back revision 4.' }
  $ReplaceSettingsEvent = Read-Task8StateEvent 'source.settingsChanged' 4
  $ReplaceDimensionsEvent = Read-Task8StateEvent 'source.dimensionsChanged' 4
  if ( [int]$ReplaceDimensionsEvent.data.width -ne 640 -or [int]$ReplaceDimensionsEvent.data.height -ne 360 ) { throw 'source.replaceSettings did not emit resulting dimensions.' }
}

function Invoke-Task8SaveState([string] $Source, [string] $KindId) {
  $Saved = Send-Task8Request @{ op = 'request'; id = 'task8.save'; method = 'source.saveState'; params = @{ source = $Source } }
  if ( -not $Saved.status.ok -or [int64]$Saved.revision -ne 4 -or [int]$Saved.data.state.version -ne 1 -or
       [string]$Saved.data.state.kind -ne $KindId -or [string]$Saved.data.state.name -ne 'task8-renamed' -or
       [int]$Saved.data.state.settings.width -ne 640 ) { throw 'source.saveState did not return the versioned source-local state without mutating revision.' }
  return $Saved.data.state
}

function Invoke-Task8ResetAndLoad([string] $Source, [object] $SavedState) {
  Invoke-Task8ResetSettings $Source
  Invoke-Task8LoadState $Source $SavedState
}

function Invoke-Task8ResetSettings([string] $Source) {
  $Reset = Send-Task8Request @{ op = 'request'; id = 'task8.reset'; method = 'source.resetSettings'; ifRevision = 4; params = @{ source = $Source } }
  if ( -not $Reset.status.ok -or [int64]$Reset.revision -ne 5 ) { throw 'source.resetSettings did not commit revision 5.' }
  $ResetSettingsEvent = Read-Task8StateEvent 'source.settingsChanged' 5
  $ResetDimensionsEvent = Read-Task8StateEvent 'source.dimensionsChanged' 5
}

function Invoke-Task8LoadState([string] $Source, [object] $SavedState) {
  $SavedState.name = 'task8-loaded'
  $SavedState.settings.width = 800
  $SavedState.settings.height = 450
  $Load = Send-Task8Request @{
    op = 'request'; id = 'task8.load'; method = 'source.loadState'; ifRevision = 5
    params = @{ source = $Source; state = $SavedState }
  }
  if ( -not $Load.status.ok -or [int64]$Load.revision -ne 6 -or [string]$Load.data.state.name -ne 'task8-loaded' -or
       [int]$Load.data.state.settings.width -ne 800 -or [int]$Load.data.state.settings.height -ne 450 ) { throw 'source.loadState did not restore the edited versioned state at revision 6.' }
  $LoadRenamedEvent = Read-Task8StateEvent 'source.renamed' 6
  $LoadSettingsEvent = Read-Task8StateEvent 'source.settingsChanged' 6
  $LoadDimensionsEvent = Read-Task8StateEvent 'source.dimensionsChanged' 6
  if ( [string]$LoadRenamedEvent.data.name -ne 'task8-loaded' -or [int]$LoadDimensionsEvent.data.width -ne 800 -or [int]$LoadDimensionsEvent.data.height -ne 450 ) { throw 'source.loadState normalized source events incorrectly.' }
  Invoke-Task8Refresh $Source
}

function Invoke-Task8Refresh([string] $Source) {
  $Refresh = Send-Task8Request @{ op = 'request'; id = 'task8.refresh'; method = 'source.refresh'; params = @{ source = $Source } }
  if ( -not $Refresh.status.ok -or [int64]$Refresh.revision -ne 6 -or -not [bool]$Refresh.data.refreshed ) { throw 'source.refresh failed or incorrectly consumed a revision.' }
}

function Invoke-Task8SourceRemoval([string] $Source, [string] $Copy) {
  $RemoveCopy = Send-Task8Request @{ op = 'request'; id = 'task8.remove-copy'; method = 'source.remove'; ifRevision = 6; params = @{ source = $Copy } }
  if ( -not $RemoveCopy.status.ok -or [int64]$RemoveCopy.revision -ne 7 ) { throw 'Removing the duplicated source did not commit revision 7.' }
  $CopyRemoved = Read-Task8StateEvent 'source.removed' 7
  if ( [string]$CopyRemoved.data.source -ne $Copy ) { throw 'source.removed identified the wrong duplicated source.' }

  $Remove = Send-Task8Request @{ op = 'request'; id = 'task8.remove'; method = 'source.remove'; ifRevision = 7; params = @{ source = $Source } }
  if ( -not $Remove.status.ok -or [int64]$Remove.revision -ne 8 ) { throw 'Removing the original source did not commit revision 8.' }
  $Removed = Read-Task8StateEvent 'source.removed' 8
  if ( [string]$Removed.data.source -ne $Source ) { throw 'source.removed identified the wrong original source.' }

  $FinalList = Send-Task8Request @{ op = 'request'; id = 'task8.final-list'; method = 'source.list'; params = @{} }
  if ( @($FinalList.data.sources).Count -ne 0 -or [int64]$FinalList.revision -ne 8 ) { throw 'source.list was not empty after removing both Task 8 sources.' }
}

function Complete-Task8Session {
  $Close = Send-Task8Request @{ op = 'request'; id = 'task8.close'; method = 'session.close'; params = @{}; ifRevision = 8 }
  if ( -not $Close.status.ok -or [int64]$Close.revision -ne 9 ) { throw 'session.close did not commit revision 9 after the Task 8 lifecycle.' }
  $script:Task8Process.StandardInput.Close()
  if ( -not $script:Task8Process.WaitForExit(15000) ) { $script:Task8Process.Kill($true); throw 'obs-engine did not exit after Task 8 session.close.' }
  if ( $script:Task8Process.ExitCode -ne 0 ) { throw "obs-engine exited with code $($script:Task8Process.ExitCode)." }
  Write-Host "Protocol-v2 complete source namespace smoke test passed (next seq=$($script:Task8NextSeq))."
}

function Invoke-Task8SourceScenario {
  $KindId = Initialize-Task8Protocol
  Invoke-Task8KindQueries $KindId
  $Source = Invoke-Task8SourceCreation $KindId
  Invoke-Task8SourceIdentity $Source $KindId
  Invoke-Task8ActivityChecks $Source
  Invoke-Task8Rename $Source
  $Copy = Invoke-Task8Duplicate $Source
  $SavedState = Invoke-Task8ReplaceAndSave $Source $KindId
  Invoke-Task8ResetAndLoad $Source $SavedState
  Invoke-Task8SourceRemoval $Source $Copy
  Complete-Task8Session
}

function Write-Task8Diagnostics {
  $StderrText = ''
  if ( $null -ne $script:Task8ErrorTask ) {
    try { $StderrText = $script:Task8ErrorTask.GetAwaiter().GetResult() }
    catch { $StderrText = "Failed to collect redirected stderr: $_" }
  }
  $ExitState = 'not-started'
  if ( $null -ne $script:Task8Process ) {
    if ( $script:Task8Process.HasExited ) { $ExitState = "exited:$($script:Task8Process.ExitCode)" }
    else { $ExitState = 'still-running' }
  }
  @(
    "engine=$($script:Task8Engine.FullName)"; "exit_state=$ExitState"; "next_seq=$($script:Task8NextSeq)"; ''
    '=== smoke failure ==='; $script:Task8FailureText; ''; '=== obs-engine stderr ==='; $StderrText
  ) | Set-Content -Path $script:Task8DiagnosticFile -Encoding utf8
}

function Invoke-Task8SourceSmoke {
  try {
    Initialize-Task8Runtime
    Invoke-Task8SourceScenario
  }
  catch {
    $script:Task8FailureText = ($_ | Out-String)
    Write-Error $script:Task8FailureText
    if ( $null -ne $script:Task8Process -and -not $script:Task8Process.HasExited ) {
      try { $script:Task8Process.Kill($true); $script:Task8Process.WaitForExit(5000) | Out-Null }
      catch { Write-Warning "Failed to terminate obs-engine after Task 8 smoke failure: $_" }
    }
  }
  finally { Write-Task8Diagnostics }
  if ( $null -ne $script:Task8FailureText ) { throw 'Protocol-v2 source namespace smoke test failed. See _task8-diagnostics/v2-source-smoke.txt in the runtime artifact.' }
}

Invoke-Task8SourceSmoke

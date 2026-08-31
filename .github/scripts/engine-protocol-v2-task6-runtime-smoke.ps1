$ErrorActionPreference = 'Stop'

$script:Task6InstallRoot = $null
$script:Task6DiagnosticFile = $null
$script:Task6Process = $null
$script:Task6ErrorTask = $null
$script:Task6Engine = $null
$script:Task6FailureText = $null
$script:Task6NextSeq = [uint64]1

function Initialize-Task6Runtime {
  $script:Task6InstallRoot = Resolve-Path 'build_x64/install'
  $DiagnosticDir = Join-Path $script:Task6InstallRoot '_task6-diagnostics'
  New-Item -ItemType Directory -Force -Path $DiagnosticDir | Out-Null
  $script:Task6DiagnosticFile = Join-Path $DiagnosticDir 'v2-runtime-smoke.txt'

  $script:Task6Engine = Get-ChildItem -Path $script:Task6InstallRoot -Filter 'obs-engine.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -eq $script:Task6Engine ) {
    throw 'obs-engine.exe was not found in the installed runtime.'
  }

  $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
  $StartInfo.FileName = $script:Task6Engine.FullName
  $StartInfo.WorkingDirectory = $script:Task6Engine.Directory.FullName
  $StartInfo.UseShellExecute = $false
  $StartInfo.RedirectStandardInput = $true
  $StartInfo.RedirectStandardOutput = $true
  $StartInfo.RedirectStandardError = $true
  $StartInfo.CreateNoWindow = $true

  $script:Task6Process = [System.Diagnostics.Process]::new()
  $script:Task6Process.StartInfo = $StartInfo
  if ( -not $script:Task6Process.Start() ) {
    throw 'Failed to start obs-engine.exe.'
  }
  $script:Task6ErrorTask = $script:Task6Process.StandardError.ReadToEndAsync()
}

function Read-Task6EngineMessage {
  $ReadTask = $script:Task6Process.StandardOutput.ReadLineAsync()
  if ( -not $ReadTask.Wait(30000) ) {
    throw 'Timed out waiting 30 seconds for obs-engine stdout.'
  }
  $Line = $ReadTask.Result
  if ( $null -eq $Line ) {
    $ExitText = if ( $script:Task6Process.HasExited ) { "exit=$($script:Task6Process.ExitCode)" } else { 'process still running' }
    throw "obs-engine closed stdout unexpectedly ($ExitText)."
  }
  Write-Host "obs-engine stdout: $Line"
  return ($Line | ConvertFrom-Json)
}

function Send-Task6Request([hashtable] $Request) {
  $Json = $Request | ConvertTo-Json -Compress -Depth 30
  Write-Host "obs-engine stdin:  $Json"
  $script:Task6Process.StandardInput.WriteLine($Json)
  $script:Task6Process.StandardInput.Flush()
  $Response = Read-Task6EngineMessage
  if ( $Response.op -ne 'response' -or [string]$Response.id -ne [string]$Request.id ) {
    throw "Expected response for '$($Request.id)' but received a different message."
  }
  return $Response
}

function Read-Task6StateEvent([string] $Name, [int64] $Revision) {
  $Event = Read-Task6EngineMessage
  if ( $Event.op -ne 'event' -or [string]$Event.event -ne $Name ) {
    throw "Expected event '$Name' but received '$($Event.event)'."
  }
  if ( [uint64]$Event.seq -ne $script:Task6NextSeq ) {
    throw "Event '$Name' had seq=$($Event.seq), expected $script:Task6NextSeq."
  }
  if ( [int64]$Event.revision -ne $Revision ) {
    throw "Event '$Name' had revision=$($Event.revision), expected $Revision."
  }
  if ( $null -ne $Event.telemetry ) {
    throw "State event '$Name' was incorrectly marked as telemetry."
  }
  $script:Task6NextSeq++
  return $Event
}

function Assert-Task6CanonicalHandle([string] $Handle, [string] $Label) {
  if ( $Handle -notmatch '^[1-9][0-9]*$' ) {
    throw "$Label was not returned as a canonical decimal string handle: '$Handle'"
  }
}

function Assert-Task6Capabilities([object] $Hello) {
  $RequiredCapabilities = @(
    'item.create.v1'
    'item.remove.v1'
    'item.setTransform.v1'
    'scene.create.v1'
    'scene.remove.v1'
    'source.create.v1'
    'source.getSettings.v1'
    'source.kindDefaults.v1'
    'source.kindList.v1'
    'source.patchSettings.v1'
    'source.remove.v1'
  )
  $CapabilityNames = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
  foreach ( $Required in $RequiredCapabilities ) {
    if ( $CapabilityNames -notcontains $Required ) {
      throw "Task 6 capability was not advertised: $Required"
    }
  }
  foreach ( $ForbiddenNamespace in @('scene.v1', 'item.v1') ) {
    if ( $CapabilityNames -contains $ForbiddenNamespace ) {
      throw "Partial Task 6 support must not claim complete namespace capability $ForbiddenNamespace."
    }
  }
}

function Get-Task6ColorKind([object] $Kinds) {
  $ColorKind = $Kinds.data.kinds | Where-Object { $_.id -eq 'color_source_v3' } | Select-Object -First 1
  if ( $null -eq $ColorKind ) {
    $ColorKind = $Kinds.data.kinds | Where-Object { $_.id -eq 'color_source' } | Select-Object -First 1
  }
  if ( $null -eq $ColorKind ) {
    throw 'No Color Source kind was registered in source.kindList.'
  }
  return $ColorKind
}

function Invoke-Task6Bootstrap {
  $Ready = Read-Task6EngineMessage
  if ( $Ready.event -ne 'ready' -or [int]$Ready.protocol -ne 1 ) {
    throw 'Migration bootstrap ready event changed unexpectedly.'
  }
  $Hello = Send-Task6Request @{
    op = 'request'
    id = 'task6.hello'
    method = 'session.hello'
    params = @{}
  }
  if ( -not $Hello.status.ok -or [int64]$Hello.revision -ne 0 ) {
    throw 'session.hello failed or a new engine did not begin at revision 0.'
  }
  Assert-Task6Capabilities $Hello
}

function Initialize-Task6Protocol {
  Invoke-Task6Bootstrap
  $Kinds = Send-Task6Request @{
    op = 'request'
    id = 'task6.kinds'
    method = 'source.kindList'
    params = @{}
  }
  if ( -not $Kinds.status.ok -or [int64]$Kinds.revision -ne 0 ) {
    throw 'source.kindList failed or changed the engine revision.'
  }
  $ColorKind = Get-Task6ColorKind $Kinds

  Invoke-Task6Defaults $ColorKind
  Invoke-Task6BadCreate $ColorKind
  Invoke-Task6Subscribe
  return [string]$ColorKind.id
}

function Invoke-Task6Defaults([object] $ColorKind) {
  $Defaults = Send-Task6Request @{
    op = 'request'
    id = 'task6.defaults'
    method = 'source.kindDefaults'
    params = @{ kind = [string]$ColorKind.id }
  }
  if ( -not $Defaults.status.ok -or [int64]$Defaults.revision -ne 0 -or
       [string]$Defaults.data.kind -ne [string]$ColorKind.id -or $null -eq $Defaults.data.settings ) {
    throw 'source.kindDefaults did not return defaults without mutating revision state.'
  }
}

function Invoke-Task6BadCreate([object] $ColorKind) {
  $BadCreate = Send-Task6Request @{
    op = 'request'
    id = 'task6.bad-create'
    method = 'source.create'
    params = @{ kind = [string]$ColorKind.id; settings = 7 }
    ifRevision = 0
  }
  if ( $BadCreate.status.ok -or $BadCreate.status.code -ne 'bad_request' -or [int64]$BadCreate.revision -ne 0 ) {
    throw 'source.create accepted wrong-typed settings or consumed revision state on rejection.'
  }
}

function Invoke-Task6Subscribe {
  $Subscribe = Send-Task6Request @{
    op = 'request'
    id = 'task6.subscribe'
    method = 'session.subscribe'
    params = @{ subscriptions = @(
      @{ pattern = 'item.*' }
      @{ pattern = 'scene.*' }
      @{ pattern = 'source.*' }
    ) }
  }
  if ( -not $Subscribe.status.ok -or [int64]$Subscribe.revision -ne 0 ) {
    throw 'Task 6 event subscription setup failed.'
  }
}

function Invoke-Task6SceneCreation {
  $Scene = Send-Task6Request @{
    op = 'request'
    id = 'task6.scene-create'
    method = 'scene.create'
    params = @{ name = 'task6-scene' }
    ifRevision = 0
  }
  if ( -not $Scene.status.ok -or [int64]$Scene.revision -ne 1 ) {
    throw 'scene.create did not commit revision 1.'
  }
  $SceneHandle = [string]$Scene.data.scene
  Assert-Task6CanonicalHandle $SceneHandle 'scene.create handle'
  $SceneCreated = Read-Task6StateEvent 'scene.created' 1
  if ( [string]$SceneCreated.data.scene -ne $SceneHandle ) {
    throw 'scene.created did not identify the created scene.'
  }
  return $SceneHandle
}

function Invoke-Task6SourceCreation([string] $ColorKind) {
  $Source = Send-Task6Request @{
    op = 'request'
    id = 'task6.source-create'
    method = 'source.create'
    params = @{
      kind = $ColorKind
      name = 'task6-color'
      settings = @{ width = 320; height = 180; color = 4294901760 }
    }
    ifRevision = 1
  }
  if ( -not $Source.status.ok -or [int64]$Source.revision -ne 2 ) {
    throw 'source.create did not commit revision 2.'
  }
  $SourceHandle = [string]$Source.data.source
  Assert-Task6CanonicalHandle $SourceHandle 'source.create handle'
  $SourceCreated = Read-Task6StateEvent 'source.created' 2
  if ( [string]$SourceCreated.data.source -ne $SourceHandle ) {
    throw 'source.created did not identify the created source.'
  }

  $IntegerHandle = Send-Task6Request @{
    op = 'request'
    id = 'task6.integer-handle'
    method = 'source.getSettings'
    params = @{ source = [int64]$SourceHandle }
  }
  if ( $IntegerHandle.status.ok -or $IntegerHandle.status.code -ne 'bad_request' -or
       [int64]$IntegerHandle.revision -ne 2 ) {
    throw 'Protocol v2 accepted a numeric runtime handle instead of the canonical decimal string form.'
  }
  return $SourceHandle
}

function Invoke-Task6SceneSourceSetup([string] $ColorKind) {
  $SceneHandle = Invoke-Task6SceneCreation
  $SourceHandle = Invoke-Task6SourceCreation $ColorKind
  return @{ Scene = $SceneHandle; Source = $SourceHandle }
}

function Invoke-Task6ItemCreation([hashtable] $Handles) {
  $Item = Send-Task6Request @{
    op = 'request'
    id = 'task6.item-create'
    method = 'item.create'
    params = @{ scene = $Handles.Scene; source = $Handles.Source }
    ifRevision = 2
  }
  if ( -not $Item.status.ok -or [int64]$Item.revision -ne 3 ) {
    throw 'item.create did not commit revision 3.'
  }
  $ItemHandle = [string]$Item.data.item
  Assert-Task6CanonicalHandle $ItemHandle 'item.create handle'
  $ItemCreated = Read-Task6StateEvent 'item.created' 3
  if ( [string]$ItemCreated.data.item -ne $ItemHandle -or
       [string]$ItemCreated.data.scene -ne $Handles.Scene -or
       [string]$ItemCreated.data.source -ne $Handles.Source ) {
    throw 'item.created identity payload was incorrect.'
  }
  return $ItemHandle
}

function Invoke-Task6Transform([string] $ItemHandle) {
  $Transform = Send-Task6Request @{
    op = 'request'
    id = 'task6.transform'
    method = 'item.setTransform'
    params = @{
      item = $ItemHandle
      transform = @{
        position = @{ x = 32.0; y = 24.0 }
        scale = @{ x = 1.25; y = 1.25 }
        rotation = 5.0
      }
    }
    ifRevision = 3
  }
  if ( -not $Transform.status.ok -or [int64]$Transform.revision -ne 4 ) {
    throw 'item.setTransform did not commit revision 4.'
  }
  if ( [double]$Transform.data.transform.position.x -ne 32.0 -or
       [double]$Transform.data.transform.position.y -ne 24.0 -or
       [double]$Transform.data.transform.scale.x -ne 1.25 -or
       [double]$Transform.data.transform.scale.y -ne 1.25 -or
       [double]$Transform.data.transform.rotation -ne 5.0 ) {
    throw 'item.setTransform did not return the canonical resulting transform.'
  }
  $TransformChanged = Read-Task6StateEvent 'item.transformChanged' 4
  if ( [string]$TransformChanged.data.item -ne $ItemHandle ) {
    throw 'item.transformChanged did not identify the transformed item.'
  }
}

function Invoke-Task6ItemTransform([hashtable] $Handles) {
  $ItemHandle = Invoke-Task6ItemCreation $Handles
  Invoke-Task6Transform $ItemHandle
  return $ItemHandle
}

function Invoke-Task6InitialSettings([string] $SourceHandle) {
  $Settings = Send-Task6Request @{
    op = 'request'
    id = 'task6.settings'
    method = 'source.getSettings'
    params = @{ source = $SourceHandle }
  }
  if ( -not $Settings.status.ok -or [int64]$Settings.revision -ne 4 -or
       [int]$Settings.data.settings.width -ne 320 -or [int]$Settings.data.settings.height -ne 180 ) {
    throw 'source.getSettings did not round-trip the initial libobs settings.'
  }
}

function Invoke-Task6StalePatch([string] $SourceHandle) {
  $StalePatch = Send-Task6Request @{
    op = 'request'
    id = 'task6.stale-patch'
    method = 'source.patchSettings'
    params = @{ source = $SourceHandle; settings = @{ width = 999 } }
    ifRevision = 3
  }
  if ( $StalePatch.status.ok -or $StalePatch.status.code -ne 'revision_conflict' -or
       [int64]$StalePatch.revision -ne 4 -or [int64]$StalePatch.status.details.actualRevision -ne 4 ) {
    throw 'Stale source.patchSettings was not rejected at revision 4.'
  }
}

function Invoke-Task6SettingsPatch([string] $SourceHandle) {
  $Patch = Send-Task6Request @{
    op = 'request'
    id = 'task6.patch'
    method = 'source.patchSettings'
    params = @{ source = $SourceHandle; settings = @{ width = 640; height = 360 } }
    ifRevision = 4
  }
  if ( -not $Patch.status.ok -or [int64]$Patch.revision -ne 5 -or
       [int]$Patch.data.settings.width -ne 640 -or [int]$Patch.data.settings.height -ne 360 ) {
    throw 'source.patchSettings did not commit/read back revision 5.'
  }
  $SettingsChanged = Read-Task6StateEvent 'source.settingsChanged' 5
  if ( [string]$SettingsChanged.data.source -ne $SourceHandle -or
       [int]$SettingsChanged.data.settings.width -ne 640 ) {
    throw 'source.settingsChanged payload was incorrect.'
  }
  $DimensionsChanged = Read-Task6StateEvent 'source.dimensionsChanged' 5
  if ( [string]$DimensionsChanged.data.source -ne $SourceHandle -or
       [int]$DimensionsChanged.data.width -ne 640 -or [int]$DimensionsChanged.data.height -ne 360 ) {
    throw 'Task 8 source bridge did not normalize the Color Source dimension change.'
  }
}

function Invoke-Task6SettingsMutation([string] $SourceHandle) {
  Invoke-Task6InitialSettings $SourceHandle
  Invoke-Task6StalePatch $SourceHandle
  Invoke-Task6SettingsPatch $SourceHandle
}

function Invoke-Task6ItemRemoval([string] $ItemHandle) {
  $RemoveItem = Send-Task6Request @{
    op = 'request'
    id = 'task6.item-remove'
    method = 'item.remove'
    params = @{ item = $ItemHandle }
    ifRevision = 5
  }
  if ( -not $RemoveItem.status.ok -or [int64]$RemoveItem.revision -ne 6 ) {
    throw 'item.remove did not commit revision 6.'
  }
  $ItemRemoved = Read-Task6StateEvent 'item.removed' 6
  if ( [string]$ItemRemoved.data.item -ne $ItemHandle ) {
    throw 'item.removed did not identify the removed item.'
  }
}

function Invoke-Task6SecondItem([hashtable] $Handles) {
  $Item2 = Send-Task6Request @{
    op = 'request'
    id = 'task6.item-create-2'
    method = 'item.create'
    params = @{ scene = $Handles.Scene; source = $Handles.Source }
    ifRevision = 6
  }
  if ( -not $Item2.status.ok -or [int64]$Item2.revision -ne 7 ) {
    throw 'Second item.create did not commit revision 7.'
  }
  $Item2Handle = [string]$Item2.data.item
  Assert-Task6CanonicalHandle $Item2Handle 'second item.create handle'
  $Item2Created = Read-Task6StateEvent 'item.created' 7
  if ( [string]$Item2Created.data.item -ne $Item2Handle ) {
    throw 'Second item.created payload was incorrect.'
  }
  return $Item2Handle
}

function Invoke-Task6ItemLifecycle([hashtable] $Handles, [string] $ItemHandle) {
  Invoke-Task6ItemRemoval $ItemHandle
  return Invoke-Task6SecondItem $Handles
}

function Invoke-Task6SourceRemoval([string] $SourceHandle, [string] $ItemHandle) {
  $RemoveSource = Send-Task6Request @{
    op = 'request'
    id = 'task6.source-remove'
    method = 'source.remove'
    params = @{ source = $SourceHandle }
    ifRevision = 7
  }
  if ( -not $RemoveSource.status.ok -or [int64]$RemoveSource.revision -ne 8 ) {
    throw 'source.remove did not commit exactly one revision for the cascade.'
  }
  $CascadeItemRemoved = Read-Task6StateEvent 'item.removed' 8
  $SourceRemoved = Read-Task6StateEvent 'source.removed' 8
  if ( [string]$CascadeItemRemoved.data.item -ne $ItemHandle -or
       [string]$SourceRemoved.data.source -ne $SourceHandle ) {
    throw 'source.remove cascade event payload/order was incorrect.'
  }

  $MissingSource = Send-Task6Request @{
    op = 'request'
    id = 'task6.source-missing'
    method = 'source.getSettings'
    params = @{ source = $SourceHandle }
  }
  if ( $MissingSource.status.ok -or $MissingSource.status.code -ne 'not_found' -or
       [int64]$MissingSource.revision -ne 8 ) {
    throw 'Removed source handle did not become invalid without changing revision.'
  }
}

function Invoke-Task6SceneRemoval([string] $SceneHandle) {
  $RemoveScene = Send-Task6Request @{
    op = 'request'
    id = 'task6.scene-remove'
    method = 'scene.remove'
    params = @{ scene = $SceneHandle }
    ifRevision = 8
  }
  if ( -not $RemoveScene.status.ok -or [int64]$RemoveScene.revision -ne 9 ) {
    throw 'scene.remove did not commit revision 9.'
  }
  $SceneRemoved = Read-Task6StateEvent 'scene.removed' 9
  if ( [string]$SceneRemoved.data.scene -ne $SceneHandle ) {
    throw 'scene.removed did not identify the removed scene.'
  }
}

function Complete-Task6Session {
  $Close = Send-Task6Request @{
    op = 'request'
    id = 'task6.close'
    method = 'session.close'
    params = @{}
    ifRevision = 9
  }
  if ( -not $Close.status.ok -or [int64]$Close.revision -ne 10 ) {
    throw 'session.close did not commit the final revision 10.'
  }
  $script:Task6Process.StandardInput.Close()
  if ( -not $script:Task6Process.WaitForExit(15000) ) {
    $script:Task6Process.Kill($true)
    throw 'obs-engine did not exit after Task 6 session.close.'
  }
  if ( $script:Task6Process.ExitCode -ne 0 ) {
    throw "obs-engine exited with code $($script:Task6Process.ExitCode)."
  }
  $Remaining = $script:Task6Process.StandardOutput.ReadToEnd()
  if ( -not [string]::IsNullOrWhiteSpace($Remaining) ) {
    throw "Task 6 produced unexpected extra stdout after the final response: $Remaining"
  }
  Write-Host 'Protocol-v2 source/scene/item lifecycle smoke test passed.'
}

function Invoke-Task6ProtocolScenario {
  $ColorKind = Initialize-Task6Protocol
  $Handles = Invoke-Task6SceneSourceSetup $ColorKind
  $ItemHandle = Invoke-Task6ItemTransform $Handles
  Invoke-Task6SettingsMutation $Handles.Source
  $SecondItemHandle = Invoke-Task6ItemLifecycle $Handles $ItemHandle
  Invoke-Task6SourceRemoval $Handles.Source $SecondItemHandle
  Invoke-Task6SceneRemoval $Handles.Scene
  Complete-Task6Session
}

function Write-Task6Diagnostics {
  $StderrText = ''
  if ( $null -ne $script:Task6ErrorTask ) {
    try {
      $StderrText = $script:Task6ErrorTask.GetAwaiter().GetResult()
    }
    catch {
      $StderrText = "Failed to collect redirected stderr: $_"
    }
  }
  $ExitState = 'not-started'
  if ( $null -ne $script:Task6Process ) {
    if ( $script:Task6Process.HasExited ) {
      $ExitState = "exited:$($script:Task6Process.ExitCode)"
    } else {
      $ExitState = 'still-running'
    }
  }
  @(
    "engine=$($script:Task6Engine.FullName)"
    "exit_state=$ExitState"
    "next_event_seq=$($script:Task6NextSeq)"
    ''
    '=== smoke failure ==='
    $script:Task6FailureText
    ''
    '=== obs-engine stderr ==='
    $StderrText
  ) | Set-Content -Path $script:Task6DiagnosticFile -Encoding utf8
}

function Invoke-Task6RuntimeSmoke {
  try {
    Initialize-Task6Runtime
    Invoke-Task6ProtocolScenario
  }
  catch {
    $script:Task6FailureText = ($_ | Out-String)
    Write-Error $script:Task6FailureText
    if ( $null -ne $script:Task6Process -and -not $script:Task6Process.HasExited ) {
      try {
        $script:Task6Process.Kill($true)
        $script:Task6Process.WaitForExit(5000) | Out-Null
      }
      catch {
        Write-Warning "Failed to terminate obs-engine after Task 6 smoke failure: $_"
      }
    }
  }
  finally {
    Write-Task6Diagnostics
  }
  if ( $null -ne $script:Task6FailureText ) {
    throw 'Protocol-v2 Task 6 runtime smoke test failed. See _task6-diagnostics/v2-runtime-smoke.txt in the runtime artifact.'
  }
}

Invoke-Task6RuntimeSmoke

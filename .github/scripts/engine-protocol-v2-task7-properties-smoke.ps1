$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Task7InstallRoot = $null
$script:Task7DiagnosticFile = $null
$script:Task7Process = $null
$script:Task7ErrorTask = $null
$script:Task7Engine = $null
$script:Task7FailureText = $null

function Initialize-Task7Runtime {
  $script:Task7InstallRoot = Resolve-Path 'build_x64/install'
  $DiagnosticDir = Join-Path $script:Task7InstallRoot '_task7-diagnostics'
  New-Item -ItemType Directory -Force -Path $DiagnosticDir | Out-Null
  $script:Task7DiagnosticFile = Join-Path $DiagnosticDir 'v2-properties-smoke.txt'
  $script:Task7Engine = Get-ChildItem -Path $script:Task7InstallRoot -Filter 'obs-engine.exe' -File -Recurse |
    Select-Object -First 1
  if ( $null -eq $script:Task7Engine ) {
    throw 'obs-engine.exe was not found in the installed runtime.'
  }
  $StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
  $StartInfo.FileName = $script:Task7Engine.FullName
  $StartInfo.WorkingDirectory = $script:Task7Engine.Directory.FullName
  $StartInfo.UseShellExecute = $false
  $StartInfo.RedirectStandardInput = $true
  $StartInfo.RedirectStandardOutput = $true
  $StartInfo.RedirectStandardError = $true
  $StartInfo.CreateNoWindow = $true
  $script:Task7Process = [System.Diagnostics.Process]::new()
  $script:Task7Process.StartInfo = $StartInfo
  if ( -not $script:Task7Process.Start() ) {
    throw 'Failed to start obs-engine.exe.'
  }
  $script:Task7ErrorTask = $script:Task7Process.StandardError.ReadToEndAsync()
}

function Read-Task7EngineMessage {
  $ReadTask = $script:Task7Process.StandardOutput.ReadLineAsync()
  if ( -not $ReadTask.Wait(30000) ) {
    throw 'Timed out waiting 30 seconds for obs-engine stdout.'
  }
  $Line = $ReadTask.Result
  if ( $null -eq $Line ) {
    $ExitText = if ( $script:Task7Process.HasExited ) { "exit=$($script:Task7Process.ExitCode)" } else { 'process still running' }
    throw "obs-engine closed stdout unexpectedly ($ExitText)."
  }
  Write-Host "obs-engine stdout: $Line"
  return ($Line | ConvertFrom-Json)
}

function Send-Task7Request([hashtable] $Request) {
  $Json = $Request | ConvertTo-Json -Compress -Depth 40
  Write-Host "obs-engine stdin:  $Json"
  $script:Task7Process.StandardInput.WriteLine($Json)
  $script:Task7Process.StandardInput.Flush()
  $Response = Read-Task7EngineMessage
  if ( $Response.op -ne 'response' -or [string]$Response.id -ne [string]$Request.id ) {
    throw "Expected response for '$($Request.id)' but received a different message."
  }
  return $Response
}

function Assert-Task7Capabilities([object] $Hello) {
  $Required = @(
    'properties.v1'
    'properties.get.v1'
    'properties.getListItems.v1'
    'properties.invokeButton.v1'
    'properties.refresh.v1'
    'properties.resolve.v1'
    'properties.validate.v1'
  )
  $CapabilityNames = @($Hello.data.capabilities | ForEach-Object { [string]$_.name })
  foreach ( $Name in $Required ) {
    if ( $CapabilityNames -notcontains $Name ) {
      throw "Task 7 capability was not advertised: $Name"
    }
  }
}

function Invoke-Task7Bootstrap {
  $Ready = Read-Task7EngineMessage
  if ( $Ready.event -ne 'ready' -or [int]$Ready.protocol -ne 1 ) {
    throw 'Migration bootstrap ready event changed unexpectedly.'
  }
  $Hello = Send-Task7Request @{ op = 'request'; id = 'task7.hello'; method = 'session.hello'; params = @{} }
  if ( -not $Hello.status.ok -or [int64]$Hello.revision -ne 0 ) {
    throw 'session.hello failed or a new engine did not begin at revision 0.'
  }
  Assert-Task7Capabilities $Hello
}

function Invoke-Task7KindSchema([hashtable] $SlideTarget) {
  $Schema = Send-Task7Request @{
    op = 'request'; id = 'task7.kind-schema'; method = 'properties.get'; params = @{ target = $SlideTarget }
  }
  Assert-Task7SchemaTarget $Schema
  Assert-Task7SchemaProperties $Schema
}

function Assert-Task7SchemaTarget([object] $Schema) {
  if ( -not $Schema.status.ok -or [int64]$Schema.revision -ne 0 -or
       [string]$Schema.data.target.type -ne 'sourceKind' -or
       [string]$Schema.data.target.kind -ne 'slideshow_v2' ) {
    throw 'properties.get did not return the slideshow_v2 source-kind form at revision 0.'
  }
}

function Assert-Task7SchemaProperties([object] $Schema) {
  $BehaviorProperty = @($Schema.data.properties | Where-Object { $_.name -eq 'playback_behavior' }) | Select-Object -First 1
  $SpeedProperty = @($Schema.data.properties | Where-Object { $_.name -eq 'transition_speed' }) | Select-Object -First 1
  if ( $null -eq $BehaviorProperty -or [string]$BehaviorProperty.type -ne 'list' ) {
    throw 'slideshow_v2 playback_behavior list property was not serialized.'
  }
  if ( $null -eq $SpeedProperty -or [string]$SpeedProperty.type -ne 'int' -or
       [int]$SpeedProperty.min -ne 0 -or [int]$SpeedProperty.max -ne 3600000 ) {
    throw 'slideshow_v2 transition_speed numeric constraints were not serialized.'
  }
}

function Invoke-Task7ListItems([hashtable] $SlideTarget) {
  $ListItems = Send-Task7Request @{
    op = 'request'; id = 'task7.list-items'; method = 'properties.getListItems'
    params = @{ target = $SlideTarget; property = 'playback_behavior' }
  }
  if ( -not $ListItems.status.ok -or [int64]$ListItems.revision -ne 0 -or
       [int]$ListItems.data.itemCount -ne 3 ) {
    throw 'properties.getListItems did not return the slideshow playback behavior choices.'
  }
  $BehaviorValues = @($ListItems.data.items | ForEach-Object { [string]$_.value })
  foreach ( $Expected in @('always_play', 'stop_restart', 'pause_unpause') ) {
    if ( $BehaviorValues -notcontains $Expected ) {
      throw "playback_behavior list lost expected value '$Expected'."
    }
  }
}

function Invoke-Task7Resolve([hashtable] $SlideTarget) {
  $Resolved = Send-Task7Request @{
    op = 'request'; id = 'task7.resolve'; method = 'properties.resolve'
    params = @{ target = $SlideTarget; settings = @{ slide_mode = 'mode_manual' }; changedProperty = 'slide_mode' }
  }
  if ( -not $Resolved.status.ok -or [int64]$Resolved.revision -ne 0 -or
       [string]$Resolved.data.settings.slide_mode -ne 'mode_manual' ) {
    throw 'properties.resolve did not apply candidate settings to a non-mutating working copy.'
  }
}

function Invoke-Task7Validate([hashtable] $SlideTarget) {
  $Validation = Send-Task7Request @{
    op = 'request'; id = 'task7.validate'; method = 'properties.validate'
    params = @{ target = $SlideTarget; settings = @{ transition_speed = -1 } }
  }
  if ( -not $Validation.status.ok -or [int64]$Validation.revision -ne 0 -or $Validation.data.valid ) {
    throw 'properties.validate did not reject an out-of-range transition_speed without mutating revision state.'
  }
  $SpeedIssue = @($Validation.data.issues | Where-Object { $_.property -eq 'transition_speed' -and $_.code -eq 'range' }) |
    Select-Object -First 1
  if ( $null -eq $SpeedIssue ) {
    throw 'properties.validate did not report the transition_speed range issue.'
  }
}

function Invoke-Task7ReadOnlyKindChecks {
  $SlideTarget = @{ type = 'sourceKind'; kind = 'slideshow_v2' }
  Invoke-Task7KindSchema $SlideTarget
  Invoke-Task7ListItems $SlideTarget
  Invoke-Task7Resolve $SlideTarget
  Invoke-Task7Validate $SlideTarget
}

function Get-Task7ColorKind {
  $Kinds = Send-Task7Request @{ op = 'request'; id = 'task7.kinds'; method = 'source.kindList'; params = @{} }
  $ColorKind = $Kinds.data.kinds | Where-Object { $_.id -eq 'color_source_v3' } | Select-Object -First 1
  if ( $null -eq $ColorKind ) {
    $ColorKind = $Kinds.data.kinds | Where-Object { $_.id -eq 'color_source' } | Select-Object -First 1
  }
  if ( $null -eq $ColorKind ) {
    throw 'No Color Source kind was registered.'
  }
  return [string]$ColorKind.id
}

function Invoke-Task7SourceFixture([string] $ColorKind) {
  $Source = Send-Task7Request @{
    op = 'request'; id = 'task7.source-create'; method = 'source.create'
    params = @{ kind = $ColorKind; name = 'task7-color'; settings = @{ width = 320; height = 180; color = 4294901760 } }
    ifRevision = 0
  }
  if ( -not $Source.status.ok -or [int64]$Source.revision -ne 1 ) {
    throw 'Task 7 fixture source did not commit revision 1.'
  }
  $SourceHandle = [string]$Source.data.source
  if ( $SourceHandle -notmatch '^[1-9][0-9]*$' ) {
    throw 'Task 7 fixture source handle was not canonical.'
  }
  return @{ Target = @{ type = 'source'; source = $SourceHandle }; Handle = $SourceHandle }
}

function Invoke-Task7LiveRead([hashtable] $Fixture) {
  $LiveSchema = Send-Task7Request @{
    op = 'request'; id = 'task7.live-get'; method = 'properties.get'; params = @{ target = $Fixture.Target }
  }
  if ( -not $LiveSchema.status.ok -or [int64]$LiveSchema.revision -ne 1 -or
       [int]$LiveSchema.data.settings.width -ne 320 ) {
    throw 'properties.get did not read live source settings at revision 1.'
  }

  $LiveResolved = Send-Task7Request @{
    op = 'request'; id = 'task7.live-resolve'; method = 'properties.resolve'
    params = @{ target = $Fixture.Target; settings = @{ width = 777 }; changedProperty = 'width' }
  }
  if ( -not $LiveResolved.status.ok -or [int64]$LiveResolved.revision -ne 1 -or
       [int]$LiveResolved.data.settings.width -ne 777 ) {
    throw 'properties.resolve did not expose the live-source working candidate at unchanged revision 1.'
  }

  $ActualSettings = Send-Task7Request @{
    op = 'request'; id = 'task7.actual-settings'; method = 'source.getSettings'; params = @{ source = $Fixture.Handle }
  }
  if ( -not $ActualSettings.status.ok -or [int64]$ActualSettings.revision -ne 1 -or
       [int]$ActualSettings.data.settings.width -ne 320 ) {
    throw 'properties.resolve leaked its working width into the real source settings.'
  }
}

function Invoke-Task7LiveRefresh([hashtable] $Fixture) {
  $Refresh = Send-Task7Request @{
    op = 'request'; id = 'task7.refresh'; method = 'properties.refresh'; params = @{ target = $Fixture.Target }
  }
  if ( -not $Refresh.status.ok -or [int64]$Refresh.revision -ne 1 -or
       -not $Refresh.data.refreshed -or [int]$Refresh.data.settings.width -ne 320 ) {
    throw 'properties.refresh did not rebuild the real live-source state.'
  }

  $NotButton = Send-Task7Request @{
    op = 'request'; id = 'task7.not-button'; method = 'properties.invokeButton'
    params = @{ target = $Fixture.Target; property = 'width' }; ifRevision = 1
  }
  if ( $NotButton.status.ok -or $NotButton.status.code -ne 'bad_request' -or
       [int64]$NotButton.revision -ne 1 ) {
    throw 'properties.invokeButton accepted a non-button property or consumed a revision on rejection.'
  }
}

function Complete-Task7Session {
  $Close = Send-Task7Request @{
    op = 'request'; id = 'task7.close'; method = 'session.close'; params = @{}; ifRevision = 1
  }
  if ( -not $Close.status.ok -or [int64]$Close.revision -ne 2 ) {
    throw 'session.close did not commit revision 2 after the Task 7 read-only property calls.'
  }
  $script:Task7Process.StandardInput.Close()
  if ( -not $script:Task7Process.WaitForExit(15000) ) {
    $script:Task7Process.Kill($true)
    throw 'obs-engine did not exit after Task 7 session.close.'
  }
  if ( $script:Task7Process.ExitCode -ne 0 ) {
    throw "obs-engine exited with code $($script:Task7Process.ExitCode)."
  }
  Write-Host 'Protocol-v2 properties smoke test passed.'
}

function Write-Task7Diagnostics {
  $StderrText = ''
  if ( $null -ne $script:Task7ErrorTask ) {
    try { $StderrText = $script:Task7ErrorTask.GetAwaiter().GetResult() }
    catch { $StderrText = "Failed to collect redirected stderr: $_" }
  }
  $ExitState = 'not-started'
  if ( $null -ne $script:Task7Process ) {
    if ( $script:Task7Process.HasExited ) { $ExitState = "exited:$($script:Task7Process.ExitCode)" }
    else { $ExitState = 'still-running' }
  }
  @(
    "engine=$($script:Task7Engine.FullName)"
    "exit_state=$ExitState"
    ''
    '=== smoke failure ==='
    $script:Task7FailureText
    ''
    '=== obs-engine stderr ==='
    $StderrText
  ) | Set-Content -Path $script:Task7DiagnosticFile -Encoding utf8
}

function Invoke-Task7PropertiesSmoke {
  try {
    Initialize-Task7Runtime
    Invoke-Task7Bootstrap
    Invoke-Task7ReadOnlyKindChecks
    $ColorKind = Get-Task7ColorKind
    $Fixture = Invoke-Task7SourceFixture $ColorKind
    Invoke-Task7LiveRead $Fixture
    Invoke-Task7LiveRefresh $Fixture
    Complete-Task7Session
  }
  catch {
    $script:Task7FailureText = ($_ | Out-String)
    Write-Error $script:Task7FailureText
    if ( $null -ne $script:Task7Process -and -not $script:Task7Process.HasExited ) {
      try {
        $script:Task7Process.Kill($true)
        $script:Task7Process.WaitForExit(5000) | Out-Null
      }
      catch { Write-Warning "Failed to terminate obs-engine after Task 7 smoke failure: $_" }
    }
  }
  finally { Write-Task7Diagnostics }
  if ( $null -ne $script:Task7FailureText ) {
    throw 'Protocol-v2 properties smoke test failed. See _task7-diagnostics/v2-properties-smoke.txt in the runtime artifact.'
  }
}

Invoke-Task7PropertiesSmoke

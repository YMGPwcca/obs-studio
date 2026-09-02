param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:AllEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1

function Fail-Task30([string] $Message) { throw "Task 30: $Message" }

function Start-Task30Engine([string] $Root) {
    $engine = Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $Root).Path -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task30 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName; $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task30 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:AllEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Stop-Task30Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task30 "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task30Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task30 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task30 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task30 "engine emitted non-JSON stdout: $line" }
}

function Send-Task30([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task30Message
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); $script:AllEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task30 "wrong response for $($Request.id)." }
        return $message
    }
}

function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task30 "$Label did not succeed at revision $Revision." }
}

function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    $actual = if ($null -ne $Response.status.PSObject.Properties['code']) { [string]$Response.status.code } else { '<missing>' }
    if ($Response.status.ok -or $actual -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task30 "$Label did not return $Code at revision $Revision (actual=$actual revision=$($Response.revision))." }
}

function Read-Task30Event([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task30Message }
        if ($event.op -ne 'event') { Fail-Task30 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task30 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task30 "event $Name has the wrong revision." }
        return $event
    }
}

function Invoke-Task30Mutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task30 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-Ok $response ($State.Current + 1) $Label; $State.Current++
    foreach ($event in $Events) { Read-Task30Event $event $State.Current | Out-Null }
    return $response
}

function Initialize-Task30Session {
    Start-Task30Engine $InstallRoot
    if ([string](Read-Task30Message).event -ne 'ready') { Fail-Task30 'ready marker was not received.' }
    $hello = Send-Task30 @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }; Assert-Ok $hello 0 'session.hello'
    $required = @('virtualCamera.v1', 'virtualCamera.getCapabilities.v1', 'virtualCamera.configure.v1', 'virtualCamera.unconfigure.v1',
        'virtualCamera.start.v1', 'virtualCamera.stop.v1', 'virtualCamera.getState.v1', 'virtualCamera.setTarget.v1', 'virtualCamera.getTarget.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name }); foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task30 "missing capability $name." } }
    $sub = Send-Task30 @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'virtualCamera.*' }, @{ pattern = 'source.*' }, @{ pattern = 'canvas.*' }, @{ pattern = 'engine.stopping' }) } }; Assert-Ok $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0; Output = ''; Source = ''; MainCanvas = '' }
}

function Invoke-Task30Unavailable($State) {
    $capabilities = Send-Task30 @{ op = 'request'; id = 'capabilities'; method = 'virtualCamera.getCapabilities'; params = @{} }; Assert-Ok $capabilities $State.Current 'virtualCamera.getCapabilities'
    foreach ($field in @('apiImplemented', 'outputModulePresent', 'backendModulePresent', 'outputRegistered', 'backendReady', 'available', 'active', 'busy')) {
        if ($null -eq $capabilities.data.PSObject.Properties[$field]) { Fail-Task30 "capabilities omitted $field." }
    }
    if ($capabilities.data.available) { return $false }
    $configure = Send-Task30 @{ op = 'request'; id = 'configure'; method = 'virtualCamera.configure'; params = @{} }; Assert-Error $configure 'unsupported_capability' $State.Current 'unavailable virtualCamera.configure'
    $target = Send-Task30 @{ op = 'request'; id = 'target'; method = 'virtualCamera.getTarget'; params = @{} }; Assert-Ok $target $State.Current 'unconfigured virtualCamera.getTarget'
    if ($target.data.configured -or [string]$target.data.target.type -ne 'program') { Fail-Task30 'unconfigured Virtual Camera target was not the program default.' }
    return $true
}

function Invoke-Task30Available($State) {
    $configured = Invoke-Task30Mutation $State 'configure' 'virtualCamera.configure' @{} @('output.created', 'virtualCamera.configChanged') 'virtualCamera.configure'
    $State.Output = [string]$configured.data.output
    $view = Send-Task30 @{ op = 'request'; id = 'output-view'; method = 'output.get'; params = @{ output = $State.Output } }; Assert-Ok $view $State.Current 'managed Virtual Camera Output view'
    if ([string]$view.data.state.role -ne 'virtualCamera' -or [string]$view.data.state.managedBy -ne 'virtualCamera') { Fail-Task30 'Virtual Camera role was not visible on the managed Output.' }
    $main = Send-Task30 @{ op = 'request'; id = 'main'; method = 'canvas.getMain'; params = @{} }; Assert-Ok $main $State.Current 'canvas.getMain'; $State.MainCanvas = [string]$main.data.canvas
    $canvasTarget = Invoke-Task30Mutation $State 'canvas-target' 'virtualCamera.setTarget' @{ target = @{ type = 'canvas'; canvas = $State.MainCanvas } } @('virtualCamera.targetChanged') 'set Virtual Camera Canvas target'
    if ([string]$canvasTarget.data.target.type -ne 'canvas') { Fail-Task30 'Canvas target was not reported.' }
    $start = Invoke-Task30Mutation $State 'start' 'virtualCamera.start' @{} @('output.started') 'virtualCamera.start'
    if (-not $start.data.state.active) { Fail-Task30 'Virtual Camera did not become active.' }
    $activeCaps = Send-Task30 @{ op = 'request'; id = 'active-caps'; method = 'virtualCamera.getCapabilities'; params = @{} }; Assert-Ok $activeCaps $State.Current 'active Virtual Camera capabilities'
    if (-not $activeCaps.data.active -or -not $activeCaps.data.busy) { Fail-Task30 'active Virtual Camera capability state was not reported.' }
    $busyTarget = Send-Task30 @{ op = 'request'; id = 'busy-target'; method = 'virtualCamera.setTarget'; params = @{ target = @{ type = 'program' } } }; Assert-Error $busyTarget 'busy' $State.Current 'active Virtual Camera retarget'
    $null = Invoke-Task30Mutation $State 'stop' 'virtualCamera.stop' @{} @('output.stopping') 'virtualCamera.stop'
    $next = $State.Current + 1
    Read-Task30Event 'output.stopped' $next | Out-Null; $State.Current = $next
    $source = Invoke-Task30Mutation $State 'source' 'source.create' @{ kind = 'color_source_v3'; name = 'task30-target'; settings = @{ color = 16711680; width = 640; height = 360 } } @('source.created') 'target source.create'
    $State.Source = [string]$source.data.source
    $sourceTarget = Invoke-Task30Mutation $State 'source-target' 'virtualCamera.setTarget' @{ target = @{ type = 'source'; source = $State.Source } } @('virtualCamera.targetChanged') 'set Virtual Camera Source target'
    if ([string]$sourceTarget.data.target.type -ne 'source') { Fail-Task30 'Source target was not reported.' }
    $removeSource = Send-Task30 @{ op = 'request'; id = 'remove-source'; method = 'source.remove'; params = @{ source = $State.Source } }; Assert-Error $removeSource 'object_in_use' $State.Current 'remove active Virtual Camera source target'
    $null = Invoke-Task30Mutation $State 'program-target' 'virtualCamera.setTarget' @{ target = @{ type = 'program' } } @('virtualCamera.targetChanged') 'restore Virtual Camera program target'
    $null = Invoke-Task30Mutation $State 'source-remove' 'source.remove' @{ source = $State.Source } @('source.removed') 'remove released Virtual Camera source target'
    $unconfigured = Invoke-Task30Mutation $State 'unconfigure' 'virtualCamera.unconfigure' @{} @('output.removed', 'virtualCamera.configChanged') 'virtualCamera.unconfigure'
    if ($unconfigured.data.configured) { Fail-Task30 'Virtual Camera remained configured after unconfigure.' }
}

function Close-Task30Session($State) {
    $close = Send-Task30 @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }; Assert-Ok $close ($State.Current + 1) 'session.close'
    Read-Task30Event 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task30 'engine did not exit.' }
    Stop-Task30Engine
    if (@($script:AllEvents | Where-Object { [string]$_.event -in @('virtualCamera.started', 'virtualCamera.stopped') }).Count -ne 0) { Fail-Task30 'Virtual Camera emitted duplicate lifecycle aliases.' }
}

try {
    $state = Initialize-Task30Session
    $unavailable = Invoke-Task30Unavailable $state
    if (-not $unavailable) { Invoke-Task30Available $state }
    Close-Task30Session $state
    if ($unavailable) { Write-Output 'Task 30 virtual-camera integration: PASS (unavailable capability path)' } else { Write-Output 'Task 30 virtual-camera integration: PASS' }
} catch {
    try { Stop-Task30Engine } catch { }
    throw
}

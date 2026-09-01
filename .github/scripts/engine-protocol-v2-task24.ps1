param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:LastMessage = $null

function Fail-Task24([string] $Message) { throw "Task 24: $Message" }
function Start-Task24Engine([string] $Root) {
    $resolved = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolved -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task24 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $engine.FullName; $info.WorkingDirectory = $engine.Directory.FullName
    $info.ArgumentList.Add('--plugin=task24-encoder-source'); $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true; $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task24 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}
function Stop-Task24Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task24 "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}
function Read-Task24Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task24 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task24 'engine stdout closed unexpectedly.' }
    try { $script:LastMessage = ($line | ConvertFrom-Json); return $script:LastMessage } catch { Fail-Task24 "engine emitted non-JSON stdout: $line" }
}
function Send-Task24([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task24Message
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task24 "wrong response for $($Request.id)." }
        return $message
    }
}
function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    if (-not $Response.status.ok) { Fail-Task24 "$Label failed: $($Response.status.code): $($Response.status.message)." }
    if ([int64]$Response.revision -ne $Revision) { Fail-Task24 "$Label revision mismatch." }
}
function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task24 "$Label did not return $Code." }
}
function Read-Task24Event([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) { $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value } else { Read-Task24Message }
        if ($event.op -ne 'event') { Fail-Task24 'expected an event.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task24 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task24 "event $Name revision mismatch." }
        return $event
    }
}
function Invoke-Task24Mutation($State, [string] $Id, [string] $Method, [hashtable] $Params, [string] $Event, [string] $Label) {
    $response = Send-Task24 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-Ok $response ($State.Current + 1) $Label; $State.Current++; if ($Event) { Read-Task24Event $Event $State.Current | Out-Null }; return $response
}
function Initialize-Task24Session {
    Start-Task24Engine $InstallRoot
    if ([string](Read-Task24Message).event -ne 'ready') { Fail-Task24 'ready marker was not received.' }
    $hello = Send-Task24 @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }; Assert-Ok $hello 0 'session.hello'
    $required = @('encoderGroup.v1','encoderGroup.list.v1','encoderGroup.get.v1','encoderGroup.create.v1','encoderGroup.remove.v1','encoderGroup.add.v1','encoderGroup.removeEncoder.v1','encoderGroup.getEncoders.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name }); foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task24 "missing capability $name." } }
    $sub = Send-Task24 @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'encoder.*' }, @{ pattern = 'encoderGroup.*' }, @{ pattern = 'engine.stopping' }) } }; Assert-Ok $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0 }
}
try {
    $state = Initialize-Task24Session
    $one = Invoke-Task24Mutation $state 'create-one' 'encoder.create' @{ type = 'video'; kind = 'task23_test_video'; name = 'task24-one' } 'encoder.created' 'create encoder one'
    $two = Invoke-Task24Mutation $state 'create-two' 'encoder.create' @{ type = 'video'; kind = 'task23_test_video'; name = 'task24-two' } 'encoder.created' 'create encoder two'
    $oneHandle = [string]$one.data.encoder; $twoHandle = [string]$two.data.encoder
    $group = Invoke-Task24Mutation $state 'create-group' 'encoderGroup.create' @{} 'encoderGroup.created' 'create group'
    $groupHandle = [string]$group.data.group
    $null = Invoke-Task24Mutation $state 'add-one' 'encoderGroup.add' @{ group = $groupHandle; encoder = $oneHandle } 'encoderGroup.changed' 'add encoder one'
    Read-Task24Event 'encoder.groupChanged' $state.Current | Out-Null
    $duplicate = Send-Task24 @{ op = 'request'; id = 'duplicate'; method = 'encoderGroup.add'; params = @{ group = $groupHandle; encoder = $oneHandle } }; Assert-Error $duplicate 'already_exists' $state.Current 'duplicate group add'
    $secondGroup = Invoke-Task24Mutation $state 'create-second-group' 'encoderGroup.create' @{} 'encoderGroup.created' 'create second group'
    $secondHandle = [string]$secondGroup.data.group
    $twoGroups = Send-Task24 @{ op = 'request'; id = 'two-groups'; method = 'encoderGroup.add'; params = @{ group = $secondHandle; encoder = $oneHandle } }; Assert-Error $twoGroups 'object_in_use' $state.Current 'encoder in two groups'
    $encoderBusy = Send-Task24 @{ op = 'request'; id = 'encoder-busy'; method = 'encoder.remove'; params = @{ encoder = $oneHandle } }; Assert-Error $encoderBusy 'object_in_use' $state.Current 'remove grouped encoder'
    $groupBusy = Send-Task24 @{ op = 'request'; id = 'group-busy'; method = 'encoderGroup.remove'; params = @{ group = $groupHandle } }; Assert-Error $groupBusy 'object_in_use' $state.Current 'remove non-empty group'
    $members = Send-Task24 @{ op = 'request'; id = 'members'; method = 'encoderGroup.getEncoders'; params = @{ group = $groupHandle } }; Assert-Ok $members $state.Current 'get group members'
    if (@($members.data.encoders).Count -ne 1 -or [string]$members.data.encoders[0].encoder -ne $oneHandle) { Fail-Task24 'group membership snapshot was incorrect.' }
    $null = Invoke-Task24Mutation $state 'remove-one' 'encoderGroup.removeEncoder' @{ group = $groupHandle; encoder = $oneHandle } 'encoderGroup.changed' 'remove encoder from group'
    Read-Task24Event 'encoder.groupChanged' $state.Current | Out-Null
    $null = Invoke-Task24Mutation $state 'remove-group' 'encoderGroup.remove' @{ group = $groupHandle } 'encoderGroup.removed' 'remove empty group'
    $null = Invoke-Task24Mutation $state 'remove-second-group' 'encoderGroup.remove' @{ group = $secondHandle } 'encoderGroup.removed' 'remove second empty group'
    $null = Invoke-Task24Mutation $state 'remove-one-encoder' 'encoder.remove' @{ encoder = $oneHandle } 'encoder.removed' 'remove encoder one'
    $null = Invoke-Task24Mutation $state 'remove-two-encoder' 'encoder.remove' @{ encoder = $twoHandle } 'encoder.removed' 'remove encoder two'
    $close = Send-Task24 @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $state.Current; params = @{} }; Assert-Ok $close ($state.Current + 1) 'session.close'; Read-Task24Event 'engine.stopping' ([int64]$close.revision) | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task24 'engine did not exit.' }; Stop-Task24Engine
    Write-Output 'Task 24 encoder group integration: PASS'
} catch {
    if ($null -ne $script:LastMessage) { Write-Host ('last protocol message: ' + ($script:LastMessage | ConvertTo-Json -Compress -Depth 50)) }
    if ($null -ne $script:ErrorTask) { Write-Host ('engine stderr: ' + $script:ErrorTask.GetAwaiter().GetResult()) }
    try { Stop-Task24Engine } catch { }
    throw
}

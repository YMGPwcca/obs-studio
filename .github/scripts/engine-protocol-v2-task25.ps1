param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1
$script:Sentinel = [Guid]::NewGuid().ToString('N')

function Fail-Task25([string] $Message) { throw "Task 25: $Message" }
function Start-Task25Engine([string] $Root) {
    $resolved = (Resolve-Path -LiteralPath $Root).Path
    $engine = Get-ChildItem -LiteralPath $resolved -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task25 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName; $info.WorkingDirectory = $engine.Directory.FullName
    $info.ArgumentList.Add('--plugin=task25-service'); $info.UseShellExecute = $false; $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true; $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task25 'failed to start obs-engine.exe.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync(); $script:PendingEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}
function Stop-Task25Engine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($stderr.Contains($script:Sentinel)) { Fail-Task25 'secret sentinel appeared on stderr.' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task25 "engine exited with $($script:Engine.ExitCode)." }
}
function Read-Task25Message {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task25 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task25 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task25 "engine emitted non-JSON stdout: $line" }
}
function Send-Task25([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 50)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task25Message
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task25 "wrong response for $($Request.id)." }
        return $message
    }
}
function Assert-NoSentinel($Value, [string] $Label) {
    $json = $Value | ConvertTo-Json -Compress -Depth 50
    if ($json.Contains($script:Sentinel)) { Fail-Task25 "$Label exposed the secret sentinel." }
}
function Assert-Ok($Response, [int64] $Revision, [string] $Label) {
    Assert-NoSentinel $Response $Label
    if (-not $Response.status.ok) { Fail-Task25 "$Label failed: $($Response.status.code)." }
    if ([int64]$Response.revision -ne $Revision) { Fail-Task25 "$Label revision mismatch." }
}
function Assert-Error($Response, [string] $Code, [int64] $Revision, [string] $Label) {
    Assert-NoSentinel $Response $Label
    if ($Response.status.ok -or [string]$Response.status.code -ne $Code -or [int64]$Response.revision -ne $Revision) { Fail-Task25 "$Label did not return $Code." }
}
function Read-Task25Event([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) { $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value } else { Read-Task25Message }
        Assert-NoSentinel $event "event $Name"
        if ($event.op -ne 'event') { Fail-Task25 'expected an event.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task25 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task25 "event $Name revision mismatch." }
        return $event
    }
}
function Invoke-Task25Mutation($State, [string] $Id, [string] $Method, [hashtable] $Params, [string] $Event, [string] $Label) {
    $response = Send-Task25 @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-Ok $response ($State.Current + 1) $Label; $State.Current++; if ($Event) { Read-Task25Event $Event $State.Current | Out-Null }; return $response
}
function Initialize-Task25Session {
    Start-Task25Engine $InstallRoot
    if ([string](Read-Task25Message).event -ne 'ready') { Fail-Task25 'ready marker was not received.' }
    $hello = Send-Task25 @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }; Assert-Ok $hello 0 'session.hello'
    $required = @('service.v1','service.kindList.v1','service.kindDefaults.v1','service.kindProperties.v1','service.list.v1','service.get.v1','service.create.v1','service.remove.v1','service.rename.v1','service.getSettings.v1','service.patchSettings.v1','service.replaceSettings.v1','service.getProperties.v1','service.getProtocol.v1','service.getPreferredOutputKind.v1','service.getSupportedResolutions.v1','service.getMaxFps.v1','service.getMaxBitrates.v1','service.getSupportedVideoCodecs.v1','service.getSupportedAudioCodecs.v1','service.getEncoderRecommendations.v1','service.canConnect.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name }); foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task25 "missing capability $name." } }
    $sub = Send-Task25 @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{ subscriptions = @(@{ pattern = 'service.*' }, @{ pattern = 'engine.stopping' }) } }; Assert-Ok $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0 }
}
function Invoke-Task25KindChecks {
    $kinds = Send-Task25 @{ op = 'request'; id = 'kinds'; method = 'service.kindList'; params = @{} }
    Assert-Ok $kinds 0 'service.kindList'
    if (@($kinds.data.kinds | Where-Object { [string]$_.id -eq 'task25_test_service' }).Count -ne 1) { Fail-Task25 'fixture service kind was not listed.' }
    foreach ($request in @(
        @{ op = 'request'; id = 'kind-defaults'; method = 'service.kindDefaults'; params = @{ kind = 'task25_test_service' } },
        @{ op = 'request'; id = 'kind-properties'; method = 'service.kindProperties'; params = @{ kind = 'task25_test_service' } }
    )) { Assert-Ok (Send-Task25 $request) 0 $request.id }
}

function Invoke-Task25ServiceReads($State, [string] $Service) {
    foreach ($request in @(
        @{ op = 'request'; id = 'get'; method = 'service.get'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'get-settings'; method = 'service.getSettings'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'get-properties'; method = 'service.getProperties'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'protocol'; method = 'service.getProtocol'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'output-kind'; method = 'service.getPreferredOutputKind'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'resolutions'; method = 'service.getSupportedResolutions'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'fps'; method = 'service.getMaxFps'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'bitrates'; method = 'service.getMaxBitrates'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'video-codecs'; method = 'service.getSupportedVideoCodecs'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'audio-codecs'; method = 'service.getSupportedAudioCodecs'; params = @{ service = $Service } },
        @{ op = 'request'; id = 'can-connect'; method = 'service.canConnect'; params = @{ service = $Service } }
    )) { Assert-Ok (Send-Task25 $request) $State.Current $request.id }
}

function Invoke-Task25SecretChecks($State, [string] $Service, $Created) {
    if (-not $Created.data.secrets.streamKey.set -or -not $Created.data.secrets.password.set) { Fail-Task25 'secret presence metadata was not set.' }
    $patch = Invoke-Task25Mutation $State 'patch' 'service.patchSettings' @{ service = $Service; settings = @{ label = 'patched' } } 'service.settingsChanged' 'service.patchSettings'
    if (-not $patch.data.secrets.streamKey.set) { Fail-Task25 'patch did not preserve omitted secret.' }
    $replace = Invoke-Task25Mutation $State 'replace' 'service.replaceSettings' @{ service = $Service; settings = @{ server = 'rtmp://127.0.0.1/replaced'; key = $script:Sentinel; label = 'replaced' } } 'service.settingsChanged' 'service.replaceSettings'
    if (-not $replace.data.secrets.streamKey.set -or $replace.data.secrets.password.set) { Fail-Task25 'replace secret metadata was incorrect.' }
    $clear = Invoke-Task25Mutation $State 'clear' 'service.patchSettings' @{ service = $Service; settings = @{ label = 'cleared' }; clearSecrets = @(@{ name = 'key' }) } 'service.settingsChanged' 'clear secret'
    if ($clear.data.secrets.streamKey.set) { Fail-Task25 'explicit secret clear did not clear the stream key.' }
}

function Complete-Task25($State, [string] $Service) {
    $recommend = Send-Task25 @{ op = 'request'; id = 'recommend'; method = 'service.getEncoderRecommendations'; params = @{ service = $Service; videoSettings = @{ bitrate = 1000 }; audioSettings = @{ bitrate = 64 } } }
    Assert-Ok $recommend $State.Current 'service.getEncoderRecommendations'
    if ($recommend.data.liveEncodersMutated -or [int]$recommend.data.videoSettings.recommended_bitrate -ne 2400) { Fail-Task25 'recommendations were not read-only or deterministic.' }
    $null = Invoke-Task25Mutation $State 'rename' 'service.rename' @{ service = $Service; name = 'task25-service-renamed' } 'service.renamed' 'service.rename'
    $bad = Send-Task25 @{ op = 'request'; id = 'bad'; method = 'service.patchSettings'; params = @{ service = $Service; settings = 'not-an-object' } }
    Assert-Error $bad 'bad_request' $State.Current 'invalid service settings'
    $null = Invoke-Task25Mutation $State 'remove' 'service.remove' @{ service = $Service } 'service.removed' 'service.remove'
    $close = Send-Task25 @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }
    Assert-Ok $close ($State.Current + 1) 'session.close'
    Read-Task25Event 'engine.stopping' ([int64]$close.revision) | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task25 'engine did not exit.' }
    Stop-Task25Engine
}

try {
    $state = Initialize-Task25Session
    Invoke-Task25KindChecks
    $settings = @{ server = 'rtmp://127.0.0.1/task25'; key = $script:Sentinel; username = 'user'; password = 'pass'; passphrase = 'phrase'; bearer_token = 'bearer'; label = 'initial' }
    $created = Invoke-Task25Mutation $state 'create' 'service.create' @{ kind = 'task25_test_service'; name = 'task25-service'; settings = $settings } 'service.created' 'service.create'
    $serviceHandle = [string]$created.data.service
    Invoke-Task25ServiceReads $state $serviceHandle
    Assert-NoSentinel $created 'service.create'
    Invoke-Task25SecretChecks $state $serviceHandle $created
    Complete-Task25 $state $serviceHandle
    Write-Output 'Task 25 service integration: PASS'
} catch {
    try { Stop-Task25Engine } catch { }
    throw
}

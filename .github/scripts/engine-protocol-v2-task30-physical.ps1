param([Parameter(Mandatory = $true)] [string] $InstallRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$script:Engine = $null
$script:ErrorTask = $null
$script:PendingEvents = [System.Collections.Generic.List[object]]::new()
$script:AllEvents = [System.Collections.Generic.List[object]]::new()
$script:NextSequence = [uint64]1

function Fail-Task30Physical([string] $Message) { throw "Task 30 physical: $Message" }

function Start-Task30PhysicalEngine([string] $Root) {
    $engine = Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $Root).Path -Filter 'obs-engine.exe' -File -Recurse |
        Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs-engine\.exe$' } | Select-Object -First 1
    if ($null -eq $engine) { Fail-Task30Physical 'obs-engine.exe was not found.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $engine.FullName
    $info.WorkingDirectory = $engine.Directory.FullName; $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $script:Engine = [Diagnostics.Process]::new(); $script:Engine.StartInfo = $info
    if (-not $script:Engine.Start()) { Fail-Task30Physical 'failed to start the production Engine package.' }
    $script:ErrorTask = $script:Engine.StandardError.ReadToEndAsync()
    $script:PendingEvents = [System.Collections.Generic.List[object]]::new()
    $script:AllEvents = [System.Collections.Generic.List[object]]::new(); $script:NextSequence = [uint64]1
}

function Stop-Task30PhysicalEngine {
    if ($null -eq $script:Engine) { return }
    if (-not $script:Engine.HasExited) { $script:Engine.Kill(); $script:Engine.WaitForExit() }
    $stderr = if ($null -ne $script:ErrorTask) { $script:ErrorTask.GetAwaiter().GetResult() } else { '' }
    if ($script:Engine.ExitCode -ne 0) { Fail-Task30Physical "engine exited with $($script:Engine.ExitCode).`n$stderr" }
}

function Read-Task30PhysicalMessage {
    $read = $script:Engine.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(30000)) { Fail-Task30Physical 'timed out waiting for engine stdout.' }
    $line = $read.Result
    if ($null -eq $line) { Fail-Task30Physical 'engine stdout closed unexpectedly.' }
    try { return ($line | ConvertFrom-Json) } catch { Fail-Task30Physical "non-JSON stdout: $line" }
}

function Send-Task30Physical([hashtable] $Request) {
    $script:Engine.StandardInput.WriteLine(($Request | ConvertTo-Json -Compress -Depth 60)); $script:Engine.StandardInput.Flush()
    while ($true) {
        $message = Read-Task30PhysicalMessage
        if ($message.op -eq 'event') { $script:PendingEvents.Add($message); $script:AllEvents.Add($message); continue }
        if ($message.op -ne 'response' -or [string]$message.id -ne [string]$Request.id) { Fail-Task30Physical "wrong response for $($Request.id)." }
        return $message
    }
}

function Assert-PhysicalOk($Response, [int64] $Revision, [string] $Label) {
    $code = if ($null -ne $Response.status.PSObject.Properties['code']) { [string]$Response.status.code } else { '<missing>' }
    $message = if ($null -ne $Response.status.PSObject.Properties['message']) { [string]$Response.status.message } else { '<missing>' }
    if (-not $Response.status.ok -or [int64]$Response.revision -ne $Revision) { Fail-Task30Physical "$Label did not succeed at revision $Revision (actualRevision=$($Response.revision) code=$code message=$message)." }
}

function Read-Task30PhysicalEvent([string] $Name, [int64] $Revision) {
    while ($true) {
        $event = if ($script:PendingEvents.Count -gt 0) {
            $value = $script:PendingEvents[0]; $script:PendingEvents.RemoveAt(0); $value
        } else { Read-Task30PhysicalMessage }
        if ($event.op -ne 'event') { Fail-Task30Physical 'expected an event message.' }
        if ([uint64]$event.seq -ne $script:NextSequence) { Fail-Task30Physical 'event sequence was not monotonic.' }
        $script:NextSequence++
        if ([string]$event.event -ne $Name) { continue }
        if ([int64]$event.revision -ne $Revision) { Fail-Task30Physical "event $Name has the wrong revision (expected=$Revision actual=$($event.revision))." }
        return $event
    }
}

function Invoke-Task30PhysicalMutation($State, [string] $Id, [string] $Method, [hashtable] $Params,
    [string[]] $Events, [string] $Label) {
    $response = Send-Task30Physical @{ op = 'request'; id = $Id; method = $Method; params = $Params }
    Assert-PhysicalOk $response ($State.Current + 1) $Label; $State.Current++
    foreach ($event in $Events) { Read-Task30PhysicalEvent $event $State.Current | Out-Null }
    return $response
}

function Start-Task30Consumer([string] $CapturePath) {
    $ffmpeg = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($null -eq $ffmpeg) { Fail-Task30Physical 'ffmpeg.exe is required for the DirectShow consumer.' }
    $info = [Diagnostics.ProcessStartInfo]::new(); $info.FileName = $ffmpeg.Source
    $info.WorkingDirectory = (Split-Path -Parent $CapturePath); $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    foreach ($argument in @('-hide_banner', '-loglevel', 'error', '-f', 'dshow', '-video_size', '1920x1080', '-framerate', '60', '-i', 'video=OBS Virtual Camera', '-frames:v', '8', '-vf', 'format=bgr24', '-f', 'rawvideo', '-y', $CapturePath)) { $info.ArgumentList.Add($argument) }
    $consumer = [Diagnostics.Process]::new(); $consumer.StartInfo = $info
    if (-not $consumer.Start()) { Fail-Task30Physical 'failed to start the DirectShow consumer.' }
    return $consumer
}

function Wait-Task30Consumer($Consumer, [string] $CapturePath) {
    if (-not $Consumer.WaitForExit(30000)) { $Consumer.Kill(); $Consumer.WaitForExit(); Fail-Task30Physical 'DirectShow consumer did not terminate.' }
    $stderr = $Consumer.StandardError.ReadToEnd()
    if ($Consumer.ExitCode -ne 0) { Fail-Task30Physical "DirectShow consumer failed: $stderr" }
    if (-not (Test-Path -LiteralPath $CapturePath -PathType Leaf)) { Fail-Task30Physical 'DirectShow consumer produced no capture.' }
}

function Assert-Task30ColorCapture([string] $CapturePath, [string] $Name, [int] $ExpectedBlue, [int] $ExpectedGreen, [int] $ExpectedRed) {
    $width = 1920; $height = 1080; $bytesPerPixel = 3; $frameBytes = $width * $height * $bytesPerPixel
    $bytes = [IO.File]::ReadAllBytes($CapturePath)
    if ($bytes.Length -lt ($frameBytes * 4) -or ($bytes.Length % $frameBytes) -ne 0) { Fail-Task30Physical "$Name consumer capture did not contain complete frames." }
    $frames = [int]($bytes.Length / $frameBytes)
    $sampleXs = @([int]($width / 2), [int]($width / 4), [int](($width * 3) / 4)); $sampleYs = @([int]($height / 2), [int]($height / 4), [int](($height * 3) / 4))
    $blue = 0L; $green = 0L; $red = 0L; $samples = 0
    foreach ($frame in @($frames - 1)) {
        foreach ($y in $sampleYs) {
            foreach ($x in $sampleXs) {
                $offset = ($frame * $frameBytes) + (($y * $width + $x) * $bytesPerPixel)
                $blue += $bytes[$offset]; $green += $bytes[$offset + 1]; $red += $bytes[$offset + 2]; $samples++
            }
        }
    }
    $blue = [int]($blue / $samples); $green = [int]($green / $samples); $red = [int]($red / $samples)
    if ([Math]::Abs($blue - $ExpectedBlue) -gt 70 -or [Math]::Abs($green - $ExpectedGreen) -gt 70 -or [Math]::Abs($red - $ExpectedRed) -gt 70) {
        Fail-Task30Physical "$Name frame color was wrong (B=$blue G=$green R=$red)."
    }
    return [pscustomobject]@{ name = $Name; frames = $frames; width = $width; height = $height; pixelFormat = 'bgr24'; blue = $blue; green = $green; red = $red }
}

function Initialize-Task30PhysicalSession {
    Start-Task30PhysicalEngine $InstallRoot
    if ([string](Read-Task30PhysicalMessage).event -ne 'ready') { Fail-Task30Physical 'ready marker was not received.' }
    $hello = Send-Task30Physical @{ op = 'request'; id = 'hello'; method = 'session.hello'; params = @{} }; Assert-PhysicalOk $hello 0 'session.hello'
    $required = @('virtualCamera.v1', 'virtualCamera.getCapabilities.v1', 'virtualCamera.configure.v1', 'virtualCamera.unconfigure.v1', 'virtualCamera.start.v1', 'virtualCamera.stop.v1', 'virtualCamera.getState.v1', 'virtualCamera.setTarget.v1', 'virtualCamera.getTarget.v1')
    $caps = @($hello.data.capabilities | ForEach-Object { [string]$_.name }); foreach ($name in $required) { if ($caps -notcontains $name) { Fail-Task30Physical "missing capability $name." } }
    $sub = Send-Task30Physical @{ op = 'request'; id = 'sub'; method = 'session.subscribe'; params = @{
            subscriptions = @(@{ pattern = 'output.*' }, @{ pattern = 'virtualCamera.*' }, @{ pattern = 'source.*' }, @{ pattern = 'engine.stopping' }) } }; Assert-PhysicalOk $sub 0 'session.subscribe'
    return [pscustomobject]@{ Current = [int64]0; Output = ''; Red = ''; Green = ''; Blue = ''; Path = '' }
}

function Invoke-Task30PhysicalSetup($State) {
    $red = Invoke-Task30PhysicalMutation $State 'red' 'source.create' @{ kind = 'color_source_v3'; name = 'physical-red'; settings = @{ width = 1920; height = 1080; color = 4278190335 } } @('source.created') 'red source.create'; $State.Red = [string]$red.data.source
    $green = Invoke-Task30PhysicalMutation $State 'green' 'source.create' @{ kind = 'color_source_v3'; name = 'physical-green'; settings = @{ width = 1920; height = 1080; color = 4278255360 } } @('source.created') 'green source.create'; $State.Green = [string]$green.data.source
    $blue = Invoke-Task30PhysicalMutation $State 'blue' 'source.create' @{ kind = 'color_source_v3'; name = 'physical-blue'; settings = @{ width = 1920; height = 1080; color = 4294901760 } } @('source.created') 'blue source.create'; $State.Blue = [string]$blue.data.source
    $configured = Invoke-Task30PhysicalMutation $State 'configure' 'virtualCamera.configure' @{} @('output.created', 'virtualCamera.configChanged') 'production virtualCamera.configure'; $State.Output = [string]$configured.data.output
    if (-not $configured.data.targetAvailable -or [string]$configured.data.target.type -ne 'program') { Fail-Task30Physical 'Virtual Camera did not default to the available program target.' }
}

function Invoke-Task30PhysicalColor($State, [string] $Name, [string] $Source, [int] $Blue, [int] $Green, [int] $Red, [string] $OutputDirectory) {
    $target = Invoke-Task30PhysicalMutation $State "target-$Name" 'virtualCamera.setTarget' @{ target = @{ type = 'source'; source = $Source } } @('virtualCamera.targetChanged') "set $Name source target"
    if ([string]$target.data.target.type -ne 'source') { Fail-Task30Physical "$Name source target was not reported." }
    $start = Send-Task30Physical @{ op = 'request'; id = "start-$Name"; method = 'virtualCamera.start'; params = @{} }
    $startRevision = [int64]$start.revision
    Assert-PhysicalOk $start $startRevision "$Name virtualCamera.start"
    if ($startRevision -le $State.Current) { Fail-Task30Physical "$Name virtualCamera.start did not advance the revision." }
    Read-Task30PhysicalEvent 'output.started' $startRevision | Out-Null
    $State.Current = $startRevision
    if (-not $start.data.state.active) { Fail-Task30Physical "$Name Virtual Camera state was not active." }
    $capture = Join-Path $OutputDirectory "task30-$Name.raw"
    $consumer = Start-Task30Consumer $capture
    Wait-Task30Consumer $consumer $capture
    $evidence = Assert-Task30ColorCapture $capture $Name $Blue $Green $Red
    $stop = Send-Task30Physical @{ op = 'request'; id = "stop-$Name"; method = 'virtualCamera.stop'; params = @{} }
    $stopCode = if ($null -ne $stop.status.PSObject.Properties['code']) { [string]$stop.status.code } else { '<missing>' }
    if (-not $stop.status.ok) { Fail-Task30Physical "$Name virtualCamera.stop failed (code=$stopCode)." }
    $stoppingRevision = [int64]$stop.revision
    Read-Task30PhysicalEvent 'output.stopping' $stoppingRevision | Out-Null
    $stopRevision = [int64]$stop.revision
    if ($stopRevision -lt $stoppingRevision) { Fail-Task30Physical "$Name virtualCamera.stop returned an invalid revision." }
    $stoppedRevision = $stopRevision + 1
    Read-Task30PhysicalEvent 'output.stopped' $stoppedRevision | Out-Null
    $State.Current = $stoppedRevision
    return $evidence
}

function Invoke-Task30PhysicalCleanup($State) {
    $null = Invoke-Task30PhysicalMutation $State 'program-target' 'virtualCamera.setTarget' @{ target = @{ type = 'program' } } @('virtualCamera.targetChanged') 'restore program target'
    $remove = Send-Task30Physical @{ op = 'request'; id = 'remove-red'; method = 'source.remove'; params = @{ source = $State.Red } }
    Assert-PhysicalOk $remove ($State.Current + 1) 'remove red source'
    Read-Task30PhysicalEvent 'source.removed' ($State.Current + 1) | Out-Null
    $State.Current++
    $remove = Send-Task30Physical @{ op = 'request'; id = 'remove-green'; method = 'source.remove'; params = @{ source = $State.Green } }
    Assert-PhysicalOk $remove ($State.Current + 1) 'remove green source'
    Read-Task30PhysicalEvent 'source.removed' ($State.Current + 1) | Out-Null
    $State.Current++
    $remove = Send-Task30Physical @{ op = 'request'; id = 'remove-blue'; method = 'source.remove'; params = @{ source = $State.Blue } }
    Assert-PhysicalOk $remove ($State.Current + 1) 'remove blue source'
    Read-Task30PhysicalEvent 'source.removed' ($State.Current + 1) | Out-Null
    $State.Current++
    $null = Invoke-Task30PhysicalMutation $State 'unconfigure' 'virtualCamera.unconfigure' @{} @('output.removed', 'virtualCamera.configChanged') 'production virtualCamera.unconfigure'
    $close = Send-Task30Physical @{ op = 'request'; id = 'close'; method = 'session.close'; ifRevision = $State.Current; params = @{} }; Assert-PhysicalOk $close ($State.Current + 1) 'production session.close'
    Read-Task30PhysicalEvent 'engine.stopping' $close.revision | Out-Null
    if (-not $script:Engine.WaitForExit(30000)) { Fail-Task30Physical 'production engine did not exit.' }
    Stop-Task30PhysicalEngine
}

try {
    $state = Initialize-Task30PhysicalSession
    $capabilities = Send-Task30Physical @{ op = 'request'; id = 'capabilities'; method = 'virtualCamera.getCapabilities'; params = @{} }; Assert-PhysicalOk $capabilities 0 'physical Virtual Camera capabilities'
    if (-not $capabilities.data.available -or -not $capabilities.data.backendReady -or -not $capabilities.data.outputRegistered) { Fail-Task30Physical 'physical Virtual Camera backend was not positively registered and ready.' }
    $outputDirectory = Join-Path (Get-Location).Path 'build_x64\physical-virtual-camera'
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Invoke-Task30PhysicalSetup $state
    $evidence = @(
        (Invoke-Task30PhysicalColor $state 'red' $state.Red 0 0 255 $outputDirectory),
        (Invoke-Task30PhysicalColor $state 'green' $state.Green 0 255 0 $outputDirectory),
        (Invoke-Task30PhysicalColor $state 'blue' $state.Blue 255 0 0 $outputDirectory)
    )
    Invoke-Task30PhysicalCleanup $state
    $formatFile = Join-Path $env:APPDATA 'obs-virtualcam.txt'
    $format = if (Test-Path -LiteralPath $formatFile -PathType Leaf) { (Get-Content -LiteralPath $formatFile -Raw).Trim() } else { '' }
    if ($format -ne '1920x1080x166666') { Fail-Task30Physical "Virtual Camera format metadata was unexpected: $format" }
    Write-Output ("Task 30 physical DirectShow consumer: PASS ({0} color captures, format={1})" -f $evidence.Count, $format)
} catch {
    try { Stop-Task30PhysicalEngine } catch { }
    throw
}

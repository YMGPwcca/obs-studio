$ErrorActionPreference = 'Stop'

$script:InstallRoot = $null
$script:Process = $null
$script:ErrorTask = $null
$script:Engine = $null
$script:DiagnosticFile = $null
$script:FailureText = $null

function Initialize-Task3Diagnostics {
    $script:InstallRoot = Resolve-Path 'build_x64/install'
    $diagnosticDir = Join-Path $script:InstallRoot '_task3-diagnostics'
    New-Item -ItemType Directory -Force -Path $diagnosticDir | Out-Null
    $script:DiagnosticFile = Join-Path $diagnosticDir 'v2-capabilities-smoke.txt'
}

function Start-Task3Engine {
    $script:Engine = Get-ChildItem -Path $script:InstallRoot -Filter 'obs-engine.exe' -File -Recurse |
        Select-Object -First 1
    if ($null -eq $script:Engine) {
        throw 'obs-engine.exe was not found in the installed runtime.'
    }
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $script:Engine.FullName
    $startInfo.WorkingDirectory = $script:Engine.Directory.FullName
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
}

function Read-Task3EngineMessage {
    $readTask = $script:Process.StandardOutput.ReadLineAsync()
    if (-not $readTask.Wait(30000)) {
        throw 'Timed out waiting 30 seconds for obs-engine stdout.'
    }
    $line = $readTask.Result
    if ($null -eq $line) {
        $exitText = if ($script:Process.HasExited) { "exit=$($script:Process.ExitCode)" } else { 'process still running' }
        throw "obs-engine closed stdout unexpectedly ($exitText)."
    }
    Write-Host "obs-engine stdout: $line"
    return ($line | ConvertFrom-Json)
}

function Send-Task3Request([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 20
    Write-Host "obs-engine stdin:  $json"
    $script:Process.StandardInput.WriteLine($json)
    $script:Process.StandardInput.Flush()
    return Read-Task3EngineMessage
}

function Get-Task3CapabilityNames([object] $Response) {
    return @($Response.data.capabilities | ForEach-Object { [string]$_.name })
}

function Assert-Task3Hello([object] $Hello) {
    if ($Hello.op -ne 'response' -or $Hello.id -ne 'task3.hello' -or -not $Hello.status.ok) {
        throw 'session.hello did not return a valid v2 success envelope.'
    }
    if ([int]$Hello.data.protocol.major -ne 2 -or [int]$Hello.data.protocol.minor -ne 0) {
        throw 'session.hello returned an unexpected protocol version.'
    }
    if ([int64]$Hello.revision -ne 0 -or [int64]$Hello.data.revision -ne 0) {
        throw 'Read-only session.hello unexpectedly changed revision state.'
    }
}

function Assert-Task3HelloCapabilities([object] $Hello, [string[]] $Required) {
    $helloNames = Get-Task3CapabilityNames $Hello
    foreach ($requiredName in $Required) {
        if ($helloNames -notcontains $requiredName) {
            throw "session.hello lost required Task 3 capability: $requiredName"
        }
    }
    foreach ($capability in @($Hello.data.capabilities)) {
        if ([string]::IsNullOrWhiteSpace([string]$capability.name)) {
            throw 'Capability descriptor is missing a name.'
        }
        if ([bool]$capability.experimental) {
            throw "Stable capability was unexpectedly marked experimental: $($capability.name)"
        }
    }
    return $helloNames
}

function Assert-Task3CapabilitiesResponse([object] $Response, [string[]] $HelloNames) {
    if ($Response.id -ne 'task3.capabilities' -or -not $Response.status.ok) {
        throw 'engine.getCapabilities failed.'
    }
    if ([int64]$Response.revision -ne 0) {
        throw 'engine.getCapabilities unexpectedly changed the revision.'
    }
    $responseNames = Get-Task3CapabilityNames $Response
    if (($responseNames -join '|') -ne ($HelloNames -join '|')) {
        throw 'engine.getCapabilities does not match session.hello capability advertisement.'
    }
}

function Assert-Task3Unknown([object] $Unknown) {
    if ($Unknown.id -ne 'task3.unimplemented' -or $Unknown.status.ok -or
        $Unknown.status.code -ne 'unsupported_method') {
        throw 'Unknown v2 semantic methods must remain unsupported.'
    }
}

function Assert-Task3Ping([object] $Ping) {
    if ($Ping.id -ne 'task3.ping' -or -not $Ping.status.ok -or -not $Ping.data.pong) {
        throw 'session.ping regressed during Task 3.'
    }
}

function Assert-Task3Close([object] $Close) {
    if ($Close.id -ne 'task3.close' -or -not $Close.status.ok) {
        throw 'session.close regressed during Task 3.'
    }
}

function Initialize-Task3Protocol {
    $required = @('engine.capabilities.v1', 'session.close.v1', 'session.hello.v1', 'session.ping.v1')
    $ready = Read-Task3EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        throw 'Migration bootstrap ready event changed unexpectedly.'
    }
    $hello = Send-Task3Request @{
        op = 'request'; id = 'task3.hello'; method = 'session.hello'; params = @{}
    }
    Assert-Task3Hello $hello
    return Assert-Task3HelloCapabilities $hello $required
}

function Invoke-Task3CapabilityQuery([string[]] $HelloNames) {
    $getCapabilities = Send-Task3Request @{
        op = 'request'; id = 'task3.capabilities'; method = 'engine.getCapabilities'; params = @{}
    }
    Assert-Task3CapabilitiesResponse $getCapabilities $HelloNames
}

function Invoke-Task3UnsupportedQuery {
    $unknown = Send-Task3Request @{
        op = 'request'; id = 'task3.unimplemented'; method = 'task3.nonexistent'; params = @{}
    }
    Assert-Task3Unknown $unknown
    $ping = Send-Task3Request @{
        op = 'request'; id = 'task3.ping'; method = 'session.ping'; params = @{}
    }
    Assert-Task3Ping $ping
}

function Complete-Task3Session {
    $close = Send-Task3Request @{
        op = 'request'; id = 'task3.close'; method = 'session.close'; params = @{}
    }
    Assert-Task3Close $close
    $script:Process.StandardInput.Close()
    if (-not $script:Process.WaitForExit(15000)) {
        $script:Process.Kill($true)
        throw 'obs-engine did not exit after session.close.'
    }
    if ($script:Process.ExitCode -ne 0) {
        throw "obs-engine exited with code $($script:Process.ExitCode)."
    }
    Write-Host 'Protocol-v2 capability smoke test passed.'
}

function Invoke-Task3ProtocolScenario {
    $helloNames = Initialize-Task3Protocol
    Invoke-Task3CapabilityQuery $helloNames
    Invoke-Task3UnsupportedQuery
    Complete-Task3Session
}

function Stop-Task3AfterFailure {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) {
        try {
            $script:Process.Kill($true)
            $script:Process.WaitForExit(5000) | Out-Null
        }
        catch {
            Write-Warning "Failed to terminate obs-engine after Task 3 smoke failure: $_"
        }
    }
}

function Write-Task3Diagnostics {
    $stderrText = ''
    if ($null -ne $script:ErrorTask) {
        try { $stderrText = $script:ErrorTask.GetAwaiter().GetResult() }
        catch { $stderrText = "Failed to collect redirected stderr: $_" }
    }
    $exitState = 'not-started'
    if ($null -ne $script:Process) {
        if ($script:Process.HasExited) { $exitState = "exited:$($script:Process.ExitCode)" }
        else { $exitState = 'still-running' }
    }
    @(
        "engine=$($script:Engine.FullName)"
        "exit_state=$exitState"
        ''
        '=== smoke failure ==='
        $script:FailureText
        ''
        '=== obs-engine stderr ==='
        $stderrText
    ) | Set-Content -Path $script:DiagnosticFile -Encoding utf8
}

function Invoke-Task3CapabilitiesSmoke {
    Initialize-Task3Diagnostics
    try {
        Start-Task3Engine
        Invoke-Task3ProtocolScenario
    }
    catch {
        $script:FailureText = ($_ | Out-String)
        Write-Error $script:FailureText
        Stop-Task3AfterFailure
    }
    finally {
        Write-Task3Diagnostics
    }
    if ($null -ne $script:FailureText) {
        throw 'Protocol-v2 capability smoke test failed. See _task3-diagnostics/v2-capabilities-smoke.txt in the runtime artifact.'
    }
}

Invoke-Task3CapabilitiesSmoke

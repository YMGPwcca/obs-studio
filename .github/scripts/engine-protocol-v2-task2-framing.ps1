$ErrorActionPreference = 'Stop'

$script:InstallRoot = $null
$script:Process = $null
$script:ErrorTask = $null
$script:Engine = $null
$script:DiagnosticFile = $null
$script:FailureText = $null

function Initialize-Task2Diagnostics {
    $script:InstallRoot = Resolve-Path 'build_x64/install'
    $diagnosticDir = Join-Path $script:InstallRoot '_task2-diagnostics'
    New-Item -ItemType Directory -Force -Path $diagnosticDir | Out-Null
    $script:DiagnosticFile = Join-Path $diagnosticDir 'v2-smoke.txt'
}

function Start-Task2Engine {
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

function Read-Task2EngineMessage {
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

function Send-Task2Request([hashtable] $Request) {
    $json = $Request | ConvertTo-Json -Compress -Depth 20
    Write-Host "obs-engine stdin:  $json"
    $script:Process.StandardInput.WriteLine($json)
    $script:Process.StandardInput.Flush()
    return Read-Task2EngineMessage
}

function Assert-Task2Hello([object] $Hello) {
    if ($Hello.op -ne 'response' -or $Hello.id -ne 'task2.hello' -or -not $Hello.status.ok) {
        throw 'session.hello did not return a valid v2 success envelope.'
    }
    if ([int]$Hello.data.protocol.major -ne 2 -or [int]$Hello.data.protocol.minor -ne 0) {
        throw 'session.hello returned an unexpected protocol version.'
    }
    if ([int64]$Hello.revision -ne 0 -or [int64]$Hello.data.revision -ne 0) {
        throw 'Task 2 framing must remain at revision 0 before revision tracking is implemented.'
    }
    if ([int64]$Hello.data.maxMessageBytes -ne 262144) {
        throw 'session.hello returned an unexpected message-size limit.'
    }
    if (-not ($Hello.data.PSObject.Properties.Name -contains 'capabilities')) {
        throw 'session.hello must expose the capabilities field used by later protocol tasks.'
    }
}

function Assert-Task2Ping([object] $Ping) {
    if ($Ping.id -ne 'task2.ping' -or -not $Ping.status.ok -or -not $Ping.data.pong) {
        throw 'session.ping failed v2 request correlation.'
    }
}

function Assert-Task2Unknown([object] $Unknown) {
    if ($Unknown.id -ne 'task2.unknown' -or $Unknown.status.ok -or
        $Unknown.status.code -ne 'unsupported_method') {
        throw 'Unknown v2 semantic methods must return unsupported_method.'
    }
}

function Assert-Task2BadParams([object] $BadParams) {
    if ($BadParams.id -ne 'task2.badparams' -or $BadParams.status.ok -or
        $BadParams.status.code -ne 'bad_request') {
        throw 'Wrong-typed v2 params did not return a correlated bad_request.'
    }
}

function Assert-Task2BadId([object] $BadId) {
    if ([string]$BadId.id -ne '' -or $BadId.status.ok -or $BadId.status.code -ne 'bad_request') {
        throw 'Invalid v2 request IDs must return an uncorrelated bad_request envelope.'
    }
}

function Assert-Task2Close([object] $Close) {
    if ($Close.id -ne 'task2.close' -or -not $Close.status.ok) {
        throw 'session.close did not return a valid v2 success envelope.'
    }
}

function Initialize-Task2Protocol {
    $ready = Read-Task2EngineMessage
    if ($ready.event -ne 'ready' -or [int]$ready.protocol -ne 1) {
        throw 'Migration bootstrap ready event changed unexpectedly.'
    }
    $hello = Send-Task2Request @{
        op = 'request'; id = 'task2.hello'; method = 'session.hello'; params = @{}
    }
    Assert-Task2Hello $hello
}

function Invoke-Task2RequestScenarios {
    $ping = Send-Task2Request @{
        op = 'request'; id = 'task2.ping'; method = 'session.ping'; params = @{}
    }
    Assert-Task2Ping $ping

    $unknown = Send-Task2Request @{
        op = 'request'; id = 'task2.unknown'; method = 'task2.nonexistent'; params = @{}
    }
    Assert-Task2Unknown $unknown

    $badParams = Send-Task2Request @{
        op = 'request'; id = 'task2.badparams'; method = 'session.ping'; params = 7
    }
    Assert-Task2BadParams $badParams

    $badId = Send-Task2Request @{
        op = 'request'; id = 42; method = 'session.ping'; params = @{}
    }
    Assert-Task2BadId $badId
}

function Complete-Task2Session {
    $close = Send-Task2Request @{
        op = 'request'; id = 'task2.close'; method = 'session.close'; params = @{}
    }
    Assert-Task2Close $close
    $script:Process.StandardInput.Close()
    if (-not $script:Process.WaitForExit(15000)) {
        $script:Process.Kill($true)
        throw 'obs-engine did not exit after session.close.'
    }
    if ($script:Process.ExitCode -ne 0) {
        throw "obs-engine exited with code $($script:Process.ExitCode)."
    }
    Write-Host 'Protocol-v2 framing smoke test passed.'
}

function Invoke-Task2ProtocolScenario {
    Initialize-Task2Protocol
    Invoke-Task2RequestScenarios
    Complete-Task2Session
}

function Stop-Task2AfterFailure {
    if ($null -ne $script:Process -and -not $script:Process.HasExited) {
        try {
            $script:Process.Kill($true)
            $script:Process.WaitForExit(5000) | Out-Null
        }
        catch {
            Write-Warning "Failed to terminate obs-engine after Task 2 smoke failure: $_"
        }
    }
}

function Write-Task2Diagnostics {
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

function Invoke-Task2FramingSmoke {
    Initialize-Task2Diagnostics
    try {
        Start-Task2Engine
        Invoke-Task2ProtocolScenario
    }
    catch {
        $script:FailureText = ($_ | Out-String)
        Write-Error $script:FailureText
        Stop-Task2AfterFailure
    }
    finally {
        Write-Task2Diagnostics
    }
    if ($null -ne $script:FailureText) {
        throw 'Protocol-v2 framing smoke test failed. See _task2-diagnostics/v2-smoke.txt in the runtime artifact.'
    }
}

Invoke-Task2FramingSmoke

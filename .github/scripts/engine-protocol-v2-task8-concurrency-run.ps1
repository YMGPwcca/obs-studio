$ErrorActionPreference = 'Stop'

function Invoke-Task8ConcurrencyRegression {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$DiagnosticDir = Join-Path $InstallRoot '_task8-concurrency-diagnostics'
New-Item -ItemType Directory -Force -Path $DiagnosticDir | Out-Null
$DiagnosticFile = Join-Path $DiagnosticDir 'a-f-regression.txt'

try {
    $Plugin = Get-ChildItem -Path 'build_x64' -Filter 'task8-concurrency-source.dll' -File -Recurse |
      Where-Object { $_.FullName -notlike "$InstallRoot*" } |
      Select-Object -First 1
    if ($null -eq $Plugin) {
        throw 'Built Task 8 concurrency module was not found.'
    }

    $RuntimeModule = Get-ChildItem -Path $InstallRoot -Filter 'image-source.dll' -File -Recurse |
      Select-Object -First 1
    if ($null -eq $RuntimeModule) {
        throw 'Could not locate the installed OBS module directory.'
    }

    $Destination = Join-Path $RuntimeModule.Directory.FullName $Plugin.Name
    Copy-Item -LiteralPath $Plugin.FullName -Destination $Destination -Force
    "Staged CI-only module: $Destination" | Tee-Object -FilePath $DiagnosticFile

    & '.github/scripts/engine-protocol-v2-task8-concurrency.ps1' -InstallRoot $InstallRoot *>&1 |
      Tee-Object -FilePath $DiagnosticFile -Append
}
catch {
    $Failure = ($_ | Out-String)
    @('', '=== workflow failure ===', $Failure) | Add-Content -Path $DiagnosticFile -Encoding utf8
    Write-Error $Failure
    throw
}

}

Invoke-Task8ConcurrencyRegression

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('12', '13', '14', '15', '16', '17', '18', '19', '20')]
    [string] $Task,
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot,
    [Parameter(Mandatory = $true)]
    [string] $ConsumerPath
)

$ErrorActionPreference = 'Stop'

function Invoke-Phase2Lane {
    $scriptPath = ".github/scripts/engine-protocol-v2-task$Task.ps1"
    if ($Task -eq '17') {
        & $scriptPath -InstallRoot $InstallRoot -ConsumerPath $ConsumerPath
    } else {
        & $scriptPath -InstallRoot $InstallRoot
    }
    if (-not $?) {
        throw "Phase 2 Task $Task lane failed."
    }
}

Invoke-Phase2Lane

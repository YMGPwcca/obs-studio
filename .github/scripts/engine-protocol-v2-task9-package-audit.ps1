$ErrorActionPreference = 'Stop'

function Invoke-Task9PackageAudit {
$ErrorActionPreference = 'Stop'
$InstallRoot = Resolve-Path 'build_x64/install'
$Unexpected = Get-ChildItem -Path $InstallRoot -Filter 'task9-interaction-source.dll' -File -Recurse
if (@($Unexpected).Count -ne 0) {
  throw 'Task 9 CI-only interaction module leaked into the normal install tree.'
}

}

Invoke-Task9PackageAudit

$ErrorActionPreference = 'Stop'

function Invoke-Task8FixtureRemove {
    $installRoot = (Resolve-Path 'build_x64/install').Path
    $installed = Get-ChildItem -LiteralPath $installRoot -Filter 'task8-concurrency-source.dll' -File -Recurse
    if (@($installed).Count -ne 1) {
        throw 'Expected exactly one explicitly staged Task 8 concurrency module.'
    }
    Remove-Item -LiteralPath $installed[0].FullName -Force
    if (@(Get-ChildItem -LiteralPath $installRoot -Filter 'task8-concurrency-source.dll' -File -Recurse).Count -ne 0) {
        throw 'Task 8 CI-only concurrency module remained after cleanup.'
    }
}

Invoke-Task8FixtureRemove

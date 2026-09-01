$ErrorActionPreference = 'Stop'
$bin = (Resolve-Path 'build_x64/install/bin/64bit').Path
Push-Location $bin
try {
    & .\obs-engine-task23-encoder-bridge-test.exe
    if ($LASTEXITCODE -ne 0) { throw "Task 23 active encoder bridge test failed (exit=$LASTEXITCODE)." }
}
finally {
    Pop-Location
}

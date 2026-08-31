$ErrorActionPreference = 'Stop'

function Invoke-Task8ConcurrencyFixtureBuild {
$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task8-concurrency-plugin --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Task 8 concurrency test module build failed (exit=$LASTEXITCODE)."
}

}

Invoke-Task8ConcurrencyFixtureBuild

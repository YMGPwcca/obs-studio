$ErrorActionPreference = 'Stop'

function Invoke-Task9FixtureBuild {
$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task9-interaction-plugin --parallel
if ($LASTEXITCODE -ne 0) {
  throw "Task 9 interaction source build failed (exit=$LASTEXITCODE)."
}

}

Invoke-Task9FixtureBuild

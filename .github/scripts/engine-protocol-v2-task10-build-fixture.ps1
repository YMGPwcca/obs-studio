$ErrorActionPreference = 'Stop'

function Invoke-Task10FixtureBuild {
$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task10-media-plugin --parallel
if ($LASTEXITCODE -ne 0) {
  throw "Task 10 media source build failed (exit=$LASTEXITCODE)."
}

}

Invoke-Task10FixtureBuild

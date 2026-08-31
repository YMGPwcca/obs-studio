$ErrorActionPreference = 'Stop'

function Invoke-Task11FixtureBuild {
$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task11-filter-plugin --parallel
if ($LASTEXITCODE -ne 0) {
  throw "Task 11 filter source build failed (exit=$LASTEXITCODE)."
}

}

Invoke-Task11FixtureBuild

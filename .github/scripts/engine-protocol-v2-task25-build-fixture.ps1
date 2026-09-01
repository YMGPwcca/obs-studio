$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task25-service-plugin --parallel 1
if ($LASTEXITCODE -ne 0) { throw "Task 25 service fixture build failed (exit=$LASTEXITCODE)." }

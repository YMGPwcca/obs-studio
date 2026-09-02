$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task23-encoder-plugin obs-engine-task25-service-plugin obs-engine-task26-output-plugin --parallel 1
if ($LASTEXITCODE -ne 0) { throw "Task 26 fixture build failed (exit=$LASTEXITCODE)." }

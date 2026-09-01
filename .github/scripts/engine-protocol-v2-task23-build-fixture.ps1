$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task23-encoder-plugin obs-engine-task23-encoder-bridge-test --parallel
if ($LASTEXITCODE -ne 0) { throw "Task 23 encoder fixture build failed (exit=$LASTEXITCODE)." }

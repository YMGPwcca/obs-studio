$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task23-encoder-plugin --parallel 1
if ($LASTEXITCODE -ne 0) { throw "Task 24 encoder fixture build failed (exit=$LASTEXITCODE)." }

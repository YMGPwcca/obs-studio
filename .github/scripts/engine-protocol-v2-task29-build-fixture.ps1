$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task23-encoder-plugin obs-engine-task29-replay-plugin --parallel 1
if ($LASTEXITCODE -ne 0) { throw "Task 29 fixture build failed (exit=$LASTEXITCODE)." }

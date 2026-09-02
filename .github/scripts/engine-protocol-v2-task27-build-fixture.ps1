$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task23-encoder-plugin obs-engine-task27-recording-plugin --parallel 1
if ($LASTEXITCODE -ne 0) { throw "Task 27 fixture build failed (exit=$LASTEXITCODE)." }

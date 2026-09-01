$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task21-audio-plugin --parallel
if ($LASTEXITCODE -ne 0) { throw "Task 21 audio fixture build failed (exit=$LASTEXITCODE)." }

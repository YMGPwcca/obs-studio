$ErrorActionPreference = 'Stop'
cmake --build build_x64 --config RelWithDebInfo --target obs-engine-task22-hotkey-plugin --parallel
if ($LASTEXITCODE -ne 0) { throw "Task 22 hotkey fixture build failed (exit=$LASTEXITCODE)." }

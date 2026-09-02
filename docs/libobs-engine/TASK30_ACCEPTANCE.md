# Task 30 Windows Virtual Camera acceptance

Runtime-bearing Task 30 checkpoint is the Task 30 implementation commit
recorded in the final Phase-3 handoff. The local build used the repository
GUID `A3FCE0F5-3493-419F-958A-ABA1250EC20B` from `CMakePresets.json` and
included the packaged x64 `obs-virtualcam-module64.dll` and `win-dshow.dll`.

## Deterministic protocol lane

`.github/scripts/engine-protocol-v2-task30.ps1` passed on the Windows x64
package. It verified the complete capability descriptor, the stable
`unsupported_capability` path when registration is absent, managed Output
creation/removal, `virtualCamera` role visibility, program/Canvas/source
target handling, active retarget rejection, stale-target removal protection,
canonical Output lifecycle, and clean shutdown.

## Physical Windows lane

`.github/scripts/engine-protocol-v2-task30-physical.ps1` passed against the
production-only package with no CI-only modules staged. An external FFmpeg
DirectShow consumer opened the actual `OBS Virtual Camera` device and captured
eight complete BGR24 frames for each of three Engine source targets: red,
green, and blue. Each capture was 1920x1080, and the measured channel values
matched the requested solid color within the acceptance tolerance. The run
covered consumer connect/disconnect, three target switches between clean
start/stop cycles, restart behavior, and Engine shutdown. The generated
virtual-camera metadata was `1920x1080x166666` (60 fps).

| Capture | Bytes | SHA-256 | Frames | Expected |
|---|---:|---|---:|---|
| `task30-red.raw` | 49,766,400 | `7777449B798B450283EECF180D4AF64AFD087848071E47A1156EB877C4439FD8` | 8 | BGR `(0,0,255)` |
| `task30-green.raw` | 49,766,400 | `1A8DEAC2A3034640C79154E50E420732961FBFAA7B8202AC5879DAB003CCEA9D` | 8 | BGR `(0,255,0)` |
| `task30-blue.raw` | 49,766,400 | `C9C64993114EF6133EDACA759E6C7AAC2D572415292CDCE5C023D4F3A15A20B5` | 8 | BGR `(255,0,0)` |

Physical functionality result: **PASS**. Rebinding the registry to the exact
workspace COM DLL was attempted with `regsvr32 /i /s` but returned exit code
3 because this shell is not elevated; the host already had a registered OBS
Virtual Camera device, so the DirectShow consumer used that existing driver
while the Engine, `win-dshow`, and shared-queue path came from the exact local
package. A separately elevated registration check remains an environment
follow-up, not a substituted protocol or consumer test.

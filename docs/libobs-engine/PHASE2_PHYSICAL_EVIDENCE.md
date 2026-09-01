# Phase 2 Physical Windows GPU Evidence

**Date:** 2026-09-01
**Branch:** `phase2-scene-render-graph`
**Runtime artifact candidate:** `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`
**Host:** Windows 11 Pro 25H2, build `26200`, revision `9278`, x64
**CPU:** AMD Ryzen 7 9700X, 8 physical cores / 16 logical processors

## GPU and adapter identity

| Adapter | Device | Driver | HAGS |
|---|---|---|---|
| 0 | AMD Radeon RX 9060 XT | `32.0.31041.1004` | Enabled |
| 1 | AMD Radeon(TM) Graphics | `32.0.21045.5002` | Unsupported/disabled |

The engine resource descriptor reported adapter LUID `00000000-0001315D` for
the active RX 9060 XT path. The negative consumer check used
`FFFFFFFF-FFFFFFFF` and correctly rejected the adapter mismatch.

## Exact artifacts and command

Engine:

```text
E:\Side projects\libobs\build_x64\install\bin\64bit\obs-engine.exe
```

Consumer:

```text
E:\Side projects\libobs\build_x64\engine\RelWithDebInfo\obs-engine-preview-consumer-test.exe
```

The integrated physical driver was run as:

```powershell
& .github/scripts/engine-protocol-v2-phase2-physical.ps1 `
  -InstallRoot (Resolve-Path 'build_x64/install') `
  -ConsumerPath (Resolve-Path 'build_x64/engine/RelWithDebInfo/obs-engine-preview-consumer-test.exe')
```

The driver verifies the ready marker, capabilities, event sequence and
response-before-event ordering, graph operations, cross-process D3D11 opening,
keyed-mutex synchronization, BGRA pixels, resource generations, invalidation,
cleanup, stderr classification, and clean engine exit.

## Physical result

The complete integrated scenario passed:

```text
Physical Item transform/crop/bounds/order/visibility/lock/blend coverage: PASS
D3D11 adapter-mismatch rejection: PASS
Physical Scene -> Canvas -> Preview -> Transition -> Program flow: PASS
Physical Studio transition cancellation/interruption: PASS
Physical current Scene target removal/invalidation: PASS
Physical PreviewOutput retarget/resize/resource recreation: PASS
Physical stderr classification: PASS
Phase 2 integrated physical acceptance: PASS
```

The item coverage included transform, crop, bounds, order, visibility, lock,
scale filter, blend mode, and blend method, with the fixture restored before
pixel assertions. The live Scene target was removed while its PreviewOutput
still existed; the output became unavailable before the output was destroyed.
All test Sources, Scenes, the private Canvas, PreviewOutputs, and the session
were then removed or closed in deterministic lifetime order.

## Cross-process pixel and synchronization evidence

Every static target acquired 24 frames with zero keyed-mutex timeouts. The
consumer opened the engine-owned shared texture on the matching adapter and
validated BGRA8 pixels:

| Target | Center BGRA | Frames | Timeouts | Unique checksums | Resource generation |
|---|---|---:|---:|---:|---:|
| Program | `[0, 0, 255, 255]` | 24 | 0 | 1 | 1 |
| Preview | `[255, 0, 0, 255]` | 24 | 0 | 1 | 1 |
| Scene | `[0, 255, 0, 255]` | 24 | 0 | 1 | 1 |
| Source | `[0, 255, 0, 255]` | 24 | 0 | 1 | 1 |
| Canvas | `[0, 255, 0, 255]` | 24 | 0 | 1 | 1 |

The corresponding static checksums were:

```text
Program: 5736738925427773467
Preview: 4742208568145280259
Scene/Source/Canvas: 527320876978591107
```

The physical Studio transition ran for 90 consumer frames with zero timeouts.
Program changed from the red checksum `5736738925427773467` to the blue
checksum `4742208568145280259`, with **47 unique checksums observed**. Preview
remained the blue target with one stable checksum. This proves actual content
change rather than only successful texture opening.

The cancellation run restored Program to red, started a 600 ms Studio
transition, and immediately called `program.setScene` for the green Scene. It
acquired 90 frames with zero timeouts, observed 2 unique checksums, and ended
on green checksum `527320876978591107`. The driver verified one command
revision, `program.sceneChanged` followed by `transition.ended`, logical
`previousScene` equal to the red Scene, idle Studio/Transition/Program state,
and no delayed duplicate `transition.ended`.

After retargeting to Canvas, Canvas video settings were reset and the output
resource was resized to `96x54`. The resized consumer acquired 24 frames with
zero timeouts, center BGRA `[0, 255, 0, 255]`, checksum
`11388457050740651251`, matching adapter LUID `00000000-0001315D`, and
resource generation `3`.

## Physical build artifact hashes

Source SHA: `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`.

| File | SHA-256 |
|---|---|
| `obs-engine.exe` | `4A1D35E2AE4A9E3641CF76513EB0BAEE7D8CFCF03AD1EF6D90232E414A92DF8D` |
| `obs.dll` | `DB4CF4BC846AD4A901D8260FF68FFF5EDADBE000EFC7F6A6CCF1515049FAD7F7` |
| `libobs-d3d11.dll` | `2972C28EBF41AB76493AA8EEE46356F4B81B5209FAF6F30C322CE0337691F13B` |
| `libobs-winrt.dll` | `5BE8F3C5050AB21C629CC162E8DA9E59103C2A6A92FCBDDBE773C4449344E5DF` |
| `w32-pthreads.dll` | `50FC04DCF7D7A681F0E0BF3974FD5042FA34A03C765B4DE341E7622E69DAC44D` |
| `obs-transitions.dll` | `9D7795F23843088832C9FEA867B7B557BFA43E397966B892546E900AC046E282` |
| `obs-engine-preview-consumer-test.exe` | `7E66902CC9E0913B2B4EAECE2B1C1368CBC1D8185976A85F388312906925EC34` |

The hosted exact-SHA regression artifact is separate: artifact ID `9802766472`,
name `obs-engine-windows-x64-phase2-regression-13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`,
source SHA `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`. Its binary hashes are
recorded separately in `PHASE2_ACCEPTANCE.md`; they differ from the physical
build because the Windows builds are not byte-reproducible.

## stderr and exit-state review

The engine exited cleanly. The driver failed closed on the forbidden legacy
fallback-canvas warning and on any unclassified warning/error. The only
classified diagnostics were host/optional-module conditions observed on this
machine: HAGS enabled, absent AJA hardware, unavailable CoreAudio AAC,
absent DeckLink hardware, unavailable NVIDIA optional effects/NVENC, and no
VLC installation. No fallback Canvas warning, unexpected diagnostic, crash,
deadlock, stdout corruption, or consumer timeout occurred.

## Boundary

This is physical acceptance of the D3D11 shared PreviewOutput and Phase-2
composition path on the stated Windows host. It does not claim forced
device-loss recovery; that remains planned later work. Hosted exact-SHA CI and
independent advisor review are still required before human acceptance.

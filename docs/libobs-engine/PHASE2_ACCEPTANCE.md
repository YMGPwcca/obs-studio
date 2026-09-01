# LibOBS Engine Protocol v2 — Phase 2 Acceptance Record

**Status:** Phase 2 **IN REVIEW** / ready for independent advisor review
**Snapshot date:** 2026-09-01
**Candidate branch:** `phase2-scene-render-graph`
**Phase-2 base:** `15457fcfd7abf3f9f0ddd8f43f3f1885980de8c8`
**Accepted Task-11 implementation parent:** `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
**Runtime implementation checkpoint:** `95214baed67950c430dc1fd1a0411d69c761f999`
**Current verification tip when this record was prepared:** `2a974c55b700826459c0aa6736a9d06526db9d06`

The runtime checkpoint is the last commit that changes engine/runtime or the
consumer fixture. The commits after it are verification-only acceptance-driver
and evidence-record changes; `git diff 95214baed67950c430dc1fd1a0411d69c761f999..HEAD -- engine`
is empty. The final branch tip must still be resolved with `git rev-parse HEAD`
after any documentation-only update.

This record deliberately does not claim `ACCEPTED`. No production branch was
moved, no `engine-protocol-v2` ref was advanced, and Task 21 or later was not
started.

## Ordered Phase-2 commit chain

| Commit | Scope |
|---|---|
| `3dea90d60fecf1c88a5eb91af23a7160da600da8` | Phase-2 architecture and ownership model |
| `6be51d166b03474765d0f8242af441c61748a03f` | Task 12 — `scene.*` |
| `141906c4249d1ecdbf4e79188dbcb8016d04a3c2` | Task 13 — `item.*` |
| `47ddc99c15f95de87a895d3e1ca3dd66bac3befe` | Task 14 — `canvas.*` |
| `94c52dc5d0728ea4bd75efa95a04568f614eebdf` | Task 15 — `program.*` |
| `438ee2328c8f82f363ef551e8ff85b96b1e076d6` | Task 16 — `preview.*` |
| `9c268d76ec6f4f6a51a75dbfd41b8f23d637ad43` | Task 15 follow-up — program route utility include |
| `cde3063445f7a8977b40a09cab3e547203657d68` | Task 17 — D3D11 shared PreviewOutput transport |
| `aee7464375c7c875b6c407e0f909546e990d3355` | Task 19 — `transition.*` |
| `42c724c1d247bc64f15475c3edc1d39393ba2f7c` | Task 18 — `studio.*` |
| `0748d0f323d1b2ee9a4403b01004718e4cbf6f99` | Task 20 — `previewOutput.*` |
| `7e476bbf384d9bdca37dc9a7f405a430ef1a8190` | Phase-2 verification, regression lanes, package audit, complexity hardening |
| `95214baed67950c430dc1fd1a0411d69c761f999` | D3D11 consumer adapter-scan correction |
| `782551e733bd589d15072c351d251013e7b90317` | Final integrated physical acceptance driver |
| `2a974c55b700826459c0aa6736a9d06526db9d06` | Physical evidence fields and resource-generation reporting |

The implementation order is the authorized dependency order:

```text
12 -> 13 -> 14 -> 15 -> 16 -> 17 -> 19 -> 18 -> 20
```

## Scope and architecture result

Tasks 12–20 now form one live render graph:

```text
Canvas -> Scene -> Item -> Source
                 |
                 +-> Program / Preview
                         |
                         +-> Transition / Studio
                                 |
                                 +-> PreviewOutput
```

The implementation preserves the single stdin/stdout newline-delimited
Engine Protocol v2 boundary. stdout remains protocol-only; logs remain on
stderr. Handles are process-local, ephemeral canonical decimal strings. The
Controller owns durable/user state, while the Engine owns runtime libobs state.
No raw pointers, COM pointers, HWNDs, OBS WebSocket, REST, GraphQL, or second
Controller-facing control planes were introduced.

The D3D11 PreviewOutput path uses an engine-owned legacy DXGI shared resource,
full-width shared-handle extraction, adapter-LUID validation, and keyed-mutex
ownership. The Controller-side consumer opens the resource with
`ID3D11Device::OpenSharedResource`; it does not close or duplicate the engine's
legacy shared handle. Resource generations change on resize and Canvas video
reset. See [`D3D11_SHARED_PREVIEW_V1.md`](../../engine/D3D11_SHARED_PREVIEW_V1.md)
and [`PREVIEW_OUTPUT_V1.md`](../../engine/PREVIEW_OUTPUT_V1.md).

## Local verification matrix

All local runtime lanes used the audited Windows artifact from the runtime
implementation checkpoint above. The exact-SHA hosted workflow is
`.github/workflows/engine-protocol-v2-phase2.yaml`; its hosted run was pending
at the time this record was first prepared and must be recorded after push.

| Lane | Local result | Evidence |
|---|---|---|
| Task 1 / 1.1 | PASS | footprint, Protocol-v1 path, package boundary |
| Task 2 | PASS | bounded v2 framing |
| Task 3 | PASS | capability discovery with Phase-2 capability filtering |
| Task 4 | PASS | revision and optimistic-concurrency semantics |
| Task 5 | PASS | subscriptions, sequence, overflow/resync |
| Task 6 | PASS | runtime smoke, including Phase-2 scene/item events |
| Task 7 | PASS | properties bridge and smoke lanes |
| Task 8 | PASS | source namespace, concurrency A–F, fixture cleanup |
| Task 9 | PASS | interaction namespace and callback fixture |
| Task 10 | PASS | media namespace and timeout/settlement behavior |
| Task 11 | PASS | filter namespace and timeout ownership race |
| Task 12 | PASS | `.github/scripts/engine-protocol-v2-task12.ps1` |
| Task 13 | PASS | `.github/scripts/engine-protocol-v2-task13.ps1` |
| Task 14 | PASS | `.github/scripts/engine-protocol-v2-task14.ps1` |
| Task 15 | PASS | `.github/scripts/engine-protocol-v2-task15.ps1` |
| Task 16 | PASS | `.github/scripts/engine-protocol-v2-task16.ps1` |
| Task 17 | PASS | Task-17 D3D11 consumer integration plus physical GPU run |
| Task 18 | PASS | `.github/scripts/engine-protocol-v2-task18.ps1` |
| Task 19 | PASS | `.github/scripts/engine-protocol-v2-task19.ps1` |
| Task 20 | PASS | `.github/scripts/engine-protocol-v2-task20.ps1` |
| Complexity self-tests | PASS | pinned checker self-tests |
| Complexity gate | PASS | pinned lizard and PowerShell AST gate |
| Normal package audit | PASS | package contains only the intended headless runtime surface |
| Final integrated physical flow | PASS | [`PHASE2_PHYSICAL_EVIDENCE.md`](PHASE2_PHYSICAL_EVIDENCE.md) |

The final local Tasks 1–11 orchestration ended with:

```text
Task 9 interaction namespace: PASS
Task 10 media namespace: PASS
Task 11 filter integration: PASS
Task 11 timeout ownership race regression: PASS
Tasks 1-11 exact-SHA regression matrix: PASS
```

The Phase-2 lane runner passed Tasks 12–20 on the same audited artifact. The
normal package audit passed before and after fixture use.

## Complexity evidence

The gate used `lizard==1.24.0` in an isolated target path and the repository's
pinned `powershell-yaml==0.4.12` parser. The final local measurement was made
at `2a974c55b700826459c0aa6736a9d06526db9d06`; documentation-only commits do
not alter the measured source scope.

| Metric | Final value |
|---|---:|
| Scoped function/script-body count | 1,735 |
| Average cyclomatic complexity | 4.001 |
| Median cyclomatic complexity | 4 |
| P90 cyclomatic complexity | 8 |
| Maximum cyclomatic complexity | 13 |
| Scopes above CC 5 | 441 |
| Scopes above CC 7 | 191 |
| Scopes above CC 10 | 1 |

The sole CC 13 scope is the pre-existing upstream
`libobs/obs-source.c::obs_source_destroy_defer`; no new Phase-2 scope exceeds
the repository gate's CC 10 limit. New Phase-2 scopes at the boundary include
the D3D11 consumer `main`, `runtime_scene_v2.cpp::v2_scene_remove`,
`runtime_preview_output_v2.cpp::v2_preview_output_set_target`, and the relevant
scene/item/canvas helpers at CC 10. The physical PowerShell driver has a CC 5
top-level body; its highest measured named function is CC 8.

## Artifact evidence

The audited local runtime package was built from runtime checkpoint
`95214baed67950c430dc1fd1a0411d69c761f999` and reported
`0.0.1-git-95214baed`:

| File | SHA-256 |
|---|---|
| `build_x64/install/bin/64bit/obs-engine.exe` | `B5037B3B9E69B6F676FB72A11257BDC9BA197DE794DF4CA68098ED9CFE03F7E8` |
| `build_x64/install/bin/64bit/obs.dll` | `9B64618CD5B5FBAEB8ED893C726F38EF2A916964E9D7E8E27FF81AFC83C4532A` |
| `build_x64/install/bin/64bit/libobs-d3d11.dll` | `2972C28EBF41AB76493AA8EEE46356F4B81B5209FAF6F30C322CE0337691F13B` |
| `build_x64/engine/RelWithDebInfo/obs-engine-preview-consumer-test.exe` | `7E66902CC9E0913B2B4EAECE2B1C1368CBC1D8185976A85F388312906925EC34` |

The normal package audit found exactly one `obs-engine.exe` and no OBS
frontend/browser/WebSocket/test executable leakage in the normal package.

## Remaining debt and review boundary

- Hosted GitHub Actions exact-SHA evidence must be recorded after the branch is
  pushed. Local evidence does not substitute for that hosted run.
- Forced graphics device/adapter-loss recovery is not claimed in Phase 2. The
  physical run verifies resize, Canvas video reset, resource replacement,
  adapter mismatch rejection, and clean shutdown; systematic device-loss
  reconstruction remains the separately planned Task 44 scope.
- T-bar/UI ownership remains intentionally outside the Engine contract. The
  Studio contract exposes runtime transition semantics only.
- No production promotion, master merge, `engine-protocol-v2` update, or Task
  21+ work is authorized by this branch.

## Final status

```text
Tasks 1-11: ACCEPTED and regression-green
Task 12: IMPLEMENTED / IN PHASE REVIEW
Task 13: IMPLEMENTED / IN PHASE REVIEW
Task 14: IMPLEMENTED / IN PHASE REVIEW
Task 15: IMPLEMENTED / IN PHASE REVIEW
Task 16: IMPLEMENTED / IN PHASE REVIEW
Task 17: IMPLEMENTED / IN PHASE REVIEW
Task 18: IMPLEMENTED / IN PHASE REVIEW
Task 19: IMPLEMENTED / IN PHASE REVIEW
Task 20: IMPLEMENTED / IN PHASE REVIEW
Phase 2: IN REVIEW / READY FOR INDEPENDENT REVIEW
Task 21+: NOT STARTED / NOT AUTHORIZED
```

# LibOBS Engine Protocol v2 — Phase 2 Acceptance Record

**Status:** Phase 2 **IN REVIEW** / ready for independent advisor review
**Snapshot date:** 2026-09-01
**Candidate branch:** `phase2-scene-render-graph`
**Phase-2 base:** `15457fcfd7abf3f9f0ddd8f43f3f1885980de8c8`
**Accepted Task-11 implementation parent:** `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
**Previous reviewed HEAD:** `db2f878672869cf38a5d6b798bdf0044ad507595`
**Runtime candidate SHA:** `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`
**Complexity measurement SHA:** `33845ef42cdb8e16cfc122b17032266ed0b9640a`
**Final verification workflow:** `33513275145`

The runtime candidate includes the three repair commits, the complete
complexity-baseline freeze, and the lease-test correction. The final physical
run and hosted exact-SHA matrix both used this committed candidate. Later
documentation-only handoff changes do not alter the runtime candidate; the
current branch tip must still be checked with `git rev-parse HEAD`.

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
| `71e65a5b4bbf63347ebc67b01593a49ed38095cd` | Repair — transactional Canvas video-mix reset and deterministic failure seam |
| `ba88ff308df6d217c7a42a0739459ad6582abdf2` | Repair — canonical Studio cancellation, duration ownership, progress telemetry, and lease shape |
| `5206d23c90943968b6667ebd5837ed80670bf0d0` | Repair — complete-baseline checker and adversarial workflow lane |
| `55aca3debcc6a08a28ddaac589e3f08cf18a147c` | Complete Phase-2 complexity baseline freeze |
| `33845ef42cdb8e16cfc122b17032266ed0b9640a` | Test correction — tolerate unrelated read-side revision advance in lease checks |
| `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8` | Final complete-baseline refresh after test correction |

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

## Repair-pass blocker resolution

- Complexity: the checker now requires a complete baseline/continuity match;
  the final freeze covers all 1,834 enforced scopes.
- Studio cancellation: `program.setScene` synchronously suppresses and drains
  the real transition callback owner, preserves the logical prior Program
  Scene, and emits `program.sceneChanged` then one `transition.ended` at the
  command revision.
- Canvas reset: the private libobs reset path creates the replacement mix
  before swapping settings/mix membership; injected allocation failure leaves
  the old Canvas and PreviewOutput resource untouched.
- Duration: `transition.durationChanged` is the only duration event owner;
  Studio setters are convenience entry points.
- Progress: `transition.progress` is bounded opt-in telemetry through the
  existing dispatcher and never advances the mutation revision per frame.
- PreviewOutput leases: consumer attachment remains ephemeral and is absent
  from canonical wire snapshots.

## Local verification matrix

All local runtime lanes used the clean Windows artifact built from runtime
candidate `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`. The exact-SHA hosted
workflow is `.github/workflows/engine-protocol-v2-phase2.yaml`; final run
`33513275145` passed at that same SHA. The standalone complexity workflow
`33513274930` also passed at the same SHA.

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
| Task 14 failure atomicity | PASS | deterministic test-hook build and A→B retry lane |
| Task 15 | PASS | `.github/scripts/engine-protocol-v2-task15.ps1` |
| Task 16 | PASS | `.github/scripts/engine-protocol-v2-task16.ps1` |
| Task 17 | PASS | Task-17 D3D11 consumer integration plus physical GPU run |
| Task 18 | PASS | `.github/scripts/engine-protocol-v2-task18.ps1` |
| Task 19 | PASS | `.github/scripts/engine-protocol-v2-task19.ps1` |
| Task 20 | PASS | `.github/scripts/engine-protocol-v2-task20.ps1` |
| Complexity self-tests | PASS | pinned checker self-tests |
| Complexity gate | PASS | pinned lizard and PowerShell AST gate |
| Complexity freeze completeness | PASS | 1,834 enforced / 1,834 matched / 0 unbaselined |
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

The repair-specific adversarial coverage passed at the same local candidate:

- Studio cancellation A→B then immediate Program C: one command revision,
  `program.sceneChanged` then `transition.ended`, logical previous Scene A,
  idle agreement, and no delayed duplicate or route to B.
- Near-completion cancellation: one completion owner and no duplicate end.
- Canvas reset failure injection: error at the old revision, unchanged Canvas
  generation/share descriptor/rendering, no reset/resource events; retry then
  committed exactly one reset revision and one resource generation step.
- Duration setters: both mutate the one `TransitionEntry.duration_ms` state and
  emit only `transition.durationChanged`; equal values are no-ops.
- Progress telemetry: opt-in only, finite `0..1` samples, no per-frame
  revisions, bounded/coalesced delivery, and no telemetry-induced resync.
- PreviewOutput leases: `consumerAttached` is absent from canonical wire
  snapshots and lease operations remain non-revisioned.

## Hosted exact-SHA matrix

Workflow run: [33513275145](https://github.com/YMGPwcca/obs-studio/actions/runs/33513275145)

Head SHA: `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`

| Hosted lane | Job ID | Result |
|---|---:|---|
| Complexity self-tests and gate | `99873922227` | PASS |
| Tasks 1–11 same-SHA regression matrix | `99873922387` | PASS |
| Task 14 failure-atomic Canvas reset | `99875439478` | PASS |
| Task 12 | `99875439497` | PASS |
| Task 17 | `99875439525` | PASS |
| Task 19 | `99875439537` | PASS |
| Task 14 | `99875439541` | PASS |
| Task 20 | `99875439559` | PASS |
| Task 15 | `99875439571` | PASS |
| Task 16 | `99875439626` | PASS |
| Task 18 | `99875439656` | PASS |
| Task 13 | `99875439814` | PASS |

The only hosted annotation was the platform Node.js 20 deprecation notice for
the pinned `upload-artifact` action. It did not affect any job result.

The standalone Phase-1 complexity workflow also passed: run
`33513274930`, job `99873921498`, head SHA
`13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`.

## Complexity evidence

The gate used `lizard==1.24.0` in an isolated target path and the repository's
pinned `powershell-yaml==0.4.12` parser. The complete final snapshot was
measured at `33845ef42cdb8e16cfc122b17032266ed0b9640a` and frozen by the
baseline-refresh commit `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`.

| Metric | Final value |
|---|---:|
| Scoped function/script-body count | 1,834 |
| Average cyclomatic complexity | 3.957 |
| Median cyclomatic complexity | 3 |
| P90 cyclomatic complexity | 8 |
| Maximum cyclomatic complexity | 13 |
| Scopes above CC 5 | 461 |
| Scopes above CC 7 | 196 |
| Scopes above CC 10 | 1 |

The sole CC 13 scope is the pre-existing upstream
`libobs/obs-source.c::obs_source_destroy_defer`; no new Phase-2 scope exceeds
the repository gate's CC 10 limit. New Phase-2 scopes at the boundary include
the D3D11 consumer `main`, `runtime_scene_v2.cpp::v2_scene_remove`,
`runtime_preview_output_v2.cpp::v2_preview_output_set_target`, and the relevant
scene/item/canvas helpers at CC 10. The physical PowerShell driver has a CC 5
top-level body; its highest measured named function is CC 8.

Complexity-freeze details:

- Old accepted baseline blob: `81e2f631d41e84b71569ea46ca9439da367dc567`.
- New complete Phase-2 baseline blob: `eb5ed120ddc3f159ce5d0d3a317fd4c741532525`.
- Checker pin: `eb5ed120ddc3f159ce5d0d3a317fd4c741532525`.
- Enforced scopes: 1,834; baseline/continuity matched: 1,834;
  unbaselined: 0.
- Named functions: 1,783; script bodies: 51; named-function average 4.012,
  median 4, P90 8, max 13; enforced average 3.957, median 3, P90 8, max 13.
- Exact exception: `libobs/obs-source.c::obs_source_destroy_defer` at CC 13.
- Freeze self-test: introduced scope at CC 2, intentional CC 3 regression failed
  against baseline 2, restored CC 2 passed with `1/1/0` completeness summary.

## Artifact evidence

Physical build artifacts were produced on the stated Windows host from exact
runtime candidate `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8` and are recorded
in [`PHASE2_PHYSICAL_EVIDENCE.md`](PHASE2_PHYSICAL_EVIDENCE.md):

| File | SHA-256 |
|---|---|
| `obs-engine.exe` | `4A1D35E2AE4A9E3641CF76513EB0BAEE7D8CFCF03AD1EF6D90232E414A92DF8D` |
| `obs.dll` | `DB4CF4BC846AD4A901D8260FF68FFF5EDADBE000EFC7F6A6CCF1515049FAD7F7` |
| `libobs-d3d11.dll` | `2972C28EBF41AB76493AA8EEE46356F4B81B5209FAF6F30C322CE0337691F13B` |
| `libobs-winrt.dll` | `5BE8F3C5050AB21C629CC162E8DA9E59103C2A6A92FCBDDBE773C4449344E5DF` |
| `w32-pthreads.dll` | `50FC04DCF7D7A681F0E0BF3974FD5042FA34A03C765B4DE341E7622E69DAC44D` |
| `obs-transitions.dll` | `9D7795F23843088832C9FEA867B7B557BFA43E397966B892546E900AC046E282` |
| `obs-engine-preview-consumer-test.exe` | `7E66902CC9E0913B2B4EAECE2B1C1368CBC1D8185976A85F388312906925EC34` |

Hosted final exact-SHA artifacts came from Phase-2 workflow run
`33513275145`, regression artifact ID `9802766472`, and the same source SHA
`13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`:

| File | SHA-256 |
|---|---|
| `obs-engine.exe` | `B445554FF92A8F3B0CB5B86D21E3D9C7736C155298CCCB43A736B07F8747884D` |
| `obs.dll` | `FDB0570BFB36761EFC116C63A556AD6446751A04D95E4E0929776C62E6E1203A` |
| `libobs-d3d11.dll` | `21D5DD13E606A4CA9A5A87129627790362B4ACC020B2CBC5A1C2FA3C9F1C2E90` |
| `libobs-winrt.dll` | `E7738E6F33985F73581F5F149D1C09F10DFA835034A96FB8B989384E162ED90B` |
| `w32-pthreads.dll` | `7C925D8C0CCC332EB429F175784003EF49E0BEF06AD194002DF4F4CAAFBF9EF0` |
| `obs-transitions.dll` | `E8F709383A59577E8565B3C3713A26186F5D66CB934F9953DBBDAA13C95293D4` |

The normal package audit found exactly one `obs-engine.exe` and no OBS
frontend/browser/WebSocket/test executable leakage in the normal package.
The physical and hosted hash tables identify different build outputs from the
same source SHA; they are not being presented as byte-identical binaries.

## Remaining debt and review boundary

- Hosted GitHub Actions run `33513275145` passed all final Phase-2 jobs at the
  exact runtime candidate SHA, including the new Canvas failure lane and the
  complete complexity-baseline check.
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

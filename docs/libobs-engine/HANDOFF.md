# LibOBS Engine Protocol v2 — Local AI Agent Handoff

**Handoff date:** 2026-09-01
**Repository:** `YMGPwcca/obs-studio`  
**Production branch:** `engine-protocol-v2`
**Candidate branch:** `phase2-scene-render-graph`
**Historical script-body hardening branch:** `phase1-scriptbody-hardening`
**Historical workflow executable hardening branch:** `phase1-workflow-exec-hardening`
**Accepted Task-10 implementation:** `6a590c2985a99d186c8eecd0241acdc824d32168` (`fix(engine): correlate media actions by source ticket`)
**Accepted Task-10 documentation checkpoint:** `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`
**Accepted Task-11 implementation:** `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
**Task-11 acceptance record:** `TASK11_ACCEPTANCE.md`
**Accepted Phase-1 complexity hardening:** `1b2ddacbb36c39bb61fd645594f0746f106956bf`
**Phase-1 acceptance record:** `PHASE1_COMPLEXITY_ACCEPTANCE.md`
**Human-approved reviewed Phase-2 tip:** `1c71ceaf502eb622d37efc45842525967818fb6f`
**Reviewed Phase-2 runtime candidate:** `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`
**Complete Phase-2 complexity baseline:** `eb5ed120ddc3f159ce5d0d3a317fd4c741532525`
**Phase-2 acceptance commit:** the single documentation-only child of the reviewed Phase-2 tip
**Phase-3 candidate branch:** `phase3-output-stack`
**Phase-3 authorization:** Tasks 21–30 only; Task 31+ remains out of scope
**Task 10 implementation status:** COMPLETE / ACCEPTED
**Task 11 implementation status:** COMPLETE / ACCEPTED
**Phase-1 complexity hardening status:** COMPLETE / ACCEPTED
**Phase-1 script-body hardening status:** COMPLETE / ACCEPTED in current lineage
**Phase-1 workflow executable hardening status:** COMPLETE / ACCEPTED in current lineage
**Phase-2 Tasks 12-20 status:** COMPLETE / ACCEPTED
**Phase-2 status:** ACCEPTED
**Task 21–30 status:** AUTHORIZED / NOT YET IMPLEMENTED on the Phase-3 candidate

The workflow executable hardening cleanup was the tooling-only pre-Phase-2
checkpoint and is incorporated in the accepted Phase-2 lineage. Its historical
evidence remains unchanged. The human-approved Phase-2 candidate is recorded at
the reviewed tip and runtime candidate above. The Phase-3 candidate is a new
branch from the exact accepted Phase-2 checkpoint; see
`PHASE3_ARCHITECTURE.md` for its authorized scope.

This file exists so a local AI coding agent can continue the project without access to the previous ChatGPT conversation. It records project decisions, accepted behavior, verification evidence, known traps, and the required working process. **Verify everything against the checked-out source before changing it.**

---

## 1. Mission

The project is splitting OBS Studio into two intentionally separated pieces:

1. A small, headless, GPL `obs-engine.exe` process that links and owns `libobs` plus OBS runtime plugins.
2. A private/proprietary Controller/UI that launches and controls that engine over a semantic Engine Protocol.

The goal is not to expose every C symbol in libobs. The goal is a **frontend-equivalent semantic control surface** that lets a private UI implement normal OBS Studio functionality while keeping the GPL-linked runtime in its own process.

The Controller should eventually be able to build a polished private OBS-like application: sources, scenes, scene items, source properties, interaction, media controls, filters, canvases, studio mode, transitions, audio, encoders/services/outputs, streaming/recording/replay/virtual camera, screenshots, missing files, statistics, runtime/module information, extensions, and recovery tooling.

---

## 2. Architecture decisions that are already settled

### 2.1 One Controller-facing API only

The private Controller talks to `obs-engine.exe` through the custom Engine Protocol. Do **not** add a second API. In particular:

- do not bolt in OBS WebSocket as another Controller-facing control plane;
- do not add REST;
- do not add GraphQL;
- do not add an unauthenticated TCP listener;
- do not make the Controller sometimes use Engine Protocol and sometimes call a second mechanism.

The protocol style may borrow good ideas from OBS WebSocket, but it is this project’s own protocol and should improve on/fit the engine architecture.

### 2.2 Transport

The initial transport is redirected standard input/output using newline-delimited UTF-8 JSON.

- requests arrive on `stdin`;
- responses/events leave on `stdout`;
- logs are `stderr` only;
- each protocol message is exactly one JSON line;
- message size is bounded (currently 256 KiB / `262144` bytes advertised in hello);
- the Controller launches the process and owns its standard handles.

The semantic protocol should remain transport-independent. A future transport may be possible, but it must preserve the same protocol and must not become a parallel API.

### 2.3 State ownership

**Controller owns durable/product state**, including at least:

- projects/workspaces;
- persistent UUIDs/identities across engine restart;
- scene-collection/profile persistence policy;
- undo/redo history;
- UI state/layout;
- user/business/application logic;
- automation templates/accounts and private integrations;
- recovery decisions and reconciliation policy.

**Engine owns runtime libobs state**, including:

- libobs source/scene/item/output/etc. objects;
- runtime plugin instances;
- ephemeral object handles;
- current runtime media/output/audio state;
- callbacks/signals and runtime event normalization.

The Controller reconstructs runtime state after engine restart from its durable model. Engine handles are never durable IDs.

### 2.4 Handle model

Raw C pointers never cross the boundary. Current v2 runtime implementations use **canonical decimal handle strings**, e.g. `"1"`.

Current validation convention:

- non-empty decimal;
- no leading zero except impossible zero itself;
- zero rejected;
- must parse completely;
- bounded to signed 64-bit positive range in current helpers;
- process-local and ephemeral;
- invalid after engine restart.

Note: an older generic paragraph in `engine/PROTOCOL_V2.md` discusses JSON integers where safe or decimal strings. Current implemented `source.*` and `interaction.*` behavior is stricter: canonical decimal strings. Before extending more object namespaces, reconcile/document the convention rather than introducing inconsistent handle encodings.

### 2.5 GPL/process boundary

This fork carries the GNU GPL v2 license in `COPYING`. The headless engine is GPL and links libobs/plugins. Keep GPL-linked implementation on the engine side. The private Controller communicates out-of-process over the protocol.

Do not treat this file as legal advice. For distribution work, Task 47 explicitly requires a licensing/distribution audit. The engineering invariant is: **do not move libobs-linked GPL implementation into the proprietary Controller merely for convenience.**

---

## 3. Protocol-v2 invariants

Read `engine/PROTOCOL_V2.md` in full. The following points have repeatedly mattered in implementation and tests.

### 3.1 Envelope

V2 requests are semantic RPC-like envelopes with string request IDs, `method`, object `params`, and optional `ifRevision` only for mutating engine-state methods. Responses have `status`, `revision`, and `data`/error details. Events carry a monotonic event sequence and a revision.

### 3.2 Global revision

There is one engine-state revision.

- Read-only/transient operations do not increment it.
- A successful externally visible canonical-state mutation consumes exactly one revision.
- A command-owned set of state events uses the **same revision** as its response.
- The command response is written before command-owned events.
- An unrelated asynchronous canonical state change receives its own subsequent revision.
- Stale `ifRevision` produces `revision_conflict` and no mutation.
- `ifRevision` on a non-mutating method is `bad_request`.

Do not make revisions track high-frequency telemetry such as playback time, meters, pointer movement, or frame ticks.

### 3.3 Event ordering and backpressure

- Protocol output must have one ordered writer path.
- libobs callbacks never write directly to stdout.
- State events are normalized/queued.
- Event sequence is independent from state revision.
- Subscriptions filter delivery.
- Queue/deferred overflow cannot silently drop canonical changes: it must cause `session.resyncRequired` so the Controller can rebuild from snapshots.

### 3.4 Error vocabulary

Use the stable protocol error model and existing helpers. Existing namespaces use codes such as:

- `bad_request`
- `not_found`
- `already_exists` where applicable
- `revision_conflict`
- `unsupported_capability`
- `busy`
- `obs_error`
- `internal_error`

Do not leak raw pointers, C++ exception internals, Win32 object pointers, or plugin-private implementation details through errors.

---

## 4. Current implementation status

Tasks 1–11 are accepted. Task 11 was implemented on the isolated
`task11-codex` branch and is accepted at
`e7b34828cb9fbd55bae01f97148f1ec93a4ae015`. See `TASK11_ACCEPTANCE.md` for
the exact human approval, CI, artifact, physical, and review evidence.

### Task 1 / 1.1 — headless host and package cleanup

Completed. The engine is a deliberately small Windows host, with D3D11 initialization, module allowlisting, safe DLL loading, redirected stdio protocol, no network listener, and a runtime package that excludes the normal OBS UI executable.

Important host behavior is described in `engine/README.md`, but its command examples are mostly legacy v1. Use it for host/security context only.

### Task 2 — Protocol v2 framing

Completed. String request IDs, v2 request/response/error parsing, max message bounds, protocol-only stdout behavior, etc.

### Task 3 — capability discovery

Completed. `session.hello`/capability advertisement and capability-level method evolution.

### Task 4 — revisions / optimistic guards

Completed. Global revision and `ifRevision` semantics.

### Task 5 — event queue/subscriptions

Completed. Subscription filtering, event queue, sequence/order, overflow/resync behavior.

### Task 6 — initial runtime objects v2

Completed. Initial source/scene/item semantic runtime bridge sufficient to support later namespaces.

### Task 7 — generic `properties.*`

Completed. This is strategically important: arbitrary OBS plugins expose settings through `obs_properties_t`, so the proprietary UI can generate property controls without every plugin needing a custom Controller integration.

### Task 8 — complete `source.*`

Completed after a substantial deferred-video-update concurrency bug was fixed and then covered by deterministic A–F testing. See section 6 below.

### Task 9 — `interaction.*`

Completed and physically accepted on Windows. See section 7 below.

### Task 10 — `media.*`

Accepted in `6a590c2985a99d186c8eecd0241acdc824d32168`, with exact queued-action
tickets and permanent-observer settlement. The namespace has all 11 methods,
stable state strings, deterministic timeout/orphan/overflow resync, and a
CI-only fixture. The concrete wire contract is `engine/MEDIA_V1.md`.

---

## 4.1 Phase-2 candidate — Tasks 12–20

The explicit Phase-2 goal is implemented on `phase2-scene-render-graph` in
the dependency order `12 -> 13 -> 14 -> 15 -> 16 -> 17 -> 19 -> 18 -> 20`.
The candidate includes complete Scene/Item/Canvas/Program/Preview semantics,
dynamic Transition and Studio orchestration, and tagged PreviewOutput routing
for Program, Preview, Scene, Source, and Canvas. The D3D11 transport uses the
engine-owned legacy DXGI shared resource plus keyed-mutex synchronization and
advertises its capability only when the live backend supports it.

The Phase-2 architecture and namespace contracts are recorded in
`PHASE2_ARCHITECTURE.md` and the `engine/*_V1.md` documents. Deterministic
Task 12–20 scripts, the D3D11 consumer fixture, the Canvas failure-atomicity
lane, and the complete complexity-baseline freeze are present. Local Tasks
1–11 and 12–20 lanes, package audit, and final physical evidence are recorded
in `PHASE2_ACCEPTANCE.md` and `PHASE2_PHYSICAL_EVIDENCE.md`; hosted run
`33513275145` passed all listed jobs at exact head
`13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`. Human approval accepted Phase 2;
the final acceptance commit changes documentation only, and Task 21+ remains
out of scope and unauthorized.

## 5. Important source files today

Start with `SOURCE_REVIEW_GUIDE.md`, but the core current engine map is:

- `engine/host.cpp` — process entry/bootstrap and host lifecycle.
- `engine/config.cpp`, `engine/config.hpp` — command-line/runtime configuration.
- `engine/protocol.cpp`/headers — shared/legacy protocol plumbing.
- `engine/protocol_v2.cpp`, `engine/protocol_v2.hpp` — v2 method classification, capabilities, dispatch, common guard/revision/event behavior.
- `engine/revision.cpp`, `engine/revision.hpp` — revision state/mutation guards.
- `engine/events.cpp`, `engine/events.hpp` — event queue, subscriptions, sequence, overflow/resync.
- `engine/validation.cpp`, `engine/validation.hpp` — common protocol validation helpers.
- `engine/runtime.hpp` — `Engine` runtime object maps and current v2 method declarations.
- `engine/runtime.cpp` — core runtime/legacy functionality and object lifetime.
- `engine/runtime_v2.cpp` — initial v2 runtime object operations.
- `engine/runtime_properties_v2.cpp` — generic properties implementation.
- `engine/runtime_source_v2.cpp` — complete source namespace and source callback/event bridge.
- `engine/runtime_source_settle_v2.cpp` — deferred video-source update settlement.
- `engine/source_event_capture.hpp` — header-only capture routing/thread isolation used by source event ownership.
- `engine/runtime_interaction_v2.cpp` — Task-9 transient interaction bridge.
- `engine/runtime_media_v2.cpp` — Task-10 media state observer, action settlement, and event bridge.
- `engine/runtime_filter_v2.cpp` — Task-11 filter ownership, observer, settlement, and event bridge.
- `engine/protocol_filter_v2.cpp` — Task-11 capability/filter router around the unchanged v2 core.
- `engine/task11_filter_source.cpp` — CI-only deterministic Task-11 parent/filter fixture.
- `engine/task8_concurrency_source.cpp` — CI-only deterministic Task-8 plugin.
- `engine/task9_interaction_source.cpp` — CI-only deterministic Task-9 interaction source.
- `engine/task10_media_source.cpp` — CI-only deterministic Task-10 media source.
- `engine/CMakeLists.txt` — headless executable plus test-only target definitions.
- `engine/PROTOCOL_V2.md` — canonical protocol method/event namespace contract.
- `engine/SOURCE_V1.md`, `INTERACTION_V1.md`, `MEDIA_V1.md`, `FILTER_V1.md`, `PROPERTIES_V1.md`, `RUNTIME_OBJECTS_V1.md`, `EVENTS_V1.md` — concrete namespace/task contracts.

CI:

- `.github/actions/build-obs/` — project Windows build action used by task workflows.
- `.github/scripts/engine-protocol-v2-*.ps1` — deterministic Windows integration drivers.
- `.github/workflows/engine-protocol-v2-task1.yaml` through `task11.yaml` plus `task8-concurrency.yaml` — regression lanes.

---

## 6. Task 8 deep handoff: source event settlement

This area was the hardest bug so far. Future agents must understand it before touching source callbacks, source settings, revisions, or related event ownership.

### 6.1 Root problem

For a video source, `obs_source_update` can defer the plugin’s actual update callback to the video thread. The protocol request may therefore return from the immediate libobs call before the source emits its `update` signal / dimensions change.

Without special ownership handling, a single Controller request can become:

- request response revision N;
- delayed source callback later interpreted as unrelated async mutation revision N+1;

which breaks the one-command/one-revision contract and can desynchronize optimistic Controller state.

### 6.2 Current solution

`engine/runtime_source_v2.cpp` owns the callback normalization/deferred queues; `engine/runtime_source_settle_v2.cpp` settles command-owned delayed source updates.

Key behavior:

1. The protocol opens a source-event capture window around runtime mutation execution.
2. Callbacks on the command thread can be captured directly.
3. callbacks on other threads while a command capture is active are deferred in bounded batches.
4. After a source settings mutation, settlement identifies which deferred batch actually belongs to the command.
5. A deferred batch is claimed only when:
   - it is for the same source handle;
   - it contains `source.settingsChanged`;
   - the canonical settings JSON in that event equals the post-mutation source settings snapshot.
6. Matching events are promoted into the command result and deduplicated by semantic event name + source handle.
7. Unrelated deferred batches stay queued and later receive independent revisions.
8. For video sources, settlement waits for the real source `"update"` signal using a temporary condition-variable waiter, bounded by 5 seconds.
9. It scans before connecting, connects the waiter, scans again to close the registration race, waits/scans on signals, disconnects, then performs a final scan to close the timeout-boundary race.
10. If ownership cannot be proven before timeout, it sets overflow/lost-settlement state; the existing flush path requires Controller resync rather than silently making up ownership.

### 6.3 Why a graphics task barrier was rejected

Do not replace this with a graphics no-op task and assume “the video update must have run before my task.” OBS graphics/video task ordering can allow the no-op graphics task to run before the next `tick_sources` update. It is not a valid ownership barrier.

### 6.4 Deterministic A–F suite

Task 8 has a CI-only test module and integration harness that cover:

- **A:** patch/update A + A’s delayed callback => response revision N and A settings/dimensions events revision N.
- **B:** command on A while unrelated B updates => A gets N; B gets N+1.
- **C:** B and C events must not cross-dedupe merely because event names match.
- **D:** removing A must not suppress unrelated B callbacks.
- **E:** after unrelated event advances revision, a stale guarded request must return `revision_conflict` with no mutation.
- **F:** deferred bridge overflow must produce `session.resyncRequired`; no silent canonical-state loss.

The first A–F workflow initially appeared red even though all cases passed. The wrapper incorrectly treated an empty PowerShell `$LASTEXITCODE` after a successful script as failure. That false negative was fixed at accepted Task-8 SHA `e88ceb0a1e1103c3297cd1bd589e56e28ae638e4`.

### 6.5 Known structural debt

`SourceV2State` and `DeferredSourceEventBatch` are currently duplicated, token-for-token/ODR-equivalently, in both:

- `engine/runtime_source_v2.cpp`
- `engine/runtime_source_settle_v2.cpp`

`runtime_source_settle_v2.cpp` explicitly comments that the definitions must remain equivalent until moved into a dedicated private header. This is legal as currently structured but brittle. A future cleanup should create a private shared header. **Do not casually refactor it in the middle of an unrelated namespace task**, because Task 8’s concurrency invariants are sensitive and already accepted.

---

## 7. Task 9 deep handoff: interaction

Task 9 final implementation commit is `f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc`, one clean commit on the accepted Task-8 parent.

Methods:

- `interaction.focus`
- `interaction.mouseMove`
- `interaction.mouseButton`
- `interaction.mouseWheel`
- `interaction.key`
- `interaction.text`
- `interaction.reset`

Concrete JSON schema is in `engine/INTERACTION_V1.md`.

### 7.1 Design

- Requires canonical decimal `params.source`.
- Source must advertise `OBS_SOURCE_INTERACTION`.
- Uses libobs interaction APIs only (`obs_source_send_*` via `obs-interaction.h`).
- No arbitrary Win32 message injection.
- No HWND/WPARAM/LPARAM/native object crossing.
- Interaction is transient input, so successful delivery does not increment revision.
- `ifRevision` is rejected for `interaction.*` by the common non-mutating-method guard.
- Source-local pointer coordinates are validated against dimensions except permitted out-of-bounds release/leave semantics.
- Modifiers are a semantic object, not a raw bitmask.
- Key native integer fields are bounded metadata passed to libobs callbacks only.
- `interaction.text` validates UTF-8 and synthesizes one libobs key down/up pair per Unicode scalar, because libobs has no separate generic text callback.
- U+0000 is rejected because `obs_key_event.text` is NUL-terminated with no separate length.
- At most 256 distinct held keys are tracked per source; a new distinct key beyond that returns `busy`.
- Reset releases tracked held mouse buttons and keys, then sends mouse leave and focus out.
- Per-source interaction tracking is pruned opportunistically when source handles disappear.

### 7.2 CI-only deterministic source

`engine/task9_interaction_source.cpp` registers `task9_interaction_source` with `OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION` and logs every callback. It is a test fixture only:

- `EXCLUDE_FROM_ALL`;
- no install rule;
- normal package explicitly checked to ensure it is absent;
- workflow builds/stages it only for integration, then removes it before the production artifact upload.

### 7.3 Final CI

On `f59d6b6c...`, the same-SHA project matrix was 11/11 check runs green. Task-9’s dedicated Windows lane successfully:

1. built the Windows minimal runtime;
2. asserted normal install excluded the Task-9 fixture DLL;
3. built the CI-only fixture;
4. staged it;
5. ran the full Task-9 callback integration test;
6. removed the fixture;
7. uploaded the production runtime.

Production Task-9 artifact:

- name: `obs-engine-windows-x64-task9`
- historical artifact ID: `9690957023`
- SHA-256: `04e7651e8d006117cf4b6ae1e578ba47d657a2c1ee9cc1aeec2358ca3314c09f`

The production artifact intentionally had no currently packaged source kind with `interaction:true`; the deterministic fixture had been stripped as designed.

### 7.4 Physical Windows acceptance

A temporary test-only branch `task9-physical-acceptance` was created solely to package the same accepted engine implementation together with the CI interaction fixture. Its extra commit was `5916aa0fadf99759fedd8d5599a5369232896cbf`, parent `f59d6b6c...`; it did not alter production engine behavior. Workflow run `33185731362` was green, including the integration test before upload. Physical-test artifact SHA-256 was `ab897e0823796c0aa89ebab1091cd258fc24abde4b8379955e1efafd5384be9e`.

The operator ran on physical Windows:

`obs-engine.exe --plugin=task9-interaction-source`

Observed acceptance:

- engine started on AMD Ryzen 7 9700X / Windows 25H2 build 26200.9278;
- D3D11 initialized;
- test module loaded;
- hello advertised all `interaction.*` capabilities;
- fresh test source was `"1"`, revision 1;
- focus callback exact, revision remained 1;
- mouse move exact, revision 1;
- mouse button left-down exact, revision 1;
- mouse wheel exact, revision 1;
- key `a` down exact metadata, revision 1;
- text `Hi` generated H down/up, i down/up, revision 1;
- reset generated left-button up, key `a` up, mouse leave, focus out; response `releasedKeys:1`, `releasedButtons:1`, revision 1;
- source remove => revision 2;
- session close => revision 3;
- clean libobs shutdown.

Task 9 is therefore physically accepted.

---

## 8. Task 10 deep handoff: media settlement

Task 10 is accepted at `6a590c2985a99d186c8eecd0241acdc824d32168`. The
concrete wire contract is `engine/MEDIA_V1.md`; the implementation is
`engine/runtime_media_v2.cpp`.

### 8.1 Upstream behavior verified

The current libobs `obs_source_media_play_pause`, restart, stop, next, previous,
and set-time APIs enqueue `MEDIA_ACTION_*` records under
`source->media_actions_mutex`. `process_media_actions()` drains them from
`obs_source_video_tick()` and then emits the corresponding core signal. The
public APIs return before that processing. `media_started` and `media_ended` are
source-originated signals; this snapshot has no generic media-error signal.

Built-in behavior is not uniform: `obs-ffmpeg` updates its state in the media
callback while its decoder/seek work can continue asynchronously; VLC delegates
to libVLC and reports opening/ended transitions from worker callbacks; slideshow
sources update a local state machine and emit lifecycle signals. The Controller
must therefore use the normalized state/event contract rather than infer timing
from a libobs function return.

### 8.2 Current settlement contract

- Every media-capable source has a weak-reference observer for core action and
  lifecycle signals.
- Mutating requests establish a media capture gate, retire any pre-capture direct
  callbacks before taking the revision guard, obtain an exact source-local action
  ticket from the private libobs enqueue bridge, and wait up to five seconds on
  the permanent observer's condition variable. No per-request signal waiter or
  sleep-based synchronization is used.
- Deferred media batches are bounded to the existing 1024-event scale and retain
  the source-local action ticket. Settlement matches source handle, expected
  signal, and exact ticket; other same-source or other-source batches remain
  independent and receive later revisions or resync as required.
- `setPosition` uses the branch's internal `media_time` signal emitted by
  libobs after the queued plugin callback returns. It proves action callback
  processing, not completion of plugin-internal decoder work; the response
  position is a fresh signed millisecond snapshot.
- On timeout or uncertain ownership, the request does not claim a successful
  command mutation. The timed-out ticket is retained so a late completion cannot
  be claimed by a later request; the bridge forces `session.resyncRequired` after
  the late completion, even when it has no normal state event. Natural
  state/lifecycle events outside a request get their own revision. Position ticks
  never create revisions.
- `media.error` is emitted only when an observable media signal finds a
  transition to `OBS_MEDIA_STATE_ERROR`; `media.getState` remains authoritative
  when a plugin changes state without a generic signal.

### 8.3 Task-10 fixture and acceptance

`engine/task10_media_source.cpp` is `EXCLUDE_FROM_ALL`, has no install rule, and
registers `task10_media_source` plus a no-seek variant. The corrective integration
coverage adds pre-existing same-signal actions, timed-out/orphan follow-up
actions, late set-position completion, blocking callback teardown, removal with
an outstanding callback, and shutdown with an outstanding callback. The normal
package audit confirms that the fixture and frontend/WebSocket binaries are
absent. The operator handoff records exact-SHA hosted CI, physical Windows
acceptance, and independent raw-evidence review as complete; Task 10 is
accepted.

The handoff audit also removed `obs-websocket` and `obs-browser` from the default
safe-module list in `08010cdc6` and `2744b3bcb`. They remain explicit opt-in or
build-disabled paths, not normal Controller runtime modules.

---

## 9. Physical Windows hardware context already proven

Do not treat this as a requirement that every task use the exact same adapter selection, but it is useful acceptance history.

Physical machine evidence across Tasks 8/9:

- CPU: AMD Ryzen 7 9700X, 8C/16T.
- RAM: 32 GB class (libobs reported ~31.8 GB physical).
- Windows: 25H2 build `26200`, revision `9278`.
- GPUs: AMD integrated graphics + AMD Radeon RX 9060 XT (~16 GB dedicated VRAM).
- Task-8 earlier run showed real RX 9060 XT D3D11 acceptance; Task-9 acceptance package selected the integrated AMD adapter by default and still behaved correctly.
- Optional warnings for missing AJA/DeckLink/NVIDIA SDK/VLC/NVENC are expected on this machine and are not engine failures.

When GPU-specific tasks arrive (shared texture, preview output, device-loss, encoder paths), test the intended adapter explicitly and do not assume prior source/interaction acceptance proves those paths.

---

## 10. Local-agent operating procedure

### 9.1 Initial checkout audit

Before editing:

```bash
git status --short --branch
git branch --show-current
git rev-parse HEAD
git log --oneline --decorate -20
```

Expected branch is `engine-protocol-v2`. If HEAD has moved past the handoff commit, inspect every intervening commit before assuming this document is current.

Then:

```bash
git diff f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc..HEAD -- engine .github docs/libobs-engine AGENTS.md
```

If only documentation handoff changes are present, `f59d6b6c...` remains the engine baseline. If production engine code moved, re-evaluate status from source and CI.

### 9.2 Read before code

For a new namespace:

1. Read its exact method/event list in `engine/PROTOCOL_V2.md`.
2. Search for any namespace-specific doc.
3. Search `engine/protocol_v2.cpp` to understand current classification/capability/mutation dispatch.
4. Read `engine/runtime.hpp` and adjacent runtime implementation.
5. Search libobs public headers for the intended API.
6. Search libobs implementation to understand callback/threading/lifetime/state semantics.
7. Inspect relevant OBS plugins; public libobs declarations alone are often insufficient to understand deferred behavior.
8. Decide canonical-state vs transient command vs telemetry semantics before coding.
9. Decide revision/event ownership and asynchronous callback settlement before coding.
10. Decide deterministic test fixture strategy before coding.

### 9.3 Do not blindly mirror OBS frontend code

The project needs frontend-equivalent semantics, but the engine is not `obs64.exe` and should not grow Qt/frontend dependencies merely to copy OBS UI behavior. Prefer public libobs APIs and semantic protocol models.

### 9.4 Make tests deterministic

For plugin-specific behavior, build a tiny CI-only OBS source/filter/output test module that exposes exactly the signal/callback transitions needed. This was successful for Tasks 8 and 9.

Normal artifact must not contain test fixtures. Assert this in CI before explicit staging, and preferably again after cleanup.

### 9.5 Review twice

**Pass 1 — implementation correctness:**

- lifetime/reference ownership;
- callback thread behavior;
- deadlocks/lock ordering;
- asynchronous settlement;
- error paths;
- state bounds;
- plugin absence/failure;
- overflow/backpressure;
- resource cleanup/shutdown.

**Pass 2 — protocol/product boundary:**

- method names exactly match spec;
- handles canonical/opaque;
- no raw pointers/platform objects;
- capability advertisement correct;
- canonical vs telemetry classification correct;
- exactly-one-revision mutations;
- response-before-command-event;
- unrelated async event gets independent revision;
- `ifRevision` policy correct;
- test fixture excluded from production package;
- no second control API introduced;
- no Task N+1 scope creep.

---

## 11. What to do next

Task 10 and Task 11 are accepted. Task 11's acceptance record documents that
the candidate:

- verified branch/HEAD;
- read all handoff docs;
- inspected current source;
- reconciled any mismatch;
- reviewed the `filter.*` section of `engine/PROTOCOL_V2.md`;
- inspected current libobs filter ownership/reference/reorder APIs;
- audited libobs deferred-update counter/coalescing behavior and the candidate's
  private tracked-update serial bridge;
- inspected representative filter plugins and the generic properties bridge;
- read and reconciled `engine/FILTER_V1.md`, the Task-11 plan/audit, the
  deterministic script, and the exact-SHA workflow.

The advisor WIP is preserved locally and remotely on
`wip/task11-advisor-handoff-137b2e5` at
`137b2e5bd341caa1c3bc128bccd7b81376f27c32`; the older unauthorized
implementation remains on `wip/task11-unauthorized`. Neither WIP branch is
accepted or merged. Preserve the media bridge's separate bounded queue,
exact-ticket settlement rules, the internal `media_time` signal contract, and
the no-WebSocket/no-browser default allowlist. The human-approved Phase-2
candidate is accepted through Task 20. Do not begin Task 21 or later on this
branch.

---

## 12. Things not to “fix” opportunistically

Unless they block the active task, do not bundle unrelated cleanup into a namespace implementation. In particular:

- do not rewrite the Task-8 source event system because it looks complicated;
- do not combine the duplicated `SourceV2State` cleanup with `media.*` unless there is a direct, reviewed need;
- do not replace the custom Engine Protocol with WebSocket/REST/GraphQL;
- do not add persistent IDs to the engine just because ephemeral handles are inconvenient;
- do not put Controller persistence/undo/UI/business logic into the engine;
- do not start browser-source bundling unless a roadmap task or concrete product requirement calls for it;
- do not ship CI-only test modules;
- do not claim physical acceptance from GitHub-hosted Windows CI.
- do not claim that a media seek has completed merely because a plugin callback
  returned; the current `media_time` signal records callback completion and the
  returned position is still a libobs snapshot.

---

## 13. Handoff completion definition

This handoff is successful if a local agent can, using only the repository:

1. explain the process/license/state boundary;
2. explain revision/event semantics;
3. identify Tasks 1–11 and Phase-2 Tasks 12–20 as accepted;
4. explain the Task-8 deferred update bug and why the current settlement exists;
5. explain Task-9 transient interaction semantics and physical acceptance;
6. explain Task-10 media action settlement, media-time limitations, and resync behavior;
7. locate the relevant source/CI files;
8. produce a source-verified design for Task 11 before editing;
9. implement/test/review one roadmap task without accidentally changing the architectural contract.

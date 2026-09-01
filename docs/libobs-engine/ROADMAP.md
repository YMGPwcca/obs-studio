# LibOBS Engine Protocol v2 — Detailed Roadmap (Tasks 1–50)

This is the current repo-native roadmap. It preserves the original staged plan while updating completion state through accepted Phase 2. Future-task details are **plans**, not claims that APIs already exist. Before each task, re-read `engine/PROTOCOL_V2.md` and inspect source/libobs; source reality may require refining the plan.

## Roadmap operating rules

Every task follows the same gate:

1. Verify branch/HEAD and previous accepted SHA.
2. Read exact protocol namespace/method/event definitions.
3. Inspect current engine source and relevant libobs headers + implementation + representative plugins.
4. Resolve canonical-state vs transient-command vs telemetry behavior.
5. Resolve lifetime, callback-thread, revision/event ownership, failure, timeout and overflow behavior before coding.
6. Implement only the active task.
7. Add deterministic unit/integration coverage; use CI-only libobs fixtures where necessary.
8. Run all earlier project regression lanes plus the new lane on the same final SHA.
9. Audit artifact contents.
10. Review complete diff twice.
11. Run physical Windows acceptance when relevant.
12. Update status/handoff and stop before the next task.

---

# Phase A — Engine/protocol foundations

## Task 1 — Headless host refactor — COMPLETE

**Goal:** produce a small Windows `obs-engine.exe` that initializes libobs without the normal OBS Qt frontend and can be launched as a child process by a separate Controller.

**Accepted characteristics:**

- libobs startup/shutdown owned by the engine;
- D3D11/video initialization;
- redirected stdin/stdout control;
- runtime source/scene/item ownership;
- no UI dependency as the control surface.

**Regression expectation:** engine starts, emits ready, accepts baseline commands, and shuts down cleanly.

## Task 1.1 — Headless package/security cleanup — COMPLETE

**Goal:** make the install/runtime artifact genuinely headless and tighten module/DLL loading.

**Accepted characteristics:**

- no normal OBS frontend binary required in engine package;
- module allowlist/safe module loading;
- `--plugin=NAME` explicit extension/test hook;
- capture-only `win-capture` behavior by default;
- restricted Windows dependency/DLL search behavior;
- working directory pinned appropriately;
- no TCP/HTTP/WebSocket listener.

## Task 2 — Protocol v2 framing — COMPLETE

**Goal:** establish stable semantic request/response/error envelopes over bounded NDJSON.

**Acceptance themes:** string request IDs, bounded identifiers/messages, structured errors, protocol-only stdout, parser robustness.

## Task 3 — Capability discovery — COMPLETE

**Goal:** let Controller discover exact engine semantic capabilities rather than infer them from version strings.

**Acceptance themes:** hello/capability list, additive capability evolution, only advertise implemented behavior.

## Task 4 — Revisions and optimistic guards — COMPLETE

**Goal:** support deterministic Controller synchronization and conflict detection.

**Acceptance themes:** global monotonic canonical-state revision; exactly one revision per successful command-owned mutation; `ifRevision`; conflict details; no mutation on conflict.

## Task 5 — Event queue, subscriptions, sequencing and resync — COMPLETE

**Goal:** safely normalize asynchronous libobs callbacks into an ordered Controller event stream.

**Acceptance themes:** subscriptions, event sequence, response-before-command-event, bounded queues, `session.resyncRequired` on incremental-state loss.

## Task 6 — Initial runtime objects v2 — COMPLETE

**Goal:** provide enough v2 source/scene/item runtime object plumbing to build later semantic namespaces.

**Acceptance themes:** opaque handles, object ownership, initial lifecycle, v2 dispatch, shutdown cleanup.

## Task 7 — Generic `properties.*` — COMPLETE

**Goal:** expose `obs_properties_t` semantically so arbitrary plugins can be configured by the private UI.

**Acceptance themes:** property schema inspection, target resolution, list items, refresh, validation, button invocation, plugin variability, stable error model.

---

# Phase B — Complete source behavior

## Task 8 — Complete `source.*` — COMPLETE

**Goal:** provide complete generic source kind discovery, source lifecycle, settings, state snapshots, flags/dimensions/activity/showing/missing-file integration, refresh and state save/load.

**Critical accepted work:** source callback bridge, per-source dedupe, command-owned asynchronous video-update settlement, unrelated callback independence, overflow/resync, deterministic A–F concurrency suite, physical Windows regression.

**Do not regress:** see `HANDOFF.md` Task-8 deep section.

## Task 9 — `interaction.*` — COMPLETE

**Goal:** deliver focus/mouse/wheel/key/text/reset to `OBS_SOURCE_INTERACTION` sources using libobs APIs only.

**Accepted semantics:** transient/non-revisioned delivery, source-local validation, semantic modifiers, bounded held-key tracking, reset cleanup, no OS-native message injection, deterministic callback fixture, physical Windows acceptance.

## Task 10 — `media.*` — COMPLETE / ACCEPTED

**Goal:** expose controllable media playback state and transport controls for sources that support libobs media control.

**Protocol names currently listed in `PROTOCOL_V2.md`:**

- `media.getState`
- `media.play`
- `media.pause`
- `media.togglePause`
- `media.stop`
- `media.restart`
- `media.next`
- `media.previous`
- `media.getDuration`
- `media.getPosition`
- `media.setPosition`

**Implemented events:**

- `media.started`
- `media.playing`
- `media.paused`
- `media.stopped`
- `media.ended`
- `media.error`
- `media.stateChanged`

**Key design question:** transport-state changes are canonical runtime state, but position progression is high-frequency telemetry. Do not increment revision for every playback tick.

**Accepted implementation:** `6a590c2985a99d186c8eecd0241acdc824d32168` adds `MEDIA_V1.md`, a source-correlated bounded media observer/settler, exact source-local queued-action tickets, permanent-observer settlement, orphan completion resynchronization, and the CI-only `task10_media_source` fixture. The fork's internal `media_time` signal marks queued set-time callback processing because upstream has no generic seek signal. Missing callback/ownership and deferred overflow paths return/emit resynchronization rather than fabricate success. Full research record: `TASK10_MEDIA_PLAN.md`.

Local validation passed with the Task 1–9 regression lanes, full Windows x64 build/install, package audit, deterministic M1–M15-equivalent coverage, and Windows fixture execution. The operator handoff records exact-SHA hosted CI, physical Windows acceptance, and independent raw-evidence audit as complete.

## Task 11 — `filter.*` — COMPLETE / ACCEPTED

**Goal:** manage filters attached to source-like objects using generic source/filter semantics rather than plugin-specific UI logic.

**Accepted scope:**

- enumerate filters on a parent;
- discover filter kinds;
- create/remove/rename/reorder filters;
- get/patch/replace filter settings via source/property machinery;
- enable/disable state;
- filter lifecycle/state events;
- parent/filter handle relationships and removal cleanup.

**Research requirements:** inspect libobs filter ownership/reference rules and filter reordering APIs; determine whether filters can emit source callbacks already covered by source bridge; avoid duplicate event streams.

**Acceptance:** deterministic parent + filter fixture, ordering/lifetime tests, plugin-property reuse, stale parent/filter handle cases.

The accepted implementation is `e7b34828cb9fbd55bae01f97148f1ec93a4ae015` on
`task11-codex`, based on accepted Task-10 checkpoint
`e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`. Its exact-SHA Task-11 lane, full
Task 1–11 regression matrix, package audit, physical Windows gate, independent
review, and explicit human approval are recorded in `TASK11_ACCEPTANCE.md`.

Phase-1 cyclomatic-complexity hardening is COMPLETE / ACCEPTED at reviewed
checkpoint `1b2ddacbb36c39bb61fd645594f0746f106956bf`. The final frozen
complexity baseline and explicit human approval are recorded in
`PHASE1_COMPLEXITY_ACCEPTANCE.md`. This acceptance does not authorize Task 12.

The separate pre-Phase-2 PowerShell script-body hardening cleanup is COMPLETE /
ACCEPTED in the current lineage. Its historical branch/evidence is
`phase1-scriptbody-hardening` / `PHASE1_SCRIPTBODY_HARDENING.md`; it is
tooling-only and does not change the accepted runtime or named-function result.

The final pre-Phase-2 workflow executable-code hardening cleanup is COMPLETE /
ACCEPTED in the current lineage. Its historical branch/evidence is
`phase1-workflow-exec-hardening` / `PHASE1_WORKFLOW_EXEC_HARDENING.md`; it
extracts substantial workflow `run:` logic into measured scripts.

---

# Phase C — Full composition and canvas/frontend-equivalent scene behavior

## Task 12 — Complete `scene.*` — COMPLETE / ACCEPTED

**Goal:** expand scene lifecycle beyond initial Task-6 primitives.

**Planned scope:** scene list/get/create/remove/rename/duplicate/state operations as specified by the canonical protocol; scene signals; full private-scene ownership.

**Acceptance themes:** duplicate names/collisions, removal with items, event/revision ownership, scene-as-source relationships if exposed.

## Task 13 — Complete `item.*` — COMPLETE / ACCEPTED

**Goal:** expose full scene-item composition behavior.

**Planned scope:** create/remove/reorder, visibility, lock, transform, bounds, crop, blend behavior where supported, source relation, group/parent relations if protocol includes them.

**Critical:** scene-item reference ownership is subtle; inspect libobs ref semantics before every lifecycle change. Transform changes should be one canonical revision per command and normalized events should not double-count libobs callbacks.

## Task 14 — `canvas.*` — COMPLETE / ACCEPTED

**Goal:** support canvas/video-space configuration needed for a custom frontend and future multi-canvas semantics.

**Research:** current branch already creates a `Main` canvas; inspect libobs canvas APIs available in this upstream snapshot. Define creation/configuration/lifetime carefully and avoid assuming OBS frontend single-canvas global behavior if libobs supports more.

**Acceptance:** dimensions/format/fps state, object association, rebuild/device implications, deterministic invalid configuration errors.

## Task 15 — `program.*` — COMPLETE / ACCEPTED

**Goal:** explicitly control/query current program composition output state in semantic v2 form.

**Scope:** program scene/source selection and events; relation to scene lifecycle; behavior when selected program scene is removed.

## Task 16 — `preview.*` — COMPLETE / ACCEPTED

**Goal:** represent preview-side composition separately from program where the product needs studio/edit workflows.

**Important:** UI selection is Controller state; only model engine preview state that actually affects rendering/runtime semantics.

## Task 17 — D3D11 shared-texture preview transport — COMPLETE / ACCEPTED

**Goal:** give the private Windows UI low-latency rendered frames without embedding OBS frontend windows.

**Major design work:**

- shared D3D11 resource creation;
- secure handle duplication/lifetime;
- adapter/LUID matching;
- pixel format/color space;
- resize/recreate generation numbers;
- keyed mutex/fence/synchronization strategy;
- device-lost behavior;
- Controller crash cleanup;
- no raw COM pointers over protocol.

**Physical acceptance mandatory:** multi-GPU machine, including RX 9060 XT path and adapter mismatch/recreation scenarios.

## Task 18 — `studio.*` — COMPLETE / ACCEPTED

**Goal:** studio-mode semantic orchestration between preview/program and transition behavior.

**Keep boundaries:** UI controls remain Controller; engine exposes runtime preview/program/transition state and commands.

## Task 19 — `transition.*` — COMPLETE / ACCEPTED

**Goal:** discover/create/configure/select/control transitions, including duration/state and transition lifecycle callbacks.

**Research:** transition sources are sources with special APIs; ensure source events/properties interoperate without duplicate ownership.

## Task 20 — `previewOutput.*` — COMPLETE / ACCEPTED

**Goal:** manage rendered preview-output instances/consumers independently from preview logical state, especially for shared-texture or additional display consumers.

**Acceptance:** create/destroy/reconfigure output safely, resize/device loss, resource leak tests.

---

# Phase D — Audio and input control

## Task 21 — `audio.*` — AUTHORIZED / IN PROGRESS on `phase3-output-stack`

**Goal:** expose source/global audio controls necessary for a full OBS-like frontend.

**Planned semantic areas:** mute, volume, balance, sync offset, monitoring, mixer routing, track state, channel/layout where appropriate, plus meter telemetry.

**Classification:** configuration is canonical state; meters/peaks are telemetry and must be bounded/coalesced/opt-in.

**Research:** audio callback thread constraints and monitoring device behavior; avoid doing protocol serialization in realtime callbacks.

## Task 22 — `hotkey.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** enumerate/register/bind/trigger appropriate libobs hotkeys through semantic protocol.

**Boundary:** private UI owns product/global-shortcut UX. Engine only exposes libobs hotkey semantics needed by plugins/runtime. Avoid leaking native hook objects.

---

# Phase E — Output primitives

## Task 23 — `encoder.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** discover/configure/create/destroy video/audio encoders and expose properties/capabilities.

**Research:** encoder ownership, media types, codec settings, GPU adapter requirements, active-output restrictions, reconfiguration rules.

**Acceptance:** software encoder deterministic path plus hardware capability/error paths; no assumption NVIDIA/Intel/AMD encoder exists.

## Task 24 — `encoderGroup.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** group encoder configurations/resources where libobs/upstream supports coordinated operation.

**Do not invent a group abstraction unless current upstream libobs actually supports the intended semantics; verify API first.**

## Task 25 — `service.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** discover/create/configure streaming services and service properties.

**Security:** credentials are sensitive Controller-owned data at rest. Define how/when secrets enter runtime, avoid logging them, and do not echo sensitive values unnecessarily.

## Task 26 — `output.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** generic output discovery/configuration/lifecycle foundation used by recording/streaming/replay/virtual camera.

**Critical:** asynchronous start/stop callbacks, errors, reconnect state, encoder/service attachment, signal ordering, active-state restrictions.

---

# Phase F — Product output workflows

## Task 27 — `recording.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** high-level recording control/state over generic output primitives.

**Scope:** start/stop/pause/resume where supported, file/path/settings semantics, lifecycle/error/finalization events.

**Physical acceptance:** real filesystem output and playable finalized file; interrupted/failed-path behavior.

## Task 28 — `streaming.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** high-level stream start/stop/reconnect/status behavior.

**Testing:** deterministic local RTMP/SRT/etc. endpoint where possible rather than public network dependency; service/output error mapping.

## Task 29 — `replayBuffer.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** start/stop/save replay buffer and expose lifecycle/state.

**Acceptance:** file finalization, repeated save behavior, disk errors, output conflicts.

## Task 30 — `virtualCamera.*` — AUTHORIZED / PENDING on `phase3-output-stack`

**Goal:** control OBS virtual camera output where installed/supported.

**Physical acceptance mandatory:** device registration/availability is machine-specific. Return `unsupported_capability` or appropriate stable error when unavailable.

---

# Phase G — Capture utilities and diagnostics

## Task 31 — `screenshot.*` — PLANNED

**Goal:** capture source/program/preview/canvas images without exposing graphics pointers.

**Design:** asynchronous capture, bounded image sizes, file vs in-protocol/binary transfer strategy, format/quality, timeout, GPU readback behavior.

## Task 32 — `caption.*` — PLANNED

**Goal:** expose caption/CEA text submission/control supported by libobs/source paths.

**Validation:** encoding/length/rate limits; source/output capability checks.

## Task 33 — `missingFile.*` — PLANNED

**Goal:** enumerate and resolve missing-file references semantically, building on source missing-file primitives.

**Controller UX:** Controller chooses replacement path/workflow; engine applies libobs missing-file resolution.

## Task 34 — `stats.*` — PLANNED

**Goal:** CPU/GPU/render/output/performance counters needed by product diagnostics.

**Classification:** primarily telemetry. Avoid revision churn and uncontrolled high-frequency event emission.

## Task 35 — `runtime.*` — PLANNED

**Goal:** runtime/libobs/platform/device/module/environment information useful for diagnostics/capability decisions.

**Security:** do not expose sensitive host paths/secrets unnecessarily. Keep response bounded and semantic.

## Task 36 — `extension.*` — PLANNED

**Goal:** extension/plugin module discovery and appropriate runtime management/introspection.

**Research:** distinguish OBS modules, source types, frontend plugins and scripting components; the headless engine should expose only meaningful/safe runtime extension semantics.

## Task 37 — optional `script.*` — PLANNED / OPTIONAL

**Goal:** only if product needs OBS script execution and legal/security model is acceptable.

**Security-critical:** scripts are code execution. Define sandbox/trust/install policy before implementation. This task may be omitted entirely.

---

# Phase H — Formalize reconstruction and developer ergonomics

## Task 38 — Reconstruction contract — PLANNED

**Goal:** formally define how Controller rebuilds engine runtime after restart/crash and how persistent Controller IDs map to ephemeral handles.

**Deliverable:** documented ordering/dependency graph, snapshot/reconcile behavior, failure recovery, partial reconstruction semantics.

## Task 39 — Formal protocol schema — PLANNED

**Goal:** machine-readable schema for requests/responses/events/capabilities/errors, generated/validated against docs/tests.

**Possible deliverables:** JSON Schema or custom IDL plus compatibility tooling. Choose based on actual protocol needs, not fashion.

## Task 40 — Controller SDK — PLANNED

**Goal:** typed client library for launching engine, framing requests, matching responses, maintaining revision/event model, subscriptions, reconnection/reconstruction helpers.

**Boundary:** SDK remains protocol client; it must not link libobs or duplicate engine internals.

---

# Phase I — Adversarial quality and recovery

## Task 41 — Protocol fuzzing — PLANNED

**Goal:** fuzz parser/validation/state-machine boundaries: malformed JSON, oversized messages, weird UTF-8, numeric extremes, object-type confusion, invalid method/params, event subscription patterns.

**Acceptance:** no crash/hang/unbounded allocation; stable errors where parseable.

## Task 42 — Concurrency stress — PLANNED

**Goal:** systematic randomized/multi-threaded stress of callback-heavy namespaces and command/event ordering.

**Build on Task 8:** source settlement is the baseline concurrency contract. Extend stress to media/audio/output/device events.

## Task 43 — Lifetime audit — PLANNED

**Goal:** systematic reference/weak-reference/callback disconnect/object-removal/shutdown audit across every runtime object type.

**Tools:** sanitizers where build permits, debug assertions, leak tools, deterministic remove-during-callback fixtures.

## Task 44 — Device-loss recovery — PLANNED

**Goal:** D3D11/device/adapter loss and reconstruction handling, especially shared preview resources and hardware encoders.

**Physical acceptance mandatory.**

## Task 45 — Crash recovery — PLANNED

**Goal:** Controller-visible process crash detection, clean relaunch/reconstruction behavior, incomplete-output handling, stale-handle invalidation, diagnostics.

**Test:** force engine termination at multiple lifecycle points.

## Task 46 — Security audit — PLANNED

**Goal:** input validation, process launch, DLL/module loading, file/path handling, secret handling, plugin trust, IPC exposure, resource exhaustion and log leakage audit.

**Do not replace with a superficial static scan; review the actual trust boundary.**

## Task 47 — Licensing/distribution audit — PLANNED

**Goal:** verify GPL and third-party component distribution obligations, source availability, notices/licenses, plugin/codecs/runtime redistributables, Controller/engine separation and installer/update behavior.

**This is legal/compliance-sensitive; engineering review is not a substitute for counsel where needed.**

## Task 48 — Packaging/update — PLANNED

**Goal:** production package manifest, versioning, atomic engine/runtime updates, plugin/data compatibility, rollback, hash/signature policy, debug symbols/source-offer/license bundle as required.

## Task 49 — Physical Windows acceptance matrix — PLANNED

**Goal:** run a defined hardware/Windows/plugin matrix rather than one developer PC only.

**Matrix dimensions should include:**

- Windows supported versions/builds;
- AMD/NVIDIA/Intel GPUs where features depend on them;
- single vs multi-GPU;
- HAGS on/off where relevant;
- capture types;
- audio devices;
- hardware encoders;
- virtual camera availability;
- device loss/recovery;
- long-running stream/record/replay scenarios.

Record exact engine SHA/artifact/hash/environment/result.

## Task 50 — Protocol v2 freeze — PLANNED

**Goal:** declare v2 stable only after feature coverage, schema/SDK, fuzz/concurrency/lifetime/recovery/security/licensing/package/physical matrix are complete.

**Freeze requirements:**

- documented compatibility policy;
- complete capability map;
- stable error model;
- machine-readable schema;
- reconstruction contract;
- full CI suite;
- release artifact provenance;
- no known architecture-breaking debt;
- migration policy for future protocol versions.

---

# Roadmap status discipline

After each accepted task:

1. update `PROJECT_STATUS.md` with exact SHA and evidence;
2. mark the task COMPLETE here;
3. update the next-task plan or create it;
4. add any newly discovered architectural invariant/known debt to `HANDOFF.md`/`ARCHITECTURE.md`;
5. do not mark a later task active until operator explicitly authorizes it.

The current transition point is **Phase-1 accepted -> Phase-2 Tasks 12–20
accepted**. Task 21 and later remain not started and not authorized. See
`PHASE2_ACCEPTANCE.md` and `PHASE2_PHYSICAL_EVIDENCE.md` for the Phase-2
acceptance record.

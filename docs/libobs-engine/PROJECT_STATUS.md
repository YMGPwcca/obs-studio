# LibOBS Engine Protocol v2 — Project Status

**Status snapshot date:** 2026-09-01
**Production branch:** `engine-protocol-v2`  
**Accepted Task-10 implementation:** `6a590c2985a99d186c8eecd0241acdc824d32168`
**Accepted Task-10 documentation checkpoint:** `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`
**Accepted Task-11 implementation:** `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
**Task-11 acceptance record:** `TASK11_ACCEPTANCE.md`
**Candidate branch:** `phase1-workflow-exec-hardening`
**Script-body hardening branch:** `phase1-scriptbody-hardening`
**Task-11 status:** COMPLETE / ACCEPTED
**Tasks 1-11:** ACCEPTED
**Phase-1 complexity hardening:** COMPLETE / ACCEPTED
**Phase-1 script-body hardening:** IN REVIEW
**Phase-1 workflow executable hardening:** IN REVIEW

The current Phase-1 review checkpoint hardens the complexity gate's moving
post-Task-11 scope, adds exact function-identity continuity migrations, and
extends deterministic checker self-tests through A–U. It does not change
production engine/libobs behavior and does not start Task 12. Phase-1 is
accepted at reviewed checkpoint `1b2ddacbb36c39bb61fd645594f0746f106956bf`;
see `PHASE1_COMPLEXITY_ACCEPTANCE.md` for the explicit approval record.

The accepted named-function/script-body complexity snapshot was measured at
pre-freeze checkpoint `5eb1e665bfee7c868c088bf306e770e814159c54` and contains
875 named-function scopes. The current workflow executable-code review adds a
complete 1,235-scope snapshot at pre-freeze checkpoint
`52c70fa3a20f1bbd454b35680b39537abe2f7106`, pinned by baseline blob
`13b923de57d477c15113898717137968711de3ca`. The runtime implementation
reference remains `44243a501`; the workflow closure is still IN REVIEW.

The separate pre-Phase-2 script-body cleanup is in review on
`phase1-scriptbody-hardening`. It enforces PowerShell top-level script-body
budgets and refactors only verification tooling; it does not change runtime
behavior, the accepted named-function baseline, or Task-12 authorization. See
`PHASE1_SCRIPTBODY_HARDENING.md` for the provisional before/after evidence.

The final pre-Phase-2 workflow executable-code cleanup is in review on
`phase1-workflow-exec-hardening`. It extracts substantial GitHub Actions
`run:` logic into measured PowerShell scripts and fails closed on unsupported
inline executable logic. It does not change runtime behavior or authorize Task
12. See `PHASE1_WORKFLOW_EXEC_HARDENING.md` for the inventory and evidence.

This file is the current status ledger for the LibOBS split-engine project. If an older roadmap says Task 8 is active or Task 9 is only proposed, that older status is stale. Verify this ledger against Git/source/CI whenever resuming work.

---

## 1. Executive status

| Task | Scope | Status | Key acceptance point |
|---|---|---|---|
| 1 | Headless host refactor | COMPLETE | `obs-engine.exe` headless Windows libobs host |
| 1.1 | Headless package cleanup/security | COMPLETE | no normal OBS frontend in engine package; restricted module/DLL behavior |
| 2 | Protocol v2 framing | COMPLETE | bounded NDJSON v2 request/response/error parsing |
| 3 | Capability discovery | COMPLETE | hello/capability advertisement |
| 4 | Revision/optimistic concurrency | COMPLETE | global revision + `ifRevision` conflict semantics |
| 5 | Event queue/subscriptions | COMPLETE | ordered event sequence, subscriptions, overflow/resync |
| 6 | Initial runtime objects v2 | COMPLETE | initial source/scene/item bridge |
| 7 | Generic `properties.*` | COMPLETE | plugin property schema/control bridge |
| 8 | Complete `source.*` | COMPLETE | full namespace + deferred callback settlement + deterministic A–F + physical Windows |
| 9 | `interaction.*` | COMPLETE | seven methods + deterministic callback fixture + same-SHA matrix + physical Windows |
| 10 | `media.*` | COMPLETE / ACCEPTED | 11 methods, exact queued-action settlement, exact-SHA hosted and physical evidence |
| 11 | `filter.*` | COMPLETE / ACCEPTED | `e7b34828...`, exact-SHA matrix, physical Windows, artifact and independent review PASS |
| 12–50 | Later roadmap | NOT STARTED | Task 12 remains planned and unauthorized |

---

## 2. Accepted commit chain near current work

### Task 8 production/concurrency acceptance

Important history:

- `f13431c2dd72109b9ec8dbebb3c0174f7d119a15` — early complete-source attempt; **not** the accepted final settlement behavior.
- `862184529c0f1def6ec32ceafb90e708d1a187e3` — fix deferred source mutation settlement.
- `6d1a02771dcc765579016d5f89b08743b8efeca9` — settle every deferred video source update.
- `dd38b2ff306724546a9e8df0955bd1ecc99eb7f4` — `Match deferred source updates to request state`; production source-settlement behavior accepted and physically exercised.
- `22c4b526f63cfa7193b80c0be05e964fb34e3c8a` — add deterministic Task-8 A–F concurrency regression harness.
- subsequent diagnostics fixed a CI wrapper false-negative, culminating in:
- `e88ceb0a1e1103c3297cd1bd589e56e28ae638e4` — accepted Task-8 baseline with deterministic A–F concurrency workflow green.

### Task 9

- `f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc`
- subject: `feat(engine): complete protocol v2 interaction namespace`
- parent: `e88ceb0a1e1103c3297cd1bd589e56e28ae638e4`
- one clean Task-9 production commit after scratch-history squash.

Task-9 diff against Task 8 contained exactly these 11 paths:

1. `.github/scripts/engine-protocol-v2-task9.ps1` — added.
2. `.github/workflows/engine-protocol-v2-task2.yaml` — tiny update: old unsupported-method probe changed because `interaction.*` became supported.
3. `.github/workflows/engine-protocol-v2-task3.yaml` — same reason.
4. `.github/workflows/engine-protocol-v2-task4.yaml` — same reason.
5. `.github/workflows/engine-protocol-v2-task9.yaml` — added.
6. `engine/CMakeLists.txt` — Task-9 CI-only module target.
7. `engine/INTERACTION_V1.md` — concrete protocol schema.
8. `engine/protocol_v2.cpp` — capabilities/classification/dispatch.
9. `engine/runtime.hpp` — interaction runtime declarations/state pointer.
10. `engine/runtime_interaction_v2.cpp` — production implementation.
11. `engine/task9_interaction_source.cpp` — deterministic CI-only source.

### Task 10 — accepted corrective implementation

- `6a590c2985a99d186c8eecd0241acdc824d32168`
- subject: `fix(engine): correlate media actions by source ticket`
- accepted documentation checkpoint: `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`
- exact queued-action ownership, callback/mutation ordering correction, and
  media action settlement are accepted; the accepted Task-11 implementation does not alter
  the Task-10 media implementation.

Implemented methods:

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

Implemented event normalization:

- `media.started`
- `media.playing`
- `media.paused`
- `media.stopped`
- `media.ended`
- `media.error` when an observable media signal finds an ERROR transition
- `media.stateChanged`

Important semantics:

- `obs_source_media_*` controls are settled from source-specific libobs signals,
  not assumed synchronous. The fork adds the internal `media_time` signal after
  the queued set-time callback returns because upstream has no generic seek
  completion signal; the signal carries an engine/libobs-only action ticket.
- Transport state is revisioned; playback position snapshots are not a
  high-frequency revision stream. Idempotent play/pause/stop and equal seeks do
  not churn revisions.
- Media callbacks use a separate bounded, source-correlated deferred bridge with
  exact source-local action tickets. Uncertain ownership, orphan completion, or
  overflow produces `session.resyncRequired` without silently reassigning a
  callback to a later request.
- The generic libobs snapshot has no reliable error signal, so `media.error` is
  emitted only when ERROR is observed during another media signal; `getState` is
  authoritative for a point-in-time query.

Verification on the local Task-10 corrective candidate:

- VS2022 17.14 x64 `RelWithDebInfo` full build and install passed.
- `obs-engine-events-test` and `obs-engine-properties-test` passed.
- Task 8 A–F, Task 9 interaction, and Task 10 media integration passed on the
  same built engine. Task 10 now covers all methods, bounds, stale guards,
  unsupported sources, exact same-source action ownership, timed-out/orphan
  completion including set-position, deferred overflow, removal with an active
  callback, and clean shutdown with an active callback.
- Normal package audit passed: one `obs-engine.exe`; no `obs64.exe`, `obs32.exe`,
  browser/WebSocket module, or Task 8/9/10 fixture DLL. The Task-10 workflow is
  at `.github/workflows/engine-protocol-v2-task10.yaml`.
- Local Windows fixture execution passed on the AMD/Windows 25H2 machine.
  Optional AJA/DeckLink/NVENC/VLC warnings were expected and did not affect the
  engine run. The operator handoff records exact-SHA hosted CI, physical
  Windows acceptance, and independent raw-evidence audit as complete.

---

## 2.1 Task 11 accepted implementation — `filter.*`

Task 11 is accepted on `task11-codex` at implementation SHA
`e7b34828cb9fbd55bae01f97148f1ec93a4ae015`, based on accepted documentation
checkpoint `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`. The accepted implementation
adds the documented filter kind/runtime methods, explicit filter-handle-to-parent
ownership, permanent update observers with generation- and private tracked-update
serial settlement, parent-removal ordering, source-duplicate inherited-filter
discovery, and a CI-only deterministic fixture/workflow. It does not change
`engine/protocol_v2.cpp` or the accepted Task-10 media implementation.

The exact-SHA matrix, package/artifact provenance, physical Windows validation,
independent review, and human approval are recorded in `TASK11_ACCEPTANCE.md`.

---

## 3. Task 8 accepted behavior

### 3.1 `source.*` methods

The complete source namespace includes:

- `source.kindList`
- `source.kindGet`
- `source.kindDefaults`
- `source.kindProperties`
- `source.list`
- `source.get`
- `source.create`
- `source.duplicate`
- `source.remove`
- `source.rename`
- `source.getSettings`
- `source.patchSettings`
- `source.replaceSettings`
- `source.resetSettings`
- `source.getProperties`
- `source.getFlags`
- `source.getDimensions`
- `source.getState`
- `source.getActive`
- `source.getShowing`
- `source.getMissingFiles`
- `source.refresh`
- `source.saveState`
- `source.loadState`

### 3.2 Source events

- `source.created`
- `source.removed`
- `source.renamed`
- `source.settingsChanged`
- `source.activeChanged`
- `source.showingChanged`
- `source.flagsChanged`
- `source.dimensionsChanged`

### 3.3 Deferred source-update acceptance criteria

A video-source settings command may trigger a plugin update asynchronously on the video thread. Accepted implementation ensures command-owned delayed settings/dimensions changes retain the command’s revision rather than becoming a false later mutation.

Current settlement ownership test is source-specific and state-specific: same source handle + `source.settingsChanged` + canonical settings equal the source’s post-mutation snapshot.

Unrelated batches are preserved. Queue/settlement loss produces resync rather than silent event loss.

### 3.4 Deterministic A–F concurrency regression

Accepted suite verifies:

- A: command A + delayed callback A => one revision.
- B: unrelated source B update during A settlement => independent next revision.
- C: B/C events do not deduplicate each other.
- D: remove A does not suppress B callbacks.
- E: stale guard after unrelated async revision => `revision_conflict`.
- F: deferred overflow => `session.resyncRequired`.

CI-only fixture is `engine/task8_concurrency_source.cpp`; workflow `.github/workflows/engine-protocol-v2-task8-concurrency.yaml`.

### 3.5 Task-8 final physical Windows acceptance

Final accepted Task-8 build at `e88ceb0a...` was run manually on physical Windows. Fresh process flow used fixed handle `"1"`:

1. `session.hello` revision 0.
2. subscribe `source.*` revision 0.
3. `source.kindList` found `color_source_v3`.
4. create `color_source_v3` 320×180 => source `"1"`, revision 1, `source.created` revision 1.
5. `source.replaceSettings` to 640×360 with `ifRevision:1` => response revision 2.
6. `source.settingsChanged` revision 2.
7. `source.dimensionsChanged` revision 2, 640×360.
8. `source.getDimensions` => 640×360, revision 2.
9. remove source `"1"` with `ifRevision:2` => response/event revision 3.
10. `session.close` with `ifRevision:3` => revision 4 and clean shutdown.

This specifically proves the original delayed-update regression does not create an extra revision.

---

## 4. Task 9 accepted behavior

### 4.1 Capabilities/methods

Hello advertises:

- `interaction.v1`
- `interaction.focus.v1`
- `interaction.key.v1`
- `interaction.mouseButton.v1`
- `interaction.mouseMove.v1`
- `interaction.mouseWheel.v1`
- `interaction.reset.v1`
- `interaction.text.v1`

Methods:

- `interaction.focus`
- `interaction.mouseMove`
- `interaction.mouseButton`
- `interaction.mouseWheel`
- `interaction.key`
- `interaction.text`
- `interaction.reset`

### 4.2 Semantics

- Requires source with `OBS_SOURCE_INTERACTION`.
- Uses libobs interaction callbacks only.
- Transient: does not mutate global revision by itself.
- Common protocol guard rejects `ifRevision` on interaction methods.
- No OS-native message injection.
- Pointer coordinates validated in source-local pixels.
- Text encoded as validated UTF-8 scalar down/up pairs.
- Reset releases tracked protocol-held state.
- Held-key tracking bounded to 256 unique identities/source.

### 4.3 Task-9 final same-SHA CI matrix

On SHA `f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc`:

- 11 check runs completed successfully;
- zero failures;
- zero checks left running;
- earlier Task-8 concurrency/thread-isolation regression remained green;
- source/event/revision/properties/framing/runtime-object lanes remained green;
- Task-9 dedicated job `Build and verify interaction namespace` was green.

Dedicated Task-9 run ID: `33183763541`. Historical job ID: `98891335371`.

The Task-9 job passed:

- build Windows minimal runtime;
- verify normal package excludes `task9-interaction-source.dll`;
- build CI-only deterministic interaction source;
- stage it;
- execute Task-9 integration regression;
- remove it;
- upload production Windows runtime.

### 4.4 Production Task-9 artifact

Historical CI artifact:

- name: `obs-engine-windows-x64-task9`
- artifact ID: `9690957023`
- size: ~99.6 MB ZIP
- SHA-256: `04e7651e8d006117cf4b6ae1e578ba47d657a2c1ee9cc1aeec2358ca3314c09f`

This artifact intentionally excludes the CI-only test interaction source. On physical startup its normal packaged source kinds all reported `interaction:false`. That was expected and is evidence the fixture did not leak into the production package.

### 4.5 Task-9 physical acceptance bundle

Because normal packaged plugins on that artifact did not expose an interaction-capable source, a temporary physical-acceptance branch was used to produce a bundle with the CI fixture staged.

Temporary branch:

- `task9-physical-acceptance`
- packaging-only commit: `5916aa0fadf99759fedd8d5599a5369232896cbf`
- parent: accepted engine SHA `f59d6b6c...`
- workflow run: `33185731362`
- conclusion: success
- artifact: `obs-engine-windows-x64-task9-physical-acceptance`
- artifact SHA-256: `ab897e0823796c0aa89ebab1091cd258fc24abde4b8379955e1efafd5384be9e`

The extra commit only added the physical-acceptance packaging workflow. Production branch/runtime remained at `f59d6b6c...` before this documentation handoff.

### 4.6 Task-9 physical Windows result

Run command:

```powershell
.\bin\64bit\obs-engine.exe --plugin=task9-interaction-source
```

Fresh source handle was `"1"`.

Observed callback/response sequence:

- create => revision 1.
- focus true => callback, revision 1.
- mouseMove (100,50), shift => callback modifier 2, revision 1.
- left mouse down => callback modifier 16, revision 1.
- wheel deltaY 120, control => callback modifier 4, revision 1.
- key `a` down, scan 30, virtual key 65, shift => callback exact, revision 1.
- text `Hi` => H down/up then i down/up, revision 1.
- reset => left-button up + `a` key up + mouse leave + focus false; response `releasedKeys:1`, `releasedButtons:1`, revision 1.
- remove source => revision 2.
- session close => revision 3.
- clean OBS context shutdown.

Result: **Task 9 physical acceptance PASS.**

---

## 7. Current physical test environment history

Latest physical acceptance machine reported:

- AMD Ryzen 7 9700X, 8 cores / 16 threads.
- ~31.8 GiB physical RAM.
- Windows 10.0 build 26200, release 25H2, revision 9278, x64.
- AMD integrated graphics (adapter 0 on Task 9 run).
- AMD Radeon RX 9060 XT with ~15.9 GiB dedicated VRAM (adapter 1).
- AMD driver reported `32.0.21030.2001` in latest run.
- HAGS disabled/unsupported on iGPU, enabled/supported on RX 9060 XT.

Normal optional warnings on this system included absent AJA, DeckLink, NVIDIA effects SDK/NVENC, and VLC. Do not misclassify those expected plugin warnings as failures.

At the time of Task-9 physical testing system free RAM was low (~517 MB), yet the interaction lifecycle remained clean. This is not a memory-stress acceptance test and should not be used as one.

---

## 8. Current known technical debt / watch list

### 8.1 Duplicated source bridge private state

`SourceV2State` and `DeferredSourceEventBatch` are duplicated in `runtime_source_v2.cpp` and `runtime_source_settle_v2.cpp`, with an explicit requirement to keep them token-for-token equivalent. Move them to a private shared header in a dedicated cleanup when safe. Do not mix this casually with `media.*`.

### 8.2 Legacy `engine/README.md`

The README’s security/host information remains useful, but many command examples describe v1 (`{"id":1,"cmd":"hello"}` style) and are not the current v2 Controller contract. A future documentation cleanup should update it without erasing useful host/security rationale.

### 8.3 Protocol handle wording

Generic `PROTOCOL_V2.md` handle wording is looser than current source/interaction canonical-string implementations. Decide and normalize this explicitly before multiple new object namespaces spread inconsistent encodings.

### 8.4 Test-only fixture hygiene

Task 8/9 fixtures must remain CI-only. Every future deterministic plugin should follow the same no-install/package-assert pattern.

### 8.5 Browser source not currently in the minimal package

Task-9 production package had no interaction-capable source. Do not infer browser source support from generic interaction API support. Browser/plugin packaging is a separate product/extension decision and should be handled deliberately in the relevant roadmap work.

### 8.6 Media observer and upstream signal debt

The media bridge has a private deferred queue separate from the accepted source
bridge and duplicates the canonical decimal handle reader until a safe shared
private helper can be introduced. Upstream libobs has no generic media-error or
seek-completion signal; this branch adds only the internal `media_time` signal
after the queued set-time callback returns, plus a private tracked-enqueue
ticket mechanism. Real plugins may apply a seek or enter ERROR asynchronously,
so the returned position is a snapshot and `media.error` is emitted only when
an observable media signal exposes the ERROR transition. Task 42 should extend
this with stress coverage before any stronger completion guarantee is promised.

### 8.7 Allowlist boundary corrections

The handoff audit removed `obs-websocket` and `obs-browser` from the default
engine safe-module list in commits `08010cdc6` and `2744b3bcb`. Both remain
explicit opt-in/build-disabled paths; the normal Controller runtime cannot
accidentally activate either second API or browser payload.

---

## 9. Acceptance policy going forward

A future task is not complete merely because its new workflow is green. Completion requires, as applicable:

1. exact protocol contract resolved/documented;
2. production implementation;
3. input/lifetime/error/capability validation;
4. deterministic integration coverage;
5. previous regression lanes all green on the same final production SHA;
6. normal package audit;
7. two-pass diff/source review;
8. physical Windows acceptance when platform/hardware/plugin behavior cannot be fully established by hosted CI;
9. clean commit boundary;
10. explicit stop before the next task.

Record exact final SHA and acceptance evidence in this file after each completed task.

---

## 10. Immediate next status transition

The next project state transition is:

`Phase-1 complexity hardening accepted` -> `Task 12 planned/not authorized`.

Task 12 has not started and is not authorized. The advisor WIP is preserved on
`wip/task11-advisor-handoff-137b2e5` locally and remotely; the older unauthorized
implementation is preserved on `wip/task11-unauthorized`. Neither is accepted or
merged.

# Task 10 Plan — Complete `media.*` Namespace

**Status:** COMPLETE / ACCEPTED
**Prerequisite:** Tasks 1–9 accepted
**Accepted Task-10 implementation:** `6a590c2985a99d186c8eecd0241acdc824d32168`
**Accepted documentation checkpoint:** `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`
**Record:** this plan serves as the source-research and corrective-review record for the accepted namespace. Task 11 (`filter.*`) is implemented on an isolated candidate and remains in review/not accepted.

This plan is deliberately more detailed than a normal task ticket. A local coding agent should use it as an investigation checklist, then amend it if current source/libobs behavior differs.

---

## 1. Goal

Implement a complete semantic `media.*` control/query namespace for libobs sources advertising `OBS_SOURCE_CONTROLLABLE_MEDIA`, while preserving Protocol-v2 revision/event/concurrency rules.

The Controller should be able to:

- inspect current media state;
- play/resume;
- pause;
- toggle pause;
- stop;
- restart;
- move to next/previous media entry where source supports it;
- query duration;
- query current position;
- seek/set position;
- receive meaningful media lifecycle/state events without turning playback time into revision/event spam.

No plugin-specific player API should leak into Controller.

---

## 2. Protocol names already fixed by `engine/PROTOCOL_V2.md`

Methods:

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

Events:

- `media.started`
- `media.playing`
- `media.paused`
- `media.stopped`
- `media.ended`
- `media.error`
- `media.stateChanged`

Do not rename these merely to mirror a libobs C function. The Engine Protocol is semantic.

Before implementation, add a concrete namespace schema document, proposed path `engine/MEDIA_V1.md`, that fixes exact params/response/state enum/ranges and event payloads.

---

## 3. Source facts already discovered — verify locally again

### 3.1 Capability flag

`libobs/obs-source.h` defines:

```c
#define OBS_SOURCE_CONTROLLABLE_MEDIA (1 << 13)
```

Current `source.kindList`/metadata normalization already exposes this semantically as `controllableMedia`.

### 3.2 libobs media state enum

Current upstream snapshot defines:

```c
enum obs_media_state {
    OBS_MEDIA_STATE_NONE,
    OBS_MEDIA_STATE_PLAYING,
    OBS_MEDIA_STATE_OPENING,
    OBS_MEDIA_STATE_BUFFERING,
    OBS_MEDIA_STATE_PAUSED,
    OBS_MEDIA_STATE_STOPPED,
    OBS_MEDIA_STATE_ENDED,
    OBS_MEDIA_STATE_ERROR,
};
```

Protocol must map these to stable strings, not expose raw numeric enum values as the primary semantic state.

Proposed stable strings, subject to `MEDIA_V1.md` review:

- `none`
- `playing`
- `opening`
- `buffering`
- `paused`
- `stopped`
- `ended`
- `error`

Unknown future enum values must not cause undefined behavior. Decide whether to map unknown values to `unknown` plus raw diagnostic metadata or return a bounded fallback; document compatibility behavior.

### 3.3 Source implementation callbacks

`struct obs_source_info` currently provides:

- `media_play_pause(void *data, bool pause)`
- `media_restart(void *data)`
- `media_stop(void *data)`
- `media_next(void *data)`
- `media_previous(void *data)`
- `media_get_duration(void *data)`
- `media_get_time(void *data)`
- `media_set_time(void *data, int64_t milliseconds)`
- `media_get_state(void *data)`

### 3.4 Public application-facing libobs APIs

Current `libobs/obs.h` exposes:

- `obs_source_media_play_pause(source, pause)`
- `obs_source_media_restart(source)`
- `obs_source_media_stop(source)`
- `obs_source_media_next(source)`
- `obs_source_media_previous(source)`
- `obs_source_media_get_duration(source)`
- `obs_source_media_get_time(source)`
- `obs_source_media_set_time(source, ms)`
- `obs_source_media_get_state(source)`
- `obs_source_media_started(source)`
- `obs_source_media_ended(source)`

Use public libobs APIs. Do not call plugin callbacks directly from engine code.

### 3.5 Critical asynchronous behavior

**This is the Task-10 concurrency trap to solve before coding.**

Current `obs_source_media_play_pause`, restart, stop, next, previous and set-time APIs do not simply call plugin callbacks immediately. `obs-source.c` pushes `media_action` entries under `source->media_actions_mutex`.

`process_media_actions(source)` drains that queue from the source tick path when `OBS_SOURCE_CONTROLLABLE_MEDIA` is set. Therefore a protocol thread calling `obs_source_media_*` may return before the plugin callback has executed and before the source state query reflects the requested transition.

Observed core signal behavior in current libobs:

- play/pause action calls plugin `media_play_pause`, then libobs signals `media_play` or `media_pause`;
- restart calls plugin `media_restart`, then signals `media_restart`;
- stop calls plugin `media_stop`, then signals `media_stopped`;
- next calls plugin `media_next` and signals corresponding media action;
- previous likewise;
- set-time calls plugin `media_set_time`; no equivalent generic state-transition signal was observed in the inspected switch;
- plugin/core may call `obs_source_media_started` / `obs_source_media_ended`, which emit `media_started` / `media_ended` source signals.

**Do not treat media control calls as synchronously committed simply because the public function returns.** Task 8 already demonstrated how this breaks revision ownership.

---

## 4. Mandatory upstream/plugin research before design freeze

Run locally:

```bash
rg -n "OBS_SOURCE_CONTROLLABLE_MEDIA|obs_source_media_|media_play_pause|media_get_state|media_started|media_ended|process_media_actions|MEDIA_ACTION_" libobs plugins UI engine
```

Then inspect at least:

1. `libobs/obs-source.h` — enum/flags/callback declarations.
2. `libobs/obs.h` — public control APIs.
3. `libobs/obs-source.c` — action queue, tick processing, signals, teardown.
4. `libobs/obs-internal.h` — media action storage/lifetime if needed.
5. built-in `ffmpeg_source` implementation in `plugins/obs-ffmpeg`.
6. image slideshow/media-capable source if it advertises controllable media.
7. VLC source if code is available, even if VLC is absent in CI runtime.
8. normal OBS frontend media-control usage to understand user-facing semantics.

For each representative plugin answer:

- Which controls are actually implemented?
- Does `media_get_state` change during the callback or asynchronously later?
- When does it emit `media_started`/`media_ended`?
- Does pause produce a plugin/core signal only or state callback too?
- What does restart mean from stopped/ended/error?
- Does stop reset position to 0?
- How do next/previous behave with no playlist/multiple entries?
- What duration/time values signal unavailable/unknown?
- Is seek clamped, ignored, asynchronous, or error-like?
- Can state remain opening/buffering for long periods?
- Can error state occur without a generic `media_error` source signal?

Document findings in `MEDIA_V1.md` or this plan before implementation.

---

## 5. Proposed protocol schema — must be finalized before code

The following is a design proposal, not yet canonical until `MEDIA_V1.md` is committed.

### 5.1 Common target

All methods require:

```json
{"source":"1"}
```

Validation:

- canonical decimal source handle string;
- source exists;
- source advertises `OBS_SOURCE_CONTROLLABLE_MEDIA`;
- return `unsupported_capability` otherwise.

Do not use source kind ID alone as capability proof; runtime flags are authoritative.

### 5.2 `media.getState`

Read-only.

Proposed success data:

```json
{
  "source":"1",
  "state":"playing"
}
```

Optionally include position/duration only if doing so has clear consistency semantics. Prefer separate queries at first; fewer race-prone composite promises.

### 5.3 `media.play`

Semantic intent: make media play/resume.

Likely maps to:

```c
obs_source_media_play_pause(source, false)
```

Question to resolve: If already playing, is this an accepted no-op with no revision or a command event? Prefer idempotent behavior: if observed pre-state is already `playing`, return success without consuming a revision unless libobs/plugin semantics require an action.

### 5.4 `media.pause`

Likely:

```c
obs_source_media_play_pause(source, true)
```

Idempotence question same as play.

### 5.5 `media.togglePause`

There is no separate libobs toggle API. Read current state at command time, then choose pause/play.

Define states in which toggle is valid. Candidate:

- playing/opening/buffering -> pause;
- paused -> play;
- stopped/ended/none/error -> either invalid_state or semantic play/restart; must be decided from actual OBS frontend/plugin behavior, not guessed.

Because state may change between read and queued action processing, deterministic semantics may require command serialization + post-action settlement rather than naïve read-then-enqueue.

### 5.6 `media.stop`

Likely `obs_source_media_stop`.

Decide whether success is committed when action is processed, when plugin state reports stopped, or after a bounded state/signal condition.

### 5.7 `media.restart`

Likely `obs_source_media_restart`.

Restart may lead to opening/buffering before playing. Do not require instantaneous `playing` if plugin legitimately transitions asynchronously. Define a command-accepted state/effect model that still preserves revision ownership.

### 5.8 `media.next` / `media.previous`

Use public APIs. Generic capability flag does not guarantee a meaningful playlist. libobs public calls may no-op when callback absent. Need stable protocol behavior when callbacks are absent:

- `unsupported_capability` at method granularity if callback absence can be detected through public metadata safely, or
- success/no-op consistent with libobs generic API.

Prefer explicit capability/error if the engine can determine support without reaching into plugin-private structures. Investigate available public API for callback support.

### 5.9 `media.getDuration`

Read-only. Return integer milliseconds.

Proposed:

```json
{"source":"1","durationMs":12345}
```

Resolve semantics for negative values/unknown duration. Validate against real plugins. Do not cast signed negative to unsigned.

### 5.10 `media.getPosition`

Read-only. Use `obs_source_media_get_time`.

Proposed:

```json
{"source":"1","positionMs":2345}
```

Treat this as a snapshot query, not canonical state event generation.

### 5.11 `media.setPosition`

Proposed params:

```json
{"source":"1","positionMs":2345}
```

Use signed 64-bit JSON integer validation inside a sensible nonnegative range.

Resolve:

- whether zero..duration is required when duration known;
- behavior when duration unknown;
- whether exact end is valid;
- whether plugin clamps;
- whether seeking while stopped/ended/error is valid;
- whether the action is queued and when the new position is observable.

---

## 6. Revision classification — design carefully

### 6.1 Queries

These must not mutate revision:

- `media.getState`
- `media.getDuration`
- `media.getPosition`

They reject `ifRevision` under current common guard if classified non-mutating.

### 6.2 Transport controls

Likely canonical runtime mutations:

- play
- pause
- togglePause
- stop
- restart
- next
- previous
- setPosition

Why canonical? The private UI must reconcile playback transport state/selected entry/seek results, and media state is externally visible runtime state.

However, **do not consume a revision at queue-enqueue time if the underlying action has not yet executed and could fail/no-op.** Determine a settlement contract.

### 6.3 No-op semantics

Before coding, choose consistent behavior:

- successful idempotent no-op should normally not increment revision because canonical state did not change;
- if an action causes a plugin-observable restart/reload despite same state enum, that can still be a mutation; model its semantic effect/event deliberately.

### 6.4 Position progression

Natural playback advances `positionMs` continuously. This must **not** increment global revision every frame/tick.

`media.getPosition` is a snapshot. If future position telemetry is added, it should be telemetry and subscription/coalescing bounded. The current planned event list does not require `media.positionChanged`; do not invent high-frequency canonical events.

---

## 7. Event model design

### 7.1 Raw libobs source signals available

Current libobs source signal table includes at least:

- `media_play`
- `media_pause`
- `media_restart`
- `media_stopped`
- `media_next`
- `media_previous`
- `media_started`
- `media_ended`

Protocol event names are different and more semantic. Normalize them; do not expose raw signal names as a second event vocabulary.

### 7.2 Planned mapping questions

Potential mapping, to validate:

- processed play action -> `media.playing` and/or `media.stateChanged` to `playing` when state confirms;
- processed pause -> `media.paused` + stateChanged;
- stop -> `media.stopped` + stateChanged;
- `media_started` source signal -> `media.started` plus state snapshot;
- `media_ended` -> `media.ended` + stateChanged to ended;
- plugin state ERROR -> `media.error` when transition can be detected;
- restart/next/previous may first produce command-semantic stateChanged or await subsequent plugin started/playing transition.

Avoid duplicate events that express the same canonical transition twice solely because both an action signal and later polling/state signal occur.

### 7.3 `media.stateChanged`

Use this as the generic state enum transition event. Dedicated events can coexist for meaningful lifecycle moments. Decide which are guaranteed so Controller does not need to infer state from ordering quirks.

Proposed event data:

```json
{
  "source":"1",
  "state":"paused",
  "previousState":"playing"
}
```

Only include `previousState` if the engine observer cache can guarantee it accurately.

### 7.4 Error transition detection

There is no `media_error` raw source signal in the inspected current source signal table. If protocol promises `media.error`, determine how error transitions are observed:

- via state checks associated with action/lifecycle signals;
- via bounded polling observer (careful with cost/threading);
- via plugin signals not yet inspected;
- or define `media.error` only when engine observes `OBS_MEDIA_STATE_ERROR` during a known state refresh.

Do not advertise an event you cannot reliably produce.

---

## 8. Command-settlement design — highest-risk part of Task 10

Because libobs queues media actions to `process_media_actions()` on source tick, Task 10 needs an ownership/settlement strategy.

### 8.1 Requirements

For a mutating media request:

1. validate source/capability/current revision;
2. prevent pre-command media callbacks from being misattributed;
3. enqueue public libobs action;
4. wait/bound until the queued action is processed or ownership is otherwise established;
5. observe resulting semantic state/action signal;
6. produce exactly one command revision if canonical state/effect changed;
7. response first;
8. command-owned media events same revision;
9. unrelated media/source callbacks retain independent revision;
10. timeout does not fabricate success state.

### 8.2 Do not reuse Task-8 matching mechanically

Task 8 matches delayed source settings via canonical settings JSON. Media has different observability:

- a play action is represented by queued action + `media_play` signal;
- state may remain opening/buffering before playing;
- restart/next/previous may produce later started/ended transitions;
- seek may have no raw action signal;
- natural ended state is unrelated async state.

Design a media-specific observer/settler rather than force-fitting `SourceV2State`.

### 8.3 Candidate implementation structure

The corrective implementation uses:

- `struct MediaV2State` owned by `Engine`;
- per-source media observer with weak source reference and cached `obs_media_state`;
- connections to media source signals;
- bounded deferred event/action batches during command capture;
- a monotonic source-local libobs action ticket carried through each queued
  action and its core signal;
- a permanent observer plus condition variable waiter matching the exact source
  handle, expected signal, and action ticket;
- for `setPosition`, the internal `media_time` signal emitted after the queued
  set-time callback returns; it proves callback processing, not decoder seek
  completion;
- bounded timeout; on uncertain canonical ownership either return `timeout`/`obs_error` without claiming a mutation, or require resync if state may have changed after ownership was lost.

Timed-out tickets are retained as orphan candidates. A late orphan completion is
never promoted to a later request; it remains an independent uncertainty
boundary and forces resynchronization when its canonical effect is not otherwise
represented.

The exact strategy must be derived from upstream behavior and tests.

### 8.4 Why sleeping is unacceptable

Do not `sleep_for(50ms)` and hope a tick ran. That creates flaky timing, load-dependent bugs and false ownership. Wait for a real observable condition/signal with a bounded deadline.

### 8.5 Cross-source concurrency test

Like Task 8, Task 10 must prove source A’s queued media action does not claim source B’s independent media transition.

---

## 9. Deterministic test fixture design

A CI-only synthetic controllable-media source is strongly recommended.

Proposed file:

`engine/task10_media_source.cpp`

Build target:

`obs-engine-task10-media-plugin`

Properties:

- `MODULE EXCLUDE_FROM_ALL`;
- link `OBS::libobs`;
- no install rule;
- explicit source ID such as `task10_media_source`;
- output flag includes `OBS_SOURCE_CONTROLLABLE_MEDIA`; include video only if needed to ensure tick processing; since libobs processes media actions in source tick, fixture must actually be ticked by engine runtime;
- fixed deterministic duration, e.g. 10,000 ms;
- internal atomic/mutex-protected state/position;
- callbacks for every media operation;
- deterministic `media_get_state`, `media_get_duration`, `media_get_time`;
- configurable scenarios/delays for concurrency tests if needed;
- log exact callback operations to stderr for integration assertions;
- call `obs_source_media_started/ended` from fixture only when testing those lifecycle signals.

### 9.1 Fixture state machine

Suggested test model:

- initial STOPPED, position 0, duration 10,000;
- play(false): state PLAYING;
- pause(true): state PAUSED;
- restart: position 0, state PLAYING, optionally emit started;
- stop: state STOPPED, position 0;
- set time: position requested/clamped according to chosen protocol semantics;
- next/previous: increment/decrement deterministic entry index and reset position/state intentionally;
- video_tick: optionally advance position when PLAYING using deterministic time or disabled auto-progress for non-flaky tests;
- scenario to emit ENDED;
- scenario to emit ERROR;
- scenario to delay callback to prove settlement;
- scenario with independent B source transition while A command settles.

Do not make tests depend on wall-clock playback more than necessary.

### 9.2 Production package hygiene

Workflow pattern:

1. normal Windows install build;
2. assert `task10-media-source.dll` absent;
3. explicitly build fixture;
4. stage beside installed module directory;
5. run Task-10 integration with `--plugin=task10-media-source`;
6. remove fixture;
7. assert absent;
8. upload normal Task-10 production artifact.

---

## 10. Required deterministic integration cases

At minimum implement these before Task 10 acceptance.

### M1 — capability + queries

- hello advertises `media.v1` and per-method capabilities chosen by project convention;
- create source `"1"`;
- `media.getState` returns stopped/initial state at revision 1;
- duration and position correct;
- queries do not increment revision.

### M2 — play settlement

- play on source 1;
- prove plugin callback actually processed before success is considered settled;
- response gets revision 2 if state changed;
- `media.playing`/`stateChanged` command-owned events use revision 2 and come after response;
- immediate `getState` reports expected semantic state.

### M3 — pause

- pause from playing;
- exactly one new revision;
- state/events consistent.

### M4 — idempotent play/pause

- play while already playing and pause while already paused;
- verify chosen no-op revision policy explicitly.

### M5 — togglePause

- from playing -> paused;
- from paused -> playing;
- invalid/other states according to frozen schema.

### M6 — stop/restart

- stop transitions appropriately;
- restart semantics from stopped and/or ended;
- position behavior verified.

### M7 — seek

- set position valid interior position;
- position readback after settlement;
- boundary 0;
- boundary duration if allowed;
- negative rejected;
- beyond duration rejected/clamped according to documented rule;
- read-only position progression does not consume revision.

### M8 — next/previous

- deterministic entry index/state behavior through fixture;
- unsupported/no-op behavior if callback absent covered by a second fixture kind or scenario.

### M9 — ended async transition

- trigger end independent of Controller request;
- event gets its own revision if ended is treated as canonical runtime state;
- no command revision is retroactively reused.

### M10 — error state

- deterministic error transition;
- `media.error` and `stateChanged` policy proven;
- stable error information does not leak plugin internals.

### M11 — cross-source ownership

- source A play command deliberately delayed;
- source B independently changes state during A settlement;
- A response/events revision N;
- B independent event revision N+1 (or reverse if B truly commits before command capture according to proven ordering); never same ownership batch accidentally.

### M12 — stale `ifRevision`

- unrelated canonical event advances revision;
- stale media command guard returns `revision_conflict`;
- no media action enqueued/processed.

### M13 — remove during/after media state

- removal disconnects observer/state tracking safely;
- no event refers to invalid removed source after removal ordering contract;
- no use-after-free.

The corrective fixture also removes a source while a timed-out callback remains
active and verifies the handle is not reused in later events.

### M14 — overflow/resync if media observer uses bounded deferred queue

- deliberately overflow deterministic media deferred event capture;
- require `session.resyncRequired`, not silent loss.

### M15 — clean shutdown with queued media actions

- issue action near shutdown;
- ensure observers disconnect, queued state does not access destroyed fixture, process exits cleanly.

The corrective fixture keeps a timed-out media signal active through shutdown and
releases it through a condition-variable-controlled deadline so teardown is
exercised without a test sleep.

---

## 11. Validation matrix

### Source target

- missing `source` -> `bad_request`;
- wrong JSON type -> `bad_request`;
- empty string -> `bad_request`;
- `"0"` -> `bad_request`;
- `"01"` -> `bad_request`;
- overflow decimal -> `bad_request`;
- syntactically canonical unknown handle -> `not_found`;
- known non-media source -> `unsupported_capability`.

### Position

- wrong JSON type;
- negative;
- > signed 64-bit parser range / JSON integer boundary;
- > duration;
- unknown duration;
- source in invalid state;
- plugin set-time callback absent.

### State

- NONE/opening/buffering/playing/paused/stopped/ended/error mapping;
- unknown future enum handling.

### Revision

- `ifRevision` rejected on queries;
- stale guards on mutators;
- no revision for failed/no-op if chosen contract says no canonical mutation;
- exactly one revision for successful command-owned state transition.

---

## 12. Capability advertisement

Follow current capability style in `protocol_v2.cpp`.

Likely add:

- `media.v1`
- `media.getState.v1`
- `media.play.v1`
- `media.pause.v1`
- `media.togglePause.v1`
- `media.stop.v1`
- `media.restart.v1`
- `media.next.v1`
- `media.previous.v1`
- `media.getDuration.v1`
- `media.getPosition.v1`
- `media.setPosition.v1`

Verify naming convention against Tasks 7–9 before committing. Do not advertise lifecycle event capabilities separately unless the project has an explicit convention for event capabilities.

---

## 13. Proposed file changes

Expected, subject to source audit:

Production:

- `engine/MEDIA_V1.md` — new schema.
- `engine/runtime_media_v2.cpp` — new implementation/observer/settlement.
- `engine/runtime.hpp` — declarations + media state pointer.
- `engine/protocol_v2.cpp` — capabilities, enum classification, mutation/runtime dispatch.
- `engine/CMakeLists.txt` — production source + CI-only fixture target.

Testing:

- `engine/task10_media_source.cpp` — new CI-only deterministic source.
- `.github/scripts/engine-protocol-v2-task10.ps1` — integration.
- `.github/workflows/engine-protocol-v2-task10.yaml` — Windows build/package/test.

Potential earlier-workflow edits:

- if Tasks 2/3/4/other “known unsupported method” probes currently use a `media.*` method, update them to a truly nonexistent method. **Only do this if source proves necessary**, as was required for Task 9 interaction.

Documentation/status after acceptance:

- `docs/libobs-engine/PROJECT_STATUS.md`
- `docs/libobs-engine/ROADMAP.md`
- `docs/libobs-engine/HANDOFF.md` if new invariants/debt discovered.

---

## 14. Implementation style requirements

- C++20 consistent with current engine targets.
- Reuse `ObsDataPtr`, `RuntimeV2Result`, `RuntimeV2Error` and existing validation patterns.
- Do not copy-paste handle parser into a third/fourth namespace if a safe shared private helper can be introduced without destabilizing accepted code; however, avoid broad refactor solely for aesthetics. If duplication is chosen for isolated risk, record debt.
- Bound every queue/tracked map/string/value that can be Controller/plugin influenced.
- Do not throw exceptions across C/libobs callback boundaries.
- Never log protocol secrets/settings indiscriminately.
- Never call protocol writer from media callback.
- Use weak refs where observer lifetime may outlive source ownership.
- Disconnect callbacks before releasing observer/source state during shutdown.

---

## 15. Concurrency/lock review questions

Before finalizing code, answer in review notes:

1. Which thread calls each media plugin callback?
2. Which thread emits each raw media signal?
3. Does the protocol request wait on a condition that can only progress on the same blocked thread?
4. Can a media observer mutex be held while acquiring libobs’s `media_actions_mutex` or source signal mutex, causing inversion?
5. Can a callback take the engine revision guard while the request is waiting on callback completion?
6. How is command capture established before the action can be processed?
7. How are pre-capture callbacks drained/ordered?
8. How does B remain independent while A settles?
9. What is the bounded timeout?
10. What state is reported if timeout occurs after plugin action might actually execute?
11. Does uncertainty require resync?
12. Can removal/shutdown happen while waiter is connected?

Do not approve the task with vague answers.

---

## 16. CI plan

Dedicated Task-10 workflow should run Windows x64 RelWithDebInfo using the shared build action.

Stages:

1. recursive checkout;
2. normal Windows minimal runtime build;
3. assert Task-10 fixture absent from install;
4. explicit fixture build;
5. stage fixture;
6. run deterministic Task-10 script;
7. clean staged fixture;
8. assert fixture absent;
9. upload `obs-engine-windows-x64-task10` normal runtime artifact.

Then on final clean SHA verify all existing project workflows:

- task1
- task2
- task3
- task4
- task5
- task6
- task7
- task8
- task8-concurrency (including thread isolation)
- task9
- task10

No acceptance if Task-10 is green on one SHA and prior tasks are only known green on an older SHA.

---

## 17. Review pass 1 — media/runtime correctness

- public libobs APIs only;
- action queue/tick behavior understood;
- exact plugin callback support checked;
- state mapping exhaustive;
- query signed values handled;
- seek validation safe;
- observer signal connections/disconnections balanced;
- source removal safe;
- shutdown safe;
- no deadlock;
- timeout bounded;
- cross-source attribution correct;
- no unbounded polling/telemetry/event spam;
- deterministic tests truly exercise delayed processing rather than immediately mutating fixture state before libobs action execution.

---

## 18. Review pass 2 — protocol/boundary correctness

- exact `media.*` names;
- concrete `MEDIA_V1.md` matches implementation/tests;
- capability advertisement complete but not premature;
- source handle canonical string;
- non-media source returns stable unsupported error;
- query vs mutator classification correct;
- `ifRevision` behavior correct;
- exactly-one-revision command ownership;
- response-before-events;
- unrelated async ended/error has independent revision;
- playback position does not churn canonical revision;
- no raw enum/pointer/plugin private state leak;
- no OS/platform API accidentally used;
- fixture absent from package;
- no Task-11 filter work included.

---

## 19. Physical Windows acceptance plan

Physical acceptance is recommended because Task 10 depends on source ticking/media plugin behavior, though deterministic CI should already cover semantics.

Prefer two layers:

### Layer A — deterministic physical fixture

Use a packaging-only test bundle based directly on final Task-10 production SHA if normal artifact strips the fixture (it should). Run with `--plugin=task10-media-source`.

Fresh deterministic handle commands should use `source:"1"`, no placeholders.

Exercise:

- create;
- getState/duration/position;
- play;
- pause;
- toggle;
- seek;
- restart;
- stop;
- next/previous;
- async ended/error scenario if fixture exposes it;
- remove;
- close.

Paste entire console output and inspect callback logs/revisions.

### Layer B — real built-in media plugin

If packaged `ffmpeg_source` is available, use a tiny deterministic local media file bundled/generated for acceptance and test real playback. Do not depend on internet URLs.

Validate:

- state progression;
- duration/time;
- pause/resume;
- seek;
- stop/restart;
- clean removal/shutdown.

Next/previous may not apply to a single-file ffmpeg source; use a suitable playlist source only if deterministic and packaged.

---

## 20. Artifact audit

Normal Task-10 artifact must contain:

- `obs-engine.exe`;
- intended runtime plugins/dependencies/data.

Must not contain:

- `task10-media-source.dll`;
- normal `obs64.exe`/OBS frontend;
- accidental test scripts/build tree;
- unrelated new browser/UI payload unless intentionally required by another approved scope.

Record artifact name, CI run ID, artifact ID, byte size and SHA-256 in `PROJECT_STATUS.md` after acceptance.

---

## 21. Final acceptance criteria

Task 10 is COMPLETE / ACCEPTED at the operator-provided implementation and
documentation checkpoints above. This historical plan does not confer or
change Task 11 acceptance.

- [x] Source/libobs/plugin research documented.
- [x] `MEDIA_V1.md` concrete schema frozen for v1.
- [x] All 11 methods implemented.
- [x] Stable media state mapping implemented.
- [x] Unsupported source validation implemented.
- [x] Queue/tick asynchronous action settlement uses exact queued-action tickets
      and a permanent observer without sleeps.
- [x] Command-owned events/revisions are covered by the local corrective tests.
- [x] Natural async ended/error transitions independently revisioned.
- [x] Position progression kept out of revision spam.
- [x] Deterministic media fixture exists and is CI-only.
- [x] Required integration matrix M1–M15 (or justified equivalent) green locally.
- [x] All previous Task 1–9 regressions green on the local corrective tree.
- [x] Production package audit clean for the scoped forbidden artifacts.
- [x] Corrective diff reviewed twice.
- [x] Local Windows fixture execution completed.
- [x] Status/handoff/roadmap updated with the accepted Task-10 status.
- [x] Clean Task-10 corrective commit boundary created.
- [x] Task 10 hosted exact-SHA and physical acceptance evidence recorded by the
      operator handoff.
- [x] Human approval promoted Task 10 to final acceptance.
- [ ] Task 11 independent review and explicit acceptance.

Task 11 is intentionally the active review scope; no Task 12 work is included
in this plan.

---

## 22. Task-10 corrective-review record

Task 10 was implemented after the required source audit. The corrective review
adds exact action identity at the libobs queue boundary, removes temporary
per-request signal waiters, and covers same-source stale actions, orphaned
timeouts, late set-position completion, overflow, removal, and shutdown.

> I verified the current `engine-protocol-v2` HEAD and the handoff delta from accepted Task-9 SHA `f59d6b6c...`. I read the Protocol-v2 media section, current v2 dispatch/revision/event code, Task-8 settlement, Task-9 interaction implementation, libobs media public APIs and `process_media_actions`, plus representative media plugins. The critical Task-10 issue was that media controls are queued and processed on source tick, so the implemented baseline uses a media-specific bounded settlement/observer rather than a synchronous call/return assumption.

The implementation is in `engine/runtime_media_v2.cpp`, the concrete schema is
`engine/MEDIA_V1.md`, and deterministic coverage is in
`.github/scripts/engine-protocol-v2-task10.ps1` with workflow
`.github/workflows/engine-protocol-v2-task10.yaml`. Local VS2022 x64 build,
unit lanes, Task 8/9 regressions, media integration, package audit, and Windows
fixture execution are green on the accepted corrective tree. The operator
handoff records the exact-SHA hosted workflow, physical Windows acceptance, and
independent raw-evidence audit as complete.

If the local agent cannot truthfully say that, it has not completed the handoff audit.

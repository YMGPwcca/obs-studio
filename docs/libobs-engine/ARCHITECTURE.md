# LibOBS Split Architecture — Canonical Engineering Model

This document describes the architecture the local agent must preserve while extending Engine Protocol v2. It synthesizes the accepted split-engine decisions and current implementation. If source or `engine/PROTOCOL_V2.md` contradicts this file, inspect and reconcile before coding.

---

## 1. System shape

```text
+---------------------------------------------------------------+
| Private Controller / UI                                      |
|                                                               |
| - polished product UI/UX                                     |
| - durable projects and persistent IDs                        |
| - profiles / scene collections / templates                   |
| - undo/redo                                                  |
| - account/business/automation logic                          |
| - recovery/reconstruction policy                             |
| - Controller SDK/client implementation                       |
+----------------------------+----------------------------------+
                             |
                             | ONE semantic Engine Protocol
                             | initial transport: stdin/stdout NDJSON
                             v
+---------------------------------------------------------------+
| GPL obs-engine.exe                                           |
|                                                               |
| - Protocol v2 parser/dispatcher                              |
| - revision state                                             |
| - event queue/subscriptions/resync                           |
| - runtime handle tables                                      |
| - libobs semantic adapters                                   |
| - callback normalization / lifetime ownership                |
| - module/plugin loading                                      |
| - D3D11/video/audio/output runtime                           |
+----------------------------+----------------------------------+
                             |
                             v
+---------------------------------------------------------------+
| libobs + OBS runtime plugins                                 |
| sources / filters / outputs / encoders / services / etc.    |
+---------------------------------------------------------------+
```

This boundary is deliberate. Do not collapse it into a library embedded in the private Controller.

---

## 2. Why the API is semantic instead of a raw libobs C ABI proxy

A raw “call any libobs function by name” protocol would be brittle, unsafe, difficult to version, leak lifetime/threading details, and force a UI to understand plugin/internal C structures. The chosen model exposes product-semantic namespaces such as:

- `session.*`
- `engine.*`
- `module.*`
- `properties.*`
- `source.*`
- `interaction.*`
- `media.*`
- `filter.*`
- `scene.*`
- `item.*`
- `canvas.*`
- `program.*`
- `preview.*`
- `studio.*`
- `transition.*`
- `previewOutput.*`
- `audio.*`
- `hotkey.*`
- `encoder.*`
- `encoderGroup.*`
- `service.*`
- `output.*`
- `recording.*`
- `streaming.*`
- `replayBuffer.*`
- `virtualCamera.*`
- `screenshot.*`
- `caption.*`
- `missingFile.*`
- `stats.*`
- `runtime.*`
- `extension.*`
- optional `script.*`

The protocol should provide enough capability to implement normal OBS Studio behavior in a custom frontend without cloning Qt/frontend internals into the engine.

---

## 3. Controller-owned state vs Engine-owned state

### 3.1 Controller-owned durable state

These concepts should stay outside the GPL runtime unless a very specific engine semantic requires a runtime representation:

- project files and product-specific schema;
- persistent UUIDs and cross-restart identities;
- friendly saved object names/metadata beyond what libobs needs at runtime;
- profile and scene-collection product organization;
- undo/redo history;
- UI panels, selection, tab, layout and window state;
- user presets/templates;
- cloud sync/account state;
- automation/business logic;
- permissions/roles/license/account policy;
- high-level reconstruction/recovery policy.

The Controller maps its persistent IDs to fresh engine handles each engine session.

### 3.2 Engine-owned runtime state

The engine owns:

- `obs_source_t`, `obs_scene_t`, `obs_sceneitem_t`, output/encoder/service/etc. objects;
- reference counts and weak references;
- runtime plugin module instances;
- graphics/audio/video state;
- output/media lifecycle;
- callback/signal subscriptions to libobs objects;
- transient input state needed to deliver protocol semantics;
- runtime event snapshots/caches used to normalize callbacks;
- ephemeral handles.

### 3.3 Reconstruction model

On engine crash/restart:

1. old handles are discarded;
2. Controller starts a fresh engine;
3. Controller negotiates hello/capabilities;
4. Controller re-discovers available plugin kinds/capabilities/properties;
5. Controller recreates runtime objects from its durable model;
6. Controller records fresh handle mappings;
7. Controller reconciles state/events before resuming normal operations.

Later roadmap tasks explicitly formalize this reconstruction/crash-recovery contract.

---

## 4. Engine handle rules

Handles are API tokens, not identities.

Current implemented source/interaction convention is a canonical decimal string such as `"1"`.

Properties:

- opaque to Controller;
- monotonically allocated in the current engine process;
- positive;
- no leading zeros;
- process-local;
- invalid after shutdown/restart;
- not serialized as durable Controller state;
- never derived from a raw pointer;
- never reused as a persistent semantic UUID.

For manual tests, a fresh process makes low handles predictable. Use that property for operator copy/paste convenience, not as a production semantic guarantee.

---

## 5. Protocol transport and writer boundary

### 5.1 Initial transport

- stdin: requests;
- stdout: protocol responses/events only;
- stderr: human/operator logs;
- newline-delimited UTF-8 JSON;
- bounded message size.

### 5.2 Single writer

Never let a libobs callback print protocol JSON itself. Callbacks may occur on arbitrary libobs/plugin threads. They must normalize into the event system; the protocol output path preserves ordering.

Why this matters:

- prevents interleaved JSON lines;
- makes response-before-command-event enforceable;
- preserves sequence ordering;
- centralizes subscription and overflow behavior;
- prevents logging from corrupting protocol framing.

### 5.3 Future transport

A future pipe/socket/etc. transport can be considered only as a transport for the same semantic protocol. It must not become an alternate API with different behavior.

---

## 6. Revision model

Revision is the Controller’s optimistic-concurrency/canonical-state synchronization primitive.

### 6.1 Canonical mutation

A successful command that changes externally visible canonical engine state should consume exactly one revision, even if several semantic events describe the result.

Example accepted Task 8:

```text
source.replaceSettings response  revision 2
source.settingsChanged           revision 2
source.dimensionsChanged         revision 2
```

Not:

```text
response rev 2
settingsChanged rev 3
dimensionsChanged rev 4
```

when all changes are command-owned effects of the same mutation.

### 6.2 Unrelated async mutation

If source B independently changes while command A is settling, B gets a separate revision after A. Never absorb unrelated changes merely because capture is active.

### 6.3 Transient operations

Operations such as Task-9 input delivery do not represent persistent engine state and therefore do not consume a revision. Later examples likely include some preview/transient controls. Decide explicitly per method.

### 6.4 Telemetry

High-frequency values are telemetry, not state revisions. Examples:

- playback position ticks;
- audio meters;
- frame/render statistics;
- pointer movement;
- rapidly changing performance counters.

Telemetry should be opt-in/subscription-aware and bounded/coalesced as appropriate.

### 6.5 `ifRevision`

Only canonical mutating methods accept it. Common v2 guard behavior:

- absent => execute against current state;
- equal current revision => proceed;
- stale => `revision_conflict`, include expected/actual revision, do not mutate;
- supplied to non-mutating method => `bad_request`.

---

## 7. Event model

### 7.1 State events

State events represent canonical state transitions and carry the revision at which that state became visible.

### 7.2 Sequence

Events also have a monotonic delivery sequence (`seq`). Sequence and revision are different concepts: multiple events may share a revision.

### 7.3 Response ordering

For command-owned events:

1. execute/settle mutation;
2. determine/commit revision;
3. write response;
4. publish/write command-owned events with that same revision.

Controller can therefore update request state before consuming events describing the command result.

### 7.4 Subscriptions

Controller subscribes to event names/patterns. Exact names and namespace wildcards are supported by current event system. Telemetry opt-in should remain explicit.

### 7.5 Overflow

If state-event delivery cannot preserve incremental truth, emit/require `session.resyncRequired`. Silent loss is forbidden.

---

## 8. Plugin extensibility and generic properties

OBS plugins are a major reason this API should be semantic and data-driven.

`properties.*` is a foundation namespace, not a minor convenience. It bridges `obs_properties_t` metadata so the Controller can construct settings UIs for plugin source/filter/etc. objects without hard-coding every plugin.

Future namespaces should reuse generic properties for configuration rather than inventing per-plugin UI schemas unless a plugin exposes semantics that genuinely require dedicated protocol support.

Plugin IDs may be versioned/platform-specific. Discover them from the engine. Do not hard-code an example ID when discovery provides the actual current ID.

---

## 9. Module/package security model

Current host security design includes:

- no TCP/HTTP/WebSocket listener;
- Controller launches process with redirected standard handles;
- safe module allowlist;
- all expected Windows runtime modules in the branch allowlisted by default;
- explicit `--plugin=NAME` adds/requires a module for a launch;
- missing optional/default modules do not necessarily fail startup;
- required baseline modules do;
- restricted Windows DLL search behavior;
- working directory pinned to engine executable directory before `obs_startup`;
- `win-capture` capture-only mode by default; Game Capture hooks/updater disabled unless explicitly enabled.

When adding plugins for CI, stage them explicitly and keep them outside the normal install package.

---

## 10. Concurrency/lifetime philosophy

libobs is asynchronous and callback-heavy. Do not assume request-thread call/return equals completion of observable plugin state.

For every new namespace ask:

1. Does the libobs call synchronously mutate state?
2. Can plugin work be deferred to video/audio/worker thread?
3. Which signal/callback represents observable completion?
4. Can unrelated callbacks race during the command?
5. How are command-owned callbacks distinguished from unrelated state?
6. What happens on timeout?
7. What happens on queue overflow?
8. What happens if the object is removed during callback?
9. What references/weak references keep callbacks safe?
10. Which locks are held when revision is committed?
11. Could lock ordering invert with a libobs signal lock?
12. Can callbacks arrive during shutdown?

Task 8 is the canonical example of why these questions matter.

---

## 11. Semantic namespace roadmap architecture

The roadmap is intentionally layered:

1. protocol/runtime foundations;
2. source/plugin introspection and mutation;
3. source behavior (interaction, media, filters);
4. complete composition (scenes/items/canvases/program/preview/studio/transitions);
5. audio/hotkeys;
6. encoder/service/output primitives;
7. product output workflows (recording/streaming/replay/virtual camera);
8. capture utilities/telemetry/runtime/extensions;
9. reconstruction/schema/SDK/fuzzing/stress/lifetime/recovery/security/licensing/packaging/matrix;
10. Protocol v2 freeze.

Do not skip foundational namespaces simply because a later UI feature is more visible. The goal is a stable complete control plane, not a collection of frontend shortcuts.

---

## 12. What must never cross the boundary

Never expose these as normal protocol values:

- `obs_source_t *`, `obs_scene_t *`, `obs_sceneitem_t *`, etc.;
- HWND or platform window pointer;
- graphics texture/device pointer;
- plugin C callback pointer;
- `WPARAM`/`LPARAM`-style native message transport;
- process addresses;
- C++ object pointers;
- unbounded arbitrary plugin memory;
- raw file handles/OS handles unless a future carefully designed IPC primitive explicitly requires a secure duplicated-handle contract.

Task 17 D3D11 shared texture is an explicit future exception category that will require a designed handle-sharing contract, not a raw pointer escape hatch.

---

## 13. Canonical state vs telemetry decision table

Use this table as a starting point; verify per namespace.

| Concept | Default classification | Why |
|---|---|---|
| source created/removed/name/settings/dimensions | canonical state | Controller model must reconcile |
| interaction focus/mouse/key delivery | transient command | input delivery is not durable engine state |
| media play/pause/stop/seek command result | canonical runtime state when state actually changes | Controller UI must know transport state |
| media playback position ticks | telemetry | high-frequency, unsuitable for revision churn |
| scene/item structure/transforms | canonical state | composition model |
| audio mute/volume/sync offset/monitor mode | canonical state | user-controlled runtime configuration |
| audio meters | telemetry | high-frequency signal |
| recording/streaming lifecycle | canonical runtime state | critical output state |
| bitrate/dropped-frame counters | telemetry | high-frequency stats |
| preview mouse/UI selection | Controller/UI state | not engine canonical state unless explicitly modeled |

---

## 14. Compatibility/versioning philosophy

- Capabilities advertise supported semantic features.
- Additive method/capability growth is preferred.
- Do not silently change existing method meaning.
- Namespace-specific schema docs should pin concrete JSON fields/limits.
- Formal machine-readable schema is a later roadmap task, but current code/doc/tests must already behave as if compatibility matters.
- Plugin/source IDs can vary; semantic metadata and discovery should shield Controller code from unnecessary hard-coding.

---

## 15. Definition of architectural success

The architecture succeeds when a private frontend can be completely responsible for product UX/persistence while `obs-engine.exe` is completely responsible for safe runtime libobs execution, and the only coupling is a documented, versioned, testable semantic protocol whose revisions/events let the Controller maintain an accurate model under asynchronous plugin behavior, crashes, and extension variability.

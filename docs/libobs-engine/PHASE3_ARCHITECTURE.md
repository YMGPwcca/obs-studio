# Engine Protocol v2 Phase 3 Architecture

Status: AUTHORIZED / IN PROGRESS on `phase3-output-stack`

This document is the Phase-3 architecture checkpoint for Tasks 21–30. It is
based on the accepted Phase-2 runtime at `397154c95cca8c65b961ce3d517dba5ffe1039c9`,
the checked-out libobs implementation, and the explicit Phase-3 decisions in
the task authorization. It is a design and ownership record, not an acceptance
claim. Tasks 21–30 remain IN PHASE REVIEW until their implementation,
regression, package, physical, and independent-review gates are complete.

## Scope and source-verified constraints

Phase 3 adds these first-class runtime namespaces, in dependency order:

```text
audio -> hotkey -> encoder -> encoderGroup -> service -> output
                                                       |
                         recording / streaming / replayBuffer / virtualCamera
```

The Controller still owns durable project state, persistent UUIDs, account
policy, output destinations, and reconstruction. The GPL engine owns live
libobs objects and ephemeral decimal-string handles. The only Controller API
remains the existing newline-delimited JSON Engine Protocol over redirected
stdio.

The current libobs source audit fixes several boundaries that the older
protocol draft did not pin:

* Source audio controls store user-facing volume/mute state immediately, but
  apply audio actions to the audio mix asynchronously. Volume and balance are
  mutable through signal calldata, so the engine reads canonical values back
  after every mutation.
* `obs_volmeter_t` owns the audio callback path. Meter callbacks run from the
  audio path and must copy only bounded finite samples into a non-blocking,
  coalescible engine handoff; they never serialize JSON or wait for stdout.
* libobs hotkey IDs are sequential process-local `size_t` values. The wire
  identity is therefore registerer type/handle plus name and pair metadata;
  raw IDs are private lookup data only.
* A video encoder is not initialized by creation. Its plugin `create` callback
  runs when an output initializes encoders, and it needs a valid Canvas video
  context before that point. Audio encoder mixer selection is a creation-time
  zero-based libobs field exposed as a one-based protocol `audioTrack`.
* Active encoder updates are deferred to the encode/GPU path through
  `reconfigure_requested`; libobs has no public completion signal. A private
  tracked update bridge is required before an active update can be reported as
  settled.
* `obs_encoder_group_t` holds strong encoder references, permits one group per
  encoder, and defers destruction while members are active. The engine refuses
  non-empty or active group removal and never invalidates a group handle before
  its members are detached.
* A Service has one libobs output back-reference and an active flag with no
  public snapshot getter. The engine is the relationship owner and uses
  `obs_output_get_service` plus its own map to report the binding. Service
  connect-info callbacks are an internal output-start path, never a generic
  response path.
* Output start/stop/reconnect operations emit several raw signals from
  different threads. `starting`/`stopping` are intent/intermediate signals;
  `start`, `stop`, `reconnect`, and `reconnect_success`, together with live
  `obs_output_active`/`obs_output_reconnecting` snapshots, are normalized by a
  generation-aware observer into the canonical state machine below.
* The packaged Windows virtual-camera output is a raw-video output. The
  `win-dshow` module registers `virtualcam_output` only when the installed
  virtual-camera COM backend is present. The engine can bind the output to the
  Main Canvas video or an engine-owned private Canvas video mix for a semantic
  target; it never exposes a DirectShow pointer or sends frames over NDJSON.

## Process and object graph

```text
Controller durable graph / UUIDs / policy
                    |
                    | one Engine Protocol v2 stdio channel
                    v
Engine runtime registry
  Audio source handles + meter sessions
  Hotkey semantic identities -> private libobs IDs/pairs
  Encoder handles -----------+------------------------+
      | video input: Canvas  | audioTrack: 1..MAX_AUDIO_MIXES
      v                       v
  EncoderGroup (strong refs; one group per Encoder)
      |
  Service handles (one output back-reference)
      |
  Output handles (canonical lifecycle owner)
      +--> Recording role
      +--> Streaming role
      +--> Replay Buffer role
      +--> Virtual Camera role / raw video target mix

Existing Phase-2 graph remains authoritative:
  Canvas -> Scene -> Item -> Source -> Program / Preview -> PreviewOutput
```

No role creates an invisible duplicate Encoder, Service, or Output. The one
exception is `virtualCamera.configure`, which may create its required
`virtualcam_output`; that Output is immediately registered, listed, and marked
`managedBy: "virtualCamera"`.

## Ownership and lifetime table

| Runtime object | Engine owner | libobs strong references | Relationship owner | Removal policy |
| --- | --- | --- | --- | --- |
| Encoder | `encoders_` map | one engine reference; Outputs and Groups add their own refs | Engine map plus libobs Output/Group | reject while Output-bound, grouped, active, or pending unsafe settlement |
| EncoderGroup | `encoder_groups_` map | libobs group owns strong member refs | Engine map; Encoder records membership | detach every member first; reject non-empty/active removal |
| Service | `services_` map | one engine reference; Output stores relationship pointer | Engine map and Output binding | reject while bound or active; clear Output back-reference before release |
| Output | `outputs_` map | one engine reference; bound Encoder refs and Service relationship | Engine map; role maps | reject while active/starting/reconnecting/stopping or role-assigned |
| Recording role | one optional role record | no extra libobs object | role record points to existing Output handle | `recording.unconfigure` only when idle |
| Streaming role | one optional role record | no extra libobs object | role record points to existing Output handle | `streaming.unconfigure` only when idle |
| Replay role | one optional role record | no extra libobs object | role record points to existing Output handle | `replayBuffer.unconfigure` only when idle |
| Virtual Camera role | role record plus optional private target Canvas | managed Output is a normal Output entry | role record points to Output and target binding | unconfigure stops, detaches, and may destroy only its managed Output |
| Hotkey observer | `HotkeyV2State` | no ownership of registerer; private IDs are validated each use | semantic identity resolves to live registerer | disappear with registerer; no callback after retirement |
| Audio meter session | `AudioMeterSubscription` map | owns one `obs_volmeter_t`, which attaches to Source | session-local source handle and token | detach callback before Source release or session close |

Every exposed object gets a monotonically allocated canonical decimal handle.
Handles are never reused during one process and are never persisted as
Controller identity. Every callback stores a lifetime generation and uses a
weak libobs reference where the callback may outlive the engine map entry.

## Audio model (`audio.*`)

### Source controls

Audio identity is the existing Source handle; Phase 3 does not add an
AudioSource object. A source is an audio target only when its live output flags
include `OBS_SOURCE_AUDIO` (or another explicitly audited composite-audio
capability). `audio.get` returns user/canonical source state:

```text
source, muted, volumeMul, volumeDb, balance, syncOffsetNs,
monitoringEnabled, tracks, pushToTalk{enabled,delayMs},
pushToMute{enabled,delayMs}, speakerLayout
```

`volumeMul` and `volumeDb` are finite except that zero multiplier is represented
by the documented `volumeDb: null` convention for negative infinity. The engine
uses `obs_mul_to_db`/`obs_db_to_mul` semantics for conversion, validates finite
inputs, calls the source API once, and reads `obs_source_get_volume` back. A
request expressed in dB still emits only one `audio.volumeChanged` event.

Balance is one normalized `[0,1]` value. Current libobs applies balance only
to stereo source audio through its balancing path; unsupported layouts are
rejected or reported as unsupported before claiming a changed canonical value.
Sync offset is a signed nanosecond integer with an explicit safe protocol bound
below the int64 limit, and is read back after mutation. Logical track numbers
`1..MAX_AUDIO_MIXES` are translated to the internal mixer bitmask, canonicalized
sorted and unique, and never exposed as the primary wire field.

Monitoring uses `obs_source_get_monitoring_enabled` and
`obs_source_set_monitoring_enabled`; deprecated monitoring-type values are not
part of the contract. Sources with `OBS_SOURCE_DO_NOT_SELF_MONITOR` report the
restriction and do not pretend a monitoring mutation succeeded. PTT/PTM are
compound `{enabled, delayMs}` updates. Because libobs emits their settings
signals while holding the source audio mutex, observer callbacks consume the
calldata values and do not call a getter that could take that mutex.

Audio source signals are normalized by a dedicated observer. Command-owned
callbacks are captured before the revision guard and share the command
revision; external mute/volume/balance/sync/monitor/track/gating changes receive
an independent revision. The observer never writes stdout from the audio
thread. Existing Source removal ordering detaches all meter and audio
observers before the engine releases the Source reference.

### Meter sessions

`audio.subscribeMeters` creates a session-local token with a bounded source
list, `maxHz`, and `peakMode` (`sample` or `truePeak`). Peak mode is set on the
subscription's `obs_volmeter_t`; there is no global `audio.setPeakMeterMode`.
The token consumes no mutation revision. A volmeter callback copies at most
`MAX_AUDIO_CHANNELS` finite values into a bounded per-token/latest-sample
handoff. Publication is telemetry (`telemetry:true`), opt-in, coalescible and
lossy. `audio.unsubscribeMeters`, Source removal, and shutdown synchronize
callback detachment before freeing the callback context.

## Hotkey model (`hotkey.*`)

The protocol exposes registered libobs actions, not an operating-system input
surface. A hotkey identity contains:

```text
registerer: frontend | source | output | encoder | service
registererHandle: canonical handle when applicable
name: exact libobs registration name
pairPartner: semantic identity when this is one side of a pair
```

The underlying `obs_hotkey_id`/pair ID remains private and is resolved against
the current registry on every action. Private protocol Sources do not
automatically receive libobs Source hotkeys because upstream refuses to
register a Source hotkey for a private source; any registered source hotkeys
are still discoverable without exposing pointers.

Bindings use `obs_key_t` names and the four libobs semantic modifier bits
(`shift`, `control`, `alt`, `command`). Inputs are normalized in deterministic
modifier order, duplicate combinations are removed, and the whole replacement
set is validated before `obs_hotkey_load_bindings` is called. Export/import is
runtime binding transfer, not durable project persistence.

`hotkey.trigger` addresses one currently registered identity and uses the
engine's controlled libobs callback-routing path. It never calls
`obs_hotkey_inject_event`, `SendInput`, `PostMessage`, or any raw virtual-key
API. Trigger observation is an ephemeral `hotkey.triggered` notification; if
the callback changes Source audio or another canonical object, that namespace
owns the resulting real state event and revision.

Background capture is the explicit libobs setting, with one canonical
`hotkey.backgroundCaptureChanged` event. Callback routing is disabled and
detached before registerer or engine shutdown, and a stale semantic identity
returns `not_found` rather than reaching a freed callback.

## Encoder and EncoderGroup model

### Encoder input and initialization

`encoder.create` requires `type` (`video` or `audio`) and a registered kind.
Video encoders are attached to a live Canvas video context while inactive;
`videoInput` is a tagged `{type:"canvas", canvas:"..."}` object. The Main
Canvas is the normal Program mix. Audio encoders receive a one-based immutable
`audioTrack` at creation, translated to libobs's zero-based mixer index.

Creation only proves a libobs context exists. `encoder.getState` separately
reports `created`, `initialized`, and `active`. Initialization is proven only
when `obs_encoder_initialize` succeeds, normally during Output start. A
hardware kind is advertised as registered/capable only from its actual libobs
registration and capability metadata; hardware availability is reported as
unknown/unavailable until a positive runtime initialization path proves it.

Scaling and ROI are video-only and capability-gated. Scaled dimensions are
bounded positive values and can change only while libobs permits the encoder to
remain uninitialized/inactive. ROI rectangles are checked against effective
encoder dimensions and priority `[-1,1]` before `obs_encoder_add_roi`.

### Tracked settings updates

Inactive updates use a private serial-aware bridge that calls the plugin update
directly and records the boolean result. Active updates use a private
`EncoderMutationSerial` associated with the encoder generation and desired
settings. `obs_encoder_update` applies the settings object immediately but the
plugin callback may run later in `do_encode`; the private bridge emits an
engine-only completion signal with serial and success at that actual update
point. The Engine waits on a condition variable with a bounded deadline,
canonicalizes the settings from the live object, and commits one revision only
after settlement. A timeout or ambiguous plugin result forces resync/uncertain
state; it is never reported as an ordinary no-op.

For grouped encoders, the tracked update is settled only after libobs's group
frame-boundary logic has caused the participating active encoders to reach the
same reconfigure request. An unrelated encoder serial cannot settle the
request. Group update metadata remains engine-private.

### Group ownership

The Engine tracks membership explicitly because public libobs exposes no safe
group enumeration. `obs_encoder_set_group` is called only while both the
encoder and group are inactive. A group owns strong references to every member;
the Engine keeps its own encoder references. Adding to a second group, removing
while active, or removing a grouped Encoder returns `object_in_use`/`busy`.

Membership mutations publish `encoderGroup.changed` and
`encoder.groupChanged` at one revision.

## Service and secret model (`service.*`)

Service kind discovery uses `obs_enum_service_types`, defaults/properties use
the existing generic properties bridge, and runtime Service handles own one
strong `obs_service_t *`. The final read-only recommendation method is
`service.getEncoderRecommendations`; it clones supplied video/audio Encoder
settings, calls `obs_service_apply_encoder_settings`, and returns the clones
without mutating live Encoders.

Secret classification is metadata-first: known connect-info fields
(`stream ID/key`, password, username where provider-sensitive, encryption
passphrase, bearer token) plus `OBS_TEXT_PASSWORD` property metadata are
sensitive. `service.getSettings`, generic properties, events, output errors,
diagnostics, and snapshots return sanitized settings and presence metadata, not
placeholder secret strings. Patch omits a secret to preserve it; explicit
clear uses a documented clear member; replace has exact whole-settings semantics
while still never echoing the accepted value. Only the internal Output start
path may read `obs_service_get_connect_info`/provider accessors.

One centralized redactor is used for all Phase-3 strings and error paths. A
generated sentinel test scans stdout, stderr, event data, output last-error,
and shutdown diagnostics; the sentinel is never stored in source or printed in
the final report.

## Output state machine (`output.*`)

The Engine owns one observer per Output. The observer stores handle, lifetime
generation, lifecycle generation, command capture state, `active`, `starting`,
`stopping`, `reconnecting`, `paused`, last stop code, and a sanitized last
error. It holds a weak Output reference and copies only bounded semantic
payloads from callbacks.

```text
idle --output.start--> starting --start/activate--> active
  ^                       |                         |
  |                       +--stop/error------------+
  |                                                   |
  +<--stop/deactivate-- stopping <--output.stop-------+
                              |
                 active <-> reconnecting

paused is orthogonal to active.
```

`output.start` establishes capture before calling libobs. A synchronous
rejection with no state change is a zero-revision error. A successful immediate
activation commits one command revision and emits `output.started` after the
response. An accepted pending start commits `starting` and emits
`output.starting`; later actual activation is a separate lifecycle revision.
`output.stop` is a zero-revision no-op when idle, otherwise uses the same
starting/stopping/idle normalization. `forceStop` takes the immediate libobs
path but does not create a second lifecycle owner.

Raw signals are not forwarded verbatim. A callback is accepted only when its
weak reference and lifecycle generation still match the live observer. A stop
from an old generation cannot terminate a new start; a reconnect callback after
removal is ignored. Observer disconnection drains libobs signal callbacks before
the Output entry or weak state is released.

Output settings, Service/Encoder bindings, delay, and reconnect policy are
inactive-only by default and emit one consolidated
`output.configurationChanged` event per actual mutation. Void libobs setters
are followed by readback and compatibility validation; a failed or ignored
setter is not claimed as success. The reconnect policy is Engine-owned because
libobs exposes setters but no public getter; its canonical fields are
`enabled`, `retryCount`, and `retryDelaySeconds`.

Output stats are finite read-only snapshots (`totalBytes`, `totalFrames`,
`droppedFrames`, `congestion`, `connectTimeMs`) and do not start telemetry.
`output.getLastError` passes through the common redactor. Caption submission is
not part of Phase 3 and `output.sendCaption` is not advertised.

## Convenience roles

Recording, Streaming, Replay Buffer, and Virtual Camera contain only role
configuration and semantic side effects. Their underlying Output remains
visible through `output.list`/`output.get`; their `getState` aggregates that
Output's state. Role lifecycle is never duplicated as
`recording.started/stopped`, `streaming.started/stopped`, or
`replayBuffer.started/stopped`.

Role assignment is a canonical mutation with a unique `*.configChanged` event.
`*.unconfigure` requires the Output to be idle and leaves a normal Output alive.
Virtual Camera may destroy its specially managed Output after unconfigure only
after stopping, detaching, and disconnecting its observers.

### Recording

The initial real packaged path is an explicit encoded file Output, preferably
`mp4_output`/`ffmpeg_muxer` according to supported container requirements. File
paths are UTF-8, NUL-free, canonicalized, bounded, explicitly overwrite
controlled, and rejected for dangerous Windows device namespaces. Split and
chapter call only audited known Output procedures (`split_file` and
`add_chapter` where present); missing procedures return
`unsupported_capability`. Final paths come from Output/plugin signals or
readback, not from the request alone. `fileFinalized`, `fileChanged`, and
`chapterAdded` are unique role events.

### Streaming

Streaming binds a Service-required existing Output (the real packaged RTMP or
another audited network Output). Start validates Service presence, secret
presence without exposing its value, Encoder compatibility, media contexts and
Output state. Local loopback acceptance must use a real packaged network
Output, a pinned test-only local receiver, and a generated fake key; a
synthetic Output alone is insufficient. Reconnect attempts are coalesced into
the Output lifecycle revisions.

### Replay Buffer

Replay binds the packaged `replay_buffer` Output. `save` calls the audited
`save` procedure and treats `saved` as asynchronous unless the actual callback
proves completion. The saved path is obtained from the plugin's
`get_last_replay` procedure or observed file result. A repeated save, save/stop
race, path failure, or shutdown never fabricates a `replayBuffer.saved` event.

### Virtual Camera

`virtualCamera.getCapabilities` separates API implementation, registered
Output kind, installed/ready COM backend, and currently active/busy state.
Default target is Program. Program uses Main Canvas video directly. Preview,
Scene, Source, and Canvas targets use an engine-owned private Canvas/view mix
when the current libobs video model supports it; target changes while active
are rejected with `busy` unless the selected raw video context can be swapped
safely without restarting. Target removal clears the target and either emits a
blank/unavailable target state or stops cleanly, according to the proven
backend path. A physical DirectShow/Media Foundation consumer must receive
known-color frames; Output start alone is not acceptance.

## Event ownership and revision policy

Canonical Output lifecycle events are the only lifecycle owner for every role:

```text
output.starting output.started output.stopping output.stopped
output.paused output.reconnecting output.reconnected output.error
```

Phase-3 canonical events include:

```text
audio.volumeChanged audio.muteChanged audio.balanceChanged
audio.syncOffsetChanged audio.monitoringChanged audio.tracksChanged
audio.gatingChanged audio.monitoringDeviceChanged
hotkey.bindingsChanged hotkey.backgroundCaptureChanged
encoder.created encoder.removed encoder.renamed encoder.settingsChanged
encoder.inputChanged encoder.scalingChanged encoder.roiChanged
encoder.bindingChanged encoder.groupChanged encoder.activeChanged
encoderGroup.created encoderGroup.removed encoderGroup.changed
service.created service.removed service.renamed service.settingsChanged
service.bindingChanged service.activeChanged
output.created output.removed output.renamed output.configurationChanged
recording.configChanged recording.fileFinalized recording.fileChanged
recording.chapterAdded streaming.configChanged
replayBuffer.configChanged replayBuffer.saved
virtualCamera.configChanged virtualCamera.targetChanged
```

Telemetry/ephemeral notifications are:

```text
audio.meter (telemetry:true)
hotkey.triggered (ephemeral notification)
```

One successful externally visible canonical mutation consumes one global
revision. Command-owned event sets share that revision and are queued after the
response. Unrelated callbacks receive later revisions. Meters, hotkey trigger
notifications, frame/packet counters and retry ticks do not consume mutation
revisions. Any queue or ownership uncertainty forces the existing
`session.resyncRequired` boundary.

## Async settlement and shutdown

All Phase-3 callback bridges use bounded condition variables or explicit
pending state, never sleeps. Active Encoder updates settle by private serial;
group updates wait for group boundary evidence; Output start/stop/reconnect
settle through generation-aware observers; recording finalization and replay
saving settle through plugin file/saved signals; Virtual Camera target and
consumer state are detached before object release.

The shutdown order is:

```text
1. reject new Phase-3 mutations
2. stop/force-stop active role Outputs under a bounded deadline
3. drain Output stop/reconnect callbacks and mark uncertainty if needed
4. disconnect Output observers
5. unconfigure roles
6. release Output handles
7. detach Service relationships and release Services
8. detach Encoder relationships
9. detach/destroy EncoderGroups
10. release Encoders and finish tracked-update waiters
11. detach all audio meters
12. disable hotkey routing and callback observation
13. continue accepted Phase-2 teardown
14. shut down libobs
```

No observer state is freed until its signal/callback connection is disconnected
and any in-flight callback has retired. A bounded shutdown timeout records only
sanitized diagnostics and takes the safest forced-stop path available.

## Capability and acceptance strategy

Broad capabilities (`audio.v1`, `hotkey.v1`, `encoder.v1`,
`encoderGroup.v1`, `service.v1`, `output.v1`, `recording.v1`, `streaming.v1`,
`replayBuffer.v1`, `virtualCamera.v1`) are advertised only when their full
implemented method contracts are present in the current build. Dynamic kind
availability is separately discovered from libobs registration and positive
runtime checks. In particular, AMF/NVENC/QSV availability is not inferred from
a DLL filename, and Virtual Camera readiness is not inferred from
`win-dshow.dll` alone.

Deterministic CI-only fixtures are allowed for audio meters, Encoder update and
group timing, Service redaction, and Output lifecycle/reconnect/error races.
Each fixture is `EXCLUDE_FROM_ALL`, has no install rule, is explicitly staged
only by its test lane, and is asserted absent from the production package.
They supplement, but do not replace, physical acceptance using the exact hosted
Engine artifact:

* physical audio source/meter/control behavior;
* software Encoder and any positively available hardware Encoder initialized by
  a real Output;
* real recording file with video/audio and finalization;
* real packaged network Output to a local loopback receiver and reconnect;
* real Replay Buffer save and parseable file;
* real Windows Virtual Camera consumer receiving known-color frames;
* concurrent Recording + Streaming, and Phase-2 Preview/Studio regression
  while Outputs are active.

The final Phase-3 runtime SHA is frozen before final hosted acceptance. All
Task-21–30 lanes and all Tasks 1–20 regressions must use that same runtime SHA;
documentation-only evidence may follow without changing runtime-bearing files.

## Explicit Phase-3 exclusions

The following remain outside this authorization: Task 31 and later namespaces,
caption submission, general stats subscriptions, screenshot/binary transport,
formal schema, Controller SDK, fuzzing, broad stress/lifetime/device-loss/
crash/security/license phases, and reconstruction work. Only minimal test
infrastructure needed to prove Tasks 21–30 may touch adjacent code.

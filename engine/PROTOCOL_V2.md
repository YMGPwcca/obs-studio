# obs-engine Protocol v2

Status: design contract for implementation on `windows-minimal`.

This document defines the stable semantic boundary between a proprietary controller/frontend and the GPL `obs-engine` process. The protocol exposes application-facing libobs functionality, not raw libobs C symbols or plugin implementation callbacks.

## 1. Architectural boundary

The controller owns persistent/product state:

- project files and scene collections;
- profiles;
- undo/redo;
- UI layout, selection, snapping and editor state;
- templates, accounts, automation and product business logic;
- persistent object UUIDs and desired runtime state.

`obs-engine` owns live libobs/runtime state:

- libobs initialization and shutdown;
- loaded modules;
- sources, scenes, scene items, filters and transitions;
- audio mixer state;
- encoders, services and outputs;
- streaming, recording, replay buffer and virtual camera runtime objects;
- preview/render resources;
- hotkeys, media controls, properties and plugin-facing runtime bridges.

No libobs pointer crosses the process boundary. Engine handles are opaque runtime-only identifiers and MUST NOT be persisted by the controller.

## 2. Transport

Protocol v2 is transport-independent. The initial Windows implementation continues to use redirected stdin/stdout with one UTF-8 JSON object per line.

- stdin: requests from controller to engine;
- stdout: responses and events only;
- stderr: logs only;
- maximum JSON message size: implementation-defined and advertised by `session.hello`;
- binary/GPU resources are transported out-of-band and referenced by protocol metadata.

A future authenticated local transport may carry the same protocol, but protocol semantics MUST NOT depend on TCP, HTTP or WebSocket.

## 3. Versioning

The protocol uses independent major/minor versions.

- major changes may break compatibility;
- minor changes are additive;
- method behavior MUST NOT silently change incompatibly within a major version;
- experimental capabilities are versioned independently and explicitly marked experimental.

Example capability names:

- `source.v1`
- `properties.v1`
- `audio.v1`
- `preview.d3d11SharedTexture.v1`
- `canvas.v1.experimental`

Controllers MUST feature-detect capabilities rather than infer support from libobs version alone.

## 4. Message envelopes

### 4.1 Request

```json
{
  "op": "request",
  "id": "r-42",
  "method": "source.create",
  "params": {}
}
```

`id` is chosen by the controller and MUST be unique among in-flight requests for the session.

Optional fields:

```json
{
  "ifRevision": 103,
  "timeoutMs": 5000
}
```

`timeoutMs` is advisory; the controller remains responsible for its own process-level timeout policy.

### 4.2 Success response

```json
{
  "op": "response",
  "id": "r-42",
  "status": {"ok": true},
  "revision": 104,
  "data": {}
}
```

### 4.3 Error response

```json
{
  "op": "response",
  "id": "r-42",
  "status": {
    "ok": false,
    "code": "not_found",
    "message": "source handle was not found",
    "details": {}
  },
  "revision": 104
}
```

Internal pointers, exception text, memory addresses and secrets MUST NOT be exposed in errors.

### 4.4 Event

```json
{
  "op": "event",
  "seq": 812,
  "revision": 104,
  "event": "source.settingsChanged",
  "data": {}
}
```

`seq` is a monotonically increasing session event sequence. `revision` is the engine mutation revision after the event's associated state transition.

High-frequency telemetry such as audio meters MAY be coalesced or dropped independently and MUST be identified as telemetry.

## 5. Revisions and concurrency

Every externally visible mutation increments the engine revision once after the mutation is committed.

Mutating requests MAY include `ifRevision`. If the supplied revision does not match, the engine returns `revision_conflict` without applying the mutation.

`session.batch` executes subrequests in order and increments the public revision once for the committed batch. V1 does not promise database-style rollback; a batch response reports each executed operation and whether execution stopped on failure.

## 6. Object identifiers and lifetime

All live objects use opaque unsigned 64-bit engine handles encoded as JSON integers where safe, or decimal strings if a future transport cannot preserve 64-bit integers exactly.

Object classes include:

- source;
- scene;
- sceneItem;
- filter;
- transition;
- canvas;
- previewOutput;
- encoder;
- encoderGroup;
- service;
- output;
- script.

Handles are unique for the lifetime of one engine process and are never reused during that process lifetime. Handles become invalid after process restart.

The controller SHOULD maintain persistent UUID -> current engine handle mappings and reconstruct engine state after restart.

## 7. Ownership and destruction

The engine owns all libobs references corresponding to exposed handles. Explicit remove/destroy operations release engine ownership only after dependent relationships are detached according to libobs requirements.

Removing an object MUST emit removal events before any subsequently queued event refers to an already-invalid handle.

Destroying a parent MUST either:

1. deterministically destroy/detach owned children and report those effects as events, or
2. reject the request with `object_in_use` when automatic detachment would be unsafe or ambiguous.

Exact behavior is documented per object type.

## 8. Threading and event delivery

libobs callbacks MUST NOT write directly to stdout.

Callbacks enqueue normalized engine events into a bounded thread-safe queue. A single protocol writer serializes all responses/events to stdout, preserving message integrity.

State-changing libobs callbacks generated synchronously by a request may be coalesced so the controller receives one canonical semantic event instead of duplicate implementation-detail events.

## 9. Error codes

Core error codes:

- `bad_request`
- `invalid_json`
- `message_too_large`
- `unsupported_method`
- `unsupported_capability`
- `not_found`
- `already_exists`
- `invalid_state`
- `object_in_use`
- `revision_conflict`
- `permission_denied`
- `restart_required`
- `not_available`
- `busy`
- `timeout`
- `obs_error`
- `internal_error`

Methods MAY define additional stable codes.

## 10. Session API

### `session.hello`
Returns protocol version, engine/libobs versions, process metadata, limits and capabilities.

Response fields include:

- `protocol.major`
- `protocol.minor`
- `engineVersion`
- `libobsVersion`
- `platform`
- `pid`
- `encoding`
- `maxMessageBytes`
- `capabilities[]`
- `revision`

### `session.authenticate`
Reserved for transports that require authentication. On inherited stdio this capability is normally absent.

### `session.getInfo`
Returns current session information.

### `session.ping`
Round-trip liveness probe.

### `session.subscribe`
Subscribes to event patterns. Supports exact names and namespace wildcards such as `source.*`.

Telemetry streams remain explicit opt-in.

### `session.unsubscribe`
Removes subscriptions.

### `session.getSubscriptions`
Returns effective subscriptions.

### `session.batch`
Executes ordered subrequests.

### `session.close`
Gracefully closes the controller session and shuts down the engine when the transport/session owns the process.

## 11. Engine API

Methods:

- `engine.getInfo`
- `engine.getCapabilities`
- `engine.getState`
- `engine.getLocale`
- `engine.setLocale`
- `engine.getVideoSettings`
- `engine.setVideoSettings`
- `engine.getAudioSettings`
- `engine.setAudioSettings`
- `engine.getVideoLevels`
- `engine.setVideoLevels`
- `engine.getGraphicsInfo`
- `engine.getRuntimePaths`
- `engine.getStats`
- `engine.getWarnings`
- `engine.shutdown`

Video/audio reset methods MUST fail with `busy`/`invalid_state` when libobs cannot reset while active outputs exist.

Events:

- `engine.ready`
- `engine.stopping`
- `engine.localeChanged`
- `engine.videoSettingsChanged`
- `engine.audioSettingsChanged`
- `engine.videoLevelsChanged`
- `engine.warning`

## 12. Module API

Methods:

- `module.list`
- `module.get`
- `module.getLoaded`
- `module.getFailed`
- `module.getMetadata`
- `module.getCapabilities`
- `module.enable`
- `module.disable`
- `module.rescan`
- `module.getSourceKinds`
- `module.getEncoderKinds`
- `module.getOutputKinds`
- `module.getServiceKinds`

Arbitrary `module.loadFromPath` is intentionally excluded from the normal controller API. Developer builds may expose it behind a separate unsafe/developer capability.

Module enable/disable SHOULD be restart-required unless libobs explicitly guarantees safe unload/reload semantics for the specific operation.

Events:

- `module.loaded`
- `module.failed`
- `module.stateChanged`

## 13. Generic properties API

The properties API converts libobs `obs_properties_t` into a neutral JSON schema. It MUST preserve dynamic behavior, not merely serialize a static initial form.

Methods:

- `properties.get`
- `properties.resolve`
- `properties.getListItems`
- `properties.invokeButton`
- `properties.validate`
- `properties.refresh`

Supported property types:

- bool;
- int;
- float;
- text (default/password/multiline/info);
- path (open/save/directory);
- list (editable/list/radio; int/float/string/bool values);
- color / colorAlpha;
- button / URL button;
- font;
- editableList;
- frameRate;
- group / checkableGroup.

Common property fields:

- `name`
- `type`
- `description`
- `longDescription`
- `visible`
- `enabled`
- type-specific constraints;
- `requiresRefresh` when modification callbacks alter the schema.

`properties.resolve` applies candidate settings to the property set, runs libobs modification callbacks, and returns the resulting normalized schema/settings without committing the target object's settings unless explicitly requested by the owning API method.

`properties.invokeButton` executes the plugin's property button callback within the engine and returns any refreshed schema and safe metadata. URL buttons return URL metadata; the engine MUST NOT cause the proprietary UI process to execute arbitrary plugin code.

## 14. Source API

Kind methods:

- `source.kindList`
- `source.kindGet`
- `source.kindDefaults`
- `source.kindProperties`

Runtime methods:

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

Source kind metadata includes semantic capability booleans derived from libobs flags, including video, audio, interaction, controllable media, deprecated, disabled, do-not-duplicate, monitoring restrictions and requires-canvas.

Events:

- `source.created`
- `source.removed`
- `source.renamed`
- `source.settingsChanged`
- `source.activeChanged`
- `source.showingChanged`
- `source.flagsChanged`
- `source.dimensionsChanged`

## 15. Interaction API

Methods:

- `interaction.focus`
- `interaction.mouseMove`
- `interaction.mouseButton`
- `interaction.mouseWheel`
- `interaction.key`
- `interaction.text`
- `interaction.reset`

Only sources advertising interaction support accept these calls.

Pointer coordinates and modifiers are expressed in source-local logical coordinates plus explicit button/modifier enums. The engine translates them to libobs interaction structures.

## 16. Media API

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

## 17. Filter API

Kind methods:

- `filter.kindList`
- `filter.kindDefaults`
- `filter.kindProperties`

Runtime methods:

- `filter.list`
- `filter.get`
- `filter.create`
- `filter.remove`
- `filter.rename`
- `filter.duplicate`
- `filter.getSettings`
- `filter.patchSettings`
- `filter.replaceSettings`
- `filter.setEnabled`
- `filter.getEnabled`
- `filter.setOrder`
- `filter.moveUp`
- `filter.moveDown`
- `filter.moveTop`
- `filter.moveBottom`

Events:

- `filter.created`
- `filter.removed`
- `filter.renamed`
- `filter.settingsChanged`
- `filter.enabledChanged`
- `filter.orderChanged`

## 18. Scene API

Methods:

- `scene.list`
- `scene.get`
- `scene.create`
- `scene.remove`
- `scene.rename`
- `scene.duplicate`
- `scene.getItems`
- `scene.getState`

Events:

- `scene.created`
- `scene.removed`
- `scene.renamed`

Scene item groups are owned by the `item.*` namespace. Scene transition
overrides are deferred until the Transition milestone; no temporary transition
identity is exposed by the Scene namespace. `scene.getItems` uses index `0` as
the bottom render layer and larger indexes as higher layers. Every Scene has a
Canvas identity; omitted `scene.create.canvas` selects Main Canvas.

## 19. Scene item API

Methods:

- `item.get`
- `item.create`
- `item.remove`
- `item.duplicate`
- `item.getTransform`
- `item.setTransform`
- `item.setPosition`
- `item.setScale`
- `item.setRotation`
- `item.setAlignment`
- `item.setBounds`
- `item.setBoundsAlignment`
- `item.setCrop`
- `item.setCropToBounds`
- `item.setVisible`
- `item.setLocked`
- `item.setOrder`
- `item.moveUp`
- `item.moveDown`
- `item.moveTop`
- `item.moveBottom`
- `item.setScaleFilter`
- `item.setBlendMode`
- `item.setBlendMethod`
- `item.createGroup`
- `item.ungroup`
- `item.addToGroup`
- `item.removeFromGroup`
- `item.getChildren`

`item.setTransform` is the canonical compound update and may contain position, rotation, scale, alignment, bounds type, bounds alignment, bounds dimensions, crop and crop-to-bounds.

Events:

- `item.created`
- `item.removed`
- `item.transformChanged`
- `item.visibilityChanged`
- `item.lockedChanged`
- `item.orderChanged`
- `item.blendChanged`

Crop, bounds, crop-to-bounds, and scale-filter state are part of the canonical
`item.transformChanged` payload. Group structure changes may also emit
`scene.itemsChanged`; this event is not an alias for an item state event.

## 20. Canvas API

Capability: `canvas.v1.experimental`.

Methods:

- `canvas.list`
- `canvas.getMain`
- `canvas.get`
- `canvas.create`
- `canvas.remove`
- `canvas.rename`
- `canvas.getVideoSettings`
- `canvas.setVideoSettings`
- `canvas.listScenes`
- `canvas.getChannel`
- `canvas.setChannel`
- `canvas.getFlags`

The protocol MUST isolate canvas instability behind this capability so controller compatibility does not depend on libobs canvas ABI details.

## 21. Program, preview and studio APIs

Program:

- `program.getScene`
- `program.setScene`

Preview:

- `preview.getScene`
- `preview.setScene`
- `preview.getInfo`

Studio:

- `studio.getEnabled`
- `studio.setEnabled`
- `studio.getTransition`
- `studio.setTransition`
- `studio.getTransitionDuration`
- `studio.setTransitionDuration`
- `studio.transition`

These are frontend semantics implemented by `obs-engine`; they are not required to map one-to-one to a single libobs function.

Events:

- `program.sceneChanged`
- `preview.sceneChanged`
- `studio.enabledChanged`
- `studio.transitionChanged`

## 22. Transition API

Kind methods:

- `transition.kindList`
- `transition.kindDefaults`
- `transition.kindProperties`

Runtime methods:

- `transition.list`
- `transition.get`
- `transition.create`
- `transition.remove`
- `transition.rename`
- `transition.getSettings`
- `transition.patchSettings`
- `transition.replaceSettings`
- `transition.getProperties`
- `transition.getDuration`
- `transition.setDuration`
- `transition.getState`

Events:

- `transition.created`
- `transition.removed`
- `transition.renamed`
- `transition.settingsChanged`
- `transition.durationChanged`
- `transition.started`
- `transition.ended`

`transition.progress` is an opt-in, lossy telemetry event. It is sampled at a
bounded rate, carries a finite normalized progress value, and does not consume
a mutation revision.

## 23. Preview output API

Primary Windows capability: `preview.d3d11SharedTexture.v1`, advertised only
when the live graphics backend is D3D11 with shared-texture support.

Methods:

- `previewOutput.create`
- `previewOutput.destroy`
- `previewOutput.setTarget`
- `previewOutput.resize`
- `previewOutput.setEnabled`
- `previewOutput.getInfo`
- `previewOutput.getSharedTexture`
- `previewOutput.releaseSharedTexture`

Targets:

- program;
- preview;
- scene;
- source;
- canvas.

The engine owns all graphics API entry/exit calls and `gs_*` objects. The controller receives only platform-safe share metadata/handles with explicit lifetime and synchronization rules.

Events:

- `previewOutput.created`
- `previewOutput.destroyed`
- `previewOutput.targetChanged`
- `previewOutput.resourceChanged`
- `previewOutput.enabledChanged`

## 24. Audio API

The protocol uses the current boolean monitoring model. Deprecated `monitoring_type` enum semantics are not part of v2.

Methods:

- `audio.get`
- `audio.setMute`
- `audio.toggleMute`
- `audio.getVolume`
- `audio.setVolume`
- `audio.setVolumeDb`
- `audio.setBalance`
- `audio.setSyncOffset`
- `audio.getMonitoringEnabled`
- `audio.setMonitoringEnabled`
- `audio.setTracks`
- `audio.getTracks`
- `audio.setPushToTalk`
- `audio.setPushToMute`
- `audio.subscribeMeters`
- `audio.unsubscribeMeters`
- `audio.listMonitoringDevices`
- `audio.getMonitoringDevice`
- `audio.setMonitoringDevice`

Meter subscriptions specify a maximum delivery rate and a per-subscription
peak mode. Meter events are telemetry and may be coalesced. The protocol uses
logical one-based audio tracks represented by bounded object entries because
the current libobs data-array container stores object values. Zero multiplier
is represented with a documented finite dB floor; NaN and infinity never cross
the protocol boundary.

Events:

- `audio.volumeChanged`
- `audio.muteChanged`
- `audio.balanceChanged`
- `audio.syncOffsetChanged`
- `audio.monitoringChanged`
- `audio.tracksChanged`
- `audio.gatingChanged`
- `audio.monitoringDeviceChanged`
- `audio.meter`

## 25. Hotkey API

Methods:

- `hotkey.list`
- `hotkey.get`
- `hotkey.getBindings`
- `hotkey.setBindings`
- `hotkey.clearBindings`
- `hotkey.trigger`
- `hotkey.getKeyName`
- `hotkey.getKeyCombinationName`
- `hotkey.getConflicts`
- `hotkey.getBackgroundCapture`
- `hotkey.setBackgroundCapture`
- `hotkey.export`
- `hotkey.import`

Events:

- `hotkey.bindingsChanged`
- `hotkey.backgroundCaptureChanged`
- `hotkey.triggered` (telemetry)

## 26. Encoder API

Kind methods:

- `encoder.kindList`
- `encoder.kindGet`
- `encoder.kindDefaults`
- `encoder.kindProperties`
- `encoder.kindCapabilities`

Runtime methods:

- `encoder.list`
- `encoder.get`
- `encoder.create`
- `encoder.remove`
- `encoder.rename`
- `encoder.getSettings`
- `encoder.patchSettings`
- `encoder.replaceSettings`
- `encoder.getProperties`
- `encoder.getVideoInput`
- `encoder.setVideoInput`
- `encoder.getCodec`
- `encoder.getType`
- `encoder.getDimensions`
- `encoder.getState`
- `encoder.setScaledSize`
- `encoder.setScaleFilter`
- `encoder.roi.list`
- `encoder.roi.add`
- `encoder.roi.remove`
- `encoder.roi.clear`

Encoder capability metadata includes deprecated/internal, texture encoding,
dynamic bitrate, ROI, scaling and multitrack dynamic bitrate support. Kind
metadata distinguishes registration/module state from actual runtime
compatibility; a DLL filename never proves hardware availability.

Video input is a semantic Canvas descriptor. Audio `audioTrack` is immutable
at creation and is one-based (`1..MAX_AUDIO_MIXES`); `encoder.setAudioMix` is
not part of the contract. Binary encoder extra/SEI data and high-frequency
stats are deferred until a bounded transport/namespace exists.

Events:

- `encoder.created`
- `encoder.removed`
- `encoder.settingsChanged`
- `encoder.inputChanged`
- `encoder.scalingChanged`
- `encoder.roiChanged`
- `encoder.groupChanged`
- `encoder.activeChanged`

## 27. Encoder group API

Methods:

- `encoderGroup.list`
- `encoderGroup.create`
- `encoderGroup.remove`
- `encoderGroup.add`
- `encoderGroup.removeEncoder`
- `encoderGroup.getEncoders`

Events:

- `encoderGroup.created`
- `encoderGroup.removed`
- `encoderGroup.changed`

## 28. Service API

Kind methods:

- `service.kindList`
- `service.kindDefaults`
- `service.kindProperties`

Runtime methods:

- `service.list`
- `service.get`
- `service.create`
- `service.remove`
- `service.rename`
- `service.getSettings`
- `service.patchSettings`
- `service.replaceSettings`
- `service.getProperties`
- `service.getProtocol`
- `service.getPreferredOutputKind`
- `service.getSupportedResolutions`
- `service.getMaxFps`
- `service.getMaxBitrates`
- `service.getSupportedVideoCodecs`
- `service.getSupportedAudioCodecs`
- `service.getEncoderRecommendations`
- `service.canConnect`

Service secrets such as stream keys/passwords MUST be redacted by generic
get/log/snapshot operations. Responses carry only secret-presence metadata;
there is no privileged secret-returning method in the stable protocol.

Events:

- `service.created`
- `service.removed`
- `service.settingsChanged`
- `service.renamed`
- `service.bindingChanged`
- `service.activeChanged`

## 29. Output API

Kind methods:

- `output.kindList`
- `output.kindGet`
- `output.kindDefaults`
- `output.kindProperties`
- `output.kindCapabilities`

Runtime methods:

- `output.list`
- `output.get`
- `output.create`
- `output.remove`
- `output.rename`
- `output.getSettings`
- `output.patchSettings`
- `output.replaceSettings`
- `output.getProperties`
- `output.setService`
- `output.getService`
- `output.setVideoEncoder`
- `output.setAudioEncoder`
- `output.getEncoders`
- `output.start`
- `output.stop`
- `output.forceStop`
- `output.getState`
- `output.setPaused`
- `output.getPaused`
- `output.setDelay`
- `output.getDelay`
- `output.setReconnect`
- `output.getReconnect`
- `output.getStats`
- `output.getLastError`
- `output.getSupportedCodecs`

Kind capabilities expose video/audio, encoded/raw, service requirement, multitrack video/audio and pause support.

Output encoder slots are explicit zero-based video/audio array indexes and are
distinct from the one-based logical audio-track identity used by `encoder.*`.
Encoder and service binding, settings, delay, and reconnect-policy mutations
require an inactive output. Encoder binding validates media type, codec,
input liveness, and the output's declared slot capability before reading the
canonical binding back from libobs. Service binding also validates the output
protocol.

`output.getState` reports `idle`, `starting`, `active`, `reconnecting`, or
`stopping`, plus pause, service, encoder-slot, delay, reconnect-policy,
last-stop-code, and sanitized-last-error state. Output lifecycle callbacks are
normalized into the events below; command-owned events use the command's
revision and asynchronous callbacks use a later independent revision. The
response is written before command-owned events. `output.sendCaption` is not a
stable Phase-3 method; caption submission is reserved for Task 32.

Stats include, where supported:

- total bytes;
- dropped frames;
- congestion;
- connect time;
- active/reconnect state.

Events:

- `output.created`
- `output.removed`
- `output.starting`
- `output.started`
- `output.stopping`
- `output.stopped`
- `output.paused`
- `output.reconnecting`
- `output.reconnected`
- `output.error`

## 30. Recording convenience API

Methods:

- `recording.getConfig`
- `recording.configure`
- `recording.unconfigure`
- `recording.start`
- `recording.stop`
- `recording.forceStop`
- `recording.pause`
- `recording.resume`
- `recording.togglePause`
- `recording.splitFile`
- `recording.addChapter`
- `recording.getState`
- `recording.getStats`
- `recording.getCurrentPath`
- `recording.getLastFile`

This API assigns an existing file-compatible Output; it does not create a
hidden Encoder or Output graph. Lifecycle remains owned by `output.*`, so
recording does not emit duplicate `recording.started` or `recording.stopped`
events. `recording.configure` accepts an optional absolute local `path`,
explicit `overwrite`, and explicit `createDirectory` policy. Paths are
UTF-8/NUL checked, canonicalized, bounded, and reject URLs and unsupported
Windows device namespaces. File splitting and chapters call only audited
built-in Output procedures and return `unsupported_capability` when the
selected Output does not provide them.

Recording-unique events are `recording.configChanged`,
`recording.fileChanged`, `recording.fileFinalized`, and
`recording.chapterAdded`. Current/final paths are reported only from Output
settings or observed file-change/finalization state; a request path is not
treated as proof of a file having been written.

## 31. Streaming convenience API

Methods:

- `streaming.getConfig`
- `streaming.configure`
- `streaming.unconfigure`
- `streaming.start`
- `streaming.stop`
- `streaming.forceStop`
- `streaming.getState`
- `streaming.getStats`
- `streaming.getService`
- `streaming.setService`
- `streaming.getReconnectState`
- `streaming.getLastError`

Streaming assigns an existing service-backed encoded Output and does not create
a hidden Output, Encoder, or Service. `streaming.start` validates that the
Output is live and protocol-compatible, has a bound initialized Service, has a
configured credential without exposing its value, and has usable media
encoders. Lifecycle remains owned by `output.*`; streaming emits only
`streaming.configChanged`. Service, reconnect, and last-error accessors
delegate to the canonical Output relationship/state and use the common secret
redactor.

## 32. Replay buffer API

Methods:

- `replayBuffer.getConfig`
- `replayBuffer.configure`
- `replayBuffer.unconfigure`
- `replayBuffer.start`
- `replayBuffer.stop`
- `replayBuffer.save`
- `replayBuffer.getState`
- `replayBuffer.getStats`
- `replayBuffer.getLastFile`

Events use `replayBuffer.*`.

## 33. Virtual camera API

Methods:

- `virtualCamera.getCapabilities`
- `virtualCamera.configure`
- `virtualCamera.unconfigure`
- `virtualCamera.start`
- `virtualCamera.stop`
- `virtualCamera.getState`
- `virtualCamera.setTarget`
- `virtualCamera.getTarget`

Capability-gated by packaged Windows virtual camera support.

Events use `virtualCamera.*`.

## 34. Screenshot API

Methods:

- `screenshot.captureProgram`
- `screenshot.captureSource`
- `screenshot.captureScene`
- `screenshot.captureCanvas`

Results use an out-of-band binary transfer token or a validated engine-managed output path. Arbitrary filesystem writes from untrusted protocol values are not allowed.

## 35. Caption API

Methods:

- `caption.sendText`
- `caption.sendCEA708`

Targets identify the relevant output/source where applicable.

## 36. Missing file API

Methods:

- `missingFile.scan`
- `missingFile.list`
- `missingFile.resolve`
- `missingFile.ignore`
- `missingFile.resolveAll`
- `missingFile.rescan`

Events:

- `missingFile.detected`
- `missingFile.resolved`
- `missingFile.ignored`

## 37. Stats API

Methods:

- `stats.get`
- `stats.subscribe`
- `stats.unsubscribe`
- `stats.getVideo`
- `stats.getAudio`
- `stats.getRendering`
- `stats.getOutput`
- `stats.getEncoder`
- `stats.getProcess`

Stats subscriptions are telemetry with bounded maximum rates and coalescing.

## 38. Runtime synchronization API

Methods:

- `runtime.getSnapshot`
- `runtime.applySnapshot`
- `runtime.validateSnapshot`
- `runtime.clear`
- `runtime.getRevision`
- `runtime.waitForRevision`

Snapshots are runtime reconstruction aids, not the application's persistent project format.

Snapshots MUST NOT include secrets by default and MUST NOT include opaque handles as stable identity. Snapshot entries may carry controller-supplied correlation IDs specifically for reconstruction.

## 39. Extension escape hatch

Capability: `extension.v1.unstable`.

Methods:

- `extension.listProcedures`
- `extension.callProcedure`
- `extension.listSignals`
- `extension.subscribeSignal`
- `extension.unsubscribeSignal`

This API exists for plugin functionality that cannot be represented by the standardized semantic APIs. Procedure/signal parameters must be converted to safe JSON-supported scalar/object types. Raw pointers, arbitrary calldata pointers and function addresses are forbidden.

The controller SHOULD prefer standardized APIs whenever available.

## 40. Script API

Optional capability, absent from the current minimal build unless scripting is restored.

Methods:

- `script.list`
- `script.load`
- `script.unload`
- `script.reload`
- `script.getSettings`
- `script.setSettings`
- `script.getProperties`
- `script.getDescription`

Script loading paths are constrained to controller/engine-approved roots and are not arbitrary remote filesystem execution primitives.

## 41. Sensitive data

The protocol MUST treat these as sensitive:

- stream keys;
- service passwords/tokens;
- encryption passphrases;
- credentials embedded in plugin settings;
- browser cookies/session state where exposed by plugins.

Sensitive values MUST NOT appear in logs, generic snapshots, diagnostics or error messages.

Property metadata SHOULD mark known password fields as secret. Plugin settings that cannot be classified reliably require explicit controller policy.

## 42. Compatibility aliases

The existing protocol v1 commands may be retained temporarily as compatibility aliases during migration:

- `hello`
- `source.types`
- `source.defaults`
- `source.create`
- `source.update`
- `source.settings`
- `source.destroy`
- `scene.create`
- `scene.destroy`
- `scene.add`
- `item.remove`
- `item.transform`
- `program.set`
- `shutdown`

They MUST NOT define new v2 semantics. The v2 semantic methods are authoritative.

## 43. Implementation decomposition

Before implementing the majority of v2, split the current monolithic host into focused components:

```text
engine/
  main.cpp
  runtime/
    engine.cpp
    object_registry.cpp
    module_manager.cpp
    event_bridge.cpp
  protocol/
    reader.cpp
    writer.cpp
    dispatcher.cpp
    request.cpp
    response.cpp
    errors.cpp
  api/
    session_api.cpp
    engine_api.cpp
    properties_api.cpp
    source_api.cpp
    scene_api.cpp
    item_api.cpp
    filter_api.cpp
    interaction_api.cpp
    media_api.cpp
    transition_api.cpp
    audio_api.cpp
    hotkey_api.cpp
    encoder_api.cpp
    service_api.cpp
    output_api.cpp
    runtime_api.cpp
  render/
    shared_texture.cpp
```

The exact filenames may evolve, but protocol framing, event serialization, handle ownership and each semantic API should not remain embedded in one monolithic `host.cpp`.

## 44. Implementation order

1. protocol framing, request manager, capabilities, revisions, subscriptions and event writer;
2. refactor existing source/scene/item/program v1 functionality onto v2;
3. generic `properties.*`;
4. complete source/scene/item/filter APIs;
5. D3D11 shared-texture preview;
6. audio initialization/mixer/meters;
7. media/interaction/transitions/studio mode;
8. encoders/services/outputs;
9. recording/streaming/replay-buffer/virtual-camera convenience APIs;
10. hotkeys/stats/screenshots/captions/missing-files;
11. runtime snapshot/recovery;
12. extension and optional scripting;
13. compatibility, fuzzing, stress testing and plugin matrix validation.

## 45. Non-goals

Protocol v2 does NOT attempt to:

- remotely expose every libobs C function;
- expose plugin implementation callbacks such as source `video_render`, encoder `encode`, output packet callbacks or service implementation callbacks;
- expose `obs_source_t *`, `gs_texture_t *` or any other in-process pointer;
- make engine runtime state the canonical persistent project format;
- recreate OBS Studio's Qt frontend inside the GPL process;
- use OBS WebSocket as a second control API.

The target is one semantic Engine API that is sufficient for a complete custom frontend while keeping all libobs/plugin implementation details inside the GPL engine process.

# Output namespace v1

`output.*` is the canonical lifecycle owner for a live libobs output. Handles
are process-local decimal strings and are never persisted across an engine
restart. The namespace exposes only normalized state; libobs pointers,
callback names, and raw plugin objects never cross the protocol boundary.

## Kind metadata

`output.kindList` returns `{ "kinds": [...] }`. `output.kindGet` takes
`{ "kind": "..." }`. `output.kindDefaults`, `output.kindProperties`, and
`output.kindCapabilities` take the same kind selector. A kind entry contains
its id, display name, module load state, registration/load booleans, and:

```json
{
  "video": true,
  "audio": true,
  "encoded": true,
  "raw": false,
  "requiresService": false,
  "multiTrackAudio": true,
  "multiTrackVideo": true,
  "canPause": false,
  "videoCodecs": [{"value": "h264"}],
  "audioCodecs": [{"value": "aac"}],
  "protocols": [{"value": "rtmp"}]
}
```

`kindDefaults` returns sanitized default `settings`; `kindProperties` returns
sanitized default settings, serialized properties, and `deferUpdate`.

## Runtime state

`output.create` takes `kind`, optional `name`, and optional object `settings`.
`output.get` and `output.list` return output summaries. A summary contains the
output handle, name, kind, flags, nested `state`, and explicit
`videoEncoders[]`/`audioEncoders[]` slot arrays. `output.rename` changes only
the runtime name.

The nested state is shaped as follows:

```json
{
  "output": "7",
  "state": "idle",
  "initialized": true,
  "active": false,
  "reconnecting": false,
  "starting": false,
  "stopping": false,
  "paused": false,
  "service": null,
  "videoEncoders": [],
  "audioEncoders": [],
  "delay": {"seconds": 0, "activeSeconds": 0, "preserve": false},
  "reconnectPolicy": {"enabled": true, "retryCount": 20, "retryDelaySeconds": 2},
  "lastStopCode": 0,
  "sanitizedLastError": ""
}
```

`output.getSettings`, `output.patchSettings`, `output.replaceSettings`, and
`output.getProperties` use the generic properties bridge. Settings mutation is
restricted to inactive outputs. `patchSettings` applies only supplied fields;
`replaceSettings` clears the existing document before applying the replacement.

## Relationships and slots

`output.setService` accepts a service handle or JSON null and
`output.getService` returns the canonical relationship. A service must be
live, not active/incompatibly bound, and its protocol must be declared by the
output kind.

`output.setVideoEncoder` and `output.setAudioEncoder` accept an explicit
zero-based `slot` and an encoder handle or JSON null. Slot 0 is required for
the corresponding encoded media type at start. Video slots are bounded by
`MAX_OUTPUT_VIDEO_ENCODERS`; audio slots by `MAX_OUTPUT_AUDIO_ENCODERS`.
Logical audio tracks remain one-based in `encoder.*` and must not be confused
with these output slot indexes. Binding validates encoder type, output codec
support, live Canvas/audio input, and inactive output state, then reads the
binding back from libobs. `output.getEncoders` returns both slot arrays.

## Lifecycle and policy

`output.start`, `output.stop`, and `output.forceStop` normalize libobs's
asynchronous callbacks into `starting`, `active`, `reconnecting`, `stopping`,
and `idle`. A synchronous command-owned transition emits its canonical event
at the command revision after the response. A callback that settles after the
request uses an independent later revision. Repeated raw start/stop signals
are coalesced; observers are disconnected and drained before removal.

`output.setPaused`/`output.getPaused` expose pause only when the output kind
declares `canPause`; both pause and unpause use `output.paused`. Delay uses
`seconds` in the range 0..3600 and `preserve`. Reconnect policy uses
`enabled`, `retryCount` in 0..100, and `retryDelaySeconds` in 1..3600.

`output.getStats` is a read-only snapshot with `totalBytes`, `totalFrames`,
`droppedFrames`, `congestion` when finite, and `connectTimeMs` where libobs
provides them. `output.getLastError` returns only a service-secret-redacted
message. `output.getSupportedCodecs` returns video/audio/protocol arrays.

An output cannot be removed while active, starting, reconnecting, stopping, or
bound to a service/encoder. Removal never force-stops the output. The stable
Phase-3 namespace intentionally does not expose `output.sendCaption`; that
method is reserved for Task 32.

## Events

The canonical event names are:

* `output.created`
* `output.removed`
* `output.renamed`
* `output.configurationChanged`
* `output.starting`
* `output.started`
* `output.stopping`
* `output.stopped`
* `output.paused`
* `output.reconnecting`
* `output.reconnected`
* `output.error`

`output.stopped` carries `output`, `state: "idle"`, `stopCode`, and `clean`.
Non-success stops also carry `output.error` at the same lifecycle revision.
When activation/deactivation changes an owned encoder or service, the same
revision may include the corresponding `encoder.activeChanged` and
`service.activeChanged` events. High-frequency output statistics are not an
event subscription in v1.

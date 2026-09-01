# Encoder v1

The Phase-3 encoder namespace exposes libobs video and audio encoder contexts
as Engine-owned runtime objects. Handles are canonical decimal strings and are
never persisted by a Controller.

## Kinds

`encoder.kindList`, `encoder.kindGet`, `encoder.kindDefaults`,
`encoder.kindProperties`, and `encoder.kindCapabilities` report only encoder
definitions registered by libobs. `registered` and `moduleLoaded` describe
registration/module state. `actualRuntimeCompatibility` is `unknown` until an
Output has initialized the encoder; a DLL filename or registration entry is
not hardware proof.

The capability object contains `deprecated`, `internal`, `passTexture`,
`dynamicBitrate`, `roi`, `scaling`, and `multitrackDynamicBitrate`. Binary
extra-data and SEI methods are intentionally not advertised because the
current newline JSON transport has no bounded binary channel.

## Runtime objects

`encoder.create` requires `type` (`video` or `audio`) and a registered `kind`.
Video encoders use a semantic `videoInput` object:

```json
{"type":"canvas","canvas":"1"}
```

The default is Main Canvas. Audio encoders require an immutable one-based
`audioTrack` in `1..MAX_AUDIO_MIXES`; it is translated to libobs's zero-based
mixer index. There is no `encoder.setAudioMix` method.

Creation, initialization, and activity are separate state values. Creation
only establishes an Engine/libobs context; plugin initialization is normally
proven when an Output starts. `getState` also reports the semantic video
input/audio track, group (currently null), and `boundOutputs`.

## Settings and scaling

Patch and replace settings validate against the live libobs property schema.
Inactive plugin updates are observed synchronously. Active updates use the
private tracked libobs bridge and settle at the actual encoder reconfigure
point, including a future group boundary. A bounded timeout returns `timeout`
and schedules `session.resyncRequired`; it never claims that plugin state was
applied. A plugin failure returns `obs_error`, preserving the old settings when
libobs can do so and requesting resynchronization for an active uncertain
state.

Scaled size and GPU scale filter are video-only and require an inactive,
uninitialized encoder. Width and height are both zero (disable) or bounded
positive integers no greater than 16384. The result is read back from libobs.

## ROI

ROI operations are available only when `OBS_ENCODER_CAP_ROI` is registered.
The Engine checks strict rectangle ordering, 16-pixel minimum blocks, input
dimensions, finite priority, and priority `[-1,1]` before calling libobs.
`roi.list` returns reverse-addition order with an ephemeral insertion `index`;
that index is used only by the current process for `roi.remove`. Changes emit
`encoder.roiChanged` after the libobs mutation.

Encoder removal rejects `object_in_use` while grouped, output-bound, active,
or awaiting tracked update settlement. No encoder pointer crosses the
protocol boundary.

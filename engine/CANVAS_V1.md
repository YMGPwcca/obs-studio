# Engine Protocol v2 — Canvas v1 experimental

Canvas handles are canonical positive decimal strings and are valid only in the
current engine process. Main Canvas is always present, has one stable handle for
the process lifetime, and cannot be removed or renamed. Private canvases are
runtime objects owned by the engine; their UUID is diagnostic metadata, not a
persistent Controller identity.

## Methods

```text
canvas.list
canvas.getMain
canvas.get
canvas.create
canvas.remove
canvas.rename
canvas.getVideoSettings
canvas.setVideoSettings
canvas.listScenes
canvas.getChannel
canvas.setChannel
canvas.getFlags
```

The namespace is advertised as `canvas.v1.experimental` with method-level
entries. It uses the libobs Canvas/view/video-mix APIs and does not expose
internal structs or a second control plane.

## Canvas summaries and video settings

```json
{
  "canvas":"1",
  "name":"Main",
  "uuid":"6c69626f-6273-4c00-9d88-c5136d61696e",
  "isMain":true,
  "activate":true,
  "mixAudio":true,
  "sceneRef":true,
  "ephemeral":false,
  "hasVideo":true
}
```

Video settings use semantic fields rather than raw `obs_video_info` layout:

```json
{
  "width":1920,
  "height":1080,
  "outputWidth":1920,
  "outputHeight":1080,
  "fpsNumerator":60,
  "fpsDenominator":1,
  "format":"bgra",
  "colorSpace":"srgb",
  "range":"full",
  "scaleType":"bilinear",
  "adapter":0,
  "gpuConversion":false
}
```

Dimensions are bounded to `16..16384`; frame-rate numerator and denominator
are positive and bounded. Formats, colorspaces, ranges, and scale types are
validated against the current libobs enums. Main settings are read-only in
this namespace because its video mix is controlled by engine startup.

`canvas.create` accepts optional `name`, `videoSettings`, and semantic `flags`
(`activate`, `mixAudio`, `sceneRef`, `ephemeral`). A Controller cannot set the
`MAIN` flag or arbitrary raw bitmasks. New private canvases use a video mix and
default to activation, scene references, audio mixing, and ephemeral runtime
behavior.

`canvas.setVideoSettings` validates the complete proposed configuration first.
If libobs refuses reset while video is active, the method returns `busy` and
does not claim a mutation. A successful reset emits
`canvas.videoSettingsChanged` once. PreviewOutput resource invalidation is
attached by the later PreviewOutput implementation.

## Scene ownership

Every protocol Scene has exactly one Canvas. `canvas.listScenes` and
`scene.get.canvas` are derived from the same engine registry and return
consistent results. Removing a private Canvas with any owned Scene or routed
channel returns `object_in_use`; removal never cascades or silently migrates a
Scene.

## Channels

`canvas.setChannel` takes a bounded `channel` and a tagged `target` object or
JSON null:

```json
{"canvas":"1","channel":0,"target":{"type":"scene","scene":"2"}}
```

Supported target types are `source`, `scene`, and null. `canvas.getChannel`
returns the canonical tagged target and obtains/releases a strong libobs source
reference during the query. libobs performs the channel's source refcount and
activation/deactivation work. Program routing uses the Main Canvas channel but
is owned by `program.*`; Canvas channel APIs do not redefine Studio Preview.

## Events, revisions, errors

Canonical events are `canvas.created`, `canvas.removed`, `canvas.renamed`,
`canvas.videoSettingsChanged`, and `canvas.channelChanged`. A real successful
mutation consumes one global revision and the response precedes its events.
Equal rename/channel operations are successful no-ops with no revision.
Read methods reject `ifRevision`. Stable errors include `bad_request`,
`not_found`, `object_in_use`, `invalid_state`, `busy`, `obs_error`, and
`revision_conflict`.

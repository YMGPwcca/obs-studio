# Engine Protocol v2 — PreviewOutput v1 / D3D11

Phase 2 exposes a Windows D3D11 PreviewOutput for efficient cross-process
preview. The capability `preview.d3d11SharedTexture.v1` and its method-level
entries are advertised only when the live graphics device is D3D11 and reports
shared-texture support. Task 17 established the transport; Task 20 completes
the general target/routing namespace.

## Methods

```text
previewOutput.list
previewOutput.get
previewOutput.create
previewOutput.destroy
previewOutput.setTarget
previewOutput.getInfo
previewOutput.setEnabled
previewOutput.resize
previewOutput.getSharedTexture
previewOutput.releaseSharedTexture
```

Targets are explicit tagged objects: `program`, `preview`, `scene`, `source`,
and `canvas`. `previewOutput.create` and `previewOutput.setTarget` reject
unregistered handles; they never infer a target class from a bare handle.

`previewOutput.create` requires an explicit target object:

```json
{
  "target":{"type":"program"},
  "width":320,
  "height":180,
  "enabled":true,
  "pixelFormat":"bgra8",
  "colorSpace":"srgb",
  "range":"full"
}
```

Width and height default to the target video output and are bounded to
`16..16384`. Semantic scaling is `fit`, `fill`, `stretch`, or `oneToOne`, with
`fit` as the default. The only transport format in this version is
BGRA8-compatible, sRGB, full range. The response and `previewOutput.created`
event include the logical target, effective dimensions, scale,
`resourceGeneration`, adapter LUID, and the shared-texture descriptor. Handles
remain canonical decimal strings.

## Resource and handle contract

The engine creates a render-target texture with
`GS_RENDER_TARGET | GS_SHARED_KM_TEX`. The checked-out D3D11 backend creates a
legacy shared resource with `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`. The engine
queries the underlying `ID3D11Texture2D` only inside the graphics context,
gets its `IDXGIResource::GetSharedHandle`, converts the pointer-width value via
`uintptr_t` to a 64-bit protocol integer, and serializes it as a decimal string.
The lossy backend `uint32_t gs_texture_get_shared_handle` accessor is not used.

The controller opens the value with
`ID3D11Device::OpenSharedResource` on the adapter identified by the returned
LUID. The descriptor is a borrowed legacy DXGI share value:

```json
{
  "sharedTexture":{
    "type":"d3d11LegacySharedHandle",
    "handle":"...",
    "ownership":"engineOwnedUntilResourceChange",
    "controllerMustNotClose":true,
    "openApi":"ID3D11Device::OpenSharedResource"
  }
}
```

This legacy value is not an NT handle. The controller must not call
`CloseHandle` or `DuplicateHandle` on it. It must keep its opened D3D resource
alive until finished, and it must discard the descriptor when the resource
generation changes or the engine reports destruction.

## Keyed-mutex synchronization

The engine initially owns keyed-mutex key `0`. It does not render until
`getSharedTexture` attaches a consumer. For each produced frame, the producer
acquires key `0`, renders, then releases key `1`. The controller acquires key
`1`, copies/reads the texture, then releases key `0`:

```text
producer:  AcquireSync(0, 16 ms) -> render -> ReleaseSync(1)
consumer:  AcquireSync(1, bounded timeout) -> read -> ReleaseSync(0)
```

Producer acquisition is bounded at 16 ms; a timed-out frame is dropped and the
graphics thread never waits indefinitely for a crashed or stalled Controller.
The controller must release key `0` before calling
`previewOutput.releaseSharedTexture`. That method detaches the producer from
the consumer lease without consuming a mutation revision; it does not close a
legacy OS handle.

`frameSequence` is telemetry and does not consume the engine mutation revision.
No frame-ready event is emitted at video rate.

## Rendering and lifetime

Rendering runs from libobs's main graphics render callback. The selected source
is rendered through libobs into a BGRA8 staging `gs_texrender`, then scaled with
letterboxing into the shared target while preserving aspect ratio, alpha, scene
transforms, and filters. All graphics operations are inside the libobs graphics
context. No HWND, COM pointer, `gs_texture_t*`, or raw video bytes cross the
protocol.

`resourceGeneration` starts at `"1"` and increments when `resize` or a relevant
Canvas video reset replaces the resource. The `previewOutput.resourceChanged`
event carries the replacement descriptor. Destruction removes the runtime
handle and releases the engine's graphics resources after the render callback
is detached. A Controller-opened legacy resource may keep the underlying
allocation alive until its own D3D reference is released.

`previewOutput.setEnabled` is a canonical configuration mutation and emits
`previewOutput.enabledChanged`; disabled outputs retain their descriptor but
produce no frames. `previewOutput.getInfo`, `getSharedTexture`, and
`releaseSharedTexture` are read/resource-lease operations and do not consume a
mutation revision. The internal consumer attachment is ephemeral lease state;
it is intentionally absent from the canonical `getInfo`, `list`, and resource
descriptor payloads, so lease acquisition/release cannot change a
revision-governed reconstruction result.

## Stable errors and exclusions

Stable errors include `bad_request`, `not_found`, `not_available`,
`unsupported_capability`, `obs_error`, `busy`, and `revision_conflict`.
Binary capture is intentionally not implemented or advertised; large frame
data never travels through NDJSON.

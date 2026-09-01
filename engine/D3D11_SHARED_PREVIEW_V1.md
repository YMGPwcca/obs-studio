# D3D11 Shared Preview v1

This is the technical transport contract behind
`preview.d3d11SharedTexture.v1`.

## Resource creation

The engine runs on the libobs graphics context and creates a BGRA8-compatible
render-target texture with `GS_RENDER_TARGET | GS_SHARED_KM_TEX`. The checked-out
D3D11 backend maps that to `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX` and exposes a
legacy DXGI share value through `IDXGIResource::GetSharedHandle`.

The engine does not use the backend's `uint32_t` shared-handle helper. It reads
the full Windows `HANDLE` value from `GetSharedHandle`, converts it through
`uintptr_t` to a 64-bit protocol integer, and serializes it as a decimal
string. No `ID3D11Texture2D*`, `IDXGIResource*`, `gs_texture_t*`, address, or
native pointer is sent through NDJSON.

The controller enumerates adapters, selects the returned adapter LUID, creates
an `ID3D11Device`, and calls `ID3D11Device::OpenSharedResource` with the
full-width value. This is a legacy shared handle, not an NT handle: the
controller must not call `CloseHandle` or `DuplicateHandle` on the value. The
engine owns the originating resource; the controller owns only its opened D3D
resource reference.

These choices follow the current Microsoft DXGI contract:

- [`IDXGIResource::GetSharedHandle`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiresource-getsharedhandle)
  permits marshaling the legacy handle to another process and explicitly says
  it is not an NT handle;
- [`IDXGIResource1::CreateSharedHandle`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle)
  is the NT-handle alternative, but the current libobs backend does not create
  this resource form, so it is not silently claimed by this protocol.

## Keyed mutex

The producer initially owns key `0`. `getSharedTexture` attaches a consumer;
before that, the producer leaves the initial ownership intact. Each producer
frame follows:

```text
AcquireSync(0, 16 ms) -> render -> ReleaseSync(1)
```

The consumer follows:

```text
AcquireSync(1, bounded timeout) -> CopyResource/Map -> ReleaseSync(0)
```

The producer drops a frame on timeout and never waits indefinitely for a
Controller. A crashed consumer can therefore stop frame delivery but cannot
hold the engine's render thread forever. The Controller must release key `0`
before calling `previewOutput.releaseSharedTexture`.

## Rendering

The graphics callback renders Program, Preview, Scene, and Source targets via
libobs into an engine-owned BGRA8 staging `gs_texrender`, then draws into the
shared texture using the OBS default effect. Canvas targets use the Canvas view
render path. Semantic scaling is `fit` (aspect-preserving letterbox), `fill`
(aspect-preserving crop), `stretch`, or `oneToOne`; the background is
transparent. Source transforms and filters remain in the libobs render path.

## Generation and frame metadata

`resourceGeneration` is a resource-lifetime generation, not a frame counter. It
starts at `"1"` and increases on resize and Canvas video-reset replacement.
`previewOutput.resourceChanged` carries the new descriptor; the Controller
discards and reopens its old resource. `frameSequence` increments only after a
successful producer frame and is telemetry, never a global mutation revision.

Target removal keeps the PreviewOutput handle alive, clears its strong target
binding, marks `targetAvailable:false`, and emits `previewOutput.targetChanged`.
The next frame is transparent until an explicit `setTarget`.

## Lifetime and failure boundaries

PreviewOutput resources are detached from the main render callback before
engine shutdown. The engine releases its D3D objects before `obs_shutdown`;
legacy share values are not separately closed by the engine. If a controller
keeps an opened legacy resource alive, the underlying allocation remains valid
until all D3D references are released according to DXGI lifetime rules.

The in-repository `preview_consumer_test.cpp` fixture verifies adapter selection,
full-width handle opening, keyed synchronization, CPU readback, dimensions,
BGRA8 format, content change, resize/generation replacement, consumer exit, and
reopen. It is `EXCLUDE_FROM_ALL` and is never installed as a production engine
component.

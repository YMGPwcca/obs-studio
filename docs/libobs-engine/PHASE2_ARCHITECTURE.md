# Engine Protocol v2 Phase 2 Architecture

Status: implementation design for the `phase2-scene-render-graph` branch

This document records the Phase-2 runtime model for Tasks 12–20. It is
subordinate to checked-out source, the explicit Phase-2 goal, and the concrete
namespace documents. It must be updated when implementation or source audit
changes a design decision.

## Scope and execution order

Phase 2 completes the live composition path only:

```text
12 scene -> 13 item -> 14 canvas -> 15 program -> 16 preview ->
17 D3D11 PreviewOutput foundation -> 19 transition -> 18 studio ->
20 PreviewOutput routing
```

Task numbers remain the roadmap numbers; Task 19 is implemented before Task 18
because Studio transition orchestration requires real transition objects. Task
21 and all later namespaces are outside this branch.

The proprietary Controller owns persistent IDs, projects, undo/redo, UI
windows, monitor placement, fullscreen presentation, and product policy. The
GPL `obs-engine.exe` owns live libobs objects, render resources, callbacks,
ephemeral handles, and runtime state. The only Controller-facing API is Engine
Protocol v2 over the existing NDJSON stdio transport.

## Live object graph

```text
CanvasEntry
  +-- libobs obs_canvas_t (one strong engine reference)
  +-- SceneEntry[] (libobs canvas source membership)
  +-- per-canvas view/video mix
  +-- optional channel source references

SceneEntry
  +-- libobs obs_scene_t (one strong engine reference)
  +-- owning canvas handle
  +-- ItemEntry[] (engine index of scene and group items)

ItemEntry
  +-- libobs obs_sceneitem_t (one engine reference)
  +-- owning scene handle
  +-- source handle
  +-- optional group-parent handle

TransitionEntry
  +-- libobs transition obs_source_t (one strong engine reference)
  +-- kind/settings/duration metadata owned by the engine

PreviewOutputEntry
  +-- logical tagged target
  +-- render size/format/color metadata
  +-- engine-owned GPU resources and generation
  +-- bounded synchronization state
```

Raw libobs pointers, COM interfaces, HWNDs, and native addresses never cross
the protocol boundary. Handles are canonical positive decimal strings,
process-local, monotonically allocated, never reused during one process, and
invalid after restart.

## Canvas and Scene ownership

The Main Canvas is obtained once with `obs_get_main_canvas()` after libobs
startup. It receives one engine canvas handle and cannot be removed or
replaced during that engine process. Private canvases are created with
`obs_canvas_create_private` and a validated semantic video configuration.

Every protocol Scene belongs to exactly one Canvas. `scene.create` selects the
Main Canvas when `canvas` is omitted and otherwise validates the supplied
canvas handle. New scenes use `obs_canvas_scene_create`, which calls the
canvas-aware `obs_source_create_canvas` path and avoids the old private-scene
default-canvas fallback warning. Scene duplication preserves the source
reference/deep-copy mode selected by the protocol and preserves the source
scene's canvas; the duplicated scene and every duplicated item receive fresh
engine handles.

Canvas removal is rejected with `object_in_use` while it still owns Scenes.
There is no implicit cascade or silent Scene migration. Main Canvas removal is
rejected with the stable `invalid_state` error.

The current libobs Canvas implementation exposes a per-canvas `obs_view`,
optional `obs_core_video_mix`, channel refcounting, scene enumeration, and
canvas video reset. These APIs are used only after their lock/lifetime rules
are respected; the Controller sees semantic snapshots, not `obs_video_info`
layout or internal structs.

## Scene Item model

The canonical public item order is the order returned by libobs scene
enumeration: index `0` is the first item in the scene list and is the bottom
render layer; later items render above earlier items. Every Scene and Item API
uses this same convention. Groups are represented primarily by an Item handle;
their internal group Scene is not exposed as a second protocol Scene.

The canonical transform is one object containing position, scale, rotation,
alignment, bounds type/alignment/size, crop, and crop-to-bounds. `item.setTransform`
validates the complete proposed object before mutating libobs and performs one
logical update. Convenience setters delegate to the same validation and
canonical readback path. Crop/bounds changes emit `item.transformChanged` only;
`item.cropChanged` is not advertised. The lock event is
`item.lockedChanged` and the property is `locked`.

Group creation, membership, child enumeration, and ungrouping use the current
`obs_scene_add_group*`, `obs_sceneitem_group_*`, and
`obs_sceneitem_group_enum_items` APIs. The engine never registers two handles
for one `obs_sceneitem_t`. Ungrouping keeps child handles stable when libobs
keeps the child item identity; if a libobs operation creates replacement items,
the implementation emits deterministic removal/creation mappings instead of
silently retaining stale handles.

## Program, Preview, and Studio

Program and Preview are separate engine state slots. `program.setScene` always
changes actual Program routing, including while Studio is enabled. Preview
selection changes only Preview. Neither state is duplicated as an independent
Studio-owned Scene value.

Studio owns only enabled state, selected Transition handle, transition duration,
and any supported T-Bar state. `studio.transition` validates enabled Studio,
live Preview, live selected Transition, compatible Canvas/routing, and current
transition state before beginning. Program identity is changed and
`program.sceneChanged` is emitted at the documented libobs transition commit
point, not merely when a start request is accepted. Transition progress is
telemetry and never consumes a revision per frame.

## Revision and event ownership

The existing global revision/event invariants remain frozen:

* each successful externally visible canonical mutation consumes exactly one
  revision;
* command-owned events share that committed revision;
* the response is queued before command-owned events;
* failed validation/unsupported/stale operations consume zero revisions;
* unrelated asynchronous callbacks receive independent revisions;
* telemetry is bounded/coalesced and does not consume mutation revisions;
* canonical overflow or uncertain ownership requires `session.resyncRequired`.

Scene/item/canvas/Program/Preview/Transition/Studio/PreviewOutput callbacks
normalize and enqueue state; they never write stdout. Callback connections are
removed before the corresponding engine map entry or libobs strong reference is
released. Any callback whose object is already represented by a preceding
removal event is suppressed from later serialized state events.

## Runtime shutdown order

The implementation follows this dependency order, adjusted only when the
checked-out libobs ownership rules require it:

```text
disable and detach PreviewOutputs
release shared GPU resources and close engine-owned OS handles
stop Studio/transition activity
clear Preview and Program routing
disconnect Transition/Studio/Canvas/Scene/Item observers
release Transition objects
remove/release Item references
remove/release Scene references
release private Canvases
release accepted Source/Filter state
release the Main Canvas reference
shutdown libobs graphics/runtime
```

No GPU operation runs after libobs graphics destruction, and no observer may
retain a pointer to protocol infrastructure after the event dispatcher or
revision state is gone.

## PreviewOutput and D3D11 transport

Task 17 creates the PreviewOutput runtime object and the Windows-only shared
resource foundation. Task 20 completes explicit tagged routing to Program,
Preview, Scene, Source, and Canvas. Normal outputs never create an HWND or a
projector window; the Controller owns those surfaces.

The checked-out D3D11 backend supports render-target textures with
`GS_SHARED_KM_TEX`, `IDXGIKeyedMutex`, `IDXGIResource::GetSharedHandle`, and
`OpenSharedResource`. The backend's legacy public helper currently uses a
32-bit handle type, while Windows `HANDLE` is pointer-width. The Phase-2
transport therefore must use a width-preserving descriptor/bridge and must
document whether the final mechanism is a system-wide legacy shared handle or
an explicitly duplicated NT handle. A numeric value is never treated as a
portable process-local HANDLE without the corresponding Windows sharing API.

The initial SDR resource is BGRA8-compatible with explicit color-space/range
metadata. The producer owns the render resource and uses a keyed mutex with a
bounded acquire timeout; it never waits indefinitely for a dead Controller.
The consumer acquires/releases the documented keys. `resourceGeneration`
changes on resize, format/color-space resource replacement, Canvas/global video
reset, adapter/device recreation, and device loss. `frameSequence`, if
exposed, is telemetry and is independent from engine revision.

PreviewOutput callbacks render only on the libobs graphics/video context. They
obtain a temporary strong reference to their tagged target under the output
state lock, release the lock before entering source/Canvas rendering, and
never hold protocol/revision locks while calling graphics or libobs. A target
removal first detaches the output's target, then releases the target reference,
then publishes `previewOutput.targetChanged`/`resourceChanged` as applicable.

## Capabilities and documentation

Intermediate commits advertise method-level capabilities only. The intended
final set is:

```text
scene.v1
item.v1
canvas.v1.experimental
program.v1
preview.v1
preview.d3d11SharedTexture.v1
transition.v1
studio.v1
previewOutput.v1
```

Each broad capability is advertised only when every method promised by its
focused contract dispatches and has deterministic coverage. Namespace docs
must record exact request/response schemas, no-op behavior, errors, handle and
parent lifetime, callback/revision semantics, and telemetry boundaries.

## Required verification shape

Every milestone has a clean checkpoint commit and a dedicated deterministic
integration lane, while cumulative regression evidence is retained. The final
candidate must pass the complete Task 1–20 exact-SHA matrix, complexity gate,
package audit, two-pass review, and the mandatory physical Windows D3D11
integration. Phase 2 remains `IN REVIEW` until independent advisor review and
human acceptance; this branch never starts Task 21.

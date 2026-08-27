# obs-engine basic runtime object API v1

This document defines the Task 6 Protocol v2 surface that ports the existing protocol-v1 source/scene/item operations without claiming the complete source, scene, or item namespaces described by `PROTOCOL_V2.md`.

The engine advertises method-level capabilities for this subset. It intentionally does **not** advertise the broader `source.v1`, `scene.v1`, or `item.v1` namespace capabilities yet.

## Handles

All Task 6 Protocol v2 runtime handles are canonical unsigned decimal strings:

```json
{"source":"1","scene":"2","item":"3"}
```

A canonical handle is non-zero, contains ASCII decimal digits only, has no leading zero, and is within the engine's signed-64-bit JSON interoperability range. Protocol v1 keeps its existing integer-handle encoding unchanged.

For the Task 6 methods, this string form is normative and intentionally tighter than the generic integer-or-string wording in the original `PROTOCOL_V2.md` design draft. Controllers MUST send and accept the decimal-string form so JavaScript and other transports never lose opaque 64-bit handle precision.

## Migration compatibility

Protocol v1 remains available only as a legacy migration/regression path. A controller using the Protocol v2 runtime-object API MUST use the v2 methods for state-mutating source/scene/item work in that engine process.

Legacy v1 mutations do not participate in Protocol v2 revision or event accounting. Mixing v1 state mutations with v2 state mutations in one process is therefore unsupported and MUST NOT be used by the production controller. Pure-v1 compatibility sessions remain supported for regression testing while migration is in progress.

## Capabilities and methods

- `source.kindList.v1` -> `source.kindList`
- `source.kindDefaults.v1` -> `source.kindDefaults`
- `source.create.v1` -> `source.create`
- `source.getSettings.v1` -> `source.getSettings`
- `source.patchSettings.v1` -> `source.patchSettings`
- `source.remove.v1` -> `source.remove`
- `scene.create.v1` -> `scene.create`
- `scene.remove.v1` -> `scene.remove`
- `item.create.v1` -> `item.create`
- `item.remove.v1` -> `item.remove`
- `item.setTransform.v1` -> `item.setTransform`

`program.set` is not part of Task 6 and remains on the protocol-v1 compatibility path until the Program namespace is implemented.

## Source methods

### `source.kindList`

Read-only. Returns:

```json
{
  "kinds": [
    {
      "id": "color_source_v3",
      "displayName": "Color",
      "outputFlags": 32777,
      "module": "image-source.dll"
    }
  ]
}
```

The metadata is the subset already exposed by protocol v1. Full semantic source-kind metadata remains future work.

### `source.kindDefaults`

Request:

```json
{"kind":"color_source_v3"}
```

Returns the libobs defaults in `data.settings`.

### `source.create`

Request:

```json
{
  "kind":"color_source_v3",
  "name":"controller-color",
  "settings":{}
}
```

`name` and `settings` are optional. Missing settings use the libobs defaults path. A present `settings` value must be a JSON object; unlike legacy protocol v1, a wrong-typed value is rejected with `bad_request`.

Returns `source`, `name`, and `kind` and emits `source.created` after the successful response.

### `source.getSettings`

Request:

```json
{"source":"1"}
```

Returns `source` and the current libobs `settings` object.

### `source.patchSettings`

Request:

```json
{"source":"1","settings":{"width":640}}
```

Uses `obs_source_update`, so the supplied object is a patch rather than a complete replacement. Returns the resulting settings and emits `source.settingsChanged`.

### `source.remove`

Request:

```json
{"source":"1"}
```

Scene items referencing the source are removed first in ascending engine-handle order. Each dependent removal emits `item.removed`, then the source emits `source.removed`. All events from the request carry the same committed revision.

## Scene methods

### `scene.create`

Request:

```json
{"name":"Main"}
```

`name` is optional. Returns `scene` and the effective name and emits `scene.created`.

### `scene.remove`

Request:

```json
{"scene":"2"}
```

Items owned by the scene are removed first in ascending engine-handle order and emit `item.removed`. The scene then emits `scene.removed`. If the legacy protocol-v1 program channel currently references the scene, that channel is cleared before the scene is released.

## Item methods

### `item.create`

Request:

```json
{"scene":"2","source":"1"}
```

Returns `item`, `scene`, and `source` and emits `item.created`.

### `item.remove`

Request:

```json
{"item":"3"}
```

Returns the removed `item`, `scene`, and `source` identity and emits `item.removed`.

### `item.setTransform`

Task 6 ports the transform fields that existed in protocol v1: position, scale, rotation, and alignment.

Request:

```json
{
  "item":"3",
  "transform": {
    "position":{"x":32.0,"y":24.0},
    "scale":{"x":1.25,"y":1.25},
    "rotation":5.0,
    "alignment":5
  }
}
```

Every field is optional within `transform`, but at least one supported field must be present. The engine validates the complete request before applying it, reads the resulting transform back from libobs, returns the canonical transform, and emits `item.transformChanged` with the same canonical transform.

Bounds, crop, visibility, locking, ordering, scale filters, and blend state remain future Item-namespace work.

## Revisions and events

Read-only Task 6 methods do not increment the revision and reject `ifRevision` under the existing Protocol v2 rules.

Each successful mutating Task 6 request commits exactly one public engine revision regardless of how many dependent removal events it produces. A stale `ifRevision` returns `revision_conflict` and performs no libobs mutation.

For synchronous Task 6 mutations, the successful response is queued before canonical state events from that request. Event delivery, overflow, deduplication, and `session.resyncRequired` behavior follow `EVENTS_V1.md`.

# Engine Protocol v2 — Scene v1

This document freezes the Task-12 Scene runtime contract. Scene handles are
canonical positive decimal strings, local to one engine process, and invalid
after restart or removal. A Scene is always owned by exactly one engine-managed
Canvas.

## Capability and methods

Task 12 advertises method-level capabilities until the complete Phase-2
composition contract is available:

```text
scene.list.v1
scene.get.v1
scene.create.v1
scene.remove.v1
scene.rename.v1
scene.duplicate.v1
scene.getItems.v1
scene.getState.v1
```

`scene.v1` is advertised only after the intentional Scene contract is complete
and the group/transition-override ownership decisions are documented. Group
operations are Item operations. Scene transition overrides are deferred until
the Transition milestone and are not temporary Scene handles.

## Scene summary

Scene summaries contain:

```json
{
  "scene": "2",
  "name": "Camera",
  "canvas": "1",
  "width": 1920,
  "height": 1080,
  "itemCount": 2
}
```

`width` and `height` are the current libobs scene-source dimensions. For a
normal scene they follow its Canvas video space. `itemCount` counts top-level
items; `scene.getState.totalItemCount` also reports registered group children.

## `scene.list`

Read-only, no parameters. Returns `{ "scenes": [...], "count": N }` in
ascending engine Scene-handle order. The list contains only Scenes owned by
the Engine Protocol runtime registry, not unrelated legacy/frontend objects.

## `scene.get`

Read-only. Parameters:

```json
{"scene":"2"}
```

Returns one current Scene summary. Missing or stale handles return `not_found`.

## `scene.create`

Mutation. Parameters:

```json
{"name":"Camera","canvas":"1"}
```

`name` and `canvas` are optional. If `canvas` is omitted, the permanent Main
Canvas is selected. A supplied Canvas must be a live engine Canvas handle. A
present name is a non-empty UTF-8 string of at most 256 bytes; when omitted the
engine uses a generated runtime name. Creation uses libobs
`obs_canvas_scene_create`, not the legacy private-scene fallback.

The response returns the canonical Scene summary and emits `scene.created` at
the same committed revision after the response.

## `scene.rename`

Mutation. Parameters:

```json
{"scene":"2","name":"Program"}
```

The actual libobs name is read back and returned. An equal name is an idempotent
success with no revision. A changed name emits:

```json
{"scene":"2","previousName":"Camera","name":"Program"}
```

as `scene.renamed`.

## `scene.duplicate`

Mutation. Parameters:

```json
{"scene":"2","name":"Camera copy","mode":"references"}
```

The default and currently supported mode is `references`: the duplicate is
created on the same Canvas, duplicates the Scene item structure and transform
state, and retains references to the existing input Sources. The duplicate
Scene and every duplicated Item receive new engine handles; Source handles do
not change. `mode` is optional but, when supplied, must be `references`.

The response is the new Scene summary with `duplicateOf`. The command emits
`scene.created` followed by deterministic `item.created` events for newly
registered duplicate items, all at one revision and after the response.

## `scene.getItems`

Read-only. Returns top-level Items in the exact libobs scene-list order:

```json
{"scene":"2","items":[{"item":"4","source":"3","order":0}],"count":1}
```

Index/order `0` is the bottom render layer; larger order values render above
it. Group Items remain in this top-level order and expose their children via
the Item namespace.

## `scene.getState`

Read-only. Returns a runtime reconciliation snapshot containing the Scene
summary, ordered top-level Item summaries, and `totalItemCount`. This is not a
persistent project format and does not contain raw libobs objects or durable
Controller IDs.

## `scene.remove`

Mutation. A Scene removal first clears any Program/Preview slot that references
it, then removes its registered Items in deterministic order (non-group
descendants before group parents, then ascending handle), and finally removes
the Scene from its Canvas and invalidates the Scene handle. The command-owned
event order is:

```text
program.sceneChanged -> null     if Program referenced the Scene
preview.sceneChanged -> null     if Preview referenced the Scene
item.removed ...
scene.removed
```

Every event uses the one removal revision. No later event treats the removed
Scene or Item handle as live. Removal of a missing/stale handle returns
`not_found`; failed validation or a stale `ifRevision` performs no mutation.

## Errors and concurrency

The namespace uses the stable errors `bad_request`, `not_found`,
`revision_conflict`, `obs_error`, `invalid_state`, and `internal_error` as
applicable. Successful canonical mutations consume exactly one global engine
revision; idempotent rename consumes none. Read methods reject `ifRevision`.
The response is queued before all command-owned events. Callback/event queue
overflow or uncertain state ownership requires the existing
`session.resyncRequired` boundary.

# Engine Protocol v2 — Item v1

This document freezes the Task-13 Scene Item contract. Item handles are
canonical positive decimal strings, process-local, never reused in one engine
process, and invalid after removal or restart.

## Methods and events

The Item capability covers:

```text
item.get item.create item.remove item.duplicate
item.getTransform item.setTransform
item.setPosition item.setScale item.setRotation item.setAlignment
item.setBounds item.setBoundsAlignment item.setCrop item.setCropToBounds
item.setVisible item.setLocked
item.setOrder item.moveUp item.moveDown item.moveTop item.moveBottom
item.setScaleFilter item.setBlendMode item.setBlendMethod
item.createGroup item.ungroup item.addToGroup item.removeFromGroup
item.getChildren
```

Canonical events are `item.created`, `item.removed`, `item.transformChanged`,
`item.visibilityChanged`, `item.lockedChanged`, `item.orderChanged`, and
`item.blendChanged`. Group structure changes additionally use
`scene.itemsChanged` with the affected Scene and optional group handle.

## Item identity and order

An Item summary includes `item`, public `scene`, optional input `source`,
optional `parentGroup`, `isGroup`, source name/kind, zero-based `order`,
`visible`, `locked`, transform, `scaleFilter`, `blendMode`, and `blendMethod`.
The `source` field is omitted for a group container whose source is internal to
libobs. Group children are still represented by stable Item handles while the
group exists.

All arrays use libobs scene-list order. Index `0` is the bottom render layer;
larger indexes render above it. `item.setOrder` and the relative movement
methods use the same convention, for either a Scene's top-level list or a
group's child list.

## Canonical transform

The `transform` object is:

```json
{
  "position": {"x": 0, "y": 0},
  "scale": {"x": 1, "y": 1},
  "rotation": 0,
  "alignment": 5,
  "bounds": {"type":"none","alignment":0,"width":0,"height":0},
  "crop": {"left":0,"top":0,"right":0,"bottom":0},
  "cropToBounds": false
}
```

`item.setTransform` accepts any non-empty subset of these fields, validates the
complete proposed result before calling libobs, applies it as one deferred
logical update, reads the canonical result back, and emits one
`item.transformChanged`. Invalid enum/range/type/crop combinations do not
partially update the item. Supported bounds types are `none`, `stretch`,
`scaleInner`, `scaleOuter`, `scaleToWidth`, `scaleToHeight`, and `maxOnly`.

Convenience setters build a partial canonical transform and delegate to the
same validation/readback path. Crop, bounds, and crop-to-bounds do not have a
separate `item.cropChanged` event. Scale-filter changes also use
`item.transformChanged`.

Scale filters are `disable`, `point`, `bicubic`, `bilinear`, `lanczos`, and
`area`. Blend methods are `default` and `srgbOff`; blend modes are `normal`,
`additive`, `subtract`, `screen`, `multiply`, `lighten`, and `darken`.

## Visibility and locking

`item.setVisible` takes `{ "item":"4", "visible":true }` and emits
`item.visibilityChanged` only on a real change. `item.setLocked` takes
`{ "item":"4", "locked":true }` and emits `item.lockedChanged`. Equal values
are successful no-ops without a revision or event.

## Duplication and removal

`item.duplicate` duplicates a non-group Item into the same Scene by default.
An optional `scene` target is allowed only when both Scenes share one Canvas.
The Source reference and canonical Item state are retained; the new Item gets
a new engine handle and emits `item.created` with `duplicateOf`.

Removing an Item invalidates only its Item handle. Removing a group first
removes child handles in deterministic order, then the group handle. No later
event treats any removed handle as live.

## Groups

`item.createGroup` takes a public Scene, an optional name, and an optional array
of `{ "item":"..." }` handles. Group creation produces one group Item handle.
`item.addToGroup` and `item.removeFromGroup` move existing non-group Items
between a Scene and its group; `item.getChildren` returns child summaries in
child order.

Libobs's ungroup implementation creates replacement top-level scene items.
Therefore `item.ungroup` explicitly emits removal for old child/group handles
and creation for any replacement Items discovered after the operation. This
prevents stale handles while making the remapping visible to the Controller.
Nested groups are rejected because the current libobs group API disallows
them.

## Revisions and errors

Every successful real Item mutation consumes exactly one global revision and
all command-owned events share it. Read methods and idempotent/no-op setters do
not consume a revision. The response is queued before command-owned events.
The stable errors are `bad_request`, `not_found`, `invalid_state`,
`not_available`, `obs_error`, `revision_conflict`, and `internal_error`.

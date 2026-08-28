# Engine Protocol v2 — Filter API v1

This document freezes the Task 11 `filter.*` contract. Filters are libobs
source objects attached to an engine-managed source. Their handles are
engine-process-local canonical decimal strings and are never persistent.

## Capability names

The engine advertises:

- `filter.v1`
- `filter.kindList.v1`
- `filter.kindDefaults.v1`
- `filter.kindProperties.v1`
- `filter.list.v1`
- `filter.get.v1`
- `filter.create.v1`
- `filter.remove.v1`
- `filter.rename.v1`
- `filter.duplicate.v1`
- `filter.getSettings.v1`
- `filter.patchSettings.v1`
- `filter.replaceSettings.v1`
- `filter.setEnabled.v1`
- `filter.getEnabled.v1`
- `filter.setOrder.v1`
- `filter.moveUp.v1`
- `filter.moveDown.v1`
- `filter.moveTop.v1`
- `filter.moveBottom.v1`

## Common rules

All filter handles are strings containing a canonical, non-zero decimal
integer. Leading zeroes, JSON numbers, negative values, and native pointers
are rejected. A filter handle is valid only while the filter remains attached
to its parent in the current engine process.

The `source` parameter identifies an engine-managed source handle. Task 11
does not yet expose scene-source handles as filter parents; that relationship
belongs to the later scene namespace. The parent must exist and the requested
kind must be registered as `OBS_SOURCE_TYPE_FILTER`.

Filter summaries have this shape:

```json
{
  "filter": "2",
  "source": "1",
  "name": "Color Correction",
  "kind": "color_filter_v2",
  "unversionedKind": "color_filter",
  "enabled": true,
  "index": 0,
  "outputFlags": 1
}
```

`index` is the current libobs filter-array index. It is zero-based, with the
same ordering used by `obs_source_filter_get_index` and `filter.list`.

Successful operations that alter canonical filter state consume exactly one
revision. The command response is written before command-owned events. Read
methods, idempotent `rename`/`setEnabled` operations, and order operations that
already have the requested result do not consume a revision.

## Kind methods

### `filter.kindList`

No parameters. Returns `{ "kinds": [ ... ] }`. Each kind entry includes
`id`, `displayName`, `outputFlags`, `module` when known, and
`moduleLoadState`. The list comes from `obs_enum_filter_types`; input and
transition kinds are not included.

### `filter.kindDefaults`

Parameters: `{ "kind": "registered_filter_id" }`.

Returns `{ "kind": "registered_filter_id", "settings": { ... } }` using
`obs_get_source_defaults`. A kind without defaults returns `obs_error`.

### `filter.kindProperties`

Parameters: `{ "kind": "registered_filter_id" }`.

Returns the same property document as `properties.get` with a target of:

```json
{ "type": "filterKind", "kind": "registered_filter_id" }
```

The generic `properties.*` methods also accept `filterKind` targets.

## Runtime methods

### `filter.list`

Parameters: `{ "source": "1" }`.

Returns `{ "source": "1", "filters": [summary, ...] }`, ordered by
ascending `index`.

### `filter.get`

Parameters: `{ "filter": "2" }`. Returns one filter summary.

### `filter.create`

Parameters:

```json
{
  "source": "1",
  "kind": "registered_filter_id",
  "name": "optional non-empty name",
  "settings": { "optional": "initial settings" }
}
```

The default name is `engine-filter-<handle>`. The new filter is inserted at
libobs index zero. The result is its summary. The command emits
`filter.created` with that summary.

### `filter.remove`

Parameters: `{ "filter": "2" }`. Returns `{ "filter": "2", "source": "1" }`
and emits the same data as `filter.removed`. Removal detaches the filter,
releases the parent-owned reference, releases the engine reference, and
invalidates the handle permanently. Removing a source also removes all of its
known filters and emits their `filter.removed` events in index order before
`source.removed`.

### `filter.rename`

Parameters: `{ "filter": "2", "name": "new non-empty name" }`.

Returns the resulting summary. A name equal to the current name is an
idempotent success. Otherwise the libobs rename signal owns the event and
emits `filter.renamed`:

```json
{
  "filter": "2",
  "source": "1",
  "name": "new name",
  "previousName": "old name"
}
```

### `filter.duplicate`

Parameters: `{ "filter": "2", "name": "optional name" }`.

Creates an independent private duplicate, attaches it to the same parent at
libobs index zero, and preserves the enabled state. The result and
`filter.created` event are the new summary plus `duplicateOf: "2"`.

### Settings methods

`filter.getSettings` takes `{ "filter": "2" }` and returns
`{ "filter": "2", "source": "1", "settings": { ... } }`.

`filter.patchSettings` and `filter.replaceSettings` take a filter handle and a
required object-valued `settings` member. `patchSettings` applies a libobs
settings patch. `replaceSettings` clears the current settings and applies the
replacement. Each returns the resulting settings object and emits
`filter.settingsChanged`:

```json
{ "filter": "2", "source": "1", "settings": { ... } }
```

For video filters, `obs_source_update` may defer the plugin update callback to
the video thread. The engine waits for a source-correlated `update` signal and
canonical post-update settings before claiming command ownership. If that
callback cannot be correlated within the bounded settlement window, the
request is not silently treated as settled; the engine forces
`session.resyncRequired` through the normal overflow/resynchronization path.

### `filter.setEnabled` and `filter.getEnabled`

`filter.setEnabled` takes `{ "filter": "2", "enabled": true }` and returns
the resulting summary. Equal values are idempotent. A change emits
`filter.enabledChanged`:

```json
{ "filter": "2", "source": "1", "enabled": true }
```

`filter.getEnabled` takes `{ "filter": "2" }` and returns
`{ "filter": "2", "source": "1", "enabled": true }`.

### Order methods

`filter.setOrder` takes `{ "filter": "2", "index": 1 }`. The index must be
an integer in `[0, filter.list.count)`, and is applied with
`obs_source_filter_set_index`. `filter.moveUp`, `filter.moveDown`,
`filter.moveTop`, and `filter.moveBottom` take `{ "filter": "2" }` and use
the corresponding libobs movement. Async and non-async filters retain libobs'
rule that relative movement skips filters of a different async class.

Each successful order change returns the changed parent order as:

```json
{
  "source": "1",
  "filters": ["3", "2"],
  "changed": "2"
}
```

and emits `filter.orderChanged` with the same data. A request that leaves the
order unchanged is an idempotent success with no revision/event. `index` is
never passed to libobs until the engine has checked the bounds because the
libobs setter has no public bounds validation.

## Errors

- `bad_request`: malformed handles, names, kinds, settings, booleans, or
  order indexes;
- `not_found`: unknown source/filter handle or unregistered kind;
- `incompatible_filter`: libobs rejected attachment to the parent;
- `obs_error`: libobs failed to create/duplicate/read the object;
- `internal_error`: an engine invariant or handle allocation failed;
- `timeout`: a deferred filter update could not be settled; the response is
  accompanied by mandatory resynchronization behavior.

Error messages are fixed semantic text and never contain pointers, native
handles, plugin exception text, or private callback data.

## Event and overflow semantics

The filter bridge observes filter `update`, `rename`, and `enable` signals.
Callbacks never write protocol output. They enter the same bounded capture /
deferred / direct-revision pattern as the other runtime bridges, correlated by
filter handle and parent handle. Deferred filter events are never silently
dropped. A full bridge queue clears its incremental batches, schedules
`session.resyncRequired`, and advances revision state deterministically.

The bridge does not expose filter parent/target raw pointers and does not use
`obs_filter_get_parent` or `obs_filter_get_target` outside plugin callback
contexts; the engine retains the parent relationship explicitly when it
attaches the filter.

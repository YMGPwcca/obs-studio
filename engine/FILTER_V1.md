# Engine Protocol v2 — Filter API v1

Status: COMPLETE / ACCEPTED

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

The `source` parameter identifies an engine-managed source handle. Task 11 does
not add a second persistent parent identity and does not expose raw libobs
filter/parent pointers. The parent must exist and a requested filter kind must
be registered as `OBS_SOURCE_TYPE_FILTER`.

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

`index` is zero-based and uses the same current filter-array ordering as
`obs_source_filter_get_index` and `filter.list`.

Successful operations that alter canonical filter state consume exactly one
revision. The response is written before command-owned events. Read methods,
idempotent rename/enable operations, and order requests that produce no actual
order change do not consume a revision.

## Kind methods

### `filter.kindList`

No parameters. Returns `{ "kinds": [ ... ] }`. Each entry contains the
registered filter id, display name, output flags, module when known, and module
load state. The list comes from `obs_enum_filter_types`.

### `filter.kindDefaults`

Parameters: `{ "kind": "registered_filter_id" }`.

Returns `{ "kind": "registered_filter_id", "settings": { ... } }` using
`obs_get_source_defaults`. An unknown kind is `not_found`; a registered kind
without a defaults object is `obs_error`. The returned object preserves libobs
default metadata; a default-only field may therefore be omitted from its JSON
serialization while remaining readable through a live source/property form.

### `filter.kindProperties`

Parameters: `{ "kind": "registered_filter_id" }`.

Returns the normal generic property document for target:

```json
{ "type": "filterKind", "kind": "registered_filter_id" }
```

The generic `properties.*` bridge also accepts `filterKind` and live `filter`
targets.

## Runtime methods

### `filter.list`

Parameters: `{ "source": "1" }`.

Returns `{ "source": "1", "filters": [summary, ...], "count": N }` ordered by
ascending current filter index. Enumeration also registers handles for attached
filters that came into existence as nested libobs state, for example filters
copied by the already-accepted `source.duplicate` behavior.

### `filter.get`

Parameters: `{ "filter": "2" }`. Returns one current filter summary.

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

The default name is `engine-filter-<handle>`. The filter is attached with the
normal libobs filter graph API. Success returns its summary and emits
`filter.created` at the command revision.

### `filter.remove`

Parameters: `{ "filter": "2" }`.

Returns `{ "filter": "2", "source": "1" }` and emits `filter.removed` with
the same identity. Removal detaches the filter, releases the parent-owned and
engine-owned references as appropriate, and invalidates the handle permanently.

Removing a parent source also invalidates its tracked filter handles. For
tracked children, `filter.removed` is emitted in current filter-index order
before `source.removed`; all belong to the parent-removal command revision.

### `filter.rename`

Parameters: `{ "filter": "2", "name": "new non-empty name" }`.

Equal names are idempotent. An actual change returns the new summary and emits:

```json
{
  "filter": "2",
  "source": "1",
  "name": "new name",
  "previousName": "old name"
}
```

as `filter.renamed`.

### `filter.duplicate`

Parameters: `{ "filter": "2", "name": "optional name" }`.

Creates an independent private duplicate attached to the same parent and
preserves enabled state. The result and `filter.created` event contain the new
summary plus `duplicateOf: "2"`.

`source.duplicate` remains the Task-8 source operation and is deliberately not
redefined by Task 11. libobs may copy attached filters as nested state of the new
source. Task 11 does **not** synthesize separate `filter.created` events into the
existing `source.duplicate` command. The Controller discovers inherited filters
through `filter.list` on the new source and then uses their newly registered,
ephemeral filter handles. This avoids retroactively changing the already
accepted Task-8 source event contract.

### Settings methods

`filter.getSettings` takes `{ "filter": "2" }` and returns:

```json
{ "filter": "2", "source": "1", "settings": { ... } }
```

`filter.patchSettings` and `filter.replaceSettings` require an object-valued
`settings` member. Patch applies a settings patch; replace resets the current
settings to the supplied object. A real canonical change returns the resulting
settings and emits:

```json
{ "filter": "2", "source": "1", "settings": { ... } }
```

as `filter.settingsChanged`.

For video filters, `obs_source_update` updates libobs's settings object
synchronously but may defer the plugin `update` callback and `update` signal to
the video thread. The engine therefore keeps a permanent observer per tracked
filter. The request never installs or disconnects a per-request update callback.
The observer normalizes the update batch, advances a private generation, and
wakes settlement. The engine-only libobs update bridge also assigns a private
per-source serial to every update submission and includes the contiguous serial
range covered by the deferred callback in internal signal calldata. Ownership
is proven by exact filter handle, canonical post-update settings, observer
generation greater than the request baseline, and the request serial lying in
the callback's covered range. The serial evidence is private; the public
`obs_source_update`/`obs_source_reset_settings` API and wire event shape do not
change.

If ownership cannot be proven within the bounded settlement deadline, the
bridge does not fabricate success from an unrelated signal. The request
returns `timeout`, marks incremental state uncertain, and the normal deferred
flush forces `session.resyncRequired`. A later callback for the timed-out
generation is quarantined and causes another resync boundary; it cannot settle
a later request. Every timed-out update serial for a filter is retained until
a later callback's covered range proves that serial has been processed. Rename
and enable callbacks never retire this settings quarantine. Unresolved or
unknown serials remain conservative and force resynchronization; unrelated
filter batches remain independent.

### `filter.setEnabled` / `filter.getEnabled`

`filter.setEnabled` takes `{ "filter": "2", "enabled": true }`. Equal values
are idempotent. A change returns the resulting summary and emits:

```json
{ "filter": "2", "source": "1", "enabled": true }
```

as `filter.enabledChanged`.

`filter.getEnabled` returns the current boolean without changing revision.

### Ordering

`filter.setOrder` takes `{ "filter": "2", "index": 1 }`. `index` must be an
integer in `[0, filter.list.count)`. The engine validates the bound before
calling `obs_source_filter_set_index`, because libobs's setter does not provide
public bounds validation.

`filter.moveUp`, `filter.moveDown`, `filter.moveTop`, and `filter.moveBottom`
take `{ "filter": "2" }` and use libobs's relative movement rules. In
particular, relative movement preserves libobs's async/non-async class behavior.

An actual order change returns and emits `filter.orderChanged` with:

```json
{
  "source": "1",
  "filters": [{"filter":"3"}, {"filter":"2"}],
  "changed": "2"
}
```

A no-op order request is a successful non-mutation.

## Errors

- `bad_request` — malformed handles, names, kinds, settings, booleans, or order
  indexes;
- `not_found` — unknown source/filter handle or unregistered filter kind;
- `incompatible_filter` — libobs rejected attachment to the parent;
- `obs_error` — libobs failed to create/duplicate/read required state;
- `internal_error` — an engine invariant or handle/revision operation failed.

Errors never expose raw pointers, native handles, plugin exception text, or
private callback data.

## Event / concurrency semantics

The filter bridge observes `update`, `rename`, and `enable` on permanent
per-filter observers. Each update observation advances a private generation and
records the private libobs serial range. Deferred batches retain both pieces
of evidence. A settings command is command-owned only when the exact filter
handle, canonical post-update settings, generation greater than the request
baseline, and request serial coverage all match. Callbacks never write
protocol output. They enter the same bounded capture/deferred/direct-revision
model as the existing runtime bridges. A full or uncertain deferred bridge
forces `session.resyncRequired`; canonical filter changes are not silently
dropped.

Before a Task-11 mutating request acquires the global mutation/revision mutex,
the capture path waits for already-direct source/media/filter callbacks to
retire. This preserves the accepted Task-10 lock order: a callback executing
under a libobs signal mutex is not forced to wait behind a protocol request that
already holds the global mutation mutex.

The accepted Task-10 `engine/protocol_v2.cpp` implementation remains isolated as
the non-filter protocol core. Task 11 adds a thin router for capability discovery
and `filter.*`; all existing non-filter requests continue through the accepted
core.

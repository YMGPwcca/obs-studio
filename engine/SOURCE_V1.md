# Engine Protocol v2 — `source.v1`

Task 8 completes the Protocol v2 `source.*` namespace from `PROTOCOL_V2.md` section 14.

## Scope

This capability covers engine-managed input sources only. Scenes remain in `scene.*`; filters remain in `filter.*`.
All source handles are ephemeral canonical decimal strings and are valid only for the lifetime of the engine process.

Implemented methods:

- `source.kindList`
- `source.kindGet`
- `source.kindDefaults`
- `source.kindProperties`
- `source.list`
- `source.get`
- `source.create`
- `source.duplicate`
- `source.remove`
- `source.rename`
- `source.getSettings`
- `source.patchSettings`
- `source.replaceSettings`
- `source.resetSettings`
- `source.getProperties`
- `source.getFlags`
- `source.getDimensions`
- `source.getState`
- `source.getActive`
- `source.getShowing`
- `source.getMissingFiles`
- `source.refresh`
- `source.saveState`
- `source.loadState`

## Kind and flag metadata

The raw libobs `outputFlags` value is returned together with semantic booleans so a Controller does not need to copy libobs bit definitions. The semantic fields include video, audio, async-video, custom-draw, interaction, composite, do-not-duplicate, deprecated, disabled, self-monitoring availability, monitor-by-default, controllable-media, CEA-708, sRGB, don't-show-properties-on-create, and requires-canvas states.

`source.duplicate` refuses a source with `OBS_SOURCE_DO_NOT_DUPLICATE`. libobs is allowed to return another reference to the same source for those kinds, but assigning a second engine handle to the same libobs object would violate the Engine Protocol object model.

## Settings operations

- `source.patchSettings` applies a partial settings object with `obs_source_update`.
- `source.replaceSettings` clears/replaces the source settings with `obs_source_reset_settings`.
- `source.resetSettings` replaces settings with the source kind's current libobs defaults.
- `source.refresh` signals `update_properties` so device/plugin-backed property forms can refresh. It is not itself treated as an engine-state mutation.

The generic `properties.*` API remains the preferred way for a Controller to construct and resolve arbitrary plugin settings forms.

## Source-local save/load state

`source.saveState` intentionally does **not** expose `obs_save_source()` as a persistence contract. The raw libobs serializer contains graph-level/frontend-sensitive data (including filters and hotkeys) that belong to other Engine Protocol namespaces.

Instead it returns a source-local versioned state object:

```json
{
  "version": 1,
  "kind": "color_source_v3",
  "name": "Camera matte",
  "settings": {}
}
```

`source.loadState` requires version `1`, requires the saved kind to match the existing target source, and restores the name plus complete settings in-place. This preserves the engine handle and every scene item already referencing that source. A successful load is one protocol mutation/revision even if it produces multiple normalized source events.

## Events and revisions

The source namespace emits:

- `source.created`
- `source.removed`
- `source.renamed`
- `source.settingsChanged`
- `source.activeChanged`
- `source.showingChanged`
- `source.flagsChanged`
- `source.dimensionsChanged`

libobs source signals are normalized through the bounded Task 5 event dispatcher. Source callbacks never write stdout directly.

When a source signal is caused synchronously on the Protocol v2 request thread, it is captured into that request so the request consumes at most one engine revision and its response is written before its events. Captured-event deduplication is scoped by both source handle and event name, so unrelated sources cannot suppress one another. Multiple events caused by one request carry the same committed revision.

A source signal arriving on another thread while a request mutation is in progress is not folded into that request. Its normalized event batch is placed in a bounded deferred bridge queue, then committed at its own later revision after the request response and request-generated events have been queued. This also prevents a source callback from blocking on the request's revision lock while libobs is holding its signal mutex. If the deferred bridge queue overflows, the engine invalidates queued deltas and emits `session.resyncRequired` rather than silently losing canonical state.

Runtime mutating requests establish the source-capture gate **before** acquiring the revision mutation guard. Source callbacks already in flight before that gate are allowed to finish or defer, and their queued batches are drained to independent revisions before `ifRevision` is validated. This keeps the revision guard from hiding source state that crossed the request's serialization boundary and avoids a libobs-signal-mutex/revision-mutex lock cycle.

When libobs produces a source state signal outside a Protocol v2 request, the bridge commits one engine revision for that signal and publishes all normalized changes detected for that source at that revision.

The bridge is explicitly detached before `RevisionState`, `EventDispatcher`, the protocol writer, or libobs are destroyed, preventing callbacks from retaining dead protocol infrastructure during shutdown.

## Migration constraint

Legacy Protocol v1 state mutations still do not participate in Protocol v2 revisions/events. A production Controller must not mix legacy-v1 mutations with Protocol v2 mutations in the same engine process.

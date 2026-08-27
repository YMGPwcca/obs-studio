# Protocol v2 properties API v1

This document defines the Task 7 `properties.*` surface. It is a toolkit-neutral bridge over the native libobs properties API; the Controller must render the returned schema instead of hard-coding plugin-specific settings forms.

## Capabilities

Task 7 advertises:

- `properties.v1`
- `properties.get.v1`
- `properties.resolve.v1`
- `properties.getListItems.v1`
- `properties.invokeButton.v1`
- `properties.validate.v1`
- `properties.refresh.v1`

The six methods are implemented generically, but Task 7 intentionally supports only source-kind and live-source targets. Later object namespaces can add more target types without changing these method names or the schema format.

## Targets

A source-kind target addresses the property form exposed by a registered source kind and starts from that kind's libobs defaults:

```json
{"target":{"type":"sourceKind","kind":"slideshow_v2"}}
```

A live-source target addresses an existing Protocol v2 source handle and starts from that source's current libobs settings:

```json
{"target":{"type":"source","source":"2"}}
```

Runtime handles keep the canonical unsigned-decimal string rule from Task 6. Unsupported target types return `unsupported_capability`; missing or non-configurable targets return `not_found` or `not_available` as appropriate.

## Common result document

Property reads and resolutions return a document containing:

```json
{
  "target": {"type":"sourceKind","kind":"slideshow_v2"},
  "settings": {},
  "properties": [],
  "deferUpdate": false,
  "requiresRefresh": false
}
```

`settings` is a working effective-settings snapshot with libobs defaults materialized. It is not a request to persist anything.

### Sensitive text

`OBS_TEXT_PASSWORD` properties are emitted with:

```json
{"type":"text","textType":"password","sensitive":true,"hasValue":true}
```

The corresponding value is removed from returned `settings`. The Controller must not depend on reading a stored password back from the engine. A new password can still be supplied as candidate settings when the surrounding source-setting operation eventually commits it.

## Schema fields

Every property includes:

- `name`
- `type`
- `description`
- optional `longDescription`
- `visible`
- `enabled`
- optional `requiresRefresh` for the property whose callback requested a rebuild

Task 7 maps all current libobs property kinds:

- `bool`
- `int` with `min`, `max`, `step`, `numberType`, optional `suffix`
- `float` with the same numeric metadata
- `text` with `textType`, `monospace`, and info/password metadata
- `path` with `pathType`, optional `filter` and `defaultPath`
- `list` with `comboType`, `valueType`, and `itemCount`
- `color` / `colorAlpha` using the native OBS 32-bit color integer encoding
- `button` with `buttonType`; URL buttons include `url`
- `font`
- `editableList` with list/filter/default-path metadata
- `frameRate` with plugin-defined options and rational min/max ranges
- `group` with `groupType` and recursive `children`

The base schema deliberately does **not** inline list choices. Real plugins can expose very large device/window/plugin lists, so the Controller retrieves choices on demand with `properties.getListItems`. This keeps normal form discovery bounded by schema size rather than the size of every dynamic list. Internally, list contents still participate in schema-change detection.

List entries returned by `properties.getListItems` contain `name`, typed `value`, and `disabled`.

## `properties.get`

Read-only. Returns the currently resolved schema and effective settings for the target. libobs instance/kind property constructors have already applied their normal settings callbacks before serialization.

## `properties.resolve`

Read-only with respect to the Engine Protocol state model. Request:

```json
{
  "target":{"type":"sourceKind","kind":"slideshow_v2"},
  "settings":{"slide_mode":"mode_manual"},
  "changedProperty":"slide_mode"
}
```

The engine clones the target settings, applies the candidate object to the clone, and runs native libobs property modification callbacks. If `changedProperty` is supplied, that property's callback is invoked; otherwise the full libobs property-settings application pass runs.

The returned schema/settings represent the resolved working copy only. `properties.resolve` never calls `obs_source_update` and never increments the engine revision. Native property callbacks are plugin code and are invoked for the same purpose as the OBS properties UI: resolving form state from candidate settings.

`requiresRefresh` becomes true if the native callback requests a rebuild or if the full internal serialized schema, including dynamic list contents, changed while resolving the candidate.

## `properties.getListItems`

Read-only. Requires `property` naming a list property. Optional candidate `settings` and `changedProperty` can first resolve a dynamic form. The response includes the common property document plus:

```json
{"property":"playback_behavior","itemCount":3,"items":[]}
```

This is the only Task 7 wire path that returns list choices, so a Controller can refresh large or dynamic lists without inflating every normal schema response.

## `properties.validate`

Read-only. Returns structural validation results such as:

```json
{
  "valid": false,
  "issues": [
    {"property":"transition_speed","code":"range","message":"integer value is outside the property range"}
  ]
}
```

The validator checks constraints that libobs exposes structurally: scalar types/ranges, list value types and enabled choices, 32-bit color range, font object shape, editable-list array shape, and checkable-group booleans.

Obviously invalid values are rejected structurally before plugin modification callbacks run. Structurally valid candidates are then resolved through the native dynamic-property callbacks and validated again against the resulting form.

Unknown plugin-private settings are not rejected. Frame-rate storage is deliberately not over-constrained because libobs/plugins use multiple representations. This is structural Controller-side validation, not a promise that every plugin-specific semantic error can be detected before the plugin processes settings.

## `properties.refresh`

Read-only. Discards any previous working form and rebuilds properties/settings directly from the current target. Returns `refreshed: true`.

## `properties.invokeButton`

A button must exist and currently be visible and enabled.

### URL buttons

URL buttons are never executed inside the engine. The response contains:

```json
{"property":"docs","buttonType":"url","invoked":false,"url":"https://example.invalid"}
```

The private Controller decides whether/how to present or open that URL and must apply its own URL-scheme safety policy. URL-button inspection does not increment the revision.

### Default plugin buttons

Non-URL callbacks execute only for live-source targets in Task 7. A source-kind form has no live plugin instance, so attempting to invoke a normal button on `sourceKind` returns `not_available` without calling the plugin.

Executing a live plugin button is treated conservatively as an engine mutation because arbitrary plugin callback code may alter instance state. `ifRevision` is therefore permitted, and a successful callback invocation consumes exactly one public revision even if the plugin reports that no form rebuild is necessary. After the callback, the engine refreshes live settings and, when requested by the callback, rebuilds the property form; failure to obtain a new form falls back to the already-valid form so revision accounting is never silently lost after callback execution.

Task 7 does not synthesize a source-setting event for arbitrary button callbacks because libobs does not provide a generic declaration of which state the callback changed. The invoking Controller already receives the refreshed result and new revision; future object-specific signal bridges can expose independent plugin-originated changes.

## Revision rules

`properties.get`, `resolve`, `getListItems`, `validate`, and `refresh` are read-only and reject `ifRevision` under the existing Protocol v2 guard rules.

`properties.invokeButton` may mutate, so it accepts `ifRevision`. A stale guard prevents the callback from running. URL buttons return without consuming a revision; a successfully invoked non-URL live-source callback consumes one revision.

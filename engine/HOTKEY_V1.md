# Engine Protocol v2 — `hotkey.v1`

Task 22 exposes the live libobs hotkey registry without exposing native key
codes or raw `obs_hotkey_id` values. Hotkeys and bindings are runtime-only;
the Controller owns any persistent mapping to its project objects.

## Semantic identity

Every hotkey is selected by:

```json
{
  "registerer": { "type": "frontend" },
  "name": "task22.frontend"
}
```

Registerer types are `frontend`, `source`, `output`, `encoder`, and `service`.
Engine-owned Sources use their canonical Source handle. A libobs object that
is outside the Engine registry is represented only by bounded runtime metadata
(`runtimeId`, `name`, and `kind`) and never by a pointer or native address.
Pair members include a `pairPartner` semantic identity.

The response fields are `registerer`, `name`, `description`, optional
`pairPartner`, and, for `hotkey.get`/export, normalized `bindings`. No raw
libobs ID is serialized.

## Bindings

Bindings use semantic OBS key names and an explicit modifier array:

```json
{
  "key": "OBS_KEY_F5",
  "modifiers": [{ "name": "control" }, { "name": "shift" }]
}
```

The accepted modifiers are `shift`, `control`, `alt`, and `command`. The
bridge sorts modifiers in that order, removes duplicate modifiers and
duplicate combinations, and validates the complete array before applying it.
An empty `bindings` array clears all bindings. Native virtual-key integers and
`obs_hotkey_inject_event` are not part of this contract.

`hotkey.setBindings` rejects combinations already owned by another hotkey with
`already_exists`; failed validation does not call libobs and leaves existing
bindings unchanged. `hotkey.import` validates every entry, selector, key,
modifier, duplicate and conflict before applying any update. Export/import is
runtime binding serialization, not Controller project persistence.

## Methods

```text
hotkey.list
hotkey.get                         {registerer,name}
hotkey.getBindings                 {registerer,name}
hotkey.setBindings                 {registerer,name,bindings:[...]}
hotkey.clearBindings               {registerer,name}
hotkey.trigger                     {registerer,name,action:press|release|click}
hotkey.getKeyName                  {key}
hotkey.getKeyCombinationName       {binding:{key,modifiers}}
hotkey.getConflicts                {binding:{key,modifiers}}
hotkey.getBackgroundCapture
hotkey.setBackgroundCapture        {enabled}
hotkey.export
hotkey.import                      {hotkeys:[...export entries...]}
```

`hotkey.trigger` invokes only the currently resolved registered libobs
callback. `click` is one press followed by one release. The Engine uses the
libobs callback-routing API, preserves the original callback, and never calls
OS injection APIs. A callback-only trigger emits `hotkey.triggered` as
telemetry and consumes no mutation revision. If the callback changes observed
Engine state, that namespace owns the resulting canonical state event and
revision.

Background capture is the live libobs background-press setting. A change emits
the canonical `hotkey.backgroundCaptureChanged` state event and consumes one
mutation revision.

## Events and lifetime

Canonical events are `hotkey.bindingsChanged` and
`hotkey.backgroundCaptureChanged`. `hotkey.triggered` is telemetry, is bounded
and coalescible, and carries `pressed` plus semantic hotkey identity. Trigger
callbacks only enqueue bounded metadata; protocol output is written by the
normal dispatcher path. Routing and its tick callback are disabled before
Engine Source teardown and shutdown.

Stable errors include `bad_request`, `not_found`, `already_exists`,
`unsupported_capability`, `not_available`, `revision_conflict`, and
`internal_error`.

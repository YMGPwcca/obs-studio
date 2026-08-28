# Engine Protocol v2 — Interaction v1

This document fixes the concrete JSON schema for the `interaction.*` methods listed in `PROTOCOL_V2.md`.

## Semantics

- Every method requires `params.source` as a canonical decimal engine source handle string.
- The target source MUST advertise `OBS_SOURCE_INTERACTION`; otherwise the engine returns `unsupported_capability`.
- Interaction delivery is transient input, not canonical engine state. Successful interaction calls do not increment the engine revision and do not emit an interaction event solely for delivery.
- `ifRevision` is therefore invalid on `interaction.*` methods and is rejected by the common protocol guard.
- If a plugin independently changes canonical source state while handling input, that state change is reported through its normal source event/revision path.
- No platform pointer, HWND, native message identifier, `WPARAM` or `LPARAM` crosses this API.
- Native key numeric fields are bounded 32-bit metadata passed only to libobs `obs_key_event`; the engine never injects a native OS window message.

A successful response has the normal v2 envelope and at minimum:

```json
{"source":"1"}
```

## Modifier object

Pointer/key methods may include:

```json
"modifiers": {
  "capsLock": false,
  "shift": false,
  "control": false,
  "alt": false,
  "mouseLeft": false,
  "mouseMiddle": false,
  "mouseRight": false,
  "command": false,
  "numLock": false,
  "keypad": false,
  "left": false,
  "right": false
}
```

Every listed field is optional and defaults to `false`. Present fields MUST be booleans. They map to the corresponding libobs `INTERACT_*` flags. The protocol does not expose a raw modifier bitmask.

## `interaction.focus`

```json
{
  "source":"1",
  "focused":true
}
```

`focused` is required and boolean. The engine calls `obs_source_send_focus`.

## `interaction.mouseMove`

```json
{
  "source":"1",
  "x":120,
  "y":80,
  "leave":false,
  "modifiers":{"shift":true,"mouseLeft":true}
}
```

- `x` and `y` are required signed 32-bit source-local logical coordinates.
- `leave` is optional and defaults to `false`.
- When `leave=false`, the point must be within `[0,width) × [0,height)`, matching source-local pixel coordinates.
- When `leave=true`, out-of-bounds coordinates are accepted because the leave notification itself is authoritative.

The engine calls `obs_source_send_mouse_move`.

## `interaction.mouseButton`

```json
{
  "source":"1",
  "x":120,
  "y":80,
  "button":"left",
  "state":"down",
  "clickCount":1,
  "modifiers":{"mouseLeft":true}
}
```

- `button` is one of `left`, `middle`, `right`.
- `state` is `down` or `up`.
- `clickCount` is optional, defaults to `1`, and is limited to `1..3`.
- Mouse-down must be inside `[0,width) × [0,height)`. Mouse-up may be outside so a drag can terminate after leaving the source.

The engine maps the button enum and calls `obs_source_send_mouse_click`.

## `interaction.mouseWheel`

```json
{
  "source":"1",
  "x":120,
  "y":80,
  "deltaX":0,
  "deltaY":120,
  "modifiers":{"control":true}
}
```

Coordinates and deltas are required signed 32-bit integers. The point must be inside `[0,width) × [0,height)` and at least one delta must be non-zero. The engine calls `obs_source_send_mouse_wheel`.

## `interaction.key`

```json
{
  "source":"1",
  "state":"down",
  "text":"a",
  "modifiers":{"shift":true},
  "nativeModifiers":0,
  "nativeScanCode":30,
  "nativeVirtualKey":65
}
```

- `state` is required and is `down` or `up`.
- `text` is optional and may contain zero or one valid, non-NUL UTF-8 Unicode scalar value.
- `nativeModifiers`, `nativeScanCode`, and `nativeVirtualKey` are optional unsigned 32-bit integers and default to zero.
- At least one of non-empty `text`, non-zero `nativeScanCode`, or non-zero `nativeVirtualKey` is required.
- These native values are metadata for the libobs source callback only; they are never used for OS message injection.
- The engine tracks at most 256 distinct simultaneously held key identities per source. A new distinct key-down beyond that bound returns `busy` and is not delivered. Repeated key-down for an already tracked identity remains deliverable.

The engine fills `obs_key_event` and calls `obs_source_send_key_click`. U+0000 is rejected because libobs exposes `obs_key_event.text` as a NUL-terminated C string without a separate byte length.

## `interaction.text`

```json
{
  "source":"1",
  "text":"Hello",
  "modifiers":{}
}
```

`text` is required, non-empty valid UTF-8, contains no U+0000 scalar, and is limited to 4096 bytes and 1024 Unicode scalar values. libobs has no separate text callback, so the engine deterministically synthesizes one `key_click` down/up pair per Unicode scalar with native key fields set to zero. This keeps text delivery inside the generic libobs interaction boundary.

## `interaction.reset`

```json
{"source":"1"}
```

The engine tracks input state that it delivered for each source. Reset:

1. releases mouse buttons still held by this protocol session;
2. sends key-up for keys still held by this protocol session;
3. sends a mouse-leave notification;
4. sends focus-out.

The success payload additionally reports `releasedKeys` and `releasedButtons`. Reset state is transient and does not consume a revision. Per-source tracking is bounded by the live source set and stale source tracking is pruned opportunistically from the interaction path.

## Stable errors

- `bad_request`: malformed handle or interaction fields;
- `not_found`: source handle does not exist;
- `unsupported_capability`: source does not advertise `OBS_SOURCE_INTERACTION`;
- `busy`: a new distinct key-down would exceed the 256-key held-state bound for that source.

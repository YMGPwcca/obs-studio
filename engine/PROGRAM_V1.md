# Engine Protocol v2 — Program v1

Program is the actual routed scene on Main Canvas channel `0`. The engine reads
that channel when answering `program.getScene`, so the response cannot drift
from libobs routing state. Program and Preview are separate logical states;
Studio mode does not change the immediate behavior of `program.setScene`.

## Methods

```text
program.getScene
program.setScene
```

`program.setScene` requires `params.scene` as a canonical positive decimal
scene handle string or JSON null. Null clears Main Canvas channel `0`. A scene
must already be registered by the engine; Program has no cross-Canvas scene
restriction because libobs accepts a scene source on the Main Canvas channel.

Successful changes return:

```json
{
  "scene":"2",
  "canvas":"1"
}
```

or, when cleared:

```json
{
  "scene":null,
  "canvas":"1"
}
```

`program.getScene` reports the canonical scene mapping of the live Main Canvas
channel. If another API has routed a non-scene source there, the scene field is
null rather than an engine-side cached scene claim.

## Events and revisions

Each real route change consumes one global engine revision and emits exactly one
`program.sceneChanged` event after its response. The event contains `scene` as a
handle or null, `previousScene` when known, and the Main Canvas handle. Equal
routes are successful no-ops and do not consume a revision. Malformed handles
return `bad_request`; missing scenes return `not_found`; a Main Canvas routing
failure returns `obs_error`.

When a current Program Scene is removed, the engine clears Main Canvas channel
`0`, emits `program.sceneChanged` with `scene:null` before item/scene invalidation
events, and only then retires the Scene handle.

No raw source pointer, native window handle, or pixel data crosses the protocol.

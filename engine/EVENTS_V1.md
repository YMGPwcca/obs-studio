# obs-engine event delivery v1

This document is the implementation contract for the `event.delivery.v1` capability described by `PROTOCOL_V2.md`.

## Event envelope

Delivered state events use the Protocol v2 event envelope:

```json
{
  "op": "event",
  "seq": 1,
  "revision": 12,
  "event": "engine.stopping",
  "data": {}
}
```

Telemetry events additionally contain:

```json
{"telemetry": true}
```

`seq` starts at 1 for each engine process/session and increases once for each event actually emitted to the protocol writer. Coalesced or dropped telemetry does not consume a sequence number.

`revision` is the engine revision associated with the event. Session-local subscription changes do not change the engine revision.

## Subscription methods

`session.subscribe`, `session.unsubscribe`, and `session.getSubscriptions` are available when their corresponding capabilities are advertised.

`session.subscribe` accepts one or more subscription descriptors:

```json
{
  "op": "request",
  "id": "r-subscribe",
  "method": "session.subscribe",
  "params": {
    "subscriptions": [
      {"pattern": "engine.*"},
      {"pattern": "audio.meter", "telemetry": true}
    ]
  }
}
```

A pattern is either:

- an exact event name such as `engine.stopping`; or
- a terminal namespace wildcard such as `source.*`.

A namespace wildcard matches all descendants of that namespace. Global `*`, empty namespace segments, and non-terminal wildcards are invalid.

`telemetry` defaults to `false`. Telemetry is delivered only when at least one matching effective subscription has `telemetry: true`.

The session stores at most one effective descriptor for each exact pattern. Re-subscribing to the same pattern is idempotent. Re-subscribing with `telemetry: true` upgrades that pattern to telemetry-enabled. Overlap between an exact pattern and a wildcard does not duplicate an event.

Successful subscription methods return the complete effective subscription set, sorted by pattern. These methods operate on session state and therefore do not increment the engine mutation revision. `ifRevision` is not accepted on them.

`session.unsubscribe` removes the named effective pattern. Removing a pattern that is not present is idempotent. Unsubscribe descriptors identify only the pattern; callers SHOULD omit `telemetry`.

Events already accepted into the queue before an unsubscribe may still be delivered. The unsubscribe affects future publication matching.

## Bounded delivery and overflow

libobs callbacks and other producers do not write stdout. They normalize/copy event data into the bounded event dispatcher. A separate dispatcher hands complete JSON messages to the single protocol writer. The protocol writer backlog is bounded as well, so a controller that stops reading stdout cannot turn the writer into an unbounded memory sink. Backpressure occurs after the callback-facing event queue boundary; callback producers never wait on stdout.

The current event queue capacity is 1024 normalized events. The current protocol-writer backlog capacity is also 1024 complete JSON lines. These capacities are implementation limits, not promises that controllers should rely on.

Canonical state events are never silently dropped. When a state event arrives to a full event queue, the dispatcher first evicts one pending telemetry event if one is available. This preserves canonical state without forcing an unnecessary resync.

If no disposable telemetry remains and canonical state delivery still cannot be preserved:

1. queued events whose delivery can no longer be guaranteed are invalidated;
2. the engine emits the mandatory event `session.resyncRequired`, regardless of the current subscription set;
3. its `revision` is the highest revision among the invalidated queued event(s) and the event that detected the overflow;
4. `data.reason` is `event_queue_overflow`.

After `session.resyncRequired`, the controller MUST treat its cached engine state as potentially stale and reconstruct/resynchronize canonical state before relying on subsequent deltas.

## Telemetry policy

Telemetry is explicitly lossy and opt-in.

- If another pending telemetry event has the same event name, the newer sample replaces it and is moved to the back of the event queue so revision/order does not move backwards relative to intervening state events.
- If the queue is full and the telemetry event cannot be coalesced, that telemetry event may be dropped.
- Pending telemetry may be evicted to make room for a canonical state event.
- Telemetry coalescing/drop/eviction does not itself emit `session.resyncRequired`, because telemetry is not canonical state.

`transition.progress` is the bounded transition telemetry stream. It requires
an exact `transition.progress` or matching `transition.*` subscription with
`telemetry: true`, is sampled at approximately 10 Hz from the render path, and
is published through the same bounded dispatcher. It carries the canonical
transition handle, finite normalized `progress` in `0..1`, and
`state:"running"`. It never consumes a mutation revision. A failed nonblocking
telemetry enqueue is allowed to drop the sample and must not block rendering or
request a canonical resync.

## Ordering

The protocol writer is the only component that writes complete stdout lines after startup. Responses and events therefore cannot interleave bytes.

For the Task 5 `session.close` path, the successful response is queued before the subscribed `engine.stopping` event. Both carry the committed revision, and `engine.stopping` is emitted once even when multiple effective patterns match it.

Future synchronous libobs callback integration must preserve the same single-writer boundary and must not write stdout from callback threads.

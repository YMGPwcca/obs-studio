# Engine Protocol v2 — Media v1

This document fixes the concrete JSON contract for the `media.*` namespace. The
namespace controls only live sources that advertise
`OBS_SOURCE_CONTROLLABLE_MEDIA`. Handles are ephemeral engine-session handles and
are always canonical decimal strings.

## Common target and capability

Every method requires:

```json
{"source":"1"}
```

The engine validates the handle, resolves the live source, and checks the
`OBS_SOURCE_CONTROLLABLE_MEDIA` output flag. A malformed handle returns
`bad_request`, an absent handle returns `not_found`, and a source without the flag
returns `unsupported_capability`.

The engine advertises these stable capabilities when the namespace is built:

- `media.v1`
- `media.getState.v1`
- `media.play.v1`
- `media.pause.v1`
- `media.togglePause.v1`
- `media.stop.v1`
- `media.restart.v1`
- `media.next.v1`
- `media.previous.v1`
- `media.getDuration.v1`
- `media.getPosition.v1`
- `media.setPosition.v1`

## State values

`media.getState` and media event snapshots use these stable strings:

| libobs state | Protocol value |
| --- | --- |
| `OBS_MEDIA_STATE_NONE` | `none` |
| `OBS_MEDIA_STATE_PLAYING` | `playing` |
| `OBS_MEDIA_STATE_OPENING` | `opening` |
| `OBS_MEDIA_STATE_BUFFERING` | `buffering` |
| `OBS_MEDIA_STATE_PAUSED` | `paused` |
| `OBS_MEDIA_STATE_STOPPED` | `stopped` |
| `OBS_MEDIA_STATE_ENDED` | `ended` |
| `OBS_MEDIA_STATE_ERROR` | `error` |

Unknown future enum values map to `unknown`. The protocol never uses a native
numeric enum as the primary state value.

## Queries

### `media.getState`

Read-only. Returns:

```json
{"source":"1","state":"playing"}
```

The result is a point-in-time libobs callback snapshot. It does not increment the
engine revision and rejects `ifRevision`.

### `media.getDuration`

Read-only. Returns a signed 64-bit millisecond value:

```json
{"source":"1","durationMs":12345}
```

Non-negative values are durations. A negative value means the source reports an
unknown/unavailable duration; zero is preserved because it is also the libobs
value for an empty or not-yet-open media instance. The value is never cast to an
unsigned type.

### `media.getPosition`

Read-only. Returns the signed 64-bit millisecond snapshot from libobs:

```json
{"source":"1","positionMs":2345}
```

Negative values are preserved as an unknown/unavailable position. Position
queries are telemetry-like snapshots and never consume a revision.

## Transport commands

`media.play`, `media.pause`, `media.stop`, `media.restart`, `media.next`, and
`media.previous` return:

```json
{
  "source":"1",
  "action":"play",
  "state":"playing",
  "processed":true
}
```

`action` is the semantic command name. `processed` is true only after the
corresponding libobs media-action signal has been observed. A command-local
idempotent no-op returns `processed:false` and does not consume a revision:

- `play` while `playing`;
- `pause` while `paused`;
- `stop` while `stopped`.

Restart, next, and previous are always submitted because they can change the
selected entry or playback position even when the state enum is unchanged. A
processed action signal is the observable canonical effect for these generic
operations; a plugin that has no corresponding callback cannot produce that
signal and the request fails with a bounded `timeout`.

### `media.play`

Submits `obs_source_media_play_pause(source, false)` unless the source is already
`playing`.

### `media.pause`

Submits `obs_source_media_play_pause(source, true)` unless the source is already
`paused`.

### `media.togglePause`

The pre-command state selects the action atomically with request handling:

- `playing`, `opening`, or `buffering` selects pause;
- `paused` selects play;
- `none`, `stopped`, `ended`, `error`, and `unknown` return `invalid_state`.

The selected action is then settled like `media.play` or `media.pause`.

### `media.stop`

Submits `obs_source_media_stop(source)` unless the source is already `stopped`.

### `media.restart`

Submits `obs_source_media_restart(source)`. The settled response reports the
state observed at the action signal; a later `started`/`playing` transition is
reported independently if the source opens asynchronously.

### `media.next` / `media.previous`

Submit the corresponding public libobs action and settle on its action signal.
The generic protocol does not expose plugin-private playlist indexes.

### `media.setPosition`

Request:

```json
{"source":"1","positionMs":2345}
```

`positionMs` is required, an integer in `0..INT64_MAX`. If the current duration
is non-negative, the requested position must not exceed it. If duration is
negative, any non-negative signed 64-bit position is accepted. The engine
submits `obs_source_media_set_time(source, positionMs)` and waits for the fork's
internal `media_time` signal emitted after the queued plugin callback returns.

The result is:

```json
{
  "source":"1",
  "positionMs":2345,
  "state":"playing",
  "processed":true
}
```

The returned position is a fresh libobs snapshot. Position progression itself is
not a revisioned event stream.

## Events

All media state events are state events, not telemetry. Their `source` value is a
canonical handle. A command-owned event uses the command response revision and
is published after the response. An unrelated callback receives its own later
revision. Event sequence numbers follow `EVENTS_V1.md`.

### State transition event

`media.stateChanged` is emitted when the observed libobs state enum changes:

```json
{
  "source":"1",
  "state":"paused"
}
```

Only the newly observed state is reported. Plugins can change their internal
state without emitting a generic libobs state signal, so the bridge does not
claim a potentially stale `previousState` value.

### Dedicated lifecycle/action events

These events have the following payload shape:

```json
{"source":"1","state":"playing"}
```

- `media.playing` — the core `media_play` action signal was processed and the
  resulting state is `playing`;
- `media.paused` — the core `media_pause` action signal was processed and the
  resulting state is `paused`;
- `media.stopped` — the core `media_stopped` action signal was processed and the
  resulting state is `stopped`;
- `media.started` — the source emitted its libobs `media_started` signal;
- `media.ended` — the source emitted its libobs `media_ended` signal;
- `media.error` — an observed media signal found a transition into
  `OBS_MEDIA_STATE_ERROR`.

There is no generic libobs `media_error` signal in this upstream snapshot. A
source that enters `ERROR` without emitting another observable media signal can
still be queried with `media.getState`, but cannot produce a real-time
`media.error` event through this bridge.

No `media.positionChanged` event is defined. Natural position progression must
not churn the canonical engine revision.

## Asynchronous settlement and errors

libobs enqueues media actions and processes them from the video source tick. The
engine connects an action-specific waiter before enqueueing, observes the
source's normalized signal batch, and waits up to five seconds for completion.
No wall-clock sleep is used.

If the signal is not observed before the deadline, the request returns
`timeout` without claiming a successful mutation. Because the action may have
executed after observation was lost, the media bridge marks incremental delivery
uncertain and the normal event flush emits mandatory
`session.resyncRequired` with reason `event_queue_overflow`.

Deferred media batches are bounded. They are correlated by source handle and
action signal; batches for other sources remain independent and receive their
own revisions. Observer teardown occurs before source/libobs shutdown and before
engine-owned source references are released.

## Revision rules

- `getState`, `getDuration`, and `getPosition` are read-only and reject
  `ifRevision`.
- A successful idempotent no-op does not consume a revision.
- A successfully processed play, pause, stop, or seek consumes one revision
  only when the resulting state/position or an observed lifecycle event changes
  canonical state. Restart/next/previous consume one revision after their core
  action signal because their selected-entry effect is not generically
  introspectable, even when the state enum is unchanged.
- A failed validation, unsupported target, stale guard, or timeout does not
  claim a successful command revision.
- Natural started/ended/error/state transitions outside a request receive their
  own revision.

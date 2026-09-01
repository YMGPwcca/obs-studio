# Engine Protocol v2 — Studio v1

Studio mode is a small engine-side orchestration state. It does not duplicate
the canonical Program or Preview Scene values. Program remains actual Main
Canvas channel `0`; Preview remains the independent logical Preview slot.

## Methods

```text
studio.getEnabled
studio.setEnabled
studio.getTransition
studio.setTransition
studio.getTransitionDuration
studio.setTransitionDuration
studio.transition
```

T-Bar methods are intentionally not implemented or advertised because this
headless Phase-2 engine has no verified T-Bar contract.

`studio.setEnabled` changes only Studio enabled state. Disabling Studio while a
transition is running does not abandon the libobs transition; its completion
still commits the destination Program Scene safely. A disabled Studio rejects
`studio.transition` with `invalid_state`.

`studio.setTransition` accepts a canonical transition handle or JSON null. A
selected/running transition cannot be replaced while active. Duration methods
operate on the selected Transition's single engine-owned `durationMs` value.
Both setters publish the canonical `transition.durationChanged` event; Studio
passes that value to `obs_transition_start`, so no second contradictory
duration cache exists.

## Preview-to-Program transition

`studio.transition` validates enabled Studio, a live selected Transition, a
live Preview Scene/source, and a non-running transition. If Preview already
equals Program it is a successful no-op. Otherwise the engine:

1. assigns the current Program source to transition source A;
2. assigns the Preview source to transition source B;
3. starts the real libobs transition with the selected duration;
4. routes the transition source through Main Canvas channel `0` while running;
5. commits the destination Scene to Program only after libobs emits
   `transition_video_stop`.

The start response includes the selected transition, source/destination Scene
handles, duration, and `state:"running"`. It emits one command-owned
`transition.started` event at that mutation revision. Program keeps its prior
logical Scene during the animation and reports `transitioning:true`.

At completion, the queued transition observer atomically routes the destination
Scene, clears the transition-running state, and publishes one independent
revision containing `program.sceneChanged` followed by `transition.ended`. The
Program event has the destination in `scene` and the prior Program in
`previousScene`. Transition progress is telemetry and never creates a revision
per frame.

Direct `program.setScene` remains immediate even while Studio is enabled. If a
Studio transition is active, the command synchronously stops and clears the
real libobs transition, suppresses its callback settlement, applies the
requested Program route, and owns one command revision containing
`program.sceneChanged` followed by exactly one `transition.ended`. The
`program.sceneChanged.previousScene` value is the logical Program Scene from
before the transition, not the temporary transition source. Removing a
selected Transition returns `object_in_use`.

## Events and errors

Canonical Studio events are:

```text
studio.enabledChanged
studio.transitionChanged
```

`studio.setTransitionDuration` is a convenience setter for the selected
Transition and does not emit a Studio duration alias.

Program and Preview keep ownership of `program.sceneChanged` and
`preview.sceneChanged`; Studio does not emit aliases for them. Stable errors
include `bad_request`, `not_found`, `not_available`, `invalid_state`, `busy`,
and `revision_conflict`.

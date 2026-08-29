# Task 11 — `filter.*` plan and acceptance record

**Status:** IMPLEMENTED / IN REVIEW / NOT ACCEPTED
**Accepted base:** `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`
**Quarantined reference only:** `4c8b616ca2115970af3e1e4000b162416be32dac`

## Goal

Expose filters attached to engine-managed sources through one semantic Protocol
v2 namespace, reusing generic `properties.*` and preserving the accepted
revision/event/callback boundary.

Methods:

- `filter.kindList`
- `filter.kindDefaults`
- `filter.kindProperties`
- `filter.list`
- `filter.get`
- `filter.create`
- `filter.remove`
- `filter.rename`
- `filter.duplicate`
- `filter.getSettings`
- `filter.patchSettings`
- `filter.replaceSettings`
- `filter.setEnabled`
- `filter.getEnabled`
- `filter.setOrder`
- `filter.moveUp`
- `filter.moveDown`
- `filter.moveTop`
- `filter.moveBottom`

Events:

- `filter.created`
- `filter.removed`
- `filter.renamed`
- `filter.settingsChanged`
- `filter.enabledChanged`
- `filter.orderChanged`

The concrete wire contract is `engine/FILTER_V1.md`.

## Source/lifetime decisions

- Filter handles are new ephemeral engine handles; no raw filter/parent pointer
  crosses the protocol.
- The engine keeps an explicit filter-handle -> parent-source-handle relation.
  It does not use `obs_filter_get_parent` or `obs_filter_get_target` as general
  lifetime APIs.
- The engine keeps one reference for every tracked filter in addition to the
  parent-owned filter reference.
- Create/remove/reorder are synchronous graph mutations in current libobs.
- Video-filter settings are asynchronous at plugin-callback level: libobs applies
  the settings object synchronously, then may defer `update` to the video thread.
  Settlement must correlate the permanent observer's update generation plus
  canonical settings, not install a temporary signal callback.
- Parent removal invalidates filter handles and emits `filter.removed` before
  `source.removed` at the same command revision.
- `source.duplicate` copies libobs filters as nested source state. Copied
  filters receive fresh handles when registered/discovered, but Task 11 does
  not synthesize `filter.created` events into the already-accepted
  `source.duplicate` command. The controller discovers them with
  `filter.list(newSource)`.

## Concurrency invariants

The common mutating runtime order remains:

1. establish source/media/filter capture gates;
2. wait for previously-direct callbacks to retire;
3. acquire the global mutation/revision lock;
4. drain pre-command deferred callbacks;
5. validate `ifRevision`;
6. execute the mutation;
7. settle only command-owned asynchronous filter updates;
8. synchronize observers while capture is still active;
9. commit at most one command revision;
10. response first, then command-owned events;
11. flush unrelated deferred callbacks independently.

A filter settings request never connects/disconnects a per-request signal
handler. A timeout/ownership failure marks the bridge uncertain and forces
`session.resyncRequired` rather than claiming an unrelated callback.

## Deterministic verification requirements

The Task-11 lane must cover at minimum:

- capabilities/kind discovery/defaults/properties;
- create/list/get/remove/rename/duplicate;
- generic `properties.get` for live filters;
- patch/replace deferred video-filter settings settlement;
- enable get/set and idempotence;
- absolute and relative ordering plus bounds/no-op behavior;
- stale `ifRevision` queues/applies no mutation;
- source removal emits filter removals before source removal;
- source duplication preserves the accepted Task-8 wire contract: only the
  existing `source.created` event is emitted; `filter.list(newSource)` exposes
  inherited filters with fresh ephemeral handles;
- canonical filter handles become stale after removal;
- production package excludes the Task-11 fixture, frontend, WebSocket and
  browser modules;
- clean shutdown.

The final exact SHA must run all Task 1–11 project lanes, including Task-8
concurrency and Task-10 media regression. Physical Windows acceptance remains a
separate final gate because deferred filter updates depend on the real video
thread/runtime.

## Gate

Task 11 remains **NOT ACCEPTED** until source review, deterministic hosted CI,
package audit, physical Windows acceptance where required, independent final
review and explicit human approval are complete. Task 12 is not authorized.

# Task 11 — `filter.*` plan and acceptance record

## Status

**IN PROGRESS.** The source audit and protocol freeze are complete. Production
implementation and deterministic acceptance are the remaining Task 11 gates.

## Source-verified design

The current libobs branch represents a filter as an `obs_source_t` with
`OBS_SOURCE_TYPE_FILTER`. `obs_source_filter_add` takes and stores a parent
reference but has no success return; the engine verifies attachment with
`obs_source_filter_get_index`. `obs_source_filter_remove` releases the
parent-owned reference. An engine-owned filter map therefore retains one
independent filter reference and records the engine parent handle; no native
pointer crosses the protocol.

`obs_source_filter_set_order` reports no result, while
`obs_source_filter_set_index` accepts an unchecked `size_t`. The runtime will
validate the parent/filter relationship and index before calling either API,
then read the resulting index/order back. Relative movement intentionally
retains libobs' async-class skipping behavior.

`obs_source_update` applies settings immediately to the libobs settings object
but defers video-source update callbacks through the video tick. Filter
observers therefore use a bounded capture/deferred queue and settings equality
to settle command-owned video-filter updates. A timeout forces resynchronization
instead of claiming an unobserved callback. Synchronous rename and enable
signals are normalized by the same observer so command events do not get
duplicated.

`obs_source_enum_filters` invokes callbacks while holding the filter mutex and
enumerates in reverse array order. The engine only copies/refcounts filter
references in that callback and performs later map/order work outside the
libobs mutex. Filter handles are assigned monotonically when the engine first
observes an attached filter.

`obs_source_destroy` automatically removes attached filters. Before an
engine-managed parent is released, the runtime explicitly detaches known
filters, disconnects their observers, emits their removal events, and releases
the engine references. This makes source-removal cleanup deterministic.

Representative plugin audit covered `obs-filters` video and audio filters,
including `async-delay-filter.c`, `crop-filter.c`, and versioned color/key
filters. They use ordinary source settings/properties and may update on the
video thread; no plugin-specific UI behavior is required by this namespace.

## Implementation gates

- [x] Freeze `engine/FILTER_V1.md`.
- [ ] Add filter runtime registry and observer bridge.
- [ ] Extend generic properties targets for live filters and filter kinds.
- [ ] Add dispatcher/capability coverage for every Task 11 method.
- [ ] Add a deterministic CI-only parent/filter module with an update counter,
      order/lifetime observability, and a deliberately video-deferred update.
- [ ] Add deterministic integration coverage for all methods, stale handles,
      parent cleanup, order bounds, property reuse, revision ownership, and
      overflow/resync.
- [ ] Verify the normal package contains no Task 11 fixture.
- [ ] Run Tasks 1–10 on the same final Task 11 tree.
- [ ] Perform two review passes and commit implementation and docs cleanly.

## Known limitations to preserve

- `obs_filter_get_parent` and `obs_filter_get_target` are only guaranteed in
  filter callbacks and are not used as a general ownership API.
- The engine currently exposes source parents only; scene-source filter
  relationships remain later roadmap scope.
- Filter settings callback settlement proves the libobs update callback was
  processed and the settings snapshot matches; it does not provide a plugin
  internal rendering-completion guarantee.

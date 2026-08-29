# Task 11 implementation audit

**Status:** ACTIVE / IMPLEMENTATION AUTHORIZED / NOT ACCEPTED  
**Accepted base:** `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`  
**Accepted Task-10 implementation:** `6a590c2985a99d186c8eecd0241acdc824d32168`  
**Quarantined unauthorized reference:** `4c8b616ca2115970af3e1e4000b162416be32dac`

Task 11 was explicitly authorized after Task 10 final acceptance. The old
quarantined Task-11 commit and the later advisor WIP are design/reference
inputs only; neither is used as a wholesale history or implementation import.

## Source findings

Current libobs filter graph operations are synchronous at the graph layer:

- `obs_source_filter_add` changes the parent filter array under `filter_mutex`,
  emits `source_filter_add`/`filter_add`, then invokes the filter's `filter_add`
  callback.
- `obs_source_filter_remove` detaches the filter under `filter_mutex`, emits
  remove signals, invokes the filter's `filter_remove` callback, clears the
  parent/target links and releases the parent-owned reference.
- `obs_source_filter_set_order` and `obs_source_filter_set_index` update the
  array synchronously and emit `reorder_filters` after a real change.
- `obs_source_filter_set_index` does not provide a public bounds check; the
  protocol must validate the requested index before calling it.

Filter settings are different. `obs_source_update` applies the settings object
immediately, but a video filter defers its plugin `update` callback and `update`
signal to the video thread. Task 11 therefore needs asynchronous settings
settlement while create/remove/rename/enable/order can use normal command-owned
capture semantics.

`obs_filter_get_parent`/`obs_filter_get_target` are not general lifetime APIs;
libobs documents them for filter callback contexts. The engine retains explicit
parent-handle relationships instead.

## WIP findings and candidate corrections

The quarantined implementation was based on pre-corrective Task 10 state and
has four confirmed blockers:

1. Its protocol dispatch takes the mutation revision lock without the accepted
   `v2_wait_for_event_capture_callbacks()` pre-lock barrier, which would regress
   the Task-10 lock-order correction.
2. Deferred filter settings settlement creates a temporary `update` signal
   waiter and later disconnects it. libobs signal dispatch holds the signal mutex
   while invoking callbacks, so disconnect can block behind an unrelated or
   deliberately blocking callback. Task 11 will use the permanent filter
   observer plus a generation/condition-variable wakeup instead.
3. The commit did not add a dedicated Task-11 hosted workflow, so there was no
   exact-SHA package/integration lane for the namespace.
4. The current contract intentionally keeps copied filters nested under the
   already-accepted `source.duplicate` operation. They receive fresh handles
   when registered/discovered through `filter.list`; no synthetic
   `filter.created` event is added to the source command.

The advisor WIP at `137b2e5bd341caa1c3bc128bccd7b81376f27c32` also had these
candidate-level defects, independently verified before porting:

1. Its failing integration script assumed a default-only libobs setting was
   serialized as `settings.value`; the raw run failed in PowerShell before the
   first filter operation because `obs_data_set_default_int` does not emit a
   value in JSON.
2. Its settings settlement scanned deferred batches before taking the request
   generation baseline, and batches carried no generation. A pre-request batch
   with the same handle/settings could therefore be claimed by the request.
3. A settings timeout still returned success and did not quarantine a late
   completion from a later request.
4. Filter/source registry insertion had incomplete rollback on allocation or
   duplicate-map errors, and it carried an unused copied-filter adapter.
5. The fixture/test lane did not cover source-duplicate wire compatibility,
   inherited fresh handles, unrelated callbacks, blocking settlement, late
   completion, or deferred-queue overflow; its original 6-second blocker was
   also too close to the 5-second engine deadline for deterministic follow-up
   testing.

The candidate records an observer generation on every update observation,
requires exact `(handle, post-update settings, generation > baseline)` proof,
returns `timeout` on uncertain settlement, quarantines the affected handle,
uses rollback-safe registry updates, removes the unused adapter, and exercises
the missing cases in the exact-SHA Task-11 lane.

## Implementation rules for the authorized candidate

- Base all work on the accepted production head, not on the quarantine branch.
- Preserve the accepted capture -> wait-for-direct-callbacks -> mutation-lock ->
  deferred-drain ordering.
- Keep the permanent filter observer connected for its lifetime; no per-request
  filter signal connect/disconnect is allowed.
- Settle filter settings by exact filter handle plus canonical post-update
  settings and a permanent-observer `update` generation greater than the
  request baseline. A timeout returns `timeout`, quarantines the handle until
  the uncertain completion is observed, and forces resynchronization.
- Preserve unrelated deferred filter batches and force `session.resyncRequired`
  on settlement/queue uncertainty instead of guessing ownership.
- `source.duplicate` must register copied filters before returning without
  adding Task-11 `filter.created` events to the accepted Task-8 command.
- Parent removal emits `filter.removed` for attached known filters before
  `source.removed`, all with the parent-removal revision.
- CI-only Task-11 fixtures must be absent from normal artifacts.
- Task 12 remains unauthorized.

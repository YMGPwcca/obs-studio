# Task 11 implementation audit

**Status:** COMPLETE / ACCEPTED
**Accepted base:** `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`
**Accepted implementation:** `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
**Acceptance record:** `TASK11_ACCEPTANCE.md`
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

## Deferred-update identity finding

The current libobs implementation does not attach a request identity to its
public `update` signal. `obs_source_update()` applies into the shared
`source->context.settings` object and increments `defer_update_count` for video
sources. On the video thread, `obs_source_deferred_update()` snapshots that
counter, calls the plugin update callback with the shared current settings,
clears the counter only if no newer increment raced, and then emits one generic
`update` signal. An update submitted while the callback is blocked leaves more
deferred work, but the signal itself carries no way to distinguish the old and
new submission. A later observer that reads current settings can therefore see
B while processing A's signal.

The focused pre-fix reproduction on the `14ea89f14` engine proved both variants:

- timeout A with `blockMs=7500`, same-filter rename, then B returned success at
  A's late completion;
- the same sequence with same-filter enable also returned success at A's late
  completion.

Both runs showed A timing out near five seconds, the action callback consuming
the handle-only quarantine, and B succeeding roughly 2.4 seconds later while
the old callback was the only completion that could satisfy the generation and
current-settings predicate.

The correction adds the smallest private additive libobs bridge needed for this
model. Every update submission receives a per-source serial while the settings
application and deferred-count increment are synchronized. The video-thread
boundary snapshots the first/last serial range covered by the callback, keeps
newer submissions for a later callback, and emits that range only in internal
signal calldata. The public `obs_source_update()` and
`obs_source_reset_settings()` signatures remain unchanged; the tracked update
and reset hooks are declared only in non-installed internal/engine headers.
The existing global `source_update` signal is also sent before the private
serial fields are added, so its public payload declaration remains unchanged;
only the source-local observer signal carries the new internal evidence.

The engine records the tracked serial for each settings request and accepts a
settlement only when the exact filter handle, canonical post-update settings,
observer generation, and request serial coverage all match. It retains every
outstanding timed-out serial per filter rather than only one handle bit. A
rename or enable callback cannot retire that state. A late update callback
retires only serials proven processed by its covered range; its canonical event
is discarded behind a resynchronization boundary. When an older and newer
same-filter callback overlap, the newer request remains conservative and
returns timeout/resync; a later request is allowed to settle only after the
uncertainty has been accounted for.

## WIP findings and candidate corrections

The quarantined implementation was based on pre-corrective Task 10 state and
has four confirmed blockers:

1. Its protocol dispatch takes the mutation revision lock without the accepted
   `v2_wait_for_event_capture_callbacks()` pre-lock barrier, which would regress
   the Task-10 lock-order correction.
2. Deferred filter settings settlement creates a temporary `update` signal
   waiter and later disconnects it. libobs signal dispatch holds the signal mutex
   while invoking callbacks, so disconnect can block behind an unrelated or
   deliberately blocking callback. The accepted implementation uses the
   permanent filter observer plus a generation/condition-variable wakeup.
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
6. Its deterministic fixture recursively attempted update work from the create
   path before libobs had attached the fixture context data, so the test could
   depend on an uninitialized callback context instead of exercising a later
   real video-thread update.
7. Its observer cache only refreshed settings on update callbacks. A rename or
   enable callback could therefore cache a stale settings snapshot and suppress
   a subsequent legitimate `filter.settingsChanged` event.
8. Its router attempted asynchronous settings settlement even when the runtime
   operation had correctly identified an idempotent no-op, turning a successful
   no-revision request into an internal error.

The candidate records an observer generation on every update observation,
requires exact `(handle, post-update settings, generation > baseline)` proof,
returns `timeout` on uncertain settlement, quarantines the affected handle,
uses rollback-safe registry updates, removes the unused adapter, and exercises
the missing cases in the exact-SHA Task-11 lane. The fixture initializes its
settings directly during creation and reserves deferred callback behavior for
later patch/replace requests; the observer normalizes all relevant signal
types, and the router settles only actual settings mutations.

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

Independent source review and explicit human approval completed the Task-11
acceptance. The exact CI, artifact, physical Windows, review-bundle, and
non-blocking-debt record is maintained in `TASK11_ACCEPTANCE.md`.

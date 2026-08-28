# LibOBS Engine — Source Review Guide for Local Agents

The operator asked the local agent to **check all source, plans, and handoffs**, not merely trust a prose summary. This guide turns that requirement into a repeatable audit procedure.

---

## 1. First 10 minutes: establish reality

Run from repository root:

```bash
git status --short --branch
git branch --show-current
git rev-parse HEAD
git show --stat --oneline --decorate HEAD
git log --oneline --decorate --graph -30
```

At handoff creation, accepted engine implementation baseline is:

```text
f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc
feat(engine): complete protocol v2 interaction namespace
```

The handoff documentation commit will be newer. Determine whether anything newer than the handoff changed production engine code. Never assume a SHA written in documentation is still HEAD.

Useful diff:

```bash
git diff --stat f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc..HEAD
git diff f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc..HEAD -- engine .github
```

If that diff includes runtime/protocol behavior beyond documentation, stop and audit those changes before using the status file.

---

## 2. Read documents in this exact order

1. root `AGENTS.md`.
2. `docs/libobs-engine/HANDOFF.md`.
3. `docs/libobs-engine/PROJECT_STATUS.md`.
4. `docs/libobs-engine/ARCHITECTURE.md`.
5. `engine/PROTOCOL_V2.md` in full.
6. all currently implemented namespace docs:
   - `engine/EVENTS_V1.md`
   - `engine/RUNTIME_OBJECTS_V1.md`
   - `engine/PROPERTIES_V1.md`
   - `engine/SOURCE_V1.md`
   - `engine/INTERACTION_V1.md`
7. `docs/libobs-engine/ROADMAP.md`.
8. immediate task plan, currently `docs/libobs-engine/TASK10_MEDIA_PLAN.md`.

Then inspect source. Do not reverse this into “read the plan and start coding.”

---

## 3. Engine host/bootstrap audit

Inspect:

- `engine/main.cpp`
- `engine/host.cpp` and corresponding headers
- `engine/config.cpp`
- `engine/config.hpp`
- `engine/README.md`
- `engine/CMakeLists.txt`

Questions to answer:

- How are stdin/stdout/stderr used?
- What happens before `obs_startup`?
- How is the executable/working directory established?
- Which graphics API/device is initialized?
- How are modules located and allowlisted?
- Which modules are required vs optional?
- What does `--plugin=NAME` do?
- What does `--enable-game-capture` change?
- Are test-only modules part of `ALL` or install/package targets?
- Is normal OBS UI/frontend code linked into the headless engine?
- Are Windows DLL search restrictions preserved?

Important: `engine/README.md` includes v1 command examples. Verify host claims from source; do not copy v1 protocol examples into new v2 work.

---

## 4. Protocol-v2 core audit

Inspect:

- `engine/protocol_v2.cpp`
- `engine/protocol_v2.hpp`
- `engine/protocol.cpp` / protocol headers as needed
- `engine/validation.cpp/.hpp`
- `engine/revision.cpp/.hpp`
- `engine/events.cpp/.hpp`
- related unit tests.

Build a mental map of:

### 4.1 Capabilities

`kCapabilities` in `protocol_v2.cpp` is the actual advertised list. New methods normally need capability entries. Do not advertise a namespace/method until it is truly implemented and tested.

### 4.2 Method classification

`V2Method` and `classify_method()` determine known names. Exact spelling is protocol surface. Never improvise a method name that differs from `PROTOCOL_V2.md` unless you are deliberately updating the protocol contract and all tests/docs.

### 4.3 Mutation classification

`method_is_mutating()` controls `ifRevision` eligibility and revision guard behavior. Before adding any method, decide whether it is canonical-state mutation, transient command, or read-only query.

A method can cause downstream plugin callbacks even if it looks simple. “Mutating” is about canonical engine state exposed to Controller, not merely whether a C function writes memory.

### 4.4 Runtime dispatch

`method_is_runtime()` + `execute_runtime_method()` connect protocol methods to `Engine` methods. Keep dispatch exhaustive and explicit.

### 4.5 Source settlement hook

`method_needs_source_settle()` is special Task-8 machinery for source settings/load-state calls. Do not expand or bypass it casually. If a later namespace needs asynchronous ownership settlement, design namespace-appropriate logic rather than hijacking source settlement without proof.

### 4.6 Request guard and response/event order

Find the main v2 request handler and trace:

1. parse request;
2. classify method;
3. validate revision guard;
4. begin runtime/source event capture where applicable;
5. execute;
6. settle asynchronous effects if applicable;
7. commit revision if `result.mutated` / method semantics require it;
8. send response;
9. publish command-owned events;
10. flush deferred/unrelated events.

Write down the actual order from current source before modifying it.

---

## 5. Runtime object/lifetime audit

Inspect:

- `engine/runtime.hpp`
- `engine/runtime.cpp`
- `engine/runtime_v2.cpp`
- any object-specific runtime file touched by the active task.

Current `Engine` owns maps for sources/scenes/items and next-handle allocation. Future namespaces will add runtime object types. For each new object type define:

- owner;
- reference-counting rules;
- weak-reference use;
- creation failure cleanup;
- removal ordering;
- callbacks/signals lifetime;
- shutdown behavior;
- handle allocation/validation;
- stale-handle error;
- relation cleanup (e.g. removing source removes dependent item references where required).

Never expose an object before the engine owns a stable reference under its chosen lifetime rules.

---

## 6. Generic properties audit

Inspect:

- `engine/runtime_properties_v2.cpp`
- `engine/PROPERTIES_V1.md`
- Task-7 PowerShell/workflow.

Understand how property targets are represented and how source properties/defaults/lists/buttons are normalized. Future source/filter/encoder/service namespaces should reuse this generic mechanism rather than duplicate plugin UI schema logic.

Check:

- type normalization;
- list items and refresh behavior;
- button invocation mutability/revision behavior;
- error handling for unsupported/missing targets;
- whether property calls can themselves trigger callbacks/mutations.

---

## 7. Task-8 source bridge audit — mandatory before callback-heavy work

Inspect completely:

- `engine/runtime_source_v2.cpp`
- `engine/runtime_source_settle_v2.cpp`
- `engine/source_event_capture.cpp`
- `engine/source_event_capture.hpp`
- `engine/task8_concurrency_source.cpp`
- `.github/scripts/engine-protocol-v2-task8*.ps1`
- `.github/workflows/engine-protocol-v2-task8.yaml`
- `.github/workflows/engine-protocol-v2-task8-concurrency.yaml`

Answer these questions from source, not handoff prose:

1. What determines `Capture`, `Defer`, and `Direct` routing?
2. How are callbacks in flight counted?
3. Why is the source bridge mutex held across a direct revision commit?
4. How are removed-source callbacks suppressed without suppressing another source?
5. How is deferred queue size bounded?
6. What exactly happens when it overflows?
7. What constitutes a matching command-owned delayed update?
8. Why is canonical settings comparison required?
9. How does the temporary `"update"` waiter close races before/after connection/disconnection?
10. What does timeout do?
11. Which events are deduplicated and by what key?
12. Why do unrelated B/C batches retain independent revisions?
13. What happens during shutdown?
14. Where are `SourceV2State` and `DeferredSourceEventBatch` duplicated?

If you cannot answer all 14, do not refactor the source bridge or reuse its concurrency model elsewhere yet.

---

## 8. Task-9 interaction audit

Inspect:

- `engine/runtime_interaction_v2.cpp`
- `engine/INTERACTION_V1.md`
- `engine/task9_interaction_source.cpp`
- `.github/scripts/engine-protocol-v2-task9.ps1`
- `.github/workflows/engine-protocol-v2-task9.yaml`

Verify:

- no Win32 native input injection exists;
- `OBS_SOURCE_INTERACTION` is required;
- canonical source handle parsing;
- source-local bounds;
- semantic modifier mapping;
- key/text UTF-8 validation;
- NUL rejection rationale;
- held-key bound;
- reset release ordering;
- stale source tracking cleanup;
- interaction methods are absent from `method_is_mutating()`;
- `ifRevision` therefore errors;
- CI-only fixture exclusion from normal package.

This namespace is a useful pattern for **transient method semantics**, not for canonical mutations.

---

## 9. Upstream libobs audit procedure for a new namespace

Do not stop at public header declarations. For Task 10 or any later namespace, search both declarations and implementation.

Suggested local commands:

```bash
rg -n "obs_source_media_|OBS_MEDIA_STATE|media_" libobs plugins engine
```

For another namespace, substitute relevant API/type names.

Audit layers:

1. Public headers: exact API signatures and enum semantics.
2. libobs core implementation: threading, state mutation, signal order, reference ownership.
3. plugin callbacks: whether API invokes plugin callback synchronously; whether plugin defers work.
4. representative built-in plugins: practical state behavior and error/no-op behavior.
5. normal OBS frontend usage: useful for semantic intent, but do not copy Qt/frontend dependencies into engine.
6. existing obs-websocket semantics, if consulted, only as a design reference — not as a second API or automatic source of truth.

Record any disagreement between header intuition and implementation reality in the active task plan before coding.

---

## 10. CI source map

Current project-specific workflows:

- `.github/workflows/engine-protocol-v2-task1.yaml`
- `...task2.yaml`
- `...task3.yaml`
- `...task4.yaml`
- `...task5.yaml`
- `...task6.yaml`
- `...task7.yaml`
- `...task8.yaml`
- `...task8-concurrency.yaml`
- `...task9.yaml`

There are 10 workflow files but Task-8 concurrency includes an additional thread-isolation job, which is why the final Task-9 SHA had 11 check runs/jobs in the project matrix.

Inspect `.github/actions/build-obs` before duplicating build setup. Use the shared action unless there is a deliberate reason not to.

Scripts are under `.github/scripts/engine-protocol-v2-*.ps1`.

### CI rule

A new Task-N final SHA must have:

- Task-N dedicated integration green;
- all previous project workflows green on that same SHA;
- no “green old SHA + green new workflow on different SHA” acceptance.

---

## 11. CI-only test-module rule

Task 8 and 9 establish the pattern:

```cmake
add_library(test-module MODULE EXCLUDE_FROM_ALL ...)
# link libobs
# no install() rule
```

Then workflow:

1. build normal install;
2. assert test DLL absent;
3. explicitly build test module;
4. copy/stage it beside runtime modules;
5. run deterministic integration with `--plugin=...`;
6. remove it;
7. assert absent again when useful;
8. upload normal artifact.

This lets tests observe exact libobs callbacks without contaminating product runtime.

---

## 12. Packaging audit

For each final task artifact, inspect recursively. At minimum look for accidental:

- `obs64.exe` / `obs32.exe` normal frontend binaries;
- browser/UI artifacts not intended by current headless package;
- CI-only `taskN-*.dll` fixtures;
- source/build-tree files accidentally copied;
- duplicate DLLs from the wrong architecture/config;
- unexpected private keys/secrets/config;
- debug-only test executables.

Confirm `obs-engine.exe` plus intended runtime plugin/data/dependency set exists.

---

## 13. Physical Windows acceptance guide

Hosted Windows CI is not physical acceptance.

When a task needs a physical check:

1. use artifact from the exact final production implementation SHA, or a clearly documented test-only packaging commit whose parent is exactly that SHA and whose diff cannot change engine behavior;
2. verify hash/artifact provenance;
3. start fresh engine;
4. use fixed predictable handles where possible (`source:"1"`);
5. exercise task-specific success path;
6. exercise at least one critical validation/error path if practical;
7. check revision and event ordering, not just returned values;
8. check stderr callback/log evidence where deterministic fixture is used;
9. clean up objects;
10. `session.close` and require clean process exit.

If a normal production package cannot expose a deterministic target (Task 9 had no interaction-capable packaged source), a packaging-only physical fixture bundle is acceptable if:

- parent is exact accepted engine SHA;
- extra diff is test/package-only;
- CI verifies fixture behavior before upload;
- production branch/package remains clean;
- handoff records that distinction.

---

## 14. Two-pass review checklist

### Pass A — runtime correctness

- [ ] All libobs return values / nullable pointers handled.
- [ ] Strong/weak ref ownership explicit.
- [ ] No use-after-free path through callbacks.
- [ ] Callback disconnect/shutdown order correct.
- [ ] Locks do not invert with libobs signal locks.
- [ ] No unbounded state or queue.
- [ ] Async work has deterministic ownership/timeout semantics.
- [ ] Timeout fails safely/resyncs where needed.
- [ ] Object removal clears dependent tracking.
- [ ] Repeated/idempotent/no-op calls have intentional semantics.
- [ ] Unsupported plugin capability returns stable error.

### Pass B — protocol/architecture correctness

- [ ] Exact method/event names match docs.
- [ ] Capability entries accurate.
- [ ] Handle encoding consistent.
- [ ] No raw pointer/native object leak.
- [ ] Params validated by type/range/size.
- [ ] Canonical mutation classification correct.
- [ ] Exactly one revision per command-owned canonical mutation.
- [ ] Read-only/transient/telemetry calls do not churn revision.
- [ ] Response precedes command-owned events.
- [ ] Unrelated async changes retain independent revisions.
- [ ] Queue overflow/resync behavior preserved.
- [ ] No second Controller-facing API.
- [ ] CI fixture excluded from package.
- [ ] No next-task scope creep.

---

## 15. Local build notes

The established CI path is Windows x64 `RelWithDebInfo` through `.github/actions/build-obs`, with install root under `build_x64/install` in workflows. A local agent may build directly with CMake, but should mirror CI configuration sufficiently to reproduce issues.

Before inventing a command, read the action and workflow used by the latest task. Submodules are checked out recursively in project workflows.

For Linux development hosts, remember the product acceptance target is Windows. Static/source work can happen locally, but Windows-specific engine build/integration still needs a suitable Windows runner or machine.

---

## 16. When the handoff and source disagree

Do not silently choose the prose that is easier.

Create a short reconciliation note containing:

- conflicting claim;
- source path + relevant code;
- Git commit that changed reality;
- CI evidence if behavior changed;
- proposed documentation update;
- whether current task is safe to continue.

Then update `PROJECT_STATUS.md`/task plan in the same task commit when appropriate.

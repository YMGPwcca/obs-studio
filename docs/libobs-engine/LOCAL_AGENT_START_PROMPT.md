# Local AI Agent Start Prompt

Paste the following into a fresh local Codex-style agent opened at the repository root. It is intentionally strict: the agent must verify source before editing and must not silently begin Task 11.

---

You are taking over the LibOBS split-engine / Engine Protocol v2 project in this repository. You have no access to the previous chat history, so the repository handoff documents are your context.

**Do not edit production code yet. First perform a complete handoff/source audit.**

Read, in order:

1. `AGENTS.md`
2. `docs/libobs-engine/HANDOFF.md`
3. `docs/libobs-engine/PROJECT_STATUS.md`
4. `docs/libobs-engine/ARCHITECTURE.md`
5. `engine/PROTOCOL_V2.md` in full
6. `engine/EVENTS_V1.md`
7. `engine/RUNTIME_OBJECTS_V1.md`
8. `engine/PROPERTIES_V1.md`
9. `engine/SOURCE_V1.md`
10. `engine/INTERACTION_V1.md`
11. `docs/libobs-engine/SOURCE_REVIEW_GUIDE.md`
12. `docs/libobs-engine/ROADMAP.md`
13. `docs/libobs-engine/TASK10_MEDIA_PLAN.md`

Then inspect Git and source. At minimum run/check:

```bash
git status --short --branch
git branch --show-current
git rev-parse HEAD
git log --oneline --decorate --graph -30
git diff --stat e3ced05dc6f1e19a50e7da25c8f603b8f3ad90ff..HEAD
```

The accepted Task-10 engine/runtime implementation is `e3ced05dc6f1e19a50e7da25c8f603b8f3ad90ff`. Determine whether commits after it changed engine behavior. Do not trust the SHA blindly if current history differs.

Inspect at least these current engine sources before proposing work:

- `engine/protocol_v2.cpp`
- `engine/runtime.hpp`
- `engine/runtime.cpp`
- `engine/runtime_v2.cpp`
- `engine/runtime_properties_v2.cpp`
- `engine/runtime_source_v2.cpp`
- `engine/runtime_source_settle_v2.cpp`
- `engine/source_event_capture.hpp` (header-only)
- `engine/runtime_interaction_v2.cpp`
- `engine/runtime_media_v2.cpp`
- `engine/CMakeLists.txt`
- current project-specific `.github/workflows/engine-protocol-v2-task*.yaml`
- current `.github/scripts/engine-protocol-v2-*.ps1`

Specifically understand Task 8’s deferred source event ownership and Task 9’s transient interaction semantics. Do not refactor either area until you can explain their concurrency/lifetime contracts.

For the next planned Task 11, inspect upstream/current libobs source before designing anything:

```bash
rg -n "obs_source_filter_|obs_source_get_filters|obs_source_add_filter|obs_source_remove_filter|obs_source_set_filter_index|filter_add|filter_remove|reorder_filters" libobs plugins engine
```

Read the declarations and implementation, including `libobs/obs-source.h`, `libobs/obs.h`, `libobs/obs-source.c`, and representative filter plugins. Verify filter ownership/reference, ordering, callback/thread behavior, and interaction with the generic properties/source event bridges rather than assuming synchronous semantics.

Architecture requirements you must preserve:

- private Controller/UI communicates only with GPL `obs-engine.exe` through one custom Engine Protocol;
- no second OBS WebSocket/REST/GraphQL Controller API;
- initial transport is stdin/stdout NDJSON; stdout protocol-only, stderr logs;
- Controller owns persistent/project/UI/business/undo state; engine owns live libobs/runtime state;
- no libobs/raw/native pointers across the boundary;
- handles are ephemeral engine-session handles; current implemented source/interaction handles are canonical decimal strings;
- successful command-owned canonical mutations consume exactly one revision;
- command response precedes command-owned events sharing that revision;
- unrelated asynchronous canonical mutations get independent revisions;
- high-frequency telemetry does not churn revisions;
- callbacks never write directly to stdout;
- queue/ownership loss must resync rather than silently lose state;
- CI-only test plugins must never leak into production artifacts;
- implement one roadmap task at a time and stop before the next one.

Current status to verify:

- Tasks 1–9 complete; Task 10 is implemented but remains in corrective review
  with final acceptance pending.
- Task 8 deterministic A–F source concurrency + physical Windows acceptance complete.
- Task 9 all seven `interaction.*` methods complete; same-SHA regression matrix green; physical Windows acceptance complete.
- Task 10 `media.*` is implemented at `e3ced05dc6f1e19a50e7da25c8f603b8f3ad90ff`; `engine/MEDIA_V1.md` and the media settlement limits are authoritative, but exact-final-SHA acceptance is pending.
- Task 11 `filter.*` remains planned, quarantined, and NOT ACCEPTED.

**Do not begin Task 11.** Its unauthorized implementation remains quarantined, and no later task is authorized.

After the audit, report back with:

1. current branch and exact HEAD;
2. whether commits after the accepted Task-10 SHA changed engine behavior;
3. any mismatch between docs and source;
4. your source-level explanation of current revision/event dispatch;
5. your source-level explanation of Task-8 deferred update settlement;
6. your source-level explanation of Task-9 interaction lifetime/transient semantics;
7. your findings about the Task-10 media settlement contract and its known limitations;
8. your source-verified Task-11 filter implementation strategy;
9. confirmation that you have not changed production code before the audit report.

After the audit, implement only Task 11. Follow the full task gate: freeze the filter schema, inspect libobs/plugin behavior, add deterministic coverage and package assertions, run Task 1–11 regressions on one final SHA, review the diff twice, update the handoff/status/roadmap, and stop before Task 12.

For manual acceptance commands, make fresh-engine handles deterministic and use literal `"source":"1"` whenever possible. Do not give the operator commands containing `YOUR_HANDLE` placeholders that require manual editing.

---

## Expected first response from the local agent

A good first response is not “I’ll start coding filters.” It should be an audit report similar to:

> I verified the branch/HEAD, read the protocol/namespace/handoff files, and audited the current v2 dispatch/source/interaction/media code. Tasks 1–10 match the recorded accepted state, including the media-specific asynchronous settlement and its seek/error limitations. I inspected libobs filter APIs and representative filter plugins and will freeze a source-correlated filter schema before coding. I found the following source/doc mismatches or none: [...]. I have not changed production code before this report.

That response demonstrates the handoff was actually read and source-checked.

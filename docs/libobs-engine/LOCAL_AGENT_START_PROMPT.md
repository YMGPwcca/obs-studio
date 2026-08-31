# Local AI Agent Start Prompt

Paste the following into a fresh local Codex-style agent opened at the repository root. It is intentionally strict: the agent must verify source before editing and must not silently begin Task 12.

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
14. `docs/libobs-engine/TASK11_ACCEPTANCE.md`
15. `docs/libobs-engine/PHASE1_COMPLEXITY_ACCEPTANCE.md`

Then inspect Git and source. At minimum run/check:

```bash
git status --short --branch
git branch --show-current
git rev-parse HEAD
git log --oneline --decorate --graph -30
git diff --stat e7b34828cb9fbd55bae01f97148f1ec93a4ae015..HEAD
```

The accepted Task-10 engine/runtime implementation is `6a590c2985a99d186c8eecd0241acdc824d32168`, and the accepted Task-11 implementation is `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`. Determine whether commits after the accepted Task-11 SHA changed engine behavior. Do not trust the SHA blindly if current history differs.

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

For any future roadmap task, inspect upstream/current libobs source before designing anything. The accepted Task-11 filter implementation and its private tracked-update bridge are documented in `TASK11_ACCEPTANCE.md` and `TASK11_IMPLEMENTATION_AUDIT.md`:

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

- Tasks 1–11 are complete and accepted. Task 11 is accepted at
  `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`.
- Task 8 deterministic A–F source concurrency + physical Windows acceptance complete.
- Task 9 all seven `interaction.*` methods complete; same-SHA regression matrix green; physical Windows acceptance complete.
- Task 10 `media.*` is accepted at `6a590c2985a99d186c8eecd0241acdc824d32168`; `engine/MEDIA_V1.md` and the media settlement limits are authoritative.
- Phase-1 cyclomatic-complexity hardening is COMPLETE / ACCEPTED at reviewed checkpoint `1b2ddacbb36c39bb61fd645594f0746f106956bf`; see `PHASE1_COMPLEXITY_ACCEPTANCE.md` for the frozen baseline and approval evidence.
- The separate pre-Phase-2 PowerShell script-body hardening cleanup is IN REVIEW on `phase1-scriptbody-hardening`; see `PHASE1_SCRIPTBODY_HARDENING.md`. Do not treat it as accepted or modify runtime code while it is under review.
- Task 12 remains planned and NOT AUTHORIZED.

**Do not begin Task 12.** Wait for explicit human authorization before any later roadmap implementation.

After the audit, report back with:

1. current branch and exact HEAD;
2. whether commits after the accepted Task-10 SHA changed engine behavior;
3. any mismatch between docs and source;
4. your source-level explanation of current revision/event dispatch;
5. your source-level explanation of Task-8 deferred update settlement;
6. your source-level explanation of Task-9 interaction lifetime/transient semantics;
7. your findings about the Task-10 media settlement contract and its known limitations;
8. the accepted Task-11 filter implementation and its evidence record;
9. confirmation that you have not changed production code before the audit report.

After the audit, do not implement Task 12 or any later task unless the operator separately authorizes it. For an explicitly authorized future task, follow its full task gate and stop before the next unauthorized task.

For manual acceptance commands, make fresh-engine handles deterministic and use literal `"source":"1"` whenever possible. Do not give the operator commands containing `YOUR_HANDLE` placeholders that require manual editing.

---

## Expected first response from the local agent

A good first response is not “I’ll start coding filters.” It should be an audit report similar to:

> I verified the branch/HEAD, read the protocol/namespace/handoff files, and audited the current v2 dispatch/source/interaction/media/filter code. Tasks 1–11 match the recorded accepted state, including media-specific asynchronous settlement and the filter tracked-update serial bridge. Task 12 remains planned and unauthorized. I found the following source/doc mismatches or none: [...]. I have not changed production code before this report.

That response demonstrates the handoff was actually read and source-checked.

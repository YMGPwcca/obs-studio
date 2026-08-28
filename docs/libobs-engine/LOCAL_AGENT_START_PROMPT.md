# Local AI Agent Start Prompt

Paste the following into a fresh local Codex-style agent opened at the repository root. It is intentionally strict: the agent must verify source before editing and must not silently begin Task 10.

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
git diff --stat f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc..HEAD
```

The accepted Task-9 engine/runtime implementation before the handoff-doc commit is `f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc`. Determine whether commits after it are documentation-only or changed engine behavior. Do not trust the SHA blindly if current history differs.

Inspect at least these current engine sources before proposing work:

- `engine/protocol_v2.cpp`
- `engine/runtime.hpp`
- `engine/runtime.cpp`
- `engine/runtime_v2.cpp`
- `engine/runtime_properties_v2.cpp`
- `engine/runtime_source_v2.cpp`
- `engine/runtime_source_settle_v2.cpp`
- `engine/source_event_capture.cpp/.hpp`
- `engine/runtime_interaction_v2.cpp`
- `engine/CMakeLists.txt`
- current project-specific `.github/workflows/engine-protocol-v2-task*.yaml`
- current `.github/scripts/engine-protocol-v2-*.ps1`

Specifically understand Task 8’s deferred source event ownership and Task 9’s transient interaction semantics. Do not refactor either area until you can explain their concurrency/lifetime contracts.

For the next planned Task 10, inspect upstream/current libobs source before designing anything:

```bash
rg -n "OBS_SOURCE_CONTROLLABLE_MEDIA|obs_source_media_|media_play_pause|media_get_state|media_started|media_ended|process_media_actions|MEDIA_ACTION_" libobs plugins UI engine
```

Read the declarations and implementation, including `libobs/obs-source.h`, `libobs/obs.h`, `libobs/obs-source.c`, and representative media-capable plugins (especially obs-ffmpeg; also slideshow/VLC if relevant). Verify the important handoff claim that media control APIs enqueue media actions and those actions are processed from the source tick path. Investigate actual plugin state/signal behavior rather than assuming it.

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

Current accepted status to verify:

- Tasks 1–9 complete.
- Task 8 deterministic A–F source concurrency + physical Windows acceptance complete.
- Task 9 all seven `interaction.*` methods complete; same-SHA regression matrix green; physical Windows acceptance complete.
- Task 10 `media.*` is next and NOT STARTED.

**Do not begin Task 10 just because it is next.** This initial session is an audit/handoff session unless the operator separately tells you to implement Task 10.

After the audit, report back with:

1. current branch and exact HEAD;
2. whether the handoff commit is docs-only relative to `f59d6b6c...`;
3. any mismatch between docs and source;
4. your source-level explanation of current revision/event dispatch;
5. your source-level explanation of Task-8 deferred update settlement;
6. your source-level explanation of Task-9 interaction lifetime/transient semantics;
7. your findings about libobs media action queue/tick behavior and representative plugin behavior;
8. any changes you would make to `TASK10_MEDIA_PLAN.md` before coding;
9. confirmation that you have not changed production code yet.

Only after the operator explicitly says to proceed with Task 10 should you implement it. When authorized, follow the full Task-10 plan, review the complete diff twice, run Task 1–10 regressions on one final SHA, audit the artifact, perform relevant physical Windows acceptance, update the handoff/status/roadmap, and stop before Task 11.

For manual acceptance commands, make fresh-engine handles deterministic and use literal `"source":"1"` whenever possible. Do not give the operator commands containing `YOUR_HANDLE` placeholders that require manual editing.

---

## Expected first response from the local agent

A good first response is not “I’ll start coding media.” It should be an audit report similar to:

> I verified the branch/HEAD and the docs-only handoff delta, read the protocol/namespace/handoff files, and audited the current v2 dispatch/source/interaction code. Tasks 1–9 match the recorded accepted state. I also inspected libobs media APIs and confirmed the control functions enqueue media actions that are processed on source tick, so Task 10 needs media-specific asynchronous settlement rather than a synchronous call/return assumption. I found the following source/doc mismatches or none: [...]. I have not changed production code. Ready for your explicit authorization to implement Task 10.

That response demonstrates the handoff was actually read and source-checked.

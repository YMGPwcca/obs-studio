# AGENTS.md — LibOBS Engine / Protocol v2 local-agent entry point

This repository contains upstream OBS Studio plus a project-specific, headless `libobs` engine on the `engine-protocol-v2` branch. If you are a local coding agent (Codex, Claude Code, Aider, etc.), **do not start by editing code**. Establish the exact branch/HEAD, read the project handoff set below, then verify the handoff against source and Git history.

## Required reading order

1. `docs/libobs-engine/HANDOFF.md` — current project handoff, precedence rules, exact accepted baseline, and working procedure.
2. `docs/libobs-engine/PROJECT_STATUS.md` — completed tasks, accepted commits, CI/physical evidence, known debt.
3. `docs/libobs-engine/ARCHITECTURE.md` — non-negotiable process/API/state ownership architecture.
4. `engine/PROTOCOL_V2.md` — canonical semantic protocol contract. For already-implemented namespaces, also read their concrete schema docs such as `engine/SOURCE_V1.md`, `engine/INTERACTION_V1.md`, `engine/PROPERTIES_V1.md`, and `engine/RUNTIME_OBJECTS_V1.md`.
5. `docs/libobs-engine/SOURCE_REVIEW_GUIDE.md` — source map and mandatory source-audit checklist.
6. `docs/libobs-engine/ROADMAP.md` — complete Task 1–50 roadmap and acceptance policy.
7. `docs/libobs-engine/TASK10_MEDIA_PLAN.md` — accepted Task-10 design/research record.
8. `docs/libobs-engine/TASK11_ACCEPTANCE.md` — exact Task-11 acceptance evidence.
9. `docs/libobs-engine/TASK11_FILTER_PLAN.md` and `TASK11_IMPLEMENTATION_AUDIT.md` — accepted filter contract and source audit.
10. `docs/libobs-engine/PHASE1_COMPLEXITY_ACCEPTANCE.md` — explicit Phase-1 approval and frozen complexity evidence.

## Source-of-truth precedence

When documents disagree, use this order and reconcile the mismatch before continuing:

1. Current checked-out source + Git history + current CI evidence.
2. `engine/PROTOCOL_V2.md` and namespace-specific protocol documents.
3. `docs/libobs-engine/PROJECT_STATUS.md` and `HANDOFF.md`.
4. `docs/libobs-engine/ARCHITECTURE.md` and `SOURCE_REVIEW_GUIDE.md`.
5. `docs/libobs-engine/ROADMAP.md` and task plans.
6. Older design notes, chat exports, and legacy `engine/README.md` examples.

`engine/README.md` still contains substantial Protocol-v1-era examples. Treat it as host/security background, **not** the current Controller contract. The current Controller contract is Protocol v2.

## Current accepted baseline

The current accepted implementation baseline is:

- Branch: `engine-protocol-v2`
- Human-approved reviewed Phase-2 tip: `1c71ceaf502eb622d37efc45842525967818fb6f`
- Reviewed Phase-2 runtime candidate: `13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`
- Phase-2 acceptance commit: the single documentation-only child of the reviewed Phase-2 tip
- Complete Phase-2 complexity baseline: `eb5ed120ddc3f159ce5d0d3a317fd4c741532525`
- Task-9 implementation commit: `f59d6b6c87b7ca789adb6c55bc0a9c8e4ce361dc`
- Commit subject: `feat(engine): complete protocol v2 interaction namespace`
- Parent / accepted Task-8 baseline: `e88ceb0a1e1103c3297cd1bd589e56e28ae638e4`
- Task-10 (`media.*`) implementation: `6a590c2985a99d186c8eecd0241acdc824d32168`
- Task-11 (`filter.*`) implementation: `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
- Tasks 1 through 11: complete and accepted.
- Tasks 12 through 20: complete and accepted by human approval of the reviewed Phase-2 candidate.
- Phase 2: accepted. The acceptance commit changes documentation only; it does not change the reviewed runtime candidate.
- Phase-1 cyclomatic-complexity hardening: complete and accepted at reviewed checkpoint `1b2ddacbb36c39bb61fd645594f0746f106956bf`; see `docs/libobs-engine/PHASE1_COMPLEXITY_ACCEPTANCE.md`.
- Pre-Phase-2 PowerShell script-body hardening: COMPLETE / ACCEPTED in the current accepted lineage; the historical branch was `phase1-scriptbody-hardening` and its evidence record retains the original review history.
- Final pre-Phase-2 workflow executable-code hardening: COMPLETE / ACCEPTED in the current accepted lineage; the historical branch was `phase1-workflow-exec-hardening` and its evidence record retains the original review history.
- Task 21 and later: NOT STARTED on the accepted production baseline. The explicitly authorized Phase-3 candidate is `phase3-output-stack` for Tasks 21–30 only.

The Phase-2 acceptance commit advances branch HEAD beyond the reviewed runtime
candidate only with approved Markdown documentation. Verify its exact parent
and runtime-bearing diff before any later roadmap work.

## Non-negotiable architecture rules

- A private/proprietary Controller/UI talks only to the GPL `obs-engine.exe` process through **one custom Engine Protocol**.
- Do not introduce OBS WebSocket, REST, GraphQL, a second Controller-facing API, or a second control-plane listener.
- Initial transport is stdin/stdout newline-delimited JSON. The semantic protocol is intended to remain transport-independent.
- `stdout` is protocol-only. Logs go to `stderr`. libobs callbacks must never write directly to stdout. Preserve a single ordered protocol writer path.
- The Controller owns persistent/user/business state; the Engine owns runtime libobs state.
- Raw pointers and platform-native object pointers never cross the process boundary.
- Engine handles are opaque, process-local, ephemeral, canonical decimal strings in the current implementation. Never persist them across restart.
- Preserve revision semantics, response-before-event ordering, event sequence ordering, subscription/backpressure behavior, and resync-on-overflow behavior.
- Preserve the GPL process boundary and distribution obligations. `COPYING` is GPL v2. Do not move GPL-linked engine code into the proprietary Controller.

## Mandatory workflow for each roadmap task

1. Re-fetch/re-check the exact branch HEAD and working tree before editing.
2. Read the relevant protocol section and **inspect the actual upstream/current libobs declarations and implementation**, including source/plugin behavior where relevant.
3. Search the repository for existing helpers and adjacent namespaces. Do not invent parallel validation, handle, revision, event, or packaging mechanisms when a project mechanism exists.
4. Write or update a task-specific plan if source reality differs from the roadmap.
5. Implement **one namespace/task at a time**. Do not silently begin the next roadmap task.
6. Add deterministic integration coverage. Prefer CI-only synthetic libobs modules for callback/state semantics that packaged plugins cannot make deterministic.
7. Keep test-only modules `EXCLUDE_FROM_ALL`, without install rules, and assert they do not leak into normal artifacts.
8. Run the new task lane plus every previous project regression lane on the **same final SHA**.
9. Review the complete task diff twice: first for semantics/concurrency/lifetime, second for protocol/security/packaging/regressions.
10. Audit the packaged Windows runtime for accidental OBS frontend/browser/test artifacts.
11. Perform physical-Windows acceptance when the task has hardware/runtime/platform behavior that CI alone cannot establish.
12. Commit a clean task boundary and stop. Report exact SHA, tests, artifact, remaining risks, and whether physical acceptance is still needed.

## Operator ergonomics

For manual acceptance commands, make fresh-engine tests deterministic so the first created source is normally handle `"1"`. The operator explicitly does not want copy/paste commands containing placeholders such as `YOUR_HANDLE` that must be edited by hand. If multiple objects are required, structure the test so expected fresh-process handles are predictable and state them explicitly.

## Before claiming anything is complete

Do not infer completion from code existing. A task is complete only after its required implementation, deterministic integration tests, previous-task regression matrix, packaging audit, two-pass review, and applicable physical acceptance are all green. If any part is missing, say exactly what is missing.

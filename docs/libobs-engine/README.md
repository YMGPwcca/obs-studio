# LibOBS Engine Local-Agent Documentation

This directory is the repo-contained handoff for continuing the LibOBS split-engine / Engine Protocol v2 project with a local AI coding agent.

## Start here

For Codex/Claude Code/Aider or a human engineer, read:

1. `/AGENTS.md`
2. `HANDOFF.md`
3. `PROJECT_STATUS.md`
4. `ARCHITECTURE.md`
5. `/engine/PROTOCOL_V2.md`
6. `SOURCE_REVIEW_GUIDE.md`
7. `ROADMAP.md`
8. `TASK10_MEDIA_PLAN.md`
9. `LOCAL_AGENT_START_PROMPT.md`
10. `TASK11_ACCEPTANCE.md`
11. `PHASE1_COMPLEXITY_ACCEPTANCE.md`

## What is authoritative?

- **Current source/Git/CI** wins over prose.
- `/engine/PROTOCOL_V2.md` is the canonical semantic protocol contract.
- Namespace docs such as `/engine/SOURCE_V1.md`, `/engine/INTERACTION_V1.md`, and `/engine/MEDIA_V1.md` pin concrete implemented schemas.
- `PROJECT_STATUS.md` is the current completion/evidence ledger.
- This roadmap/handoff supersedes older status notes that still call Task 8 active or Task 9 proposed.

## Current handoff point

Tasks **1–11 are accepted**. Phase-2 Tasks **12–20 are also accepted** by human
approval of reviewed tip `1c71ceaf502eb622d37efc45842525967818fb6f`.

The reviewed Phase-2 runtime candidate remains:

`13faf231012b1f9ac23ebc000b4fb4260e7c2ff8`

The final Phase-2 acceptance commit is its documentation-only child. Accepted
Task-10 production engine/runtime SHA is:

`6a590c2985a99d186c8eecd0241acdc824d32168`

Task 10 `media.*` includes a source-correlated asynchronous settlement bridge and
the concrete contract in `engine/MEDIA_V1.md`. Task 11 `filter.*` is accepted at
`e7b34828cb9fbd55bae01f97148f1ec93a4ae015`; the acceptance evidence is in
`TASK11_ACCEPTANCE.md`. Task 21 is the next planned roadmap task but remains
NOT STARTED / NOT AUTHORIZED.

Phase-1 cyclomatic-complexity hardening is also **COMPLETE / ACCEPTED** at
reviewed checkpoint `1b2ddacbb36c39bb61fd645594f0746f106956bf`. Its explicit
human approval and final frozen-baseline evidence are recorded in
`PHASE1_COMPLEXITY_ACCEPTANCE.md`. The runtime implementation remains
`44243a5013007a449c1d0b9903233929bd44a141`; Phase 2 is accepted, while Task
21 and later remain planned and unauthorized.

The additional pre-Phase-2 PowerShell script-body cleanup is **COMPLETE /
ACCEPTED in the current accepted lineage**. Its historical branch was
`phase1-scriptbody-hardening`; the evidence record retains the original review
history and does not modify runtime code.

The final pre-Phase-2 workflow executable-code cleanup is **COMPLETE /
ACCEPTED in the current accepted lineage**. Its historical branch was
`phase1-workflow-exec-hardening`; the inventory/evidence record retains the
original review history and does not modify runtime code.

## Why multiple documents?

The project has several kinds of knowledge that should not be collapsed into one giant ambiguous note:

- `ARCHITECTURE.md`: decisions that should remain stable across tasks.
- `PROJECT_STATUS.md`: what has actually been completed and proven.
- `HANDOFF.md`: deep context, tricky implementation history and resumption procedure.
- `SOURCE_REVIEW_GUIDE.md`: how to verify prose against the codebase.
- `ROADMAP.md`: planned work and acceptance gates through protocol freeze.
- `TASK10_MEDIA_PLAN.md`: Task-10 implementation/research and acceptance record.
- `LOCAL_AGENT_START_PROMPT.md`: ready-to-paste initial instruction for a fresh local AI session.
- `TASK11_ACCEPTANCE.md`: final Task-11 approval and evidence record.
- `PHASE1_COMPLEXITY_ACCEPTANCE.md`: explicit Phase-1 approval and frozen complexity evidence.

Update these after each accepted task so future agents do not have to reconstruct project intent from chat history.

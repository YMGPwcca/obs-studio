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

## What is authoritative?

- **Current source/Git/CI** wins over prose.
- `/engine/PROTOCOL_V2.md` is the canonical semantic protocol contract.
- Namespace docs such as `/engine/SOURCE_V1.md`, `/engine/INTERACTION_V1.md`, and `/engine/MEDIA_V1.md` pin concrete implemented schemas.
- `PROJECT_STATUS.md` is the current completion/evidence ledger.
- This roadmap/handoff supersedes older status notes that still call Task 8 active or Task 9 proposed.

## Current handoff point

Tasks **1–11 are accepted**. Accepted Task-10 production engine/runtime SHA is:

`6a590c2985a99d186c8eecd0241acdc824d32168`

Task 10 `media.*` includes a source-correlated asynchronous settlement bridge and
the concrete contract in `engine/MEDIA_V1.md`. Task 11 `filter.*` is accepted at
`e7b34828cb9fbd55bae01f97148f1ec93a4ae015`; the acceptance evidence is in
`TASK11_ACCEPTANCE.md`. Task 12 is the next planned roadmap task but is not
authorized.

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

Update these after each accepted task so future agents do not have to reconstruct project intent from chat history.

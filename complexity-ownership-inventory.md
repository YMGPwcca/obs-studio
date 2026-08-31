# Phase-1 Complexity Ownership Inventory

Accepted HEAD: `3fc2e678d10809a4dca8b28107710534160803ab`
Ownership base: `bcd53e2914c68a62b2a9387a7e8ee3b59d1fd1df`

## Discovery

Ownership was derived from `git log BASE..accepted` author metadata, then file/function attribution was checked with current accepted-HEAD `git blame`. Archived WIP branches were not traversed.

- GitHub account: **YMGPwcca**
- Author-authored commits in accepted lineage: **64**
- Author-authored commits touching current executable scope: **98**

| Author name | Author email |
|---|---|
| Nguyễn Tuấn Nghĩa | `37042810+YMGPwcca@users.noreply.github.com` |
| YMGPwcca | `ymgpwcca@proton.me` |

## Current executable files

| File | Language | Status | Operator commits | Blame lines | Scoped functions |
|---|---|---|---:|---:|---:|
| `.github/scripts/engine-protocol-v2-task1-footprint.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | PowerShell | A | 1 | 0 | 13 |
| `.github/scripts/engine-protocol-v2-task10-build-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task10-package-audit.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task10-remove-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task10-stage-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task10.ps1` | PowerShell | M | 4 | 610 | 28 |
| `.github/scripts/engine-protocol-v2-task11-build-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task11-core-audit.ps1` | PowerShell | A | 1 | 0 | 5 |
| `.github/scripts/engine-protocol-v2-task11-package-audit.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task11-remove-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task11-stage-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | PowerShell | M | 3 | 266 | 15 |
| `.github/scripts/engine-protocol-v2-task11.ps1` | PowerShell | M | 5 | 907 | 46 |
| `.github/scripts/engine-protocol-v2-task2-framing.ps1` | PowerShell | A | 1 | 0 | 17 |
| `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | PowerShell | A | 1 | 0 | 19 |
| `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | PowerShell | A | 1 | 0 | 15 |
| `.github/scripts/engine-protocol-v2-task5-event-queue-policy.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | PowerShell | A | 1 | 0 | 18 |
| `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | PowerShell | A | 2 | 0 | 31 |
| `.github/scripts/engine-protocol-v2-task7-properties-bridge.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | PowerShell | A | 2 | 0 | 19 |
| `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1` | PowerShell | A | 1 | 0 | 4 |
| `.github/scripts/engine-protocol-v2-task8-concurrency-build-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task8-concurrency-capture-routing.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task8-concurrency-run.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | PowerShell | M | 2 | 318 | 18 |
| `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | PowerShell | A | 2 | 0 | 34 |
| `.github/scripts/engine-protocol-v2-task9-build-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task9-package-audit.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task9-remove-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task9-stage-fixture.ps1` | PowerShell | A | 1 | 0 | 1 |
| `.github/scripts/engine-protocol-v2-task9.ps1` | PowerShell | M | 2 | 346 | 15 |
| `engine/config.cpp` | C/C++ | M | 3 | 102 | 7 |
| `engine/config.hpp` | C/C++ | A | 4 | 46 | 0 |
| `engine/events_test.cpp` | C/C++ | M | 3 | 216 | 16 |
| `engine/events.cpp` | C/C++ | M | 5 | 363 | 24 |
| `engine/events.hpp` | C/C++ | M | 5 | 87 | 0 |
| `engine/host.cpp` | C/C++ | M | 10 | 273 | 16 |
| `engine/obs_source_update_private.hpp` | C/C++ | A | 1 | 13 | 0 |
| `engine/properties_sensitive.cpp` | C/C++ | A | 1 | 41 | 3 |
| `engine/properties_test.cpp` | C/C++ | M | 2 | 224 | 19 |
| `engine/properties.cpp` | C/C++ | M | 2 | 574 | 55 |
| `engine/properties.hpp` | C/C++ | A | 1 | 34 | 0 |
| `engine/protocol_filter_v2.cpp` | C/C++ | M | 4 | 455 | 18 |
| `engine/protocol_v2.cpp` | C/C++ | M | 12 | 958 | 41 |
| `engine/protocol_v2.hpp` | C/C++ | A | 4 | 44 | 0 |
| `engine/protocol.cpp` | C/C++ | A | 3 | 237 | 16 |
| `engine/protocol.hpp` | C/C++ | A | 3 | 47 | 2 |
| `engine/revision.hpp` | C/C++ | A | 3 | 96 | 12 |
| `engine/runtime_filter_v2.cpp` | C/C++ | M | 5 | 1949 | 117 |
| `engine/runtime_interaction_v2.cpp` | C/C++ | M | 2 | 707 | 46 |
| `engine/runtime_media_v2.cpp` | C/C++ | M | 4 | 1246 | 80 |
| `engine/runtime_properties_v2.cpp` | C/C++ | M | 3 | 515 | 31 |
| `engine/runtime_source_settle_v2.cpp` | C/C++ | A | 4 | 366 | 15 |
| `engine/runtime_source_v2.cpp` | C/C++ | M | 5 | 1276 | 81 |
| `engine/runtime_v2.cpp` | C/C++ | M | 3 | 619 | 28 |
| `engine/runtime.cpp` | C/C++ | M | 3 | 621 | 30 |
| `engine/runtime.hpp` | C/C++ | M | 12 | 228 | 0 |
| `engine/source_event_capture.hpp` | C/C++ | A | 1 | 43 | 4 |
| `engine/task10_media_source.cpp` | C/C++ | A | 2 | 395 | 1 |
| `engine/task11_filter_source.cpp` | C/C++ | A | 1 | 214 | 1 |
| `engine/task8_concurrency_source.cpp` | C/C++ | A | 1 | 301 | 2 |
| `engine/task9_interaction_source.cpp` | C/C++ | A | 1 | 99 | 1 |
| `engine/validation.hpp` | C/C++ | M | 2 | 24 | 2 |
| `libobs/obs-internal.h` | C/C++ | A | 3 | 36 | 3 |
| `libobs/obs-source-media-internal.h` | C/C++ | A | 1 | 34 | 0 |
| `libobs/obs-source.c` | C/C++ | M | 4 | 164 | 22 |
| `plugins/win-capture/plugin-main.c` | C/C++ | M | 2 | 42 | 6 |
| `tools/check-complexity.ps1` | PowerShell | A | 24 | 0 | 164 |
| `tools/check-complexity.tests.ps1` | PowerShell | A | 22 | 0 | 22 |

## Non-CC changed paths

These paths remain part of the authorship audit but are not function-level cyclomatic targets:

- `.github/workflows/engine-complexity.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task1.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task10.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task11.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task2.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task3.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task4.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task5.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task6.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task7.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task8.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task9.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.github/workflows/windows-minimal.yaml` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `.gitignore` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `AGENTS.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `cmake/common/versionconfig.cmake` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `CMakeLists.txt` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `CMakePresets.json` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-after.json` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-baseline.json` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-baseline.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-before.json` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-exceptions.json` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-identity-migrations.json` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-ownership-inventory.json` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-ownership-inventory.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `complexity-report.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/ARCHITECTURE.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/HANDOFF.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/LOCAL_AGENT_START_PROMPT.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/PHASE1_COMPLEXITY_ACCEPTANCE.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/PHASE1_COMPLEXITY_HARDENING.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/PHASE1_SCRIPTBODY_HARDENING.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/PHASE1_WORKFLOW_EXEC_HARDENING.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/PROJECT_STATUS.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/README.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/ROADMAP.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/SOURCE_REVIEW_GUIDE.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK10_ACCEPTANCE.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK10_MEDIA_PLAN.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK11_ACCEPTANCE.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK11_FILTER_PLAN.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK11_IMPLEMENTATION_AUDIT.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/CMakeLists.txt` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/EVENTS_V1.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/FILTER_V1.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/INTERACTION_V1.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/MEDIA_V1.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/PROPERTIES_V1.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/PROTOCOL_V2.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/README.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/RUNTIME_OBJECTS_V1.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `engine/SOURCE_V1.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `libobs/CMakeLists.txt` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `plugins/CMakeLists.txt` — Declaration/configuration/documentation path; excluded from cyclomatic targets.
- `WINDOWS_MINIMAL.md` — Declaration/configuration/documentation path; excluded from cyclomatic targets.

## Scope rules

- Historical C/C++ scope is the complete current function when accepted-HEAD blame attributes at least one line in the function to an operator commit.
- After and Check add every current C/C++ or PowerShell file added by operator work after acceptedRef, including descendants of later file renames, and add changed functions in operator-modified files by current blame or candidate diff lines.
- A renamed historical file retains exact path aliases for baseline comparison; a deleted-and-recreated path is measured as a new file identity.
- Only C/C++ and PowerShell are analyzable. Known unsupported or unknown executable paths introduced by project work fail closed instead of entering the wrong parser.
- CMake/control files and static declarations are recorded for review but are not function-level CC targets.
- Baseline measures the accepted checkout; After and Check measure the current working tree/HEAD.

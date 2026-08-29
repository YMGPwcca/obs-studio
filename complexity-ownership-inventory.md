# Phase-1 Complexity Ownership Inventory

Accepted HEAD: `3fc2e678d10809a4dca8b28107710534160803ab`
Ownership base: `bcd53e2914c68a62b2a9387a7e8ee3b59d1fd1df`

## Discovery

Ownership was derived from `git log BASE..accepted` author metadata, then file/function attribution was checked with current accepted-HEAD `git blame`. Archived WIP branches were not traversed.

- GitHub account: **YMGPwcca**
- Author-authored commits in accepted lineage: **64**
- Author-authored commits touching current executable scope: **29**

| Author name | Author email |
|---|---|
| Nguyễn Tuấn Nghĩa | `37042810+YMGPwcca@users.noreply.github.com` |
| YMGPwcca | `ymgpwcca@proton.me` |

## Current executable files

| File | Language | Status | Operator commits | Blame lines | Scoped functions |
|---|---|---|---:|---:|---:|
| `.github/scripts/engine-protocol-v2-task10.ps1` | PowerShell | A | 2 | 610 | 8 |
| `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | PowerShell | A | 2 | 266 | 10 |
| `.github/scripts/engine-protocol-v2-task11.ps1` | PowerShell | A | 3 | 907 | 14 |
| `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | PowerShell | A | 1 | 318 | 16 |
| `.github/scripts/engine-protocol-v2-task9.ps1` | PowerShell | A | 1 | 346 | 4 |
| `engine/config.cpp` | C/C++ | A | 2 | 102 | 3 |
| `engine/config.hpp` | C/C++ | A | 4 | 46 | 0 |
| `engine/events_test.cpp` | C/C++ | A | 2 | 216 | 11 |
| `engine/events.cpp` | C/C++ | A | 3 | 363 | 18 |
| `engine/events.hpp` | C/C++ | A | 3 | 87 | 0 |
| `engine/host.cpp` | C/C++ | A | 9 | 273 | 14 |
| `engine/obs_source_update_private.hpp` | C/C++ | A | 1 | 13 | 0 |
| `engine/properties_sensitive.cpp` | C/C++ | A | 1 | 41 | 3 |
| `engine/properties_test.cpp` | C/C++ | A | 1 | 224 | 5 |
| `engine/properties.cpp` | C/C++ | A | 1 | 574 | 29 |
| `engine/properties.hpp` | C/C++ | A | 1 | 34 | 0 |
| `engine/protocol_filter_v2.cpp` | C/C++ | A | 2 | 455 | 15 |
| `engine/protocol_v2.cpp` | C/C++ | A | 10 | 958 | 24 |
| `engine/protocol_v2.hpp` | C/C++ | A | 4 | 44 | 0 |
| `engine/protocol.cpp` | C/C++ | A | 3 | 237 | 16 |
| `engine/protocol.hpp` | C/C++ | A | 3 | 47 | 2 |
| `engine/revision.hpp` | C/C++ | A | 3 | 96 | 12 |
| `engine/runtime_filter_v2.cpp` | C/C++ | A | 3 | 1949 | 77 |
| `engine/runtime_interaction_v2.cpp` | C/C++ | A | 1 | 707 | 28 |
| `engine/runtime_media_v2.cpp` | C/C++ | A | 2 | 1246 | 58 |
| `engine/runtime_properties_v2.cpp` | C/C++ | A | 2 | 515 | 22 |
| `engine/runtime_source_settle_v2.cpp` | C/C++ | A | 4 | 366 | 15 |
| `engine/runtime_source_v2.cpp` | C/C++ | A | 4 | 1276 | 60 |
| `engine/runtime_v2.cpp` | C/C++ | A | 2 | 619 | 22 |
| `engine/runtime.cpp` | C/C++ | A | 2 | 621 | 25 |
| `engine/runtime.hpp` | C/C++ | A | 9 | 228 | 0 |
| `engine/source_event_capture.hpp` | C/C++ | A | 1 | 43 | 4 |
| `engine/task10_media_source.cpp` | C/C++ | A | 2 | 395 | 1 |
| `engine/task11_filter_source.cpp` | C/C++ | A | 1 | 214 | 1 |
| `engine/task8_concurrency_source.cpp` | C/C++ | A | 1 | 301 | 2 |
| `engine/task9_interaction_source.cpp` | C/C++ | A | 1 | 99 | 1 |
| `engine/validation.hpp` | C/C++ | A | 1 | 24 | 1 |
| `libobs/obs-internal.h` | C/C++ | M | 3 | 36 | 3 |
| `libobs/obs-source-media-internal.h` | C/C++ | A | 1 | 34 | 0 |
| `libobs/obs-source.c` | C/C++ | M | 3 | 164 | 16 |
| `plugins/win-capture/plugin-main.c` | C/C++ | M | 1 | 42 | 3 |

## Non-CC changed paths

These paths remain part of the authorship audit but are not function-level cyclomatic targets:

- `.github/workflows/engine-protocol-v2-task1.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task10.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task11.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task2.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task3.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task4.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task5.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task6.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task7.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task8.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/engine-protocol-v2-task9.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `.github/workflows/windows-minimal.yaml` — YAML workflow/declaration; excluded from cyclomatic targets.
- `AGENTS.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `cmake/common/versionconfig.cmake` — CMake/control script; reviewed separately because lizard is not the chosen analyzer.
- `CMakeLists.txt` — CMake/control script; reviewed separately because lizard is not the chosen analyzer.
- `CMakePresets.json` — Static JSON/configuration; excluded from cyclomatic targets.
- `docs/libobs-engine/ARCHITECTURE.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/HANDOFF.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/LOCAL_AGENT_START_PROMPT.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/PROJECT_STATUS.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/README.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/ROADMAP.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/SOURCE_REVIEW_GUIDE.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK10_ACCEPTANCE.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK10_MEDIA_PLAN.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK11_ACCEPTANCE.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK11_FILTER_PLAN.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `docs/libobs-engine/TASK11_IMPLEMENTATION_AUDIT.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/CMakeLists.txt` — CMake/control script; reviewed separately because lizard is not the chosen analyzer.
- `engine/EVENTS_V1.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/FILTER_V1.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/INTERACTION_V1.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/MEDIA_V1.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/PROPERTIES_V1.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/PROTOCOL_V2.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/README.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/RUNTIME_OBJECTS_V1.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `engine/SOURCE_V1.md` — Markdown/project documentation; excluded from cyclomatic targets.
- `libobs/CMakeLists.txt` — CMake/control script; reviewed separately because lizard is not the chosen analyzer.
- `plugins/CMakeLists.txt` — CMake/control script; reviewed separately because lizard is not the chosen analyzer.
- `WINDOWS_MINIMAL.md` — Markdown/project documentation; excluded from cyclomatic targets.

## Scope rules

- C/C++ scope is the complete current function when accepted-HEAD blame attributes at least one line in the function to an operator commit; newly added files are fully scoped.
- PowerShell scope includes all current functions in the five operator-authored integration scripts; top-level script bodies are measured separately with the AST parser.
- CMake/control files are included in the ownership inventory and reviewed separately, not treated as function-level CC targets.
- Markdown, YAML, static JSON, license text, and other declarations are recorded but excluded from CC metrics.
- Current source is measured at the accepted checkpoint for Baseline and at the candidate working tree/HEAD for After and Check.

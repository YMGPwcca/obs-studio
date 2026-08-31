# Phase-1 Workflow Executable-Code Hardening

Status: **IN REVIEW**. This is the final pre-Phase-2 complexity-scope closure;
it does not authorize Task 12 and does not change the accepted runtime or
standalone-function/script-body result.

## Starting point and inventory method

- Starting reviewed candidate: `586a15a452f9c6e6a6813f8724c81c010a050c52`
- Accepted production base: `636e5914f6e8d69853ab4ce83d80ef944e6835dc`
- Runtime hardening reference: `44243a5013007a449c1d0b9903233929bd44a141`
- Initial inventory parser: PyYAML `6.0.2`, using node-level `yaml.compose`
  mappings and scalar source marks for jobs, steps, `run`, `shell`, and line
  ranges.
- Enforced checker parser: pinned `powershell-yaml` `0.4.12` / YamlDotNet AST,
  loaded by the complexity workflow. The checker uses mapping/sequence/scalar
  nodes and parser line marks; it does not parse workflow YAML with regex.
- Ownership: exact current/ref blame and current-worktree provenance against the
  existing operator identity rules; filenames alone were not treated as
  ownership evidence.
- Scope: `.github/workflows/**/*.yml` and `.github/workflows/**/*.yaml`; pure
  workflow declarations were not treated as executable code.

The inventory found 23 workflow files and 44 operator-attributable `run:`
blocks. There were 43 PowerShell blocks and one Bash/sh block. Approximate NLOC
excludes blank lines and full-line comments. Features are a conservative source
inventory, not a numeric CC claim.

| Path | Job / step | Shell / interpreter | Lines | Approx. NLOC | Classification | Executable features |
|---|---|---|---:|---:|---|---|
| `.github/workflows/engine-complexity.yaml` | `complexity` / Install isolated complexity analyzer | `bash` / Bash | 22–25 | 2 | `TRIVIAL_WRAPPER` | package setup |
| `.github/workflows/engine-complexity.yaml` | `complexity` / Run complexity checker self-tests | `pwsh` / PowerShell | 30–33 | 2 | `TRIVIAL_WRAPPER` | — |
| `.github/workflows/engine-complexity.yaml` | `complexity` / Enforce complexity budget | `pwsh` / PowerShell | 38–40 | 2 | `TRIVIAL_WRAPPER` | — |
| `.github/workflows/engine-protocol-v2-task1.yaml` | `build-smoke-package-x64` / Verify headless runtime footprint | `pwsh` / PowerShell | 30–56 | 20 | `EXECUTABLE_LOGIC` | branch, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task1.yaml` | `build-smoke-package-x64` / Smoke test obs-engine protocol v1 | `pwsh` / PowerShell | 61–273 | 185 | `EXECUTABLE_LOGIC` | branch, loop, function, try/catch/finally, protocol, process, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task1.yaml` | `build-smoke-package-x64` / Enforce smoke result | `pwsh` / PowerShell | 287–288 | 1 | `TRIVIAL_WRAPPER` | assertion |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` / Verify normal package excludes Task 10 fixture and second APIs | `pwsh` / PowerShell | 31–46 | 14 | `EXECUTABLE_LOGIC` | branch, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` / Build CI-only deterministic media source | `pwsh` / PowerShell | 49–55 | 5 | `EXECUTABLE_LOGIC` | branch, package, assertion |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` / Stage CI-only media source | `pwsh` / PowerShell | 58–73 | 14 | `EXECUTABLE_LOGIC` | branch, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` / Run Task 10 media integration regression | `pwsh` / PowerShell | 76–79 | 2 | `TRIVIAL_WRAPPER` | protocol, package |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` / Remove explicitly staged Task 10 test source | `pwsh` / PowerShell | 82–94 | 11 | `EXECUTABLE_LOGIC` | branch, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` / Verify Task 10 core isolation and Task 11 callback ordering | `pwsh` / PowerShell | 26–74 | 42 | `EXECUTABLE_LOGIC` | branch, loop, protocol, package, assertion |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` / Verify normal package excludes fixtures and second APIs | `pwsh` / PowerShell | 83–100 | 16 | `EXECUTABLE_LOGIC` | branch, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` / Build CI-only deterministic filter source | `pwsh` / PowerShell | 103–109 | 5 | `EXECUTABLE_LOGIC` | branch, package, assertion |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` / Stage CI-only filter source | `pwsh` / PowerShell | 112–126 | 13 | `EXECUTABLE_LOGIC` | branch, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` / Run Task 11 filter integration regression | `pwsh` / PowerShell | 129–132 | 2 | `TRIVIAL_WRAPPER` | protocol, package |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` / Run Task 11 timeout ownership race regression | `pwsh` / PowerShell | 135–138 | 2 | `TRIVIAL_WRAPPER` | protocol, package |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` / Remove explicitly staged Task 11 test source | `pwsh` / PowerShell | 142–152 | 9 | `EXECUTABLE_LOGIC` | branch, loop, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task2.yaml` | `build-smoke-package-x64` / Smoke test protocol v2 framing | `pwsh` / PowerShell | 33–229 | 176 | `EXECUTABLE_LOGIC` | branch, function, try/catch/finally, protocol, process, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task2.yaml` | `build-smoke-package-x64` / Enforce Task 2 smoke result | `pwsh` / PowerShell | 243–244 | 1 | `TRIVIAL_WRAPPER` | protocol, package, assertion |
| `.github/workflows/engine-protocol-v2-task3.yaml` | `build-smoke-package-x64` / Smoke test protocol v2 capabilities | `pwsh` / PowerShell | 33–245 | 190 | `EXECUTABLE_LOGIC` | branch, loop, function, try/catch/finally, protocol, process, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task3.yaml` | `build-smoke-package-x64` / Enforce Task 3 smoke result | `pwsh` / PowerShell | 259–260 | 1 | `TRIVIAL_WRAPPER` | protocol, package, assertion |
| `.github/workflows/engine-protocol-v2-task4.yaml` | `build-smoke-package-x64` / Smoke test protocol v2 revisions | `pwsh` / PowerShell | 33–233 | 180 | `EXECUTABLE_LOGIC` | branch, function, try/catch/finally, protocol, process, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task4.yaml` | `build-smoke-package-x64` / Enforce Task 4 smoke result | `pwsh` / PowerShell | 247–248 | 1 | `TRIVIAL_WRAPPER` | protocol, package, assertion |
| `.github/workflows/engine-protocol-v2-task5.yaml` | `build-smoke-package-x64` / Build and run bounded event queue policy test | `pwsh` / PowerShell | 33–94 | 54 | `EXECUTABLE_LOGIC` | branch, try/catch/finally, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task5.yaml` | `build-smoke-package-x64` / Smoke test protocol v2 subscriptions and event delivery | `pwsh` / PowerShell | 100–384 | 257 | `EXECUTABLE_LOGIC` | branch, loop, function, try/catch/finally, protocol, process, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task5.yaml` | `build-smoke-package-x64` / Enforce Task 5 verification result | `pwsh` / PowerShell | 398–399 | 1 | `TRIVIAL_WRAPPER` | package, assertion |
| `.github/workflows/engine-protocol-v2-task6.yaml` | `build-smoke-package-x64` / Smoke test protocol v2 source scene item lifecycle | `pwsh` / PowerShell | 33–524 | 454 | `EXECUTABLE_LOGIC` | branch, loop, function, try/catch/finally, protocol, process, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task6.yaml` | `build-smoke-package-x64` / Enforce Task 6 smoke result | `pwsh` / PowerShell | 538–539 | 1 | `TRIVIAL_WRAPPER` | package, assertion |
| `.github/workflows/engine-protocol-v2-task7.yaml` | `build-smoke-package-x64` / Build and run generic properties bridge test | `pwsh` / PowerShell | 33–94 | 54 | `EXECUTABLE_LOGIC` | branch, try/catch/finally, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task7.yaml` | `build-smoke-package-x64` / Smoke test protocol v2 properties API | `pwsh` / PowerShell | 100–430 | 303 | `EXECUTABLE_LOGIC` | branch, loop, function, try/catch/finally, protocol, process, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task7.yaml` | `build-smoke-package-x64` / Enforce Task 7 test results | `pwsh` / PowerShell | 444–445 | 1 | `TRIVIAL_WRAPPER` | package, assertion |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `source-event-capture-thread-isolation` / Compile and run capture routing regression | `pwsh` / PowerShell | 21–96 | 57 | `EXECUTABLE_LOGIC` | branch, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `source-event-capture-thread-isolation` / Verify production bridge ordering and isolation invariants | `pwsh` / PowerShell | 99–144 | 40 | `EXECUTABLE_LOGIC` | branch, loop, protocol, package, assertion |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `deterministic-source-concurrency-a-f` / Build CI-only deterministic source module | `pwsh` / PowerShell | 163–169 | 5 | `EXECUTABLE_LOGIC` | branch, protocol, package, assertion |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `deterministic-source-concurrency-a-f` / Run deterministic A-F integration regression | `pwsh` / PowerShell | 174–208 | 29 | `EXECUTABLE_LOGIC` | branch, try/catch/finally, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `deterministic-source-concurrency-a-f` / Enforce deterministic A-F regression | `pwsh` / PowerShell | 222–223 | 1 | `TRIVIAL_WRAPPER` | assertion |
| `.github/workflows/engine-protocol-v2-task8.yaml` | `build-smoke-package-x64` / Smoke test complete protocol v2 source namespace | `pwsh` / PowerShell | 33–492 | 414 | `EXECUTABLE_LOGIC` | branch, loop, function, try/catch/finally, protocol, process, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task8.yaml` | `build-smoke-package-x64` / Enforce Task 8 smoke result | `pwsh` / PowerShell | 506–507 | 1 | `TRIVIAL_WRAPPER` | protocol, package, assertion |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` / Verify normal package excludes Task 9 test source | `pwsh` / PowerShell | 31–38 | 6 | `EXECUTABLE_LOGIC` | branch, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` / Build CI-only deterministic interaction source | `pwsh` / PowerShell | 41–47 | 5 | `EXECUTABLE_LOGIC` | branch, protocol, package, assertion |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` / Stage CI-only interaction source | `pwsh` / PowerShell | 50–65 | 14 | `EXECUTABLE_LOGIC` | branch, protocol, filesystem, package, assertion |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` / Run Task 9 interaction integration regression | `pwsh` / PowerShell | 68–71 | 2 | `TRIVIAL_WRAPPER` | protocol, package |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` / Remove explicitly staged Task 9 test source | `pwsh` / PowerShell | 74–86 | 11 | `EXECUTABLE_LOGIC` | branch, protocol, filesystem, package, assertion |

No operator-attributable `run:` block was found in
`.github/workflows/windows-minimal.yaml`. Other workflow files contain
upstream-owned run blocks and remain outside this project-attributable scope.

Before extraction, the complete project-owned threshold lists were:

- `EXECUTABLE_LOGIC`: every row above with that classification (28 blocks);
- `TRIVIAL_WRAPPER`: the 16 rows explicitly marked above;
- `UNSUPPORTED_EXECUTABLE`: none; and
- `DECLARATIVE_ONLY`: none among `run:` blocks (pure YAML declarations had no
  executable block and were not inventoried as targets).

This inventory is frozen as the pre-edit reference. The old inline bodies were
not part of the numeric named-function/script-body baseline; they were audited
as workflow executable blocks. The accepted standalone script-body baseline at
the start of this cleanup was seven bodies: average CC 2.286, median 2, p90 5,
maximum 5, with zero bodies above 5, 7, or 10. The previously observed inline
Task-10 orchestration body was approximately CC 63, which is why the workflow
policy is now a separate enforced scope.

## Extraction map and semantic audit

Every row marked `EXECUTABLE_LOGIC` above was extracted. The workflow step name,
job, conditions, environment, working directory, timeout, and
`continue-on-error` metadata remain in the workflow. Each replacement keeps
`$ErrorActionPreference = 'Stop'` and directly invokes the new script with no
new arguments; the step inherits the same environment and propagates the
script exit code. GitHub expressions remain in YAML environment/step metadata,
not in generated PowerShell source.

| Workflow / job / step | New measured script |
|---|---|
| `engine-protocol-v2-task1.yaml` / `build-smoke-package-x64` / Verify headless runtime footprint | `.github/scripts/engine-protocol-v2-task1-footprint.ps1` |
| `engine-protocol-v2-task1.yaml` / `build-smoke-package-x64` / Smoke test obs-engine protocol v1 | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` |
| `engine-protocol-v2-task2.yaml` / `build-smoke-package-x64` / Smoke test protocol v2 framing | `.github/scripts/engine-protocol-v2-task2-framing.ps1` |
| `engine-protocol-v2-task3.yaml` / `build-smoke-package-x64` / Smoke test protocol v2 capabilities | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` |
| `engine-protocol-v2-task4.yaml` / `build-smoke-package-x64` / Smoke test protocol v2 revisions | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` |
| `engine-protocol-v2-task5.yaml` / `build-smoke-package-x64` / Build and run bounded event queue policy test | `.github/scripts/engine-protocol-v2-task5-event-queue-policy.ps1` |
| `engine-protocol-v2-task5.yaml` / `build-smoke-package-x64` / Smoke test protocol v2 subscriptions and event delivery | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` |
| `engine-protocol-v2-task6.yaml` / `build-smoke-package-x64` / Smoke test protocol v2 source scene item lifecycle | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` |
| `engine-protocol-v2-task7.yaml` / `build-smoke-package-x64` / Build and run generic properties bridge test | `.github/scripts/engine-protocol-v2-task7-properties-bridge.ps1` |
| `engine-protocol-v2-task7.yaml` / `build-smoke-package-x64` / Smoke test protocol v2 properties API | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` |
| `engine-protocol-v2-task8-concurrency.yaml` / `source-event-capture-thread-isolation` / Compile and run capture routing regression | `.github/scripts/engine-protocol-v2-task8-concurrency-capture-routing.ps1` |
| `engine-protocol-v2-task8-concurrency.yaml` / `source-event-capture-thread-isolation` / Verify production bridge ordering and isolation invariants | `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1` |
| `engine-protocol-v2-task8-concurrency.yaml` / `deterministic-source-concurrency-a-f` / Build CI-only deterministic source module | `.github/scripts/engine-protocol-v2-task8-concurrency-build-fixture.ps1` |
| `engine-protocol-v2-task8-concurrency.yaml` / `deterministic-source-concurrency-a-f` / Run deterministic A-F integration regression | `.github/scripts/engine-protocol-v2-task8-concurrency-run.ps1` |
| `engine-protocol-v2-task8.yaml` / `build-smoke-package-x64` / Smoke test complete protocol v2 source namespace | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` |
| `engine-protocol-v2-task9.yaml` / `interaction-v1` / Verify normal package excludes Task 9 test source | `.github/scripts/engine-protocol-v2-task9-package-audit.ps1` |
| `engine-protocol-v2-task9.yaml` / `interaction-v1` / Build CI-only deterministic interaction source | `.github/scripts/engine-protocol-v2-task9-build-fixture.ps1` |
| `engine-protocol-v2-task9.yaml` / `interaction-v1` / Stage CI-only interaction source | `.github/scripts/engine-protocol-v2-task9-stage-fixture.ps1` |
| `engine-protocol-v2-task9.yaml` / `interaction-v1` / Remove explicitly staged Task 9 test source | `.github/scripts/engine-protocol-v2-task9-remove-fixture.ps1` |
| `engine-protocol-v2-task10.yaml` / `media-v1` / Verify normal package excludes Task 10 fixture and second APIs | `.github/scripts/engine-protocol-v2-task10-package-audit.ps1` |
| `engine-protocol-v2-task10.yaml` / `media-v1` / Build CI-only deterministic media source | `.github/scripts/engine-protocol-v2-task10-build-fixture.ps1` |
| `engine-protocol-v2-task10.yaml` / `media-v1` / Stage CI-only media source | `.github/scripts/engine-protocol-v2-task10-stage-fixture.ps1` |
| `engine-protocol-v2-task10.yaml` / `media-v1` / Remove explicitly staged Task 10 test source | `.github/scripts/engine-protocol-v2-task10-remove-fixture.ps1` |
| `engine-protocol-v2-task11.yaml` / `filter-v1` / Verify Task 10 core isolation and Task 11 callback ordering | `.github/scripts/engine-protocol-v2-task11-core-audit.ps1` |
| `engine-protocol-v2-task11.yaml` / `filter-v1` / Verify normal package excludes fixtures and second APIs | `.github/scripts/engine-protocol-v2-task11-package-audit.ps1` |
| `engine-protocol-v2-task11.yaml` / `filter-v1` / Build, stage, and remove CI-only filter source | `.github/scripts/engine-protocol-v2-task11-build-fixture.ps1`, `.github/scripts/engine-protocol-v2-task11-stage-fixture.ps1`, `.github/scripts/engine-protocol-v2-task11-remove-fixture.ps1` |

The Task 11 timeout-race and Task 9/10/11 integration launcher steps were
already small wrappers and remain inline; the underlying race/integration
harnesses were not moved or weakened. A mechanical token audit retained all
Task 1–11 protocol request IDs/methods, expected error codes, event names,
fixture names, timeout constants, package paths, and process exit checks found
in the old bodies. The only structural change is location: the old body is in
the mapped `.ps1` file and the workflow step is a direct launcher.

## After inventory and allowlist

The final checker audit at pre-freeze SHA
`8c12beaf3d202be9e1cc6771f5c2ad23684a28b1` found 45 project-owned workflow
`run:` blocks: 44 PowerShell and one Bash. All are `TRIVIAL_WRAPPER`; there are
zero `EXECUTABLE_LOGIC`, zero `UNSUPPORTED_EXECUTABLE`, and zero
`DECLARATIVE_ONLY` run blocks. The exact remaining inline allowlist is:

| Workflow / job | Remaining inline step names | Why safe |
|---|---|---|
| `engine-complexity.yaml` / `complexity` | Install isolated complexity analyzer; Install pinned workflow YAML parser; Run complexity checker self-tests; Enforce complexity budget | One pinned package install or direct checker invocation; no control flow |
| `engine-protocol-v2-task1.yaml` / `build-smoke-package-x64` | Enforce smoke result | Direct PowerShell launcher |
| `engine-protocol-v2-task2.yaml` / `build-smoke-package-x64` | Smoke test protocol v2 framing; Enforce Task 2 smoke result | Direct script/launcher invocations |
| `engine-protocol-v2-task3.yaml` / `build-smoke-package-x64` | Smoke test protocol v2 capabilities; Enforce Task 3 smoke result | Direct script/launcher invocations |
| `engine-protocol-v2-task4.yaml` / `build-smoke-package-x64` | Smoke test protocol v2 revisions; Enforce Task 4 smoke result | Direct script/launcher invocations |
| `engine-protocol-v2-task5.yaml` / `build-smoke-package-x64` | Build and run bounded event queue policy test; Smoke test protocol v2 subscriptions and event delivery; Enforce Task 5 verification result | Direct script/launcher invocations |
| `engine-protocol-v2-task6.yaml` / `build-smoke-package-x64` | Smoke test protocol v2 source scene item lifecycle; Enforce Task 6 smoke result | Direct script/launcher invocations |
| `engine-protocol-v2-task7.yaml` / `build-smoke-package-x64` | Build and run generic properties bridge test; Smoke test protocol v2 properties API; Enforce Task 7 test results | Direct script/launcher invocations |
| `engine-protocol-v2-task8-concurrency.yaml` / both jobs | Compile and run capture routing regression; Verify production bridge ordering and isolation invariants; Build CI-only deterministic source module; Run deterministic A-F integration regression; Enforce deterministic A-F regression | Direct script/launcher invocations |
| `engine-protocol-v2-task8.yaml` / `build-smoke-package-x64` | Smoke test complete protocol v2 source namespace; Enforce Task 8 smoke result | Direct script/launcher invocations |
| `engine-protocol-v2-task9.yaml` / `interaction-v1` | Verify normal package excludes Task 9 test source; Build CI-only deterministic interaction source; Stage CI-only interaction source; Run Task 9 interaction integration regression; Remove explicitly staged Task 9 test source | Direct script/launcher invocations |
| `engine-protocol-v2-task10.yaml` / `media-v1` | Verify normal package excludes Task 10 fixture and second APIs; Build CI-only deterministic media source; Stage CI-only media source; Run Task 10 media integration regression; Remove explicitly staged Task 10 test source | Direct script/launcher invocations |
| `engine-protocol-v2-task11.yaml` / `filter-v1` | Verify Task 10 core isolation and Task 11 callback ordering; Verify normal package excludes fixtures and second APIs; Build CI-only deterministic filter source; Stage CI-only filter source; Run Task 11 filter integration regression; Run Task 11 timeout ownership race regression; Remove explicitly staged Task 11 test source | Direct script/launcher invocations |

`.github/workflows/windows-minimal.yaml` has no operator-attributable run
block. Other workflows contain upstream-owned blocks and remain outside the
project-attributable scope.

## Enforced language policy

- C/C++: numeric lizard CC enforcement.
- Standalone PowerShell: AST CC enforcement for named functions and top-level
  script bodies; script bodies are normally targeted at CC <= 5 and fail above
  CC 10.
- GitHub Actions: trivial wrappers are allowed; substantial PowerShell must be
  extracted to measured `.ps1`; substantial Bash/sh or other interpreter code
  fails closed because it has no analyzer policy here.
- Pure YAML/configuration/documentation is not a CC target. CMake/control
  scripts retain the existing manual-review treatment.

## Baseline and metrics

The complete after snapshot was measured at
`8c12beaf3d202be9e1cc6771f5c2ad23684a28b1`, committed separately, and pinned
by the subsequent checker-only literal change. Final snapshot blob and checker
pin: `81e2f631d41e84b71569ea46ca9439da367dc567`.

| Enforced class | Count | Average | Median | p90 | Maximum | >5 | >7 | >10 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Named functions | 1,200 | 3.841 | 3 | 7 | 13 | 275 | 116 | 1 |
| PowerShell script bodies | 35 | 1.257 | 1 | 2 | 5 | 0 | 0 | 0 |
| Combined enforced scopes | 1,235 | 3.767 | 3 | 7 | 13 | 275 | 116 | 1 |

The previous accepted 993 scopes remain present by exact identity/continuity:
993 matched, 242 legitimate new enforced scopes, 1,235 final scopes, and zero
current scopes treated as unbaselined accepted code after the freeze. The 242
new scopes are the extracted workflow scripts and the checker/self-test
functions required to enforce and test this policy.

All 28 newly introduced extracted scripts have script-body CC 1. Their maximum
named-function CC values are: Task 1 footprint 5; Task 1 protocol-v1 6; Task
2 framing 10; Task 3 capabilities 8; Task 4 revisions 7; Task 5 queue 9 and
subscriptions 10; Task 6 runtime 10; Task 7 bridge 9 and properties 10; Task
8 capture 6, bridge audit 8, fixture build 2, integration runner 4, and source
smoke 9; Task 9 build 2, package audit 2, stage 3, remove 3; Task 10 build 2,
package audit 2, stage 3, remove 3; Task 11 build 2, core audit 9, package
audit 2, stage 3, remove 3. No workflow-script exception was added.

The sole CC > 10 exception remains
`libobs/obs-source.c::obs_source_destroy_defer` at CC 13. Identity migrations
remain exactly `[]`.

## Deterministic tests and review state

The complete checker suite passes A–AI. A–Z remain unchanged and passing. AA
rejects inline PowerShell control flow, AB rejects an inline PowerShell
function, AC accepts a direct PowerShell wrapper, AD accepts a direct Bash
wrapper, AE and AF fail closed for unsupported inline Bash/Python, AG fails
closed on a PowerShell parser error, AH retains the policy after workflow
rename, and AI rejects a new untracked workflow before commit. The exact AA,
AB, AE, AF, AG, and AI failure text requires extraction or reports unsupported
inline executable code as applicable.

The final exact-SHA hosted matrix completed successfully at
`42d362014e52f6cb29b463cbb83922ca3224132e` (the documentation-only update that
records this result follows that tested SHA). The run IDs were:

| Lane | Run ID | Conclusion |
|---|---:|---|
| Task 1 / 1.1 | 33445702150 | success |
| Task 2 | 33445704661 | success |
| Task 3 | 33445707119 | success |
| Task 4 | 33445709413 | success |
| Task 5 | 33445711833 | success |
| Task 6 | 33445715323 | success |
| Task 7 | 33445719655 | success |
| Task 8 | 33445722636 | success |
| Task 8 concurrency | 33445725591 | success |
| Task 9 | 33445729922 | success |
| Task 10 | 33445732837 | success |
| Task 11 | 33445736076 | success |
| Complexity Regression Gate (A–AI) | 33445739056 | success |

Because only workflow wrappers and test tooling changed, the accepted physical
Task 8/10/11 smoke result remains applicable; no runtime binary was rebuilt.
Hosted Windows lanes execute the extracted scripts, and the underlying
physically accepted harness scripts were not changed.

Runtime immutability proof target:
`git diff 44243a5013007a449c1d0b9903233929bd44a141 <final> -- engine libobs plugins`
is empty. Task 12 remains **NOT STARTED / NOT AUTHORIZED**.

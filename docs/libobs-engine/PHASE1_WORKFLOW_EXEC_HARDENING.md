# Phase-1 Workflow Executable-Code Hardening

Status: **IN REVIEW**. This is the final pre-Phase-2 complexity-scope closure;
it does not authorize Task 12 and does not change the accepted runtime or
standalone-function/script-body result.

## Starting point and inventory method

- Starting reviewed candidate: `586a15a452f9c6e6a6813f8724c81c010a050c52`
- Accepted production base: `636e5914f6e8d69853ab4ce83d80ef944e6835dc`
- Runtime hardening reference: `44243a5013007a449c1d0b9903233929bd44a141`
- YAML parser: PyYAML `6.0.2`, using node-level `yaml.compose` mappings and
  scalar source marks for jobs, steps, `run`, `shell`, and line ranges.
- Ownership: exact line-range `git blame` against the existing operator identity
  rules; filenames alone were not treated as ownership evidence.
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

This inventory is frozen as the pre-edit reference. Extraction decisions and
the after-inventory will be appended below in subsequent commits.

Task 12: **NOT STARTED / NOT AUTHORIZED**

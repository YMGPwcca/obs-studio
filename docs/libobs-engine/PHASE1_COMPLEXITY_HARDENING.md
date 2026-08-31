# Phase-1 Cyclomatic Complexity Hardening

Status: **IN REVIEW**. This record is not an acceptance of the hardening work.

## Candidate and scope

- Production branch: `engine-protocol-v2`
- Accepted starting checkpoint: `3fc2e678d10809a4dca8b28107710534160803ab`
- Accepted Task-11 implementation ancestor: `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
- Hardening branch: `phase1-complexity-hardening`
- Candidate implementation/checkpoint used for hosted verification:
  `44243a5013007a449c1d0b9903233929bd44a141`
- Ownership merge-base: `bcd53e2914c68a62b2a9387a7e8ee3b59d1fd1df`
- Task 12: **NOT STARTED / NOT AUTHORIZED**

Ownership was derived from `git log BASE..accepted` author metadata, commit
patches, and accepted-HEAD `git blame`. Filename ownership was not inferred and
archived WIP branches were excluded. The discovered operator aliases are:

- `Nguyễn Tuấn Nghĩa <37042810+YMGPwcca@users.noreply.github.com>`
- `YMGPwcca <ymgpwcca@proton.me>`

The lineage contains 64 operator-authored commits; 29 of those commits
authored or materially modified executable-code scope. The complete commit and
file inventory is in `complexity-ownership-inventory.json` and its Markdown
companion. It contains 41 cyclomatic-complexity target files and 42 separately
reviewed non-CC files.

## Measurement method

- C/C++: lizard 1.24.0, invoked from an isolated temporary Python target path;
  default CCN and lizard NLOC were used.
- PowerShell: PowerShell 7.6.4 `System.Management.Automation.Language.Parser`
  AST. Function CC counts AST decisions, loops, catches, and boolean
  short-circuit operators; top-level script bodies are reported separately and
  excluded from function summaries.
- Percentile: nearest-rank `ceil(0.90 * N)`.
- No analyzer dependency was added to the production runtime.

The isolated analyzer is not a universal parser for every executable language.
The current scoped non-PowerShell script set contains no additional language
whose exact function-level CC is claimed; CMake/control files are reviewed
separately. The machine-readable reports are:

- `complexity-baseline.json` / `complexity-baseline.md`
- `complexity-before.json`
- `complexity-after.json` / `complexity-report.md`

## Before and after

| Measure | Before | After |
|---|---:|---:|
| Scoped functions | 543 | 764 |
| Average CC | 5.703 | 3.806 |
| Median CC | 4 | 3 |
| 90th percentile CC | 13 | 7 |
| Maximum CC | 61 | 13 |
| Functions with CC > 5 | 171 | 180 |
| Functions with CC > 7 | 115 | 72 |
| Functions with CC > 10 | 71 | 1 |

The scoped function count increases because cohesive extracted helpers are
measured individually. The primary reductions are protocol classification and
dispatch (61/54-class functions to low single digits), event/media publishing,
property validation/serialization, source/item adapters, filter routing, and
test-script orchestration. The full before/after function table and exhaustive
remaining lists are in `complexity-report.md`.

## Remaining complexity and exceptions

The sole CC > 10 function is:

| File | Function | CC | Decision |
|---|---|---:|---|
| `libobs/obs-source.c` | `obs_source_destroy_defer` | 13 | Explicit exception |

This is an upstream source-destruction pipeline. The operator-attributed line
is the accepted `deferred_update_mutex` destruction in the existing teardown
order. Extracting its resource and mutex destruction groups solely to lower a
metric would obscure lifetime ordering and increase safety risk. It is recorded
in `complexity-exceptions.json` with measured CC, reason, task/date, and review
note. No other CC > 10 exception is allowed.

The remaining CC 8–10 functions are primarily exact protocol validation,
observer synchronization, deferred source/media/filter settlement, libobs
initialization/update paths, and compact parser/event helpers. The accepted
Task-8/10/11 paths were not flattened into generic abstractions: their lock
ownership, callback lifetime, serial quarantine, rollback, response/event
ordering, and resync behavior remain visible. The exhaustive CC > 5, CC > 7,
and CC > 10 lists, including NLOC and parameters, are generated in
`complexity-report.md`.

## Regression gate

The repository gate is `tools/check-complexity.ps1`. Its historical ownership
baseline remains anchored at `3fc2e678d10809a4dca8b28107710534160803ab` and the
merge-base of `origin/master` for answering which pre-hardening code belongs to
the operator. `After` and `Check` then form a moving measurement universe:

1. retain every surviving historical target;
2. add every current C/C++ or PowerShell file added by operator work after the
   accepted checkpoint, measuring all functions in a new or recreated file;
3. add functions in later operator-modified files when exact accepted identity,
   current operator blame, or candidate diff lines attribute them to project
   work; and
4. carry exact historical file aliases across renames for baseline comparison.

`Check` accepts `complexity-after.json` only after validating its pinned Git
blob ID, report schema, accepted/ownership references, and resolvable
measurement provenance. A replacement or malformed baseline therefore fails
closed before any comparison can be skipped.

Deleted-and-recreated paths are treated as new identities. Baseline matching is
exact on language, scope, normalized path, function name, and signature; the
old same-file/name fallback is gone. The sole allowlist entry for
`obs_source_destroy_defer` also records its exact signature and accepted
baseline key, so a similarly named or recreated function cannot inherit it.

### Function identity continuity

`complexity-identity-migrations.json` is the reviewed escape hatch for a
function rename or an otherwise non-derivable identity change. Each entry is
an exact object in a top-level JSON array of the form:

```json
{
  "baselineKey": "cpp|function|src/a.cpp|old_name|old_name( int value)",
  "current": {
    "language": "cpp",
    "scopeKind": "function",
    "file": "src/b.cpp",
    "function": "new_name",
    "signature": "new_name( int value)"
  }
}
```

The old key must exist in `complexity-after.json`; the current identity must
occur exactly once in the complete parsed candidate inventory; old and new
identities are each one-to-one; and the old identity must no longer be present.
An explicit target cannot override a different exact/lineage or unambiguous
same-name binding. The document and nested objects reject null/non-object
entries and unsupported properties. No wildcard, prefix, regex, or fuzzy body
matching is used. A
unique same-name signature change in one file lineage is recognized
automatically; multiple baseline or current overload candidates fail closed
and require the exact metadata. Deleted-and-recreated paths always require an
explicit migration with a changed current identity; same-key recreation is
rejected because this schema cannot distinguish generations, so it remains a
fail-closed error. A migration carries the old CC budget, so it never resets a
function to the ordinary CC 10 allowance. Exceptions remain separate
metadata.

The only analyzers officially supported by this gate are C/C++ (including the
repository's `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, `.hxx`, `.inc`,
`.inl`, `.ipp`, `.tcc`, `.cppm`, and `.ixx` extensions) and PowerShell (`.ps1`,
`.psm1`). Known executable extensions such as Python, Lua, shell, batch,
JavaScript/TypeScript, Ruby, Go, Rust, Java/Kotlin, Swift, and Objective-C are
rejected with a clear fail-closed message; they are never sent to the
PowerShell parser. Unknown extensions introduced by project work are also
rejected until a language policy/analyzer is defined. Static declarations and
documentation remain explicitly non-executable.

The CI workflow is `.github/workflows/engine-complexity.yaml` and installs
`lizard==1.24.0` only into the runner's temporary directory. Before enforcing
the budget, it runs `tools/check-complexity.tests.ps1` in isolated temporary
Git repositories. The self-test covers A–H plus: I function rename without a
migration fails, then an exact migration preserves the old budget and permits
the unchanged CC; J a unique signature change inherits the old budget; K
overload ambiguity fails closed; L a genuinely unrelated CC<=10 function still
passes; M nonexistent/duplicate/wrong/stale migration metadata fails closed;
and N a combined file+function rename preserves the old budget. Existing A–H
coverage still includes new bad `.cpp`/header/PowerShell functions, the exact
exception, unsupported Python, and file-rename regression protection.
Additional O and P fixtures prove deleted-and-recreated files cannot use
automatic same-name continuity, including a rename into a previously deleted
destination; Q proves a migration cannot override an existing file-lineage
baseline binding; R proves operator provenance follows a later non-operator
file rename; S validates the pinned accepted-baseline artifact; T covers a
staged working-tree delete/recreate; U proves a function added before the
final freeze is protected by the frozen baseline afterward.

## Refactoring and semantic review

Logical implementation commits on the hardening branch are:

- `ca9f1ac77` — protocol and property dispatch simplification
- `36017d2b0` — event and media adapter simplification
- `80a286d7a` — filter dispatch and observer simplification
- `d32ad7a9e` — runtime adapters and settlement-path simplification
- `bb62656ce` — complexity evidence and regression gate
- `44243a501` — Task-11 preflight made compatible with reviewed protocol-core refactoring

The last commit replaces a pre-hardening blob-hash assertion in the Task-11
workflow with structural isolation and ordering assertions; it does not weaken
the runtime checks. No Task-12 implementation or Protocol-v2 feature was added.

During the second review pass, three unsafe extraction issues were corrected:

1. duplicate release during source-duplicate rollback;
2. raw property-schema ownership on early button-validation failure; and
3. event-vector consumption on a direct media callback route.

The original startup safe-module registration order was also restored, and the
Task-8/Task-11 source-order markers remain asserted. No public `obs_source_update`
API was changed.

## Verification on the candidate

Local Windows verification used the current VS x64 `RelWithDebInfo` build. The
complete local runtime lanes passed: direct events/properties unit tests, Task
8 deterministic A–F concurrency, Task 9 interaction, Task 10 media, Task 11
filter, Task 11 timeout ownership race, Task-8 static bridge ordering, and a
clean-package protocol smoke. The clean package had one `obs-engine.exe`, one
`obs.dll`, and no frontend, WebSocket, browser, or CI fixture DLLs.

The physical smoke ran on Windows 10/11 build 26200 (25H2), AMD Ryzen 7 9700X,
with D3D11 initialization on the installed AMD adapter. Expected optional AJA,
DeckLink, NVIDIA effects/NVENC, and VLC warnings did not affect the run.

Hosted runs below all target the same candidate SHA
`44243a5013007a449c1d0b9903233929bd44a141` and completed successfully:

| Lane | Run ID | Artifact ID / name | Artifact digest |
|---|---:|---|---|
| Task 1 | 33296581255 | 9727644571 / `obs-engine-windows-x64-task1-1` | `sha256:49a3113b632f19b5cf3211fc513bfc5bce5fc8eb311386e35316f4ddbfe41d49` |
| Task 2 | 33296582604 | 9727626722 / `obs-engine-windows-x64-task2` | `sha256:7ae1f74121982f4e662be882c2f6f3cc339d79ff82672348631023e50c125d3c` |
| Task 3 | 33296584251 | 9727662115 / `obs-engine-windows-x64-task3` | `sha256:f9eb0e1f8a5edeeba12fcb98721aabda1c9053bb08915b1f711c30044d9fa7db` |
| Task 4 | 33296585498 | 9727647927 / `obs-engine-windows-x64-task4` | `sha256:490f021a19c3d69d2293a6b17dc22971b2e279da2c427219a07d3d1ce3fc56bf` |
| Task 5 | 33296586752 | 9727647902 / `obs-engine-windows-x64-task5` | `sha256:cdf3c2455ca7897b4bf0913f8d32d8604c896e236009329e88926829e69ae404` |
| Task 6 | 33296587999 | 9727649498 / `obs-engine-windows-x64-task6` | `sha256:bee3d27dac7cb2155e31cfebcb757727a94d1b5138e050563da2d48533dd5d55` |
| Task 7 | 33296589392 | 9727649755 / `obs-engine-windows-x64-task7` | `sha256:3c0ce3938f7dde24adc2bab2d0290e29908c52feab9e466e70be85cdd710d4ce` |
| Task 8 | 33296590571 | 9727653167 / `obs-engine-windows-x64-task8` | `sha256:0a8e259b0b6e858c73b114db279f838813806c90b743a2fa91a596250360677e` |
| Task 8 concurrency | 33296591596 | 9727637098 / `obs-engine-task8-concurrency-diagnostics` | `sha256:bef10e921aabcbb533bdb919153b1d67d1756b8980d3220d314f52eee9929fbc` |
| Task 9 | 33296592957 | 9727651384 / `obs-engine-windows-x64-task9` | `sha256:42addf227f080b62a09a127aa467c414733a4db48d59d85eb38e4673d6315a78` |
| Task 10 | 33296594147 | 9727651849 / `obs-engine-windows-x64-task10` | `sha256:51db8a67ac822d2e124d8f65221f6c0e4dab5d86a57ccd37e3b8ae11ad0b539a` |
| Task 11 | 33296595413 | 9727657593 / `obs-engine-windows-x64-task11` | `sha256:ca131bb9460bcdf117f2e50814ce05fd7138ee0ce5f35437aa4986e93b1f99da` |

The hosted Task-11 run initially failed on its historical hard-coded source
blob hash; that run was not used. The rerun above passed after the preflight was
updated to permit the reviewed complexity refactor.

Local candidate binary hashes:

- `obs-engine.exe`: `3E0A4EA8379AE626F10B5E93A934DE316FAC35C45D5E8903999DCC8B66636DE7`
- `obs.dll`: `3E63A5344EB4129DF305A2827E06996BC22A8019F6037FDF120EB15093A1630D`

Local package artifact (not a hosted artifact ID):

- name: `obs-engine-windows-x64-phase1-complexity-hardening-44243a501.zip`
- SHA-256: `90BCC1E736D090EA358C4C2E993E973749EB537E3D160FCDF02D9C8EB416A79F`

## Known debt and review gate

The accepted source bridge and the media/filter bridges still have private
state duplication and a few intentionally repeated canonical-handle readers.
Further consolidation should be a separate, invariant-focused cleanup. The
remaining upstream teardown exception and high-risk settlement functions are
review items, not hidden exclusions.

Independent advisor review was requested but could not run because the Codex
workspace was out of credits. This hardening branch therefore remains pending
independent advisor review and explicit human approval.

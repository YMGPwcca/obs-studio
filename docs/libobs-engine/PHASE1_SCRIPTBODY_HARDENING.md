# Phase-1 PowerShell Script-Body Complexity Hardening

Status: **IN REVIEW**. This is the final pre-Phase-2 quality closure; it does
not authorize Task 12 and does not change the accepted runtime/named-function
Phase-1 result.

## Scope and baseline

- Accepted starting production checkpoint: `636e5914f6e8d69853ab4ce83d80ef944e6835dc`
- Runtime hardening implementation: `44243a5013007a449c1d0b9903233929bd44a141`
- Accepted named-function baseline blob: `3b800743af516c02cc90966ab233c9ace6745f43`
- Final pre-freeze measurement checkpoint: `3251102af1e62192d22a35cb5a39ce0fb6add98d`
- Final frozen after-snapshot commit: `a571bc852`
- Final frozen `complexity-after.json` blob: `6484fa71b27c917bffdbabd09fa170a2e8a4820b`
- Checker pin commit: `af9a290a`; the checker remains pinned to the blob above.
- Scope source: the existing checker’s Git ownership/provenance rules; upstream
  repository-wide PowerShell is not included.
- Analyzer: PowerShell `System.Management.Automation.Language.Parser` AST.

Before this cleanup, script bodies were measured as `scopeKind=script-body` but
excluded from enforcement. The complete pre-edit inventory is below. Top-level
statement counts are the parsed end-block statement count; NLOC is the
checker’s nonblank, non-comment count outside function definitions.

| File | Kind | CC | NLOC | Top-level statements |
|---|---|---:|---:|---:|
| `tools/check-complexity.ps1` | utility/checker | 128 | 446 | 209 |
| `.github/scripts/engine-protocol-v2-task11.ps1` | CI verification harness | 80 | 605 | 43 |
| `.github/scripts/engine-protocol-v2-task10.ps1` | CI verification harness | 63 | 443 | 31 |
| `.github/scripts/engine-protocol-v2-task9.ps1` | CI verification harness | 26 | 265 | 22 |
| `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | CI verification harness | 9 | 32 | 23 |
| `tools/check-complexity.tests.ps1` | utility/checker | 5 | 271 | 24 |
| `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | CI verification harness | 3 | 22 | 29 |

Script-body baseline summary:

| Measure | Value |
|---|---:|
| Script bodies | 7 |
| Average CC | 44.857 |
| Median CC | 26 |
| 90th percentile CC | 128 |
| Maximum CC | 128 |
| Script bodies with CC > 5 | 5 |
| Script bodies with CC > 7 | 5 |
| Script bodies with CC > 10 | 4 |

Every script body above CC 5:

- `tools/check-complexity.ps1` — CC 128, utility/checker;
- `.github/scripts/engine-protocol-v2-task11.ps1` — CC 80, CI verification harness;
- `.github/scripts/engine-protocol-v2-task10.ps1` — CC 63, CI verification harness;
- `.github/scripts/engine-protocol-v2-task9.ps1` — CC 26, CI verification harness; and
- `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` — CC 9, CI verification harness.

Every script body above CC 7 is the same five-file set. Every script body above
CC 10 is:

- `tools/check-complexity.ps1` — CC 128;
- `.github/scripts/engine-protocol-v2-task11.ps1` — CC 80;
- `.github/scripts/engine-protocol-v2-task10.ps1` — CC 63; and
- `.github/scripts/engine-protocol-v2-task9.ps1` — CC 26.

The top-25 list is the complete seven-body inventory above, ordered by CC.
The four bodies above CC 10 are substantial orchestration, not trivial
bootstrap code; they require cohesive extraction and semantic review. The
CC 9 concurrency harness also requires review because its ordering and timeout
behavior are acceptance-critical.

## Target and acceptance policy

The target is CC <= 5 for project-owned script bodies where safely practical,
with CC > 7 requiring explicit review and CC > 10 failing. No script-body
exception is authorized by this baseline. Existing named-function enforcement,
identity continuity, unsupported-language policy, exact exception isolation,
and runtime immutability remain in force.

The final report will separate named functions from script bodies and will
prove that all enforced scopes are covered by the frozen final baseline.

## Refactoring completed

The following acceptance harnesses were structurally refactored into small
entry points and cohesive scenario helpers without changing their request
payloads, expected revisions, event order, timeout behavior, cleanup, or
failure propagation:

- `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` — case runner and
  failure cleanup entry points;
- `.github/scripts/engine-protocol-v2-task9.ps1` — engine setup, interaction,
  stale-state pruning, validation, unsupported-capability, and shutdown stages;
- `.github/scripts/engine-protocol-v2-task10.ps1` — session/source setup, media
  state/action scenarios, timeout/orphan handling, overflow, cleanup, and
  shutdown stages; and
- `.github/scripts/engine-protocol-v2-task11.ps1` — session/filter graph,
  mutation/order, duplicate/source, callback-isolation, timeout/race, overflow,
  cleanup, and shutdown stages.

`.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` was audited and
did not require structural changes; its body was already CC 3.

## Semantic-preservation audit

The mechanical before/after inventory below compares the accepted starting
checkout with the refactored working tree. Counts are occurrences in the
PowerShell source; literal method names, expected error codes, and event names
were also compared as exact sets.

| Harness | Request-id assignments | Method assignments | Assert-Ok | Assert-Error | Read-Event | Read-Until-Resync | Wait calls |
|---|---:|---:|---:|---:|---:|---:|---:|
| Task 8 concurrency | 14 → 14 | 14 → 14 | 14 → 14 | 0 → 0 | 0 → 0 | 0 → 0 | 1 → 1 |
| Task 9 interaction | 27 → 27 | 27 → 27 | 22 → 22 | 7 → 7 | 0 → 0 | 0 → 0 | 1 → 1 |
| Task 10 media | 53 → 53 | 53 → 53 | 36 → 36 | 17 → 17 | 30 → 30 | 12 → 12 | 1 → 1 |
| Task 11 filter | 58 → 56 | 58 → 56 | 44 → 44 | 16 → 14 | 23 → 23 | 6 → 6 | 1 → 1 |
| Task 11 timeout race (unchanged) | 7 → 7 | 7 → 7 | 6 → 6 | 0 → 0 | 0 → 0 | 3 → 3 | 1 → 1 |

Task 11's two-count reduction is intentional: three equivalent order
validation requests and their error assertions are represented by one
data-driven loop. The three exact case IDs, `bad_request` expectations, and
revision guard remain present. Exact method-name, error-code, and event-name
sets are unchanged for every harness. The subsequent exact-SHA hosted matrix
is the runtime confirmation of this audit.

## After script-body measurement

The current post-refactor measurement contains seven enforced PowerShell
script bodies. All are at or below the normal target of CC 5.

| File | Role | CC | NLOC |
|---|---|---:|---:|
| `tools/check-complexity.tests.ps1` | utility/checker self-test | 5 | 311 |
| `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | CI verification harness | 3 | 22 |
| `.github/scripts/engine-protocol-v2-task10.ps1` | CI verification harness | 2 | 21 |
| `.github/scripts/engine-protocol-v2-task11.ps1` | CI verification harness | 2 | 23 |
| `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | CI verification harness | 2 | 17 |
| `.github/scripts/engine-protocol-v2-task9.ps1` | CI verification harness | 1 | 14 |
| `tools/check-complexity.ps1` | utility/checker | 1 | 46 |

| Script-body measure | Before | After |
|---|---:|---:|
| Count | 7 | 7 |
| Average CC | 44.857 | 2.286 |
| Median CC | 26 | 2 |
| p90 CC | 128 | 5 |
| Maximum CC | 128 | 5 |
| CC > 5 | 5 | 0 |
| CC > 7 | 5 | 0 |
| CC > 10 | 4 | 0 |

Current named-function statistics are 986 functions, average CC 3.797,
median 3, p90 7, maximum 13, with 227 above 5, 95 above 7, and one above
10. The combined enforced-scope statistics are 993 scopes, average CC 3.787,
median 3, p90 7, maximum 13, with 227 above 5, 95 above 7, and one above 10.
The sole named-function exception remains
`libobs/obs-source.c::obs_source_destroy_defer` at CC 13; there are no
script-body exceptions.

The checker now enforces both `scopeKind=function` and
`scopeKind=script-body`. Script-body continuity uses the exact
`powershell|script-body|file|<script-body>|<script-body>` identity with the
existing Git file-lineage rules; recreated paths require the existing exact
continuity mechanism. The `<script-body>` fields are fixed display sentinels,
not function-name or signature inference. Unsupported executable languages
remain fail-closed.

The deterministic suite now reports **A–Z PASS**. Cases V–Z cover new bad and
acceptable script bodies, frozen-body regression, file rename continuity, and
delete/recreate continuity. The final frozen baseline contains all 986 named
functions and all seven script bodies, for 993 enforced scopes. A final
coverage audit matched 993 current enforced scopes to 993 accepted baseline
identities/continuity relationships; zero current enforced scopes remain
ordinary unbaselined new scopes. The identity migration document remains
exactly `[]`.

## Final freeze verification

The final local checker run passes with the pinned baseline. The deterministic
suite reports **A–Z PASS**, including:

- CASE V: a new PowerShell script body at CC 12 fails specifically as a
  script-body scope;
- CASE W: a new script body at CC 5 passes;
- CASE X: frozen script-body CC 2 → CC 3 fails with baseline 2, and restored
  CC 2 passes;
- CASE Y: a renamed script body at CC 3 fails against inherited baseline 2,
  and restored CC 2 passes; and
- CASE Z: a delete/recreated script path at CC 5 fails continuity rather than
  receiving a fresh budget.

The final hosted Task 1–11 matrix, physical Windows smoke availability, and
the hosted complexity-gate run are recorded here when the same final candidate
SHA has completed those checks.

Task 12: **NOT STARTED / NOT AUTHORIZED**

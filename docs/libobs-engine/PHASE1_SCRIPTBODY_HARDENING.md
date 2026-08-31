# Phase-1 PowerShell Script-Body Complexity Hardening

Status: **IN REVIEW**. This is the final pre-Phase-2 quality closure; it does
not authorize Task 12 and does not change the accepted runtime/named-function
Phase-1 result.

## Scope and baseline

- Accepted starting production checkpoint: `636e5914f6e8d69853ab4ce83d80ef944e6835dc`
- Runtime hardening implementation: `44243a5013007a449c1d0b9903233929bd44a141`
- Accepted named-function baseline blob: `3b800743af516c02cc90966ab233c9ace6745f43`
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

Task 12: **NOT STARTED / NOT AUTHORIZED**

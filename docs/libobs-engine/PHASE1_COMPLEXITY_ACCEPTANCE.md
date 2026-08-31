# Phase-1 Cyclomatic Complexity Acceptance

Status: **COMPLETE / ACCEPTED**

Approval date: **2026-08-31**

This record documents the explicit human approval of the exact reviewed
Phase-1 hardening checkpoint. It does not authorize Task 12 or any later
roadmap task.

## Exact reviewed lineage

- Production branch before Phase-1 finalization: `engine-protocol-v2`
- Accepted starting production checkpoint:
  `3fc2e678d10809a4dca8b28107710534160803ab`
- Accepted Task-11 implementation:
  `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
- Runtime hardening implementation:
  `44243a5013007a449c1d0b9903233929bd44a141`
- Final pre-freeze complexity measurement:
  `5eb1e665bfee7c868c088bf306e770e814159c54`
- Final reviewed Phase-1 checkpoint:
  `1b2ddacbb36c39bb61fd645594f0746f106956bf`
- Final frozen `complexity-after.json` Git blob:
  `3b800743af516c02cc90966ab233c9ace6745f43`
- `complexity-identity-migrations.json`: `[]`

## Frozen complexity evidence

| Measure | Accepted value |
|---|---:|
| Scoped functions | 875 |
| Average CC | 3.795 |
| Median CC | 3 |
| 90th percentile CC | 7 |
| Maximum CC | 13 |
| Functions with CC > 5 | 204 |
| Functions with CC > 7 | 83 |
| Functions with CC > 10 | 1 |

The final snapshot records `measurementHead` as
`5eb1e665bfee7c868c088bf306e770e814159c54` and `acceptedRef` as
`3fc2e678d10809a4dca8b28107710534160803ab`. The checker pins the exact final
baseline blob and retains schema, scope-provenance, and integrity validation.

The final coverage audit reported:

- current scoped functions: **875**;
- accepted baseline keys: **875**;
- explicit continuity migrations: **0**;
- matched baseline/continuity functions: **875**; and
- unbaselined new functions: **0**.

## Sole accepted exception

The only CC > 10 exception is the exact recorded identity:

```text
libobs/obs-source.c
obs_source_destroy_defer
CC 13
```

Its exception records the exact language, scope, path, function, signature,
baseline key, measured complexity, and rationale. It preserves upstream source
destruction lifetime/resource/mutex teardown ordering; no similarly named or
recreated function inherits it.

## Deterministic and hosted verification

- The complete checker self-test suite A–U passed.
- CASE U froze a function at CC 2, proved CC 2 -> CC 3 fails against the
  frozen baseline, and proved unchanged CC 2 passes.
- The final GitHub Complexity Regression Gate was run as `33378345196`.
- Exact workflow head:
  `1b2ddacbb36c39bb61fd645594f0746f106956bf`
- Workflow conclusion: **success**.
- The independent advisor final verdict was `LUNA REVIEW: ship` with zero
  confirmed blockers.

The Task 1–11 exact-SHA regression matrix, physical Windows smoke result, and
runtime/package evidence are recorded in
`PHASE1_COMPLEXITY_HARDENING.md`. The human approval applies to that evidence
and the exact reviewed checkpoint listed above.

## Runtime and task boundary

The acceptance change is documentation/status-only. The runtime implementation
tree remains unchanged from `44243a5013007a449c1d0b9903233929bd44a141`; no
runtime rebuild or Task 1–11 rerun is required for this acceptance checkpoint.

Tasks 1–11: **ACCEPTED**

Phase-1 complexity hardening: **ACCEPTED**

Task 12: **NOT STARTED / NOT AUTHORIZED**

No merge, feature work, Protocol-v2 change, or Phase 2 work is included in
this acceptance.

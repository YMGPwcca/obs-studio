# Task 11 Final Acceptance — `filter.*`

**Acceptance date:** 2026-08-30
**Status:** ACCEPTED
**Accepted implementation SHA:** `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
**Accepted Task-10 base/documentation checkpoint:** `e8a0cb36cb2baacb8368ff5236a7a84bec9584ea`
**Acceptance branch:** `task11-codex`

## Human approval

The operator explicitly approved this exact implementation:

> I explicitly approve Task 11 at SHA `e7b34828cb9fbd55bae01f97148f1ec93a4ae015`.

## Hosted exact-SHA evidence

Every run below completed successfully against the accepted implementation SHA:

| Lane | Run ID |
|---|---:|
| Task 1 | `33263628529` |
| Task 2 | `33263630602` |
| Task 3 | `33263633024` |
| Task 4 | `33263634629` |
| Task 5 | `33263636457` |
| Task 6 | `33263638563` |
| Task 7 | `33263640078` |
| Task 8 | `33263641745` |
| Task 8 concurrency | `33263643013` |
| Task 9 | `33263644473` |
| Task 10 | `33263645827` |
| Task 11 | `33263622190` |

Task-11 artifact:

- ID: `9718032430`
- Name: `obs-engine-windows-x64-task11`
- GitHub artifact digest:
  `sha256:cd774eb40a402336d143a248566fd33314a39a0f72d189711d0195088d23bdff`
- `obs-engine.exe` SHA-256:
  `8F082901A10404FBEB8E7C828F1F2EF13EB3F72B1895EAEF46B814C649509E88`
- `obs.dll` SHA-256:
  `D745165BC1A414200C7FBED96FAC47730BB80ED632F49555B402B39D4B4BC5A3`
- CI-only fixture `task11-filter-source.dll` SHA-256:
  `C9789BB6674F2A57D342FF29083E96F941A1DA9C92ECAF9CFE7CC23E14E0EB80`

## Review and physical evidence

Independent review verdict: PASS for the Task-11 implementation, filter
contract, lifetime/ownership, settings settlement, A-to-B timeout race,
coalesced updates, rename/enable quarantine, parent-removal ordering,
`source.duplicate` compatibility, Task-10 isolation, exact-SHA CI, package
provenance, physical Windows behavior, source review, and review-bundle
integrity. Confirmed blockers: `0`.

The final review ZIP is `Task11-final-review-e7b34828.zip` with SHA-256:

`21563E74AC97B5DAD4B4B417340A276F1344B987B5E8670726891D827E09C059`

Its `SHA256SUMS` file has SHA-256:

`8DD911ABA555AA1BC75036E6F25EF2F11BAA16BF3E8DF5FB13ACE85D51443C2E`

Bundle verification matched `3,120 / 3,120` manifest entries, with `0`
missing, `0` mismatches, and `0` unmanifested files. The normal artifact had
`1,548` files; the physical test copy had `1,549`, with only the CI fixture
added. All common runtime files were byte-identical.

Physical Windows validation passed against the exact hosted engine artifact,
including the real video-thread filter integration and timeout-race validation.
The physical raw protocol audit recorded `62/62` responses, `34` events with
continuous sequence `1..34`, and nondecreasing mixed revisions.

## Corrected ownership race

The first reviewed candidate `14ea89f14f967bea9f3552632f57649b8070c41f` was
not accepted. A deterministic test showed that timeout A followed by a
same-filter rename or enable could erase the handle-only quarantine, allowing
late A to settle newer request B when shared current settings already matched B.

The accepted implementation adds a private tracked-update bridge:

- each source update receives a per-source serial under the deferred-update
  mutex;
- the video callback records the exact contiguous serial range it covers;
- updates submitted while an older callback runs remain in a later range;
- settlement requires exact filter handle, canonical settings, observer
  generation greater than baseline, and request serial contained in the range;
- every timed-out serial is retained independently;
- rename/enable callbacks cannot consume settings uncertainty;
- late or ambiguous completion forces resynchronization;
- public `obs_source_update()` and `obs_source_reset_settings()` signatures are
  unchanged;
- the global public `source_update` payload is unchanged; serial evidence is
  source-local and private.

## Non-blocking debt

The following are recorded without modification in this acceptance commit:

1. The private tracked-update functions are exported symbols from `obs.dll`,
   although their declarations are private and non-installed.
2. The source-local `update` signal has additive internal serial calldata; the
   global `source_update` signal remains compatibility-preserved.
3. Serial/generation exhaustion degrades conservatively but has no deterministic
   exhaustion test yet.
4. Some local pre/post-fix evidence uses `*-modified` version strings; exact
   hosted source provenance, artifact hashes, and physical final-candidate
   evidence establish the accepted implementation independently.

Task 12 remains `PLANNED / NOT AUTHORIZED`. No Task-12 implementation is part
of this acceptance.

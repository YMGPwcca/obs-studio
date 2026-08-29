# Task 10 Final Acceptance — `media.*`

**Acceptance date:** 2026-08-29  
**Accepted implementation SHA:** `6a590c2985a99d186c8eecd0241acdc824d32168`  
**Accepted implementation tree:** `117534fcd99b96a64d7205c702e1abeeb4316835`  
**Implementation parent:** `19dc985323332230999ffa1a25a3375629a20c42`  
**Status:** ACCEPTED  
**Subsequent roadmap task:** Task 11 `filter.*` — later accepted at
`e7b34828cb9fbd55bae01f97148f1ec93a4ae015`
**Current next roadmap task:** Task 12 — PLANNED / NOT AUTHORIZED

This is the authoritative final-acceptance record for Task 10. It closes the
corrective review of the Engine Protocol v2 `media.*` namespace. At the time of
this record it did not authorize Task 11 or any later roadmap task; Task 11 was
subsequently authorized, independently reviewed, and accepted in
`TASK11_ACCEPTANCE.md`.

## Accepted scope

Task 10 exposes all 11 planned media methods:

- `media.getState`
- `media.play`
- `media.pause`
- `media.togglePause`
- `media.stop`
- `media.restart`
- `media.next`
- `media.previous`
- `media.getDuration`
- `media.getPosition`
- `media.setPosition`

The concrete wire contract remains `engine/MEDIA_V1.md`.

The accepted corrective design includes:

- exact source-local media-action tickets assigned atomically with libobs queue insertion;
- exact settlement by `(source handle, expected signal, action ticket)`;
- permanent media observers and condition-variable settlement rather than per-request signal connect/disconnect;
- bounded timeout/orphan tracking;
- late orphan completion that cannot settle a newer request and forces a resynchronization boundary when ownership is uncertain;
- bounded deferred media events and ordered `session.resyncRequired` publication;
- response-before-command-owned-event behavior;
- independent revision ownership for unrelated asynchronous media lifecycle/state changes;
- no revision churn for natural playback position progression;
- source-removal and shutdown coverage with outstanding/timed-out media actions.

## Exact-SHA hosted CI

All 11 required project workflow lanes completed successfully on the exact
accepted implementation SHA `6a590c2985a99d186c8eecd0241acdc824d32168`:

- Task 1
- Task 2
- Task 3
- Task 4
- Task 5
- Task 6
- Task 7
- Task 8
- Task 8 concurrency
- Task 9
- Task 10

Task-10 workflow:

- workflow: `Engine Protocol v2 Task 10 Verification`
- run ID: `33240922522`
- head SHA: `6a590c2985a99d186c8eecd0241acdc824d32168`
- result: `completed / success`
- artifact name: `obs-engine-windows-x64-task10`
- artifact ID: `9711380926`
- artifact size: `99,950,411` bytes
- GitHub artifact digest: `sha256:50b7bf7408ab44177d0aa3154504010e1d323507d7af47e132a9edcab5a381d4`

The exact-SHA Task-8 concurrency run also executed and passed the production
bridge static-ordering/isolation assertion. A later local audit wrapper failed to
extract that inline YAML block correctly; that was an audit-tooling false
negative, not a production assertion failure.

## Physical Windows acceptance

The exact Task-10 CI artifact was exercised on the physical Windows host without
rebuilding or replacing its engine/libobs binaries.

Machine context:

- Windows 11 Pro, build `26200`, x64
- AMD Ryzen 7 9700X, 8C/16T
- AMD Radeon RX 9060 XT plus AMD integrated graphics
- Hyper-V/VBS enabled on the host; hardware evidence identified the machine as the physical MSI/B650M host rather than a VM guest

Exact artifact binary hashes:

- `obs-engine.exe`: `2fab794783a0f55130e91804cecd9e5d0cafea5418cb819808195627b6ab8907`
- `obs.dll`: `3b38adea3fee3462dc1a059db7b69529224f165acaf7f1c1e4c9a77b58777daf`

The deterministic Task-10 fixture was built/staged separately into a copy of the
CI runtime. The artifact engine/libobs hashes were unchanged after testing.

Physical Task-10 result:

- 30/30 acceptance cases passed;
- integration exit code `0`;
- Task-10 stderr empty for the deterministic physical suite;
- clean engine shutdown;
- no remaining `obs-engine.exe` process;
- temporary fixture removed after testing;
- original artifact extraction remained unchanged.

Coverage included exact ticket ownership, pre-existing same-source/same-type
queued actions, timeout then follow-up action, late timed-out seek/orphan resync,
blocking callback timeout, stale guards, cross-source ownership, overflow/resync,
source removal/shutdown with outstanding actions, all transport controls,
queries, invalid/unsupported targets, response/event ordering, sequence/revision
semantics and clean shutdown.

An optional real-plugin `ffmpeg_source` smoke was inconclusive because the
inactive private source reported `processed:false` before real media activation.
It was supplementary and is not part of the deterministic acceptance gate.

## Independent final audit

A read-only final audit was performed after physical acceptance. The review
bundle contained 229 evidence files. All 228 files covered by the existing
`SHA256SUMS.txt` manifest matched their recorded checksums; the manifest itself
has no self-entry.

Review archive:

- file: `Task10-final-audit-review-6a590c298.zip`
- size: `518,622` bytes
- SHA-256: `306fc6bbbf7d6646bd2668934043cfe4ce6c6a022ee88153600faf6505a3c048`

The independent review correlated source/commit, exact corrective patch,
command transcript, local regressions, hosted CI, artifact provenance and raw
physical stdout/stderr. The physical Task-10 transcript contained 69 requests
with 69 matching responses and 55 emitted events with contiguous event sequence
`1..55`.

The audit's only reported failure was a local Task-8 concurrency static-ordering
extractor that accidentally included the next YAML job label in the extracted
PowerShell. The corresponding exact-SHA GitHub workflow step ran the real
assertion and passed. This is classified as an audit-harness false negative and
is not a product blocker.

## Known non-blocking debt / future hardening

The final raw physical review observed one mixed-wire ordering case in which a
`session.resyncRequired` event at revision 36 was emitted before a read-only
response carrying revision 35. This does not violate the current protocol
contract: event `seq` is the ordered session event sequence, responses carry the
revision associated with their point-in-time operation, and unrelated
asynchronous canonical mutations may receive their own revision.

Controller/SDK implementations must therefore not assume every response/event
revision observed on stdout is globally nondecreasing. They should keep the
maximum known engine revision and must keep a resync latch set after
`session.resyncRequired` until canonical state has actually been reconstructed;
a later-arriving older read-only response must not clear it.

If the project later wants a stronger guarantee that *all mixed stdout messages*
carry nondecreasing revisions, that is a protocol-strengthening change and must
be designed/tested explicitly, preferably with Task 42 concurrency stress rather
than retroactively changing Task-10 acceptance semantics.

Other non-blocking debt retained from review:

- retired media observers are retained until engine destruction;
- the tracked media enqueue symbol is exported from `obs.dll` while its header remains private/non-installed;
- deterministic action-ticket exhaustion coverage is not yet present;
- repeated timeout tracking can enter a conservative sticky uncertainty mode;
- source bridge private-state duplication remains a separate pre-existing cleanup item.

## Human acceptance gate

After the implementation, exact-SHA CI, physical Windows acceptance and
independent evidence review were complete, the operator explicitly authorized
closing Task 10 on 2026-08-29.

**Task 10 is accepted at `6a590c2985a99d186c8eecd0241acdc824d32168`.**

This historical Task-10 acceptance did **not** authorize Task 11. The old
unauthorized Task-11 work remains quarantined at `wip/task11-unauthorized` ->
`4c8b616ca2115970af3e1e4000b162416be32dac`. No Task-11 implementation may be
started, salvaged, merged, cherry-picked or otherwise continued from that old
branch. The separately authorized clean implementation is accepted at
`e7b34828cb9fbd55bae01f97148f1ec93a4ae015`; Task 12 remains unauthorized.

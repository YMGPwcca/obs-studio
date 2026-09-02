# Task 27 recording acceptance

Runtime-bearing Task 27 checkpoint: `c8ac896c5` (`fix(engine): restrict
recording to audited file outputs`). The preceding recording implementation is
`8225a05c1`; the path-validation refactor is `d9088c1f6`. Complexity is frozen
separately at `18a4984a1`.

## Deterministic lane

`.github/scripts/engine-protocol-v2-task27.ps1` passed on the Windows x64
RelWithDebInfo package with the CI-only `task23-encoder` and
`task27-recording` modules. It covered role assignment over an explicit Output,
path rejection/canonicalization, visible Output ownership, delegated
start/stop, pause/resume/toggle, audited split/chapter procedures, file-change
and finalization events, Output removal guards, and clean teardown. The
fixtures are `EXCLUDE_FROM_ALL`, have no install rules, and the before/after
package audits passed.

## Physical Windows lane

The production-only driver `.github/scripts/engine-protocol-v2-task27-physical.ps1`
was run locally against the same runtime-bearing source checkpoint with no
CI-only modules staged. It used the packaged `obs_x264` video encoder,
`ffmpeg_aac` audio encoder, and `mp4_output` with an explicitly created output
directory. The run passed start, active frames, pause/resume, chapter, split,
clean stop, finalization, and the no-duplicate recording-lifecycle check.

FFprobe `9.0.1` parsed both resulting files with video and audio streams,
1920x1080 video, and reasonable durations:

| File | Bytes | SHA-256 | Duration |
|---|---:|---|---:|
| `task27-physical-7195defb7f904c38afab46d66081679c.mp4` | 1,283,438 | `15413D2D103BB18654B12566D990CEC95500B540D4F278C0AAB75830C1821211` | 4.181 s |
| `task27-split-16-39-18.mp4` | 873,037 | `41A3520E30846CBBA3BE4EAB69CE68F1BF3913579A6CA54FAF68CF29BD48B355` | 2.767 s |

Physical result: **PASS**. Hosted exact-SHA Phase-3 matrix and the final
independent review remain separate gates.

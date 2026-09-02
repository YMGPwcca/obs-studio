# Task 29 replay-buffer acceptance

Runtime-bearing Task 29 checkpoint: `d5ff07702` (`feat(engine): add Protocol
v2 replay buffer`).

## Deterministic lane

`.github/scripts/engine-protocol-v2-task29.ps1` passed on the Windows x64
RelWithDebInfo package with the CI-only Task 23 encoder and Task 29 replay
fixture staged. It covered explicit Output role assignment, visible
`replayBuffer` ownership, delegated lifecycle, asynchronous save
acknowledgement, repeated-save `busy` behavior, stop/save ordering, final
`replayBuffer.saved` settlement, output removal guards, and clean teardown.
The no-path failure fixture cleared `pendingSave` without fabricating a saved
event. Fixtures are `EXCLUDE_FROM_ALL`, have no install rules, and the before
and after package audits passed.

## Physical Windows lane

`.github/scripts/engine-protocol-v2-task29-physical.ps1` passed against the
production-only package with no CI-only modules staged. It used the packaged
`replay_buffer` Output, `obs_x264` video encoder, `ffmpeg_aac` audio encoder,
and the packaged `obs-ffmpeg-mux.exe` helper. The driver buffered deterministic
video/audio, saved a replay, verified the asynchronous saved event and final
path, stopped cleanly, and checked that no replay lifecycle aliases were
emitted.

FFprobe `9.0.1` parsed the final MP4 with H.264 video, AAC audio, 1920x1080
video, and a sensible duration:

| File | Bytes | SHA-256 | Duration |
|---|---:|---|---:|
| `task29-2026-09-02-17-49-33.mp4` | 1,868,093 | `E230E2EC1B45EC56025BFDA8EE3351BBA214E682E3F468393EA22E6A0E17D475` | 6.033 s |

Physical result: **PASS**. Hosted exact-SHA Phase-3 matrix and the final
independent review remain separate gates.

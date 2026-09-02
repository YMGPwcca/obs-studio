# Task 28 streaming acceptance

Runtime-bearing Task 28 checkpoint: `34b59dd24` (`feat(engine): add Protocol
v2 streaming orchestration`). The physical driver is included in the Task 28
boundary.

## Deterministic lane

`.github/scripts/engine-protocol-v2-task28.ps1` passed on the Windows x64
RelWithDebInfo package with the CI-only Task 23 encoder, Task 25 service, and
Task 26 output fixtures staged. It covered service-backed Output validation,
credential redaction, safe service forwarding, configuration and reconnect
policy, delegated output lifecycle, dependency active-state events, output
removal guards, and clean teardown. The fixtures are `EXCLUDE_FROM_ALL`, have
no install rules, and the before/after package audits passed.

## Physical Windows lane

`.github/scripts/engine-protocol-v2-task28-physical.ps1` passed locally against
the production-only package with no CI-only modules staged. It used the real
packaged `rtmp_output`, `rtmp_custom` service, `obs_x264` video encoder, and
`ffmpeg_aac` audio encoder, with a local FFmpeg RTMP receiver. The stream key
was a fresh per-run sentinel. The driver verified output-owned starting,
started, reconnecting, reconnected, and stopped events, dependency active
state changes, a connected `streaming.getState`, clean teardown, and that the
sentinel did not appear in Engine responses, events, or stderr. No
`streaming.started/stopped/reconnecting/reconnected` aliases were emitted.

FFprobe parsed the two captures from the final run with H.264 video and AAC
audio streams:

| File | Bytes | SHA-256 | Duration |
|---|---:|---|---:|
| `task28-loopback-first-ca373cb27ed643698dd12f783ef2ac9c.flv` | 262,144 | `3B772A0D7AAB12B2B39A51644C635505A153254B7F748E9C20ED76E66FC6D524` | 12.000 s |
| `task28-loopback-reconnect-e68c3e87b6c74993b62988fafcd4635e.flv` | 290,999 | `3C7A12BDBB2104D9B8355E6820121C684D911912565FB18226BD6B8982B8EA78` | 2.049 s |

Physical result: **PASS**. Hosted exact-SHA Phase-3 matrix and the final
independent review remain separate gates.

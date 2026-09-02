# Recording namespace v1

Recording is a single convenience role over an explicitly created, visible
file-compatible `output.*` object. It owns no duplicate lifecycle state and
does not create encoders. The assigned Output remains visible through
`output.list` and `output.get` and retains the normal `output.*` handle.

## Methods

* `recording.getConfig` returns `{configured, output}`.
* `recording.configure` requires an existing Output handle and accepts optional
  `path`, `overwrite`, and `createDirectory` fields.
* `recording.unconfigure` clears only the role assignment and requires the
  Output to be inactive.
* `recording.start`, `recording.stop`, and `recording.forceStop` delegate to
  the underlying Output lifecycle.
* `recording.pause`, `recording.resume`, and `recording.togglePause` delegate
  to Output pause support.
* `recording.splitFile` and `recording.addChapter` call only known audited
  procedures on `mp4_output`, `mov_output`, `ffmpeg_muxer`, or the CI-only
  deterministic recording fixture. Arbitrary proc-handler names are never
  accepted.
* `recording.getState` aggregates Output state with `configured`,
  `currentPath`, and `lastFile`.
* `recording.getStats` is the underlying read-only Output stats snapshot.
* `recording.getCurrentPath` and `recording.getLastFile` return nullable paths.

## Path policy

The Controller is trusted to choose a destination within the current user's OS
permissions. The Engine still rejects malformed/non-UTF-8 or embedded-NUL
paths, URLs, relative paths, missing extensions, unsupported Windows device
namespaces, unavailable parents, and paths over the bounded limit. Directory
creation is performed only when `createDirectory: true`; overwrite is applied
only when explicitly supplied. The Engine does not claim that a file exists
merely because the request path was accepted.

## Events

The role emits `recording.configChanged` for assignment changes,
`recording.fileChanged` for an observed Output file-change signal,
`recording.fileFinalized` for an observed final Output stop with a known path,
and `recording.chapterAdded` when an audited chapter procedure accepts a
chapter request. Output lifecycle events remain canonical:
`output.starting`, `output.started`, `output.stopping`, `output.stopped`,
`output.paused`, `output.reconnecting`, `output.reconnected`, and
`output.error`.

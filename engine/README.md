# obs-engine

`obs-engine` is a deliberately small, headless Windows host for `libobs`. It is intended to run as a separate GPL process controlled by another application over redirected standard input/output.

## Security boundary

The v1 host deliberately keeps the control surface narrow:

- No TCP, HTTP, WebSocket, named-pipe listener, or remote authentication surface.
- Protocol input is newline-delimited JSON on `stdin`; protocol output is JSON on `stdout`; logs go to `stderr`.
- A request line is limited to 256 KiB.
- OBS modules are loaded with a non-empty `obs_add_safe_module` allowlist. `win-capture` is the only module enabled by default.
- The process working directory is pinned to the engine executable directory before `obs_startup`, so libobs's relative runtime paths resolve against the packaged runtime.
- Windows DLL search is restricted to the application directory, System32, explicit user DLL directories, and a loaded module's own directory.
- Source, scene, and scene-item pointers never cross the process boundary. The protocol exposes opaque integer handles only.
- Sources and scenes are created as private libobs objects and are owned entirely by the host.
- Scene items are explicitly reference-counted by the host before their handles are exposed.
- Display/Window Capture mode disables `win-capture`'s compatibility updater and Game Capture hook initialization by default. `--enable-game-capture` opts back into upstream Game Capture behavior.

The controller should launch the process itself with redirected standard handles. Do not expose the engine's standard input/output through an unauthenticated network bridge.

## Current scope

The v1 host initializes D3D11 video and provides commands for:

- engine/version discovery;
- input source type discovery;
- source defaults, creation, settings update/readback, and destruction;
- private scene creation/destruction;
- adding/removing scene items;
- position, scale, rotation, and alignment updates;
- selecting/clearing the program scene;
- graceful shutdown.

Audio initialization, recording/streaming outputs, source-property schema translation, and preview transport are intentionally not part of v1 yet.

## Command line

```text
obs-engine [--width=N] [--height=N] [--fps=N] [--locale=NAME]
           [--plugin=NAME ...] [--enable-game-capture]
```

Defaults are `1920x1080 @ 60 FPS`, locale `en-US`, and only `win-capture` allowlisted.

## Protocol

Each request and response occupies exactly one UTF-8 JSON line.

Startup event:

```json
{"event":"ready","protocol":1,"libobs_version":"...","pid":1234,"width":1920,"height":1080,"fps":60,"game_capture_enabled":false}
```

Handshake:

```json
{"id":1,"cmd":"hello"}
```

List input source types:

```json
{"id":2,"cmd":"source.types"}
```

Create a Display Capture source using the source type reported by `source.types`:

```json
{"id":3,"cmd":"source.create","type":"monitor_capture","name":"display"}
```

On current Windows builds `win-capture` may expose the display source under a version/platform-specific identifier. The controller must use an identifier returned by `source.types` rather than hard-coding this example.

Create a scene and attach a source:

```json
{"id":4,"cmd":"scene.create","name":"program"}
{"id":5,"cmd":"scene.add","scene":2,"source":1}
{"id":6,"cmd":"program.set","scene":2}
```

The numeric values above are examples only; use handles returned by the engine.

Update an item transform:

```json
{"id":7,"cmd":"item.transform","item":3,"x":100,"y":80,"scale_x":0.75,"scale_y":0.75,"rotation":0}
```

Shutdown:

```json
{"id":8,"cmd":"shutdown"}
```

Errors are structured and do not put libobs pointers or internal exception text on the protocol stream:

```json
{"id":8,"ok":false,"error":"bad_request","message":"invalid source handle"}
```

# Windows Minimal libobs Runtime

This branch keeps upstream `master` clean and turns the build into a Windows-only, headless libobs runtime intended to be controlled out-of-process.

## Built core

- `libobs`
- `libobs-d3d11`
- `libobs-winrt`
- Windows-capable OBS runtime plugins
- `obs-engine`, the minimal headless host

The OBS Studio Qt frontend, tests, OpenGL backend, and non-Windows platform plugins are not part of the default build.

## Capture plugins

The important Windows capture modules are kept and built:

- `win-capture` — display, window, and game capture implementation
- `win-dshow` — DirectShow capture devices/webcams
- `win-wasapi` — Windows audio capture

`obs-engine` allowlists only `win-capture` by default. It sets `win-capture` to capture-only mode, which keeps Display Capture and Window Capture available while suppressing the compatibility updater and Game Capture hook initialization. Launching `obs-engine --enable-game-capture` explicitly opts back into the upstream Game Capture initialization behavior.

Other Windows-capable runtime modules are retained in the build but are not loaded by `obs-engine` unless they are explicitly added to its safe-module allowlist with `--plugin=NAME`.

## Heavy optional modules

Two modules are opt-in at build time:

```text
-DENABLE_BROWSER_SOURCE=ON
-DENABLE_WEBSOCKET=ON
```

`obs-browser` is disabled by default because of the large CEF dependency. `obs-websocket` is disabled because the intended controller should use a purpose-built IPC boundary instead of OBS Studio's remote-control protocol.

## Intended architecture

```text
Proprietary controller
        |
        | redirected stdin/stdout (JSON lines in protocol v1)
        v
Open-source obs-engine process
        |
        v
libobs + allowlisted OBS plugins
```

The host process remains deliberately thin: initialize libobs/D3D11, load allowlisted modules, map opaque controller handles to private libobs objects, expose a small control protocol, and own the runtime scene graph. Project persistence, editing state, undo/redo, automation, and product-specific behavior belong in the controller.

Protocol v1 intentionally has no network listener and no preview transport yet. See `engine/README.md` for the exact current boundary and commands.

## Upstream strategy

Do not make product changes on `master`. Keep `master` tracking `obsproject/obs-studio` and maintain this specialization on `windows-minimal` so upstream updates remain straightforward to merge or rebase.

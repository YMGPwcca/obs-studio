# Windows Minimal libobs Runtime

This branch keeps upstream `master` clean and turns the build into a Windows-only, headless libobs runtime intended to be controlled out-of-process.

## Built core

- `libobs`
- `libobs-d3d11`
- `libobs-winrt`
- the lightweight `obs-frontend-api` ABI library required by a small number of otherwise headless Windows modules
- Windows-capable OBS runtime plugins
- `obs-engine`, the minimal headless host

The OBS Studio Qt frontend/application, tests, OpenGL backend, and non-Windows platform plugins are not part of the default build. `obs-frontend-api` is built on its own; it does not pull the Qt frontend back into the runtime.

## Capture plugins

The important Windows capture modules are kept and built:

- `win-capture` — display, window, and game capture implementation
- `win-dshow` — DirectShow capture devices/webcams
- `win-wasapi` — Windows audio capture

`obs-engine` uses an explicit non-empty safe-module allowlist containing every Windows runtime module built or optionally buildable by this branch. Missing optional/default-allowlisted modules do not make startup fail. `win-capture` remains the required baseline module, while `--plugin=NAME` adds and requires an additional module for that launch.

`win-capture` still starts in capture-only mode by default, which keeps Display Capture and Window Capture available while suppressing the compatibility updater and Game Capture hook initialization. Launching `obs-engine --enable-game-capture` explicitly opts back into the upstream Game Capture initialization behavior.

## Heavy optional modules

Two modules are opt-in at build time:

```text
-DENABLE_BROWSER_SOURCE=ON
-DENABLE_WEBSOCKET=ON
```

Their module names are already present in the host's default safe-module allowlist, so enabling either build option does not require an additional runtime `--plugin` argument.

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

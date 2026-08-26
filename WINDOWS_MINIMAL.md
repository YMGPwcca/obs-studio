# Windows Minimal libobs Runtime

This branch keeps upstream `master` clean and turns the build into a Windows-only, headless libobs runtime intended to be controlled out-of-process.

## Built core

- `libobs`
- `libobs-d3d11`
- `libobs-winrt`
- Windows-capable OBS runtime plugins

The OBS Studio Qt frontend, tests, OpenGL backend, and non-Windows platform plugins are not part of the default build.

## Capture plugins

The important Windows capture modules are kept and built:

- `win-capture` — display, window, and game capture
- `win-dshow` — DirectShow capture devices/webcams
- `win-wasapi` — Windows audio capture

Other Windows-capable runtime modules are also retained in the build, including FFmpeg/media, filters, outputs/encoders, text, transitions, hardware capture modules, and related source/output modules.

## Heavy optional modules

Two modules are opt-in by default:

```text
-DENABLE_BROWSER_SOURCE=ON
-DENABLE_WEBSOCKET=ON
```

`obs-browser` is disabled by default because of the large CEF dependency. `obs-websocket` is disabled because the intended controller should use a purpose-built IPC boundary instead of OBS Studio's remote-control protocol.

## Intended architecture

```text
Proprietary controller
        |
        | versioned IPC
        v
Open-source host process
        |
        v
libobs + selected OBS plugins
```

The host process should remain thin: initialize libobs, load modules, map opaque controller handles to libobs objects, expose a small control protocol, and provide preview/output surfaces. Project persistence, editing state, undo/redo, automation, and product-specific behavior belong in the controller.

## Upstream strategy

Do not make product changes on `master`. Keep `master` tracking `obsproject/obs-studio` and maintain this specialization on `windows-minimal` so upstream updates remain straightforward to merge or rebase.

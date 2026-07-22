# Alpha GUI Direction

## Product posture

The GUI is a dense professional routing console, not a marketing shell and not
an audio engine. It runs in a separate process and communicates with the engine
only through the versioned control wire protocol. Closing, hanging, or updating
the GUI must not interrupt audio.

The first useful screen is the routing workspace. It exposes engine state,
sample rate, block size, XRUN state, the route matrix, route gain, endpoint
inventory, and live diagnostics without opening secondary windows.

## Technology

- Qt 6.8 or newer, using Qt Quick and QML for layout and interaction.
- C++20 control client using the existing named-pipe transaction and control
  wire v4 codecs.
- Qt Concurrent for control transactions so pipe timeouts never block the UI
  thread.
- Qt Quick scene graph on the platform-default RHI backend. Windows uses the
  Direct3D backend and can fall back to software rendering on machines without
  a usable GPU.
- The route matrix starts as QML delegates. When profiling shows that matrix
  size requires it, only the cell field moves to a custom `QQuickItem`; the
  application model and interaction contract stay unchanged.

The GUI build is opt-in with `SAR_BUILD_GUI=ON`. A machine without Qt can build
and test the engine exactly as before.

## Process boundary

```text
Qt Quick views
    | queued properties and commands
EngineController (GUI process)
    | control wire v4 over named pipe
sar_engine_service
    | lock-free publication
realtime graph and platform workers
```

The GUI polls lightweight diagnostics at 250 ms, runtime state at 1 second, and
the complete session at 5 seconds. Mutations are serialized and followed by a
session refresh. No GUI object, Qt type, allocation policy, or render callback
crosses into the realtime path.

## Interaction model

- A matrix cell is the primary route command. Clicking selects the route and
  toggles its connection.
- The inspector edits the selected route gain without obscuring the matrix.
- Engine start and stop remain visible in the global header.
- Device and diagnostics views stay in the same window so repeated routing work
  does not create modal-dialog churn.
- Colors communicate state: green is healthy/connected, cyan is selection,
  amber is degraded, and red is reserved for drops, clipping, or failure.
- Geometry is compact and stable: 44 px matrix cells, 42 px navigation rows,
  34 px command buttons, 3-4 px radii, and one-pixel separators.

## Delivery order

1. Live session, matrix, route gain, devices, diagnostics, engine start/stop.
2. Preset open/save, device configuration, reconnect backoff, and command
   acknowledgement feedback.
3. Virtual endpoint creation, matrix keyboard editing, route groups, and
   scalable custom matrix rendering.
4. Accessibility pass, localization, packaging, GPU/software-renderer visual
   regression screenshots, and long-running GUI disconnect testing.

## Build

```bat
cmake -S . -B build-gui -DSAR_BUILD_GUI=ON -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build-gui --target sar_gui
```

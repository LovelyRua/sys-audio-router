# System Audio Route

System Audio Route (SAR) is an open-source, Windows-first professional audio
routing system. It combines a realtime-safe routing matrix, multiple physical
WASAPI endpoints, a low-latency Virtual ASIO bridge, persistent presets,
diagnostics, and a native Qt Quick control application.

The goal is a dependable system-wide audio matrix for DAWs, hardware interfaces,
desktop applications, streaming tools, and future virtual WDM/WASAPI devices.
The portable realtime and graph layers remain platform-neutral; Windows audio is
the first production backend.

> **Alpha status:** real WASAPI render/capture and Virtual ASIO audio paths are
> working on Windows hardware. This is not yet a finished daily-driver release.
> Read [Current limits](#current-limits) before installing it on a production rig.

## What Works Today

- Event-driven WASAPI render, capture, duplex, and multi-endpoint matrix runtime.
- Multiple independently clocked WASAPI devices with queues, rate matching, and
  drift diagnostics.
- Virtual ASIO driver registration and DAW input/output transport.
- Channel routing matrix with per-route enable, mute, and gain.
- Multiple configurable Virtual ASIO device definitions and channel counts.
- Native Qt Quick UI for devices, routing, presets, meters, and diagnostics.
- Bootstrap launcher for starting the engine service and control application.
- NSIS installer and portable ZIP with deployed Qt/QML and MSVC runtimes.
- Transactional install, update, uninstall, and ASIO registration ownership.
- Realtime counters for xruns, dropped blocks, FIFO state, callback time,
  endpoint clocks, recovery, discontinuities, and underflow/overflow conditions,
  summarized as a plain-language status on the Diagnostics page.
- A background engine that keeps routing after the control panel closes, with an
  explicit "Stop engine" choice, optional start at login, a rotating engine log,
  and crash minidumps (see the [User Guide](docs/user-guide.md)).

The tested signal paths include:

```text
DAW -> SAR Virtual ASIO -> matrix -> WASAPI hardware/virtual output
WASAPI capture -> matrix -> SAR Virtual ASIO -> DAW
multiple WASAPI capture/render endpoints -> one clocked matrix
```

## Current Limits

- Physical ASIO is a **preview**. Its integration into the unified routing
  matrix is being staged and has only been exercised on limited hardware.
- Physical ASIO currently requires the complete native channel set in order.
- Virtual WDM/WASAPI endpoint creation is still under development, so ordinary
  applications (browsers, OBS, Discord) cannot yet send audio to SAR without a
  third-party virtual cable.
- The Virtual ASIO driver only accepts the engine's sample rate; a DAW project
  at a different rate cannot open the device until the engine is reconfigured.
- The driver is registered for the current Windows user only and is x64 only:
  32-bit DAWs cannot see it.
- Installer, application, and driver binaries are unsigned alpha artifacts, so
  Windows SmartScreen will warn on first run.
- Hardware and DAW compatibility coverage is still limited; do not assume that
  one successful interface represents every vendor driver.

Failed Physical ASIO configuration is transactional and does not delete the
saved WASAPI matrix. The runtime milestone in progress is making one Physical
ASIO driver the graph clock master while WASAPI endpoints run as rate-matched
followers.

## Install The Alpha

Download the latest `Windows GUI Package` artifact from GitHub Actions. The
standard path is the `.exe` installer:

1. Close DAWs currently using the SAR Virtual ASIO driver.
2. Run `SystemAudioRoute-0.1.0-windows-x64.exe`.
3. Start **System Audio Route** from the Start menu or desktop shortcut.
4. Select or add endpoints on **Audio devices**, apply the runtime, then connect
   channels on **Routing matrix**.
5. Check **Diagnostics** before trusting a session: XRUN and dropped-block
   counters should remain at zero during steady playback.

The portable ZIP includes `install-alpha.cmd` for an isolated current-user
installation. Full packaging and acceptance details are in
[Windows Alpha Packages](docs/alpha-package.md). Day-to-day use, logs, and
troubleshooting are covered in the [User Guide](docs/user-guide.md).

## Build And Test

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 C++ build tools
- CMake 3.24 or newer
- Qt 6.8.x with MSVC 2022 x64 for the GUI
- NSIS 3.03 or newer when producing the installer

Core build:

```bat
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Build the installer and portable ZIP:

```bat
scripts\windows-alpha-package.cmd
```

Packages are written to `build-alpha\package-output`. GitHub Actions also builds
and acceptance-tests both formats on every push to `main`.

For the shared Windows test machine, always use a unique slot and pass the
password through `SAR_TEST_PASSWORD` rather than on the command line:

```bat
scripts\windows-winrm-test.cmd <host> <user> <password> engineer-a
scripts\windows-winrm-local-test.cmd <host> <user> <password> engineer-a
```

The first command tests remote `main`; the second archives local `HEAD`. Neither
command includes uncommitted changes.

## Architecture

```text
Qt control UI
    | named-pipe control protocol
Engine control service
    | immutable graph/preset publication
Realtime matrix graph
    |-- Virtual ASIO transport
    |-- WASAPI clock master
    |-- rate-matched WASAPI followers
    `-- Physical ASIO matrix adapter (in progress)
```

Realtime code does not allocate, lock, log, perform file I/O, or activate COM.
Device discovery, graph construction, presets, driver registration, and recovery
decisions remain on the control side. Each hardware clock boundary is explicit
and observable.

Important directories:

- `core/realtime` - fixed audio buffers and realtime primitives.
- `core/graph` - graph execution, snapshots, and route matrix processing.
- `core/control` - commands, wire protocol, sessions, presets, and validation.
- `core/platform` - WASAPI/ASIO integration and realtime transports.
- `core/service` - runtime composition, recovery, and endpoint coordination.
- `app/gui` - Qt Quick control application.
- `driver` - Virtual ASIO driver and registration support.
- `tests/realtime` - deterministic core and Windows platform smoke tests.
- `packaging/windows` - installer and portable package implementation.

## Road To A Usable Alpha

1. Move Physical ASIO from an exclusive runtime into the unified matrix.
2. Finish mixed ASIO/WASAPI clock-master and follower recovery behavior.
3. Complete multi-Virtual-ASIO GUI workflows and DAW compatibility coverage.
4. Land the first Virtual WDM/WASAPI endpoint spike and make the driver decision.
5. Run longer hardware, hot-plug, install/update, and multi-DAW acceptance gates.

See [Current System Architecture](docs/architecture/current-system.md),
[Roadmap](docs/roadmap.md), [Product Readiness](docs/product-readiness.md), and
[Development](docs/development.md) for detailed engineering state and
validation procedures. Contributions are welcome; see
[CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md).

## Project Principles

1. Audio stability comes before feature count.
2. The realtime path must remain measurable and deterministic.
3. Configuration failures must preserve the last working session.
4. Cross-platform boundaries are designed now; Windows quality comes first.
5. Features are not called complete until real hardware and DAW workflows pass.

## License

System Audio Route is licensed under GNU GPL version 3. The Windows driver ABI
uses selected unmodified headers from Steinberg ASIO SDK 2.3.4 under its GPLv3
option. See [NOTICE.md](NOTICE.md) and the vendored SDK license for attribution.

# Native runtime editor

The optional VSTGUI preview now has an Audio devices window using the same
`QueryAudioRuntime`, `ListDevices`, `ConfigureAudioRuntime` and start/stop
commands as the existing Qt control panel. It does not create a second session
file or bypass engine-side validation. The engine must already be running;
the default bootstrap entry still uses the Qt control panel. Packages built with
`SAR_PACKAGE_NATIVE_PREVIEW=ON` add a separate native entry through the same
launcher (`--native`), which starts the engine when needed.

## Scope

- A matrix draft can contain up to the engine limit of 32 endpoints.
- Capture and playback bindings use actual enumerated WASAPI IDs, not names.
- Endpoint IDs remain graph-route identities; channel offsets are zero-based.
- Numeric fields are parsed strictly and checked against reported capabilities.
- Exactly one playback endpoint must be the clock master; capture cannot be.
- Duplicate device/direction bindings fail before applying the configuration.
- Existing physical ASIO matrix endpoints are retained unchanged and read-only.
  In particular their group, channel and timing fields survive WASAPI additions.
  A physical ASIO render remains the master, as required by the current engine.
- Legacy non-matrix and standalone ASIO modes are preserved read-only. New matrix
  explicitly creates a replacement draft; it is not applied automatically.

Refresh reconnects and updates device inventory without discarding a dirty draft.
Reload saved is a separate, confirmed discard operation. Failed Apply preserves
the draft, refreshes actual runtime state when possible, and never retries an
uncertain mutation automatically. Start/stop is disabled while edits are dirty.
The regular engine runtime reconfiguration behavior, including stream restart,
still applies when the user selects Apply.

All pipe requests run in value-owning background workers. UI timers only collect
completed results; closing a window does not wait for a pipe request. Command
IDs are distinct across simultaneous windows and repeated window lifetimes,
and responses must echo the requesting command ID.

## Window fit

The preview retains fixed logical control dimensions inside scroll viewports.
On creation, the client viewport is limited to the current monitor's work area
with DPI-aware window chrome. Small screens scroll rather than shrink text or
hide the title bar. Live re-fitting after monitor/work-area changes is not yet
implemented. The INF picker now distinguishes cancellation from Windows dialog
errors; failed selection does not erase the typed path.

## Verification

`gui_runtime_device_model_smoke` exercises draft validation, wire round trips,
device disappearance, channel ranges, clock selection, endpoint capacity,
physical ASIO field preservation, and 4096 command IDs across concurrent clients.
The bounded GUI startup check opens all three native windows and closes them.
These checks do not replace interactive device selection, Apply, start/stop,
dirty-refresh/reconnect, or the existing waveform/continuity acceptance tests.

## Interactive acceptance (2026-10-20)

VM24 used a private engine session and the non-elevated interactive codex user.
The engine binaries came from product revision `03c2fec`; this batch changes no
engine or audio-path implementation. The native client was first exercised at
`27fa10a` and the final regression pass used `f8c1c5c`. Existing experimental
adapters were selected, not installed, renamed or removed during this round.

| Flow | Observed result |
| --- | --- |
| Invalid channel count 0, Apply | Local rejection; independent runtime query confirmed no runtime installed |
| Configure two playback and two capture endpoints | Actual A/B adapter devices selected; four endpoints applied successfully |
| Refresh a dirty draft | Endpoint IDs, bindings and numeric edits retained |
| Disconnect and reconnect while dirty | Explicit transport error, followed by recovery without discarding edits |
| Apply and reopen Audio devices | Four saved endpoints read back, with render-1 master and capture-2 bound to adapter B |
| Start and stop audio | GUI state and independent control CLI agreed; the engine service stayed alive |
| Reload saved, cancel | Five-endpoint dirty draft retained |
| Reload saved, confirm | Saved four-endpoint configuration restored |
| Close and reopen editor | No cross-window command-cache collision |
| Main-window transport recovery | Offline pipe error cleared automatically after the private engine restarted |
| Small 870 x 659 console | Title bars and close controls visible; fixed-size content scrollable with muted scrollbars |
| Close native GUI | Independent CLI still responded; engine then stopped explicitly with --stop |

CI run `39354135549` for `f8c1c5c` passed 162 CTest cases, 15 script suites,
seven advisory ASan cases, the two native model/parser checks, and the bounded
three-window startup/closure check. Windows package run `39354140497` also passed.
Temporary GUI/engine processes, scheduled tasks and process-ledger entries were
closed or cleared after acceptance. No development-host tools were installed.

This proves configuration and lifecycle behavior, not a new sound-content or
waveform-continuity pass: the private test matrix contained no signal routes.
The native preview still lacks physical-ASIO editing, experimental-driver
installer integration, presets, full diagnostics and localization. It is not yet a
replacement for the shipped Qt control panel.

## Topology changes in the routing matrix

The native matrix now clamps both paging axes when it receives an authoritative
topology with fewer channels. A formerly valid last page cannot strand the user
on an empty grid after applying a smaller runtime configuration. Temporary
transport failures preserve the page position and disable navigation; a healthy
snapshot reconciles it. The portable model smoke covers empty, partial and exact
page boundaries and repeated topology shrink across 0-256 channels.

The main-window runtime button says Start audio / Stop audio: these commands
control audio streaming, not the lifetime of the background engine process.

Revision `b928502` passed CI run `39377647617`: 162 CTest cases, 15 script
suites, seven advisory ASan cases, the native model/parser checks, and native
three-window startup/closure. Windows package run `39377651538` passed too.
This follow-up was verified in CI; no new VM24 audio-content test is claimed.

## Optional installer payload

`SAR_PACKAGE_NATIVE_PREVIEW=ON` requires both GUI build options. It installs the
native EXE and its third-party licenses alongside the Qt panel. The default
shortcut is unchanged; the additional Native Preview shortcut invokes
`SystemAudioRouteLauncher.exe --native`. The ZIP installation retains the
licenses directory and `start-native-preview.cmd` as well.

Package run `39382435628` for branch revision `17c86ea` (PR merge build
`515dc64df25a4e29f5208864b84bed781ac6246f`) passed 166 CTest cases, ZIP
install/update/uninstall acceptance and EXE installer acceptance. Both package
paths launched the installed native executable, retained the existing engine PID,
and left the engine alive after disposing the native test process. The native
probe uses a force-close of its exact owned process; it does not establish
graceful native-window closure, covered separately by the startup gate.

No experimental kernel driver or manager is included, and neither installer
changes boot security settings. This is native-preview delivery, not complete
virtual-WASAPI-driver delivery or a new VM24 sound-content acceptance pass.

# Native virtual WASAPI management preview

The VSTGUI preview has a separate **Virtual WASAPI** management window. This is
an experimental driver-lab entry point, not a shipping driver installer. The Qt
control panel and regular installer do not acquire this feature implicitly.

## Package boundary

Place the independently built `sar_endpoint_manager.exe` under
`driver-lab/` beside `SystemAudioRouteVstgui.exe`. The GUI never searches PATH or
the current directory for a privileged helper. No MS-PL driver implementation,
private header, or shared-memory layout is linked into the GPL control panel.
The manager remains an independently executed program with JSON inventory and
command-line mutation requests. The preview artifact does not redistribute a
driver package or sign one on the user's machine.

The manager serializes add/rename/remove across processes using a machine-wide
named mutex. A competing operation is rejected, not queued for later execution
against a possibly changed selection. Read-only inventory remains available.

The driver currently supports at most four adapters, each publishing two fixed
stereo bus pairs at 48 kHz. The window does not offer arbitrary channel counts.
Renaming changes the PnP adapter label; Windows endpoint display names may still
come from the INF. `PnP started` is not a claim of audio readiness or good sound.

## User flow

1. Open Virtual WASAPI from the preview's routing matrix toolbar.
2. Refresh to read the actual installed adapter inventory. A missing helper or
   malformed/oversized response disables mutations instead of showing fake rows.
3. Choose an existing signed experimental INF and enter an adapter label, then
   select Add adapter. Windows requests administrator approval for the helper
   only; the GUI and audio engine do not need to run elevated.
4. Select an adapter to inspect its stable PnP ID and status. Rename uses the
   label field. Remove asks for confirmation and affects both of that adapter's
   stereo bus pairs, not the shared driver package.
5. Inspect the refreshed inventory after every operation. A cancelled UAC prompt
   is distinct from failure; an uncertain/timeout result is not success and does
   not justify automatically repeating a mutation.

All helper waits run off the UI thread. Inventory has a 15-second deadline and a
1 MiB output bound. After the Windows elevation prompt completes, mutation waits
are capped at 90 seconds, but a privileged PnP
operation is never force-killed to satisfy a timeout. Closing the management
window does not cancel an already approved device operation. The audio runtime
is neither restarted nor implicitly rerouted by this window; removing an endpoint
currently used by an audio application can interrupt that application's stream.

## Verification scope

The Windows smoke exercises structured JSON parsing and invalid inputs without
installing or removing a driver. CI compilation is necessary but does not replace
interactive UAC, add/rename/remove, helper-missing, and cancelled-operation testing
on VM24. Record those results separately from the existing matrix sound tests.

CI also launches the preview, opens its management window, and closes both
windows after five seconds. The process must exit successfully within 15 seconds;
the report and diagnostic symbols are retained even when the check fails. This
does not exercise UAC or mutate PnP devices.

The startup gate exposed two lifecycle defects during development: a null
standalone window controller during menu construction, and incorrect child-view
reference ownership on closure. VSTGUI 4.9 containers consume an initial child
reference in addition to their internal shared reference; `view_ownership.h`
adapts `makeOwned` views to that contract. Window-owned polling timers stop on
closure, and outstanding background requests do not capture UI objects.

## Interactive acceptance (2026-10-20)

VM24 ran product revision `03c2fec` as the non-elevated interactive `codex`
user. Product CI run `39258301185` passed 161 CTest cases, 15 script suites,
seven advisory ASan cases, and the bounded two-window startup/close check.
Driver manager revision `93ecef1` passed its 21 CTest cases and script suites in
run `39348123078`. No host build tools or software were installed.

| Flow | Observed result |
| --- | --- |
| Helper absent | Explicit unavailable message; mutation controls disabled |
| Helper installed, Refresh | Actual existing adapter IDs `0001` and `0002` displayed |
| Invalid relative INF | Rejected locally, inventory and typed label preserved |
| Cancel Add UAC | Neutral cancellation message; no adapter created |
| Approve Add | New `ROOT\MEDIA\0003`, label `sar-gui-qa-20261020`, started with no PnP problem |
| Rename selected `0003` | Readback label changed to `sar-gui-renamed-20261020`; stable ID unchanged |
| Cancel Remove confirmation | Adapter retained; no mutation launched |
| Approve Remove | With updated manager, `0003` removed and inventory returned to the original two adapters |

The first removal attempt with manager `c6a2bee` failed closed: an unrelated,
historical sample endpoint in `NOTPRESENT` state had no present parent devnode.
The old removal gate treated that historical entry as unresolved live ownership.
The fix ignores only the exact `NOTPRESENT` state; active, disabled, unplugged,
unknown and mixed states still require ownership checks. After the GUI retry,
all four temporary endpoints were absent, and all eight original adapter
endpoints remained active. The shared driver package was not uninstalled.

These checks establish adapter-management behavior, not a newly repeated audio
content/continuity pass or shipping driver readiness. The native INF picker was
not accepted in this round; typed absolute INF paths were used. Small-display
window placement, general runtime configuration, and installer integration
remain open. The regular installer still ships the Qt control panel.

### Follow-up picker and window acceptance

The next native GUI round (`27fa10a`, followed by layout regression at
`f8c1c5c`) exercised the real Windows Open dialog on VM24. Selecting an existing
absolute Windows INF path populated the field successfully. Reopening the
picker and cancelling preserved that path. Windows rejected a forward-slash
filename entered into its classic file dialog; selecting a normal backslash
path succeeded. No driver mutation was requested in this follow-up.

The 870 x 659 console retained visible title bars and close buttons, with fixed
logical content in themed scroll viewports. General multi-WASAPI configuration
is now available in a separate native Audio devices window; see
`native-runtime-editor.md` for its independent runtime readback and acceptance.
Installer integration and live monitor/work-area re-fitting remain open.

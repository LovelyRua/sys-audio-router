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
1 MiB output bound. Mutation waits are capped at 90 seconds, but a privileged PnP
operation is never force-killed to satisfy a timeout. Closing the management
window does not cancel an already approved device operation. The audio runtime
is neither restarted nor implicitly rerouted by this window; removing an endpoint
currently used by an audio application can interrupt that application's stream.

## Verification scope

The Windows smoke exercises structured JSON parsing and invalid inputs without
installing or removing a driver. CI compilation is necessary but does not replace
interactive UAC, add/rename/remove, helper-missing, and cancelled-operation testing
on VM24. Record those results separately from the existing matrix sound tests.

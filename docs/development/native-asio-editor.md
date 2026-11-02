# Native Virtual ASIO editor

The optional native preview exposes Virtual ASIO in the workspace navigation.
It edits the existing per-user device topology, not experimental WASAPI kernel
adapters. New devices receive a generated CLSID, device ID and broker token;
editing a name, channel count or enabled state never regenerates those identities.

The editor supports up to sixteen devices and 1-64 DAW input/output channels per
device. At least one device must remain enabled, and enabled input/output totals
must be equal. Validation is shared with the session model. The last device
cannot be removed from the draft. Removing another device or applying a topology
requires confirmation; channel removal can prune affected routes.

## Draft and transport behaviour

- Refresh reads state without discarding dirty fields or closing the device menu.
- Reload saved explicitly confirms discarding a dirty draft.
  Editing is locked during that explicit discard/read so new input cannot be lost.
- An externally changed topology blocks Apply until saved state is reloaded.
- Apply is disabled while audio runs, clients are active or telemetry is unknown.
  The engine independently enforces its authoritative runtime/client guards.
- Blocking pipe requests run on value-owning workers. Routine read requests do
  not disable name/channel editing; only mutation and reloading do.
- An uncertain reply preserves the draft and never causes an automatic resend.
- Accepted changes remain in Reloading state until a separate readback matches.
  Failure to verify within thirty seconds is an explicit error, not success.

## Compatible service reload

`ReloadVirtualAsioDevices` is appended to the command enum and accepted only on
wire version 14 or later. Its payload is the existing topology vector; no binary
field layout changes. Old engines reject the unknown enum before mutation.
`ConfigureVirtualAsioDevices` retains its previous save-and-exit behaviour for Qt
and other existing clients.

The new command validates and persists the same session, then tears down the
stopped runtime, ASIO hosts, pipes, mappings and session lock before opening
the saved configuration again in the same process. Its PID, user identity, pipe
name, log stream, instance mutex, stop event and session arguments are retained. An external stop still
takes precedence. One-shot engines and explicit `--asio-channels` startup profiles
reject this operation instead of reapplying conflicting startup parameters.

Registration synchronization first removes owned old keys, then registers the
desired topology. This also clears renamed discovery entries while retaining
CLSID identity. Rollback clears candidate entries before restoring old names, so
name swaps do not delete newly restored entries. This uses current-user registry
scope only; it does not install a kernel driver or change boot security.

## Verification scope

The native model smoke covers strict numeric parsing, sixteen-device capacity,
stable identity, duplicate/invalid names, disabled devices, aggregate balance and
wire round trips. The client smoke checks acknowledged and rejected operations.
The real service integration runs two consecutive in-process reloads, checks
the active graph shape and PID, verifies rename discovery keys, and finally
confirms the normal stop helper still works. A separate case retains legacy
save-and-exit coverage. Graph width includes non-ASIO channels;
it is not the same quantity as the sum of ASIO channels.

Once a topology is accepted, the retiring server generation rejects both reads
and mutations under its request mutex. This prevents queued old-graph edits from
overwriting the resized saved matrix and prevents premature successful readback.

CI runs these model/client/service checks before compiling the full native GUI,
whose startup gate now opens and closes six windows. These checks do not prove
audio-content continuity or full native UI parity with Qt. Physical ASIO editing,
localization, accessibility and complete installer-flow parity remain separate.

## Interactive acceptance (2026-10-30)

VM24 (`SAR-WIN11-LAB`) ran matching GUI, service and ASIO DLL binaries from
`d083672` in its non-elevated interactive user session. The fixture used a
private session file and temporary directories. The default ASIO discovery key
was absent before and after this test; no existing user configuration was changed.

| Flow | Observed result |
| --- | --- |
| Add a generated device, name it `sarguiqa1030a`, set 4 input and 4 output channels, remove the original default from the private draft, Apply | GUI reported topology reloaded and verified; independent CLI returned the new identity and 4/4 channels; its current-user registration pointed to the matching fixture DLL |
| Rename to `sarguiqa1030b`, add `sarguiqa1030c` with 2/2 channels, Apply | Both devices read back correctly; the old discovery name disappeared; the renamed device retained its CLSID and broker token |
| Disable the second device, Apply | The saved topology retained the disabled device, enabled totals became 4/4, and only the first device remained registered |
| Remove the disabled device, Apply | The saved topology contained exactly one 4/4 device; no deleted-device registration remained |
| Set input channels to 0, Refresh | Local range error and disabled Apply; the invalid dirty input survived refresh and subsequent polling |
| Reload saved, cancel | The dirty zero-channel input remained unchanged |
| Reload saved, confirm | Saved 4/4 values returned, dirty state cleared and the last-device Remove action remained disabled |
| Close both windows, stop the engine using its normal stop helper | Both processes exited; temporary tasks and process-ledger entries were cleared |
| Remove fixture-owned registrations after stopping | Zero temporary discovery entries remained and the default discovery key was still absent |

All four applied topology changes retained service PID 5972. Independent registry
and control-CLI inspection followed the GUI operations; a success message alone
was not the acceptance criterion. These are configuration, registration and
lifecycle checks, not a new DAW audio-content or continuity result.

Source revision `d083672` passed CI run
[`40990777554`](https://github.com/LovelyRua/sys-audio-router/actions/runs/40990777554):
165 CTest cases, five early model/client/service checks, 15 script suites, eight
advisory ASan cases, and six-window startup/graceful closure. Package run
[`40990782298`](https://github.com/LovelyRua/sys-audio-router/actions/runs/40990782298)
also passed, including 169 CTest cases. Qt remains the default entry; this does
not establish full native GUI replacement readiness.

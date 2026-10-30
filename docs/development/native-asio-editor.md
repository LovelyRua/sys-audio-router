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
stopped runtime, ASIO hosts, pipes, mappings and per-run locks before opening
the saved configuration again in the same process. Its PID, user identity, pipe
name, log stream and session arguments are retained. An external stop still
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
confirms the legacy command still exits. Graph width includes non-ASIO channels;
it is not the same quantity as the sum of ASIO channels.

CI runs these model/client/service checks before compiling the full native GUI,
whose startup gate now opens and closes six windows. These checks do not prove
audio-content continuity or full native UI parity with Qt. Physical ASIO editing,
localization, accessibility and complete installer-flow parity remain separate.

# Stage Final Review - 2026-08-11

## Decision

The current `main` line is a shaped-prototype candidate. The realtime engine,
real WASAPI runtime, Virtual ASIO bridge, named-pipe control plane, Qt Quick
control application, bootstrap launcher, ZIP payload, and per-user NSIS
installer are integrated and covered by automated Windows acceptance.

This decision does not declare release qualification complete. The remaining
long-duration, physical-removal, and seed-user gates below stay mandatory and
must not be silently weakened.

## Reviewed Candidate

- Executable source candidate: `4353e4c`.
- Review-record candidate `4fabaa3`: 124 of 124 CTest targets passed on the
  Windows test machine after a clean slot clone and full MSVC build.
- GitHub CI run: `31311094007`, passed.
- Windows GUI Package run: `31311094017`, passed.
- Final ZIP: SHA-256
  `FD9ED882552F5529E1BCF5225C66DE01B069641731779B3700B84B81239C98CB`.
- Final NSIS installer: SHA-256
  `2A8C078940F3DF79A635005CBB1BDAEB5C7D0EA3A8AA51EC7E861FF1B903E40D`.
- Package acceptance covers ZIP and NSIS install/update/uninstall, bootstrap,
  Qt/QML deployment, control and diagnostics handshakes, WASAPI enumeration,
  Virtual ASIO registration ownership, and install-path boundary rejection.
  The final ZIP repeated this gate on the test machine with 15 enumerated
  endpoints, both concurrent launchers successful, and the existing Virtual
  ASIO registration preserved.

## Defects Closed During Final Review

1. `sar_control_cli` reused a fixed command ID across invocations. The service
   could replay an unrelated cached response. CLI command IDs are now unique.
2. GUI command IDs restarted at `gui-1` while the engine process persisted.
   Each GUI process now uses a UUID-scoped command ID namespace.
3. The service replay cache keyed responses only by command ID. It now replays
   only byte-identical requests and rejects changed payloads with
   `command_id_conflict` without applying them.
4. Two bootstrap processes could race to launch duplicate engines. A per-user
   named mutex now serializes the full engine/pipe/GUI launch sequence, and both
   ZIP and NSIS acceptance exercise concurrent launchers.
5. An older render-deadline branch appeared unmerged. Review confirmed its
   render-before-capture scheduling and I/O-order test were already integrated
   by `35b07fb` and subsequently adapted to the bounded realtime error model.

## Review Coverage

- Realtime path: allocation, lock, file-I/O, logging, COM activation, and
  blocking-control scans across graph processing, WASAPI pumping, sample
  conversion, Virtual ASIO buses, and worker loops.
- Ownership and lifecycle: COM/event/thread shutdown, runtime replacement and
  rollback, named-pipe replay behavior, launcher concurrency, GUI single
  instance, installer ownership, and uninstall boundaries.
- User control flow: device enumeration and selection, render/duplex mode,
  matrix connection/gain/mute, preset save/load, undo/redo, diagnostics, and
  process restart identity.
- Repository health: clean `main`, no open pull requests, no Git object
  corruption, no generated build trees committed, and no credential artifacts.

## Final Interactive Gate

The 2026-08-11 two-client, 60-second REAPER rerun was correctly blocked before
launch with `interactive_user_missing`; the VM console had no logged-in desktop
user or `explorer.exe`. Evidence was saved under `.sar-evidence`. Rerun the
identical command after logging in as the WinRM user:

```bat
scripts\windows-winrm-reaper-acceptance.cmd 192.168.123.123 codex <password> C:\Users\codex\src\sys-audio-router-engineer-a\build-engineer-a\Debug "<render-endpoint-id>" stage-final-reaper 2 60
```

Earlier release-duration evidence remains valid: two REAPER producers sustained
60 seconds with zero drop or xrun, and Cakewalk plus REAPER passed a 30-second
cross-host gate. The blocked rerun is an environment gate, not a substituted
pass.

## Remaining Qualification Gates

- Run the second eight-hour duplex soak on a distinct physical endpoint pair.
- Run the 24-hour duplex soak with the published clock, xrun, FIFO, recovery,
  and callback thresholds unchanged.
- Exercise physical default-endpoint removal and pinned-endpoint removal in an
  interactive desktop session.
- Complete the ACX versus SysVAD/PortCls virtual WDM driver spike before
  committing to a kernel architecture.
- Put the GUI and installer in front of seed DAW users and close workflow and
  accessibility findings.

These are Alpha qualification tasks, not reasons to hold back the shaped
prototype from controlled hands-on testing.

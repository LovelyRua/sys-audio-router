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

- Executable source candidate: `8eb7c36`.
- Automation and package candidate: `3c87066`.
- Candidate `8eb7c36`: 124 of 124 CTest targets passed on the Windows test
  machine after a clean slot upload and full MSVC build.
- GitHub CI run: `31449581633`, passed, including the complete CTest and
  Windows automation-script suites.
- Windows GUI Package run: `31449581645`, passed.
- Final ZIP: SHA-256
  `FFA22433CE81363E5C8C661F024F930B4B3A4E93617DE732EF8793860A5389F7`.
- Final NSIS installer: SHA-256
  `248CD8E205ED0A751F997EE0D58E313E9365203186E79B90D55430E41E986550`.
- Package acceptance covers ZIP and NSIS install/update/uninstall, bootstrap,
  Qt/QML deployment, control and diagnostics handshakes, WASAPI enumeration,
  Virtual ASIO registration ownership, and install-path boundary rejection.
  The final ZIP repeated this gate in slot `stage-final-3c87066` on the test
  machine with 15 enumerated endpoints, both concurrent launchers successful,
  and the existing Virtual ASIO registration preserved.

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
6. The first non-empty WASAPI capture packet could carry the documented data
   discontinuity flag during stream transition. With no prior packet baseline,
   the graph runner incorrectly counted it as a midstream gap and entered
   recovery. The first packet now establishes the baseline; discontinuities on
   all later packets retain strict xrun and recovery handling.
7. Intentional startup, capture-starvation, and recovery silence was included in
   the render FIFO underflow counter and therefore marked an otherwise clean
   run degraded. Runtime health now remains healthy only when every underflow
   frame is exactly attributed to those explicit silence categories. Missing or
   excess attribution remains degraded.
8. The downloaded-source slot-retention self-test anchored its active-token
   fixture to a fixed date while production selection used the current UTC
   time. The test became time-dependent and failed after the fixture aged past
   the stale-token threshold. It now anchors all relative fixture timestamps to
   the current UTC time; the complete 11-script automation suite passes.

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

## Strict Real WASAPI Gate

Candidate `8eb7c36` passed three consecutive five-second duplex runs against
VB-Audio Cable B with strict healthy mode enabled. Across 15 seconds the gate
reported zero failures, zero capture discontinuities, zero xruns, zero FIFO
overflows, zero wait timeouts, and zero recovery-silence frames. All 5,568
render underflow frames were exactly attributed to 4,896 startup-silence frames
and 672 capture-starvation-silence frames. Aggregate rendered throughput was
48,601.6 frames per second. Evidence is stored in the local ignored directory
`.sar-evidence/20260811T012506Z-stage-final-duplex-health`.

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

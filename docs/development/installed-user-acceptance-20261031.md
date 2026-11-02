# Installed user-flow acceptance: 2026-10-31

## Environment and scope

The test used the dedicated VM24 `SAR-WIN11-LAB`, an interactive Windows 11
session, REAPER 7.41 and an already installed base VB-CABLE. The installer was
the PR merge build `135571c51338a337f953b433dfa4553c91c86d4f`, combining main
`68ff15a` and branch revision `5d85dbf`, from [package run 41167109694](https://github.com/LovelyRua/sys-audio-router/actions/runs/41167109694).
Its SHA-256 was `63B98BE84108A87213F92AABD1CD4B44C280E8474AC840BCB160BE9697307D54`,
verified after transfer to the VM. Installation used an isolated directory; the
REAPER project and configuration were also isolated. No VB or kernel driver was
installed during this pass.

## Installation and native controls

- The interactive installer finished, and its finish-page launch started the Qt
  panel and one background engine without a Qt plugin error. Qt remains the
  default; the separately packaged native preview used the same engine PID.
- In the native editor, selecting the existing VB-CABLE playback endpoint,
  applying and starting audio succeeded. An independent CLI reported both
  `runtime_installed=true` and `runtime_running=true`.
- With a two-input, four-output matrix, clicking connected a route. Right moved
  keyboard focus and the inspector without changing the route count; Enter
  connected a route and Shift+Enter removed it. Down followed by Enter connected
  the next row. Two stereo routes were
  then established for the playback test. These checks do not establish full
  keyboard or screen-reader accessibility.

## Release blocker: ASIO discovery

With only the registration created by the installer under the current user's
registry, REAPER displayed **No ASIO drivers**. The installed DLL was present
and the current-user registration resolved to it. This repeats the current-user
failure recorded in `reaper-user-pipe-20261029.md` and is a normal-user-flow
failure, not a passed DAW acceptance.

For diagnosis only, after confirming the corresponding machine-level keys were
absent, the same installed DLL was temporarily registered at machine scope on
VM24. After restarting the isolated REAPER instance, REAPER loaded that DLL and
showed 48 kHz, 128 samples and 2/2 channels. The engine reported one active ASIO
producer. The machine-level registration was removed and its absence verified
after the test. This comparison identifies a registration-scope compatibility
issue in this environment; it does not prove the behavior of every DAW or user
account.

## Playback evidence and limitation

Under that temporary registration workaround, the isolated REAPER project
played a 1 kHz stereo tone at -18 dBFS for approximately 19.4 seconds, routed
through the installed SAR engine to VB-CABLE. During playback, engine telemetry
reported a peak of 0.125885, one ASIO producer, zero XRUNs and zero dropped
blocks. An independent WASAPI capture on the VB-CABLE output reported:

| Metric | Result |
| --- | ---: |
| Captured frames / wall time | 1,031,040 / 31,420 ms |
| Sample rate / channels | 48,000 Hz / 2 |
| Peak / RMS | 0.129924774 / 0.0736586494 |
| Signal frames | 705,983 |
| Non-finite samples / capture errors / timeouts | 0 / 0 / 0 |
| Maximum observed adjacent-sample step | 0.229595184 |

The received-frame count is not a device-timestamp continuity measurement. The
large step is substantially above the generated -18 dBFS 1 kHz sine's
expected adjacent-sample step of roughly 0.0165 at 48 kHz. Packet gaps, device
behavior and the processing path have not been distinguished. The result proves
nonzero signal reached the receiver in this short run; it does not prove
click-free playback, uninterrupted capture, recording into REAPER, or long-term
audio reliability.

## Update and uninstall

- With the isolated REAPER process still holding the installed ASIO DLL, a
  same-version silent reinstall was started through WinRM. It returned exit code
  0 while the engine and native window stopped. The DLL hash remained unchanged.
  The expected locked-DLL refusal and unchanged running session were not
  observed.
- A separate interactive same-version reinstall was then tested with REAPER
  holding the same installed DLL and the engine running. The installer's first
  prompt offered to uninstall the existing version before installing the new one.
  Selecting its default Yes stopped the engine, closed the native window and
  removed the user-level ASIO registration before the installer showed its
  Welcome page. REAPER remained open with its audio device closed and still had
  the DLL loaded. No locked-driver warning was observed before that disruption.
  This demonstrates that the existing-version uninstall path runs before the
  protection can preserve the running installation. The installer was cancelled;
  the isolated partial installation was repaired only to permit clean removal.
- After REAPER closed and the temporary machine registration was removed, a
  same-version reinstall completed. The native launcher restarted the engine
  and the runtime configuration was read back. This is not a cross-version
  upgrade test.
- A silent uninstall invoked through WinRM returned 0 but, on subsequent
  checks, left the installation directory, user ASIO registration and native
  window present while the engine had stopped. A later silent attempt also left
  the directory and registration after all audio applications were closed. The
  Session 0 execution path and uninstaller process behavior need further
  investigation.
- After closing the native window, the interactive uninstaller reported success.
  Independent checks found the test installation directory and both user- and
  machine-level SAR ASIO registrations absent, with no SAR or REAPER process.
  This does not prove interactive uninstall while a DAW or GUI is still open.

The original engine session file was absent before testing. The generated test
session was preserved in the ignored lab evidence directory and removed from
the normal session location. Owned scheduled tasks and the test process
ledger were cleared after both passes. The short playback did not change any pre-existing REAPER
project or configuration. Raw capture and installer-result evidence remains in
the local ignored `artifacts/stabilization-20261029` workspace.

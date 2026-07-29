# Windows Test Machine

This machine is available for future Windows audio driver, DAW compatibility, and virtual device testing.

## Host

- Address: `192.168.123.123`
- Access: RDP is enabled.
- Role: Windows integration test machine.

Credentials must not be committed to this repository. Keep them in the operator's password manager or temporary local notes outside version control.

## Intended Uses

- Virtual ASIO driver installation tests.
- Virtual WDM/WASAPI endpoint installation tests.
- DAW detection and compatibility tests.
- OBS, Discord, browser, and game audio routing tests.
- Long-running xrun and latency soak tests.
- Installer and uninstall validation.

See [Windows Test Runbook](windows-test-runbook.md) for the current pull/build/test workflow.

## Operating Rules

- Do not use this machine as the primary development environment.
- Keep DAW and audio tool versions recorded before compatibility claims.
- Record Windows build number before driver debugging.
- Snapshot or otherwise preserve machine state before risky driver experiments when possible.
- Treat every installed driver as a system-level change that needs rollback notes.

## Future Test Inventory

Record installed versions here when testing begins:

| Component | Version | Notes |
| --- | --- | --- |
| Windows | `10.0.26100.4770` | Observed before the virtual endpoint spike. |
| Visual Studio Community | `2022 17.14.35` | Minimal C++, WDK, Spectre, and ATL components. |
| Visual Studio Build Tools | 2022 | Used for user-mode builds; VS 2022 Build Tools cannot host the WDK component. |
| Windows Driver Kit | `10.1.26100.6584` | `Microsoft.WindowsWDK.10.0.26100`; ACX headers present. |
| REAPER | `7.78 x64` | Virtual ASIO discovery, load, callback, and physical CABLE render evidence recorded on 2026-07-28. |
| Cakewalk by BandLab | `29.09.0.098` | Second-DAW discovery, load, callback, and simultaneous REAPER evidence recorded on 2026-07-29. |
| Cubase | TBD | |
| Ableton Live | TBD | |
| FL Studio | TBD | |
| OBS | TBD | |
| Discord | TBD | |

# Windows Test Machine

This machine is available for future Windows audio driver, DAW compatibility, and virtual device testing.

## Host

- Address: `192.168.123.3`
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
| Windows | TBD | |
| REAPER | TBD | |
| Cubase | TBD | |
| Ableton Live | TBD | |
| FL Studio | TBD | |
| OBS | TBD | |
| Discord | TBD | |


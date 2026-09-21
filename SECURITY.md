# Security Policy

System Audio Route is alpha software. Its attack surface is local:

- the per-user named-pipe control protocol (`sys-audio-route-control`);
- the Virtual ASIO broker pipe and shared-memory transport;
- session and preset files under `%APPDATA%\System Audio Route`;
- the installer and the current-user Virtual ASIO registry registration.

## Reporting a vulnerability

Please do **not** open a public issue for a security problem. Use GitHub's
private vulnerability reporting for this repository (Security tab, "Report a
vulnerability"). Include the affected version, steps to reproduce, and the
impact you observed.

We aim to acknowledge reports within seven days. Fixes ship in the next alpha
build and are noted in the release notes.

## Known limits

- Any process running as the same Windows user can send control commands to the
  engine; per-client authorization is not implemented yet.
- Release binaries are not code-signed yet.

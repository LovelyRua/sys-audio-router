# REAPER ASIO Acceptance

`scripts/windows-winrm-reaper-acceptance.cmd` runs the real Virtual ASIO gate
inside the logged-in Windows desktop. The wrapper defaults to two independent
REAPER clients; pass `1` explicitly for the legacy single-client gate.

```bat
scripts\windows-winrm-reaper-acceptance.cmd 192.168.123.123 codex <password> C:\path\to\Debug "<render-endpoint-id>" engineer-a-two-client 2 60
```

Each run creates `.sar-evidence/reaper-<slot>-<timestamp>-<run-id>` locally.
The bundle contains `preflight.json`, `manifest.json`, initial and final
diagnostic snapshots, and `result.json`. A remote copy remains under
`%LOCALAPPDATA%\SystemAudioRoute\acceptance\reaper\runs` if WinRM cannot copy
the evidence back. Set `SAR_REAPER_EVIDENCE_DIR` to choose the local directory.

The slot state records task names and exact process paths before launch. A rerun
automatically removes only stale resources that the same slot can prove it
owns. Missing Explorer sessions produce the stable block code
`interactive_user_missing`; log in to the desktop and rerun the same command.
Unknown existing processes produce `untracked_processes` with PID, path, and
start-time evidence.

Close unknown processes manually. After confirming they are disposable, the
explicit recovery mode can stop only REAPER at the requested executable path
and the engine at the requested build path:

```bat
set SAR_REAPER_RECOVER_UNTRACKED=1
scripts\windows-winrm-reaper-acceptance.cmd 192.168.123.123 codex <password> C:\path\to\Debug "<render-endpoint-id>" engineer-a-two-client 2
```

The explicit recovery mode is intentionally not the default.

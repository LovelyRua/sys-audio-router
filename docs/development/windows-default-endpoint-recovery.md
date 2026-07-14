# Windows default-endpoint recovery experiment

This experiment runs the already-built `sar_measure_wasapi_recovery.exe` in a
logged-in user's interactive session, changes both Windows default endpoints
from their original values (A) to specified devices (B), then restores A in a
PowerShell `finally` block. The measurement remains in follow-default mode; the
launcher does not pass `--capture-id` or `--render-id`.

## Prerequisites

- Build `sar_measure_wasapi_recovery` in the selected WinRM slot first.
- Keep the target user logged in with an Explorer session.
- Install `AudioDeviceCmdlets` for that interactive user and make it available
  to Windows PowerShell 5.1.
- Give the WinRM identity permission to register a limited interactive Scheduled
  Task for that user.

In the interactive user's PowerShell session, list candidate IDs with:

```powershell
Import-Module AudioDeviceCmdlets
Get-AudioDevice -List | Format-Table Type, Name, ID -AutoSize
```

Choose one `Playback` ID and one `Recording` ID that are not currently the
defaults. Both target directions are required so the run is an A-to-B-to-A
duplex experiment.

## Run

From the development machine:

```bat
set SAR_TEST_PASSWORD=<winrm-password>
set SAR_RECOVERY_TARGET_PLAYBACK_ID=<playback-B-id>
set SAR_RECOVERY_TARGET_RECORDING_ID=<recording-B-id>
scripts\windows-winrm-default-endpoint-recovery.cmd 192.168.123.123 codex "" engineer-c
```

The password is transient command input used to create the in-memory WinRM
credential. It is not included in the remote config or result files. Clear
`SAR_TEST_PASSWORD` after the run if the prompt will remain open.

The default executable path follows the existing slot layout:

```text
%USERPROFILE%\src\sys-audio-router-<slot>\build-<slot>\sar_measure_wasapi_recovery.exe
```

The positional arguments are host, WinRM user, password, slot, target Playback
ID, target Recording ID, duration in milliseconds, and an optional absolute
remote executable path. Environment variables can supply the target IDs and the
optional `SAR_RECOVERY_INTERACTIVE_USER`. Timing can be adjusted with
`SAR_RECOVERY_DURATION_MS`, `SAR_RECOVERY_SWITCH_DELAY_MS`,
`SAR_RECOVERY_TARGET_HOLD_MS`, and `SAR_RECOVERY_MAXIMUM_MS`. The duration must
leave at least one second after the pre-switch and B-hold intervals so the tool
can observe restoration to A.

Do not pass pinned endpoint IDs to the measurement executable. The experiment
must follow Windows defaults to exercise endpoint-notification reopen behavior.

## Isolation and artifacts

The launcher sanitizes the slot, resolves the selected interactive user's SID
and profile, hashes the initiating WinRM identity, and creates a new run GUID.
Concurrent users and runs therefore write to separate directories:

```text
AppData\Local\SystemAudioRoute\default-endpoint-recovery\<initiator>\<slot>\<run-guid>
```

Each retained run contains:

- `stdout.log` and `stderr.log`: measurement process streams.
- `switch.json`: original and target endpoint identities, timestamped B/A switch
  observations, and restoration errors.
- `acceptance.log`: output from the existing
  `windows-wasapi-recovery-acceptance.ps1` gate.
- `result.json`: process, acceptance, restoration, and overall exit status.
- `config.json`, `runner.ps1`, and `acceptance.ps1`: password-free inputs needed
  to audit the exact remote run.

The Scheduled Task is unique per SID, slot, and run GUID and is unregistered by
the WinRM launcher. All polling and process waits have deadlines. The runner
attempts Recording and Playback restoration independently in `finally`, so one
restore failure does not suppress the other. A restoration failure makes the
overall result fail and remains explicit in `switch.json`.

The copied acceptance script requires a zero measurement exit code, a healthy
final running snapshot, non-empty active IDs, at least one successful recovery,
no notification-reset failure, and a maximum recovery duration no greater than
the configured threshold (5000 ms by default). The retained `stdout.log` can
also be checked later with the repository script:

```powershell
scripts\windows-wasapi-recovery-acceptance.ps1 `
  -InputPath C:\path\to\stdout.log -ProcessExitCode 0
```

Run the local parser and safety self-check before sending script changes:

```powershell
powershell -NoProfile -File scripts\windows-wasapi-default-endpoint-recovery.tests.ps1
```

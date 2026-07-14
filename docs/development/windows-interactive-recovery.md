# Windows interactive recovery experiment

`sar_measure_wasapi_recovery.exe` must run in a logged-in Windows user's
interactive session to observe that session's audio endpoints. The WinRM helper
registers a temporary Scheduled Task with an `Interactive` logon type, starts it,
collects its output, and unregisters it in a `finally` block.

Build the recovery target on the test machine first. The default executable path
matches the slot layout used by the local WinRM scripts:

```text
%USERPROFILE%\src\sys-audio-router-<slot>\build-<slot>\sar_measure_wasapi_recovery.exe
```

Run the experiment from the development machine:

```bat
set SAR_TEST_PASSWORD=<winrm-password>
scripts\windows-winrm-recovery-interactive.cmd 192.168.123.123 codex "" engineer-c 30000
```

Optional positional arguments after the duration are capture endpoint ID,
render endpoint ID, and the absolute remote executable path. Empty quoted
arguments retain defaults. Environment variables provide the same optional
inputs: `SAR_RECOVERY_CAPTURE_ID`, `SAR_RECOVERY_RENDER_ID`,
`SAR_RECOVERY_EXECUTABLE`, and `SAR_RECOVERY_INTERACTIVE_USER`.

The remote machine must have exactly one user with an Explorer session unless
`SAR_RECOVERY_INTERACTIVE_USER` names the desired `DOMAIN\user`. The WinRM
credential needs permission to create Scheduled Tasks for that user.

Each run writes `stdout.log`, `stderr.log`, and `result.json` below the
interactive user's profile:

```text
AppData\Local\SystemAudioRoute\interactive-recovery\<initiator>\<slot>\<run-guid>
```

The profile, initiating WinRM identity, sanitized slot, and run GUID isolate
concurrent users and runs. Output is retained for inspection; the Scheduled Task
is always removed. Passwords are accepted only as a command argument or through
`SAR_TEST_PASSWORD` and must not be stored in repository files.

Validate a retained `stdout.log` with the machine-readable acceptance script:

```powershell
scripts\windows-wasapi-recovery-acceptance.ps1 `
  -InputPath C:\path\to\stdout.log -ProcessExitCode 0
```

The default gate requires a healthy final running snapshot, non-empty active
capture and render IDs, at least one successful recovery, no notification reset
failure, a zero process exit code, and a maximum recovery duration no greater
than 5000 ms. Use `-AllowNoSuccessfulRecovery` only for a baseline run that does
not inject an endpoint change.

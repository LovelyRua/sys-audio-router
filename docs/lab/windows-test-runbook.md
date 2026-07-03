# Windows Test Runbook

This runbook describes how to pull the GitHub repository onto the Windows test machine and run the current C++ smoke tests.

## Current Access

- Host: `192.168.123.3`
- RDP: open
- WinRM: enabled by `scripts/windows-enable-winrm-and-test.cmd` or by running the bootstrap with `SAR_ENABLE_WINRM=1` from an elevated `cmd.exe`
- SSH: not open during initial probe
- SMB admin share: not open during initial probe

If WinRM is not reachable yet, use RDP or the ESXi web console once to run the bootstrap from an elevated prompt.

## Attempt RDP Initial Program Automation

From the development machine, this may trigger the bootstrap through RDP without manual desktop interaction:

```bat
scripts\rdp-trigger-test-machine.cmd 192.168.123.3 codex <password>
```

If the remote Windows host accepts the RDP initial program setting, it will:

1. Download `scripts/windows-enable-winrm-and-test.cmd`.
2. Enable WinRM on port `5985`.
3. Download and run `scripts/windows-test-bootstrap.cmd`.
4. Write a log to `%USERPROFILE%\Desktop\sar-rdp-bootstrap.log` on the test machine.

This depends on Windows accepting `alternate shell` for the RDP session. If it is ignored, use the manual RDP steps below.

## Open RDP

From the development machine:

```bat
scripts\open-test-machine-rdp.cmd
```

Do not commit credentials to this repository.

## Bootstrap and Test

Once WinRM is enabled, run the test machine non-interactively from the development machine:

```bat
scripts\windows-winrm-test.cmd 192.168.123.3 codex <password>
```

The script downloads the latest bootstrap from `main`, builds the repository on the test machine, and fails if CMake, build, or CTest fails.

On the test machine, open `cmd.exe` and run:

```bat
curl -L https://raw.githubusercontent.com/LovelyRua/sys-audio-router/main/scripts/windows-test-bootstrap.cmd -o "%TEMP%\sar-bootstrap.cmd"
"%TEMP%\sar-bootstrap.cmd"
```

To enable WinRM while bootstrapping, run from an elevated `cmd.exe`:

```bat
set SAR_ENABLE_WINRM=1
"%TEMP%\sar-bootstrap.cmd"
```

The bootstrap script will:

1. Install or verify Git, CMake, Ninja, and Visual Studio Build Tools.
2. Clone or update `https://github.com/LovelyRua/sys-audio-router.git`.
3. Configure the CMake build.
4. Build the smoke test targets.
5. Run CTest with failure output enabled.

Default checkout path:

```text
%USERPROFILE%\src\sys-audio-router
```

To choose a different path:

```bat
"%TEMP%\sar-bootstrap.cmd" C:\src\sys-audio-router
```

## Expected Test Targets

- `realtime_smoke`
- `graph_snapshot_smoke`
- `graph_builder_smoke`
- `route_matrix_smoke`
- `preset_document_smoke`
- `diagnostics_smoke`
- `spsc_ring_buffer_smoke`
- `process_context_smoke`
- `xrun_detection_smoke`
- `spsc_ring_buffer_threaded_smoke`

## WASAPI Inspection

After building on Windows, inspect active WASAPI endpoints and the default render stream contract:

```bat
build\sar_list_wasapi_devices.exe
```

## Better Future Remote Execution

For non-interactive testing, enable one of:

- WinRM on port `5985` or `5986`.
- OpenSSH server on port `22`.

Once either is available, the same bootstrap script can be run remotely without manual RDP interaction.

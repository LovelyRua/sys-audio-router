# Windows Test Runbook

This runbook describes how to pull the GitHub repository onto the Windows test machine and run the current C++ smoke tests.

## Current Access

- Host: `192.168.123.123`
- RDP: open
- WinRM: enabled by `scripts/windows-enable-winrm-and-test.cmd` or by running the bootstrap with `SAR_ENABLE_WINRM=1` from an elevated `cmd.exe`
- SSH: not open during initial probe
- SMB admin share: not open during initial probe

If WinRM is not reachable yet, use RDP or the ESXi web console once to run the bootstrap from an elevated prompt.

## Attempt RDP Initial Program Automation

From the development machine, this may trigger the bootstrap through RDP without manual desktop interaction:

```bat
scripts\rdp-trigger-test-machine.cmd 192.168.123.123 codex <password>
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
scripts\windows-winrm-test.cmd 192.168.123.123 codex <password>
```

For concurrent work, pass a unique slot as the fourth argument:

```bat
scripts\windows-winrm-test.cmd 192.168.123.123 codex <password> engineer-a
scripts\windows-winrm-test.cmd 192.168.123.123 codex <password> engineer-b
scripts\windows-winrm-test.cmd 192.168.123.123 codex <password> engineer-c
```

Each slot uses an isolated remote checkout and build directory:

```text
%USERPROFILE%\src\sys-audio-router-engineer-a\build-engineer-a
%USERPROFILE%\src\sys-audio-router-engineer-b\build-engineer-b
%USERPROFILE%\src\sys-audio-router-engineer-c\build-engineer-c
```

This avoids most `.pdb`, `.ninja_log`, `.exe`, and CMake cache file locking
collisions when multiple engineers test at the same time. If no slot is passed,
the script keeps the historical default path.

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

## Local Archive Measurement

To upload the current local `HEAD`, build only the WASAPI measurement tools, and
run a render, duplex, or combined measurement:

```bat
scripts\windows-winrm-local-measure.cmd 192.168.123.123 codex <password> engineer-a render 1000 10 false true
scripts\windows-winrm-local-measure.cmd 192.168.123.123 codex <password> engineer-a both 5000 10 true false
```

For a pinned duplex soak, set both endpoint IDs and use a wait timeout with at
least one device period of scheduling margin:

```bat
set "SAR_MEASURE_MODE=duplex"
set "SAR_MEASURE_DURATION_MS=28800000"
set "SAR_MEASURE_TIMEOUT_MS=20"
set "SAR_MEASURE_CAPTURE_ID=<capture-endpoint-id>"
set "SAR_MEASURE_RENDER_ID=<render-endpoint-id>"
set "SAR_MEASURE_MAX_RECOVERY_SILENCE_FRAMES=2594"
set "SAR_MEASURE_MIN_FRAME_COVERAGE_BPS=9999"
scripts\windows-winrm-local-measure.cmd 192.168.123.123 codex <password> engineer-a-soak
```

The two endpoint IDs must be supplied together. Every invocation creates a
timestamped `.sar-evidence` directory by default. Set
`SAR_MEASURE_EVIDENCE_DIR` to choose another local directory. The bundle keeps
the source commit and run configuration in `manifest.json`, plus each attempt's
command, combined process output, and the final soak summary. Evidence is copied
back even when the remote gate fails whenever the WinRM session remains usable;
`.sar-evidence` is intentionally excluded from Git.
Set `SAR_MEASURE_MAX_RECOVERY_SILENCE_FRAMES` to the callback-quantized recovery
bound for the selected endpoint pair; zero leaves that optional bound disabled.
Set `SAR_MEASURE_MIN_FRAME_COVERAGE_BPS=9999` for the roadmap's 99.99% long-soak
gate. Short diagnostic runs default to 9,900 basis points (99%).

## Windows Audio Service Recovery

After building a reusable WinRM slot, run the render-only service-restart gate:

```bat
set SAR_TEST_PASSWORD=<password>
scripts\windows-winrm-audio-service-recovery.cmd 192.168.123.123 codex "" engineer-a
```

The helper starts `sar_measure_wasapi_recovery`, restarts `Audiosrv` three
seconds later, requires the render endpoint to recover within five seconds, and
retains stdout, stderr, acceptance, and result evidence under `.sar-evidence`.
A per-slot remote lock prevents concurrent service tests
from using the same build. `SAR_AUDIO_SERVICE_RESTART_DELAY_MS`,
`SAR_MAX_RECOVERY_MS`, and `SAR_RECOVERY_EVIDENCE_DIR` override the defaults.

The eighth argument enables `--require-healthy`. When enabled, the command fails
on faulted or degraded runtime summaries. The ninth argument allows endpoint
unavailability so WinRM-only sessions can still verify upload, configure, build,
and tool invocation on machines where WASAPI endpoints only appear in an
interactive desktop session. The measurement tools print a stable
`wasapi_runtime_summary ...` line before the human-readable report so long-run
automation can parse health, reason code, transfer counts, and error counts.
The same line also carries stream shape, period, partial/silent transfer, and
last-cycle flag fields so CI and lab scripts can diagnose most loop failures
without scraping the human-readable sections.
When a stream opens, the tools also print `wasapi_stream_diagnostics ...` lines
with state, direction, sample format, bit depth, buffer size, and device period
fields before the matching human-readable stream diagnostics block.

## REAPER Virtual ASIO Acceptance

REAPER must already be configured to use `System Audio Route Virtual ASIO` at
48 kHz with a 128-frame block. Run the interactive acceptance against a tested
remote build and a pinned render endpoint:

```bat
scripts\windows-winrm-reaper-acceptance.cmd 192.168.123.123 codex <password> C:\path\to\Debug "<render-endpoint-id>" engineer-a
scripts\windows-winrm-reaper-acceptance.cmd 192.168.123.123 codex <password> C:\path\to\Debug "<render-endpoint-id>" engineer-a-two-client 2
scripts\windows-winrm-reaper-acceptance.cmd 192.168.123.123 codex <password> C:\path\to\Debug "<render-endpoint-id>" engineer-a-two-client-soak 2 3600
```

The helper refuses to take over an existing REAPER or engine process. It
registers and verifies the x64 per-user driver, starts the engine and REAPER in
the logged-in Explorer session, proves that REAPER loaded the exact driver DLL,
and requires the requested number of active producers, non-zero
pushed/consumed/mixed blocks, and zero dropped blocks. Counts from one through
eight are accepted; counts above one launch isolated REAPER instances.
The optional eighth argument sets the measured duration in seconds. The gate
uses diagnostic deltas over that interval and requires at least 80% of nominal
48 kHz/128-frame producer and render coverage, zero drops, xruns, clipping, and
non-finite samples, and a peak graph callback no greater than 10 ms. Temporary
processes and Scheduled Tasks are removed on success and failure.

## Expected Test Targets

The Windows suite currently has 124 CTest targets, including the portable core
smoke tests plus WASAPI stream, graph runner, realtime thread, realtime
worker/render/duplex loop, runtime summary, preflight, and measure tool smoke
tests.

## WASAPI Inspection

After building on Windows, inspect active WASAPI endpoints and the default render stream contract:

```bat
build\sar_list_wasapi_devices.exe
```

WinRM sessions may report zero active WASAPI endpoints even when an interactive desktop session has audio devices. Use the CLI output as a session-local inspection result, not as proof that the VM has no audio stack.

## Engine Control Acceptance

Run the service and CLI acceptance flow from an interactive Windows audio
session after building `sar_engine_service` and `sar_control_cli`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows-engine-control-acceptance.ps1 -BuildPath build
```

For a multi-configuration build or a pinned render endpoint:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows-engine-control-acceptance.ps1 -BuildPath build -Configuration Release -RenderDeviceId '<render-endpoint-id>'
```

The script starts a service on a GUID-suffixed named pipe, waits for it through
the real CLI, then accepts `devices`, `runtime-configure-render`,
`runtime-start`, `diagnostics`, and `runtime-stop` only when their
machine-readable fields match the expected state. It requires at least one
active WASAPI render endpoint and at least one processed audio block. A
`finally` block attempts runtime stop and terminates the exact service PID on
all paths. The final `engine_control_acceptance ...` line is the stable result
for automation; per-command output and service stdout/stderr are retained in
the reported output directory.

Run this acceptance from an interactive desktop session. As with device
inspection, WinRM may not expose the logged-in user's active audio endpoints.

## Better Future Remote Execution

For non-interactive testing, enable one of:

- WinRM on port `5985` or `5986`.
- OpenSSH server on port `22`.

Once either is available, the same bootstrap script can be run remotely without manual RDP interaction.

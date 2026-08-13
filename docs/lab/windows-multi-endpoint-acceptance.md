# Windows Multi-Endpoint Machine Acceptance

This gate proves one repeatable vertical slice of the WASAPI matrix runtime on
a Windows audio machine:

- two distinct capture endpoints;
- one render endpoint acting as clock master;
- a persisted session snapshot;
- live and post-restart diagnostics;
- exact-process cleanup and per-slot remote serialization.

It does not install build tools, drivers, audio applications, or PowerShell
modules. Build and runtime dependencies must already exist on the remote test
machine or be produced by CI.

## Local Interactive Run

Run this from the Windows user's interactive audio session. Device IDs are
optional. When omitted, the script chooses the first two distinct active 48 kHz
WASAPI capture endpoints and the first active 48 kHz render endpoint.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows-multi-endpoint-acceptance.ps1 `
  -BuildPath C:\path\to\build\Release
```

For deterministic hardware selection, pin all three IDs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows-multi-endpoint-acceptance.ps1 `
  -BuildPath C:\path\to\build\Release `
  -CaptureDeviceIdA '<capture-endpoint-a>' `
  -CaptureDeviceIdB '<capture-endpoint-b>' `
  -RenderDeviceId '<render-endpoint>'
```

Each endpoint must expose its active native format at 48 kHz. The alpha matrix
runtime currently maps the complete native channel range, so the script obtains
the channel count from `sar_control_cli devices` rather than accepting a partial
range from the caller.

## WinRM Run

From the development machine:

```bat
scripts\windows-winrm-multi-endpoint-acceptance.cmd 192.168.123.123 codex <password> C:\path\to\build\Release engineer-a
```

Append capture A, capture B, and render endpoint IDs after the slot to pin the
selection. Concurrent engineers must use different slots. A remote exclusive
lock makes an accidental duplicate slot fail without killing or reusing another
run's service. The wrapper finds the logged-in `explorer.exe` owner and launches
the gate as an interactive scheduled task so WASAPI sees the same endpoints as
the desktop. If more than one desktop user is logged in, set
`SAR_MULTI_ENDPOINT_INTERACTIVE_USER` before invoking the CMD wrapper or pass
`-InteractiveUser` to the PowerShell wrapper.

The wrapper copies only the acceptance script to the remote machine, runs it
against the supplied existing build, and copies evidence back to
`.sar-evidence/multi-endpoint-<slot>-<timestamp>-<run-id>`. It neither checks out
source remotely nor installs software.

## Evidence Contract

A successful evidence directory contains:

- `devices.log` and `devices.json`: raw and parsed endpoint inventory;
- `selected-endpoints.json`: the exact two-capture/one-render selection;
- `runtime-configure-matrix.log` and runtime state records;
- `diagnostics-before.log`, `diagnostics-after.log`, and
  `diagnostics-restored.log`;
- `session-running.sars` and `session-restored.sars` with a verified stable
  SHA-256 across read-only restart;
- first and restored service stdout/stderr;
- `result.json`, plus WinRM wrapper manifest, preflight, console, and result
  records for remote runs.

The gate requires accepted responses, matrix mode, a running runtime, increasing
`processed_blocks`, the expected diagnostic counters, successful auto-start
after abrupt service restart, an unchanged session hash, and cleanup of the
exact service processes it started. XRUN and FIFO counters are collected as
evidence but are not required to be zero; product-quality thresholds should be
applied by a later soak policy with hardware-specific limits.

## WinRM Error 12152

The wrapper probes TCP 5985 and then calls `Test-WSMan` before creating a remote
session. Error 12152 is reported as
`winrm_http_invalid_server_response_12152`: the port answered, but its HTTP
response was not a valid WSMan response. This is an infrastructure block, not an
audio test failure.

From the remote console, run:

```bat
winrm enumerate winrm/config/listener
winrm quickconfig
netstat -ano | findstr :5985
```

Confirm that the listener is enabled and port 5985 belongs to WinRM/HTTP.sys,
remove any proxy or port-forward intercepting the port, then retry from the
development machine:

```powershell
Test-WSMan 192.168.123.123
```

The failed run retains `winrm-preflight.json` with TCP reachability, native
codes, the original error chain, and the recommended action.

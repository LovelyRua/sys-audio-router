# Public WASAPI matrix acceptance

This checks the product engine against installed public Windows audio endpoints.
It does not install a driver. Use the dedicated driver lab with a logged-on
desktop session and a fresh output directory for every run.

The source application renders to virtual bus A. SAR captures bus A, routes its
two channels through the matrix, and renders to bus B. The probe captures bus B.
Supply four distinct endpoint IDs so a direct loopback cannot substitute for the
matrix path. Select IDs from the installed driver's parent-attributed inventory;
display names alone do not establish ownership.

The CI artifact `system-audio-route-matrix-lab-tools` contains the matching engine,
control CLI, scripts, licenses, and build identity. A compatible independent
`wasapi_bridge_probe.exe` is supplied by the MS-PL driver project. The programs
communicate through public WASAPI endpoints and text reports only.
Use a probe build that emits `first_channel_power`, `second_channel_power`, and
`nonzero_samples`; older reports fail with a missing-field reason. Separate tone
powers prove both channels, while a zero nonzero-sample count proves the disabled
route control is silent even at frequencies outside the generated tones.

```powershell
scripts/windows-virtual-wasapi-matrix-preflight.ps1 `
  -BuildPath ./build/Release `
  -CaptureDeviceId '<bus-A-capture-id>' `
  -RenderDeviceId '<bus-B-render-id>' `
  -SignalProbePath '<driver-lab>/wasapi_bridge_probe.exe' `
  -ProbeRenderDeviceId '<bus-A-render-id>' `
  -ProbeCaptureDeviceId '<bus-B-capture-id>' `
  -ExerciseRouteTransitions `
  -OutputDirectory '<new-result-directory>'
```

The transition run keeps one engine instance and runtime open for four phases:

1. Routes absent: capture must contain silence while the probe sends audio.
2. Routes enabled: both channels must contain the expected tones with low crosstalk.
3. Routes removed: capture must return to silence.
4. Routes re-enabled: both channels must carry the expected tones again.

Each phase requires captured frames, capture packets, and advancing engine
processing counters. Missing, duplicate, non-finite, or oversized probe evidence
fails the run. Probe/CLI timeouts and oversized output also fail and terminate
only the owned child process. The engine receives `runtime-stop` and `--stop`;
forced cleanup after a failed graceful stop always makes acceptance fail.

`result.json` includes every completed phase, binary SHA-256 hashes, and shutdown
status. Per-phase JSON and bounded stdout/stderr logs remain beside the session,
graph, inventory, and engine log. Use a new directory rather than overwriting a
previous report. Without `-SignalProbePath`, a successful preflight proves only
endpoint setup and advancing matrix processing, not sound transfer.

A content pass is not a continuity or soak pass. Discontinuity counts remain
visible in the evidence and must be assessed separately with the driver project's
strict timing/level probe. This lab workflow is also not GUI or installer
acceptance and does not make the experimental driver a shipped product feature.

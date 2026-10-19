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

For additional steady capture timeline acceptance, supply
`-RequireCaptureTimeline` with the updated MMCSS bridge probe. The probe retains
raw discontinuity counts, excludes the first 24000 captured frames from its
timeline, and observes packets only before 2.5 seconds of wall time (before the
intentional tone tail). Require at least 90000 measured frames, MMCSS registration,
and zero steady discontinuities, timestamp errors, position gaps/overlaps, and
QPC regressions. A missing field or insufficient coverage fails closed. This
checks the receiving endpoint timeline, not waveform continuity inside the
matrix or a long-duration soak. The final second is actively supplied with
silence rather than leaving the source stream starved.

## VM24 evidence: 2026-10-19

Interactive run `c2ab6d6510994237a8e4264b249bb5bc` passed both same-adapter
and cross-adapter cases, each with the four phases above. Both engines stopped
gracefully with exit code zero. Disabled phases captured actual packets with
zero nonzero PCM16 samples; enabled phases detected both expected channel tones.
Local raw reports are retained under the workspace artifact directory, not Git.

| Case | Captured frames per phase | Discontinuity packets per phase |
| --- | --- | --- |
| Same adapter | 192000 / 191520 / 191520 / 191616 | 1 / 1 / 1 / 2 |
| Cross adapter | 159936 / 158976 / 159936 / 121632 | 3 / 3 / 3 / 5 |

This is a content/control milestone, not evidence of stable timing. In particular,
the cross-adapter capture coverage and discontinuities require separate strict
continuity analysis before claiming glitch-free operation.

The engine and CLI came from successful main package run `37250595420`, source
`68ff15ab691143b5fc4c34bfd71c974c73bf4e2d`. Git confirmed no changes in `core`,
`tools`, `app`, or root `CMakeLists.txt` between that source and this acceptance
branch. Scripts came from `7c1cc0a1b559d46c5e6743056f7e268c1a3a3dfc`;
the independent probe came from driver source
`9943f7ea558a73e2bec8e810e3b1d1afeee0c217`.
Each result records binary and script hashes for reproduction. No installer was
run and no driver or host software was installed during this round.

An earlier run passed its per-case sound checks but the outer runner reported
failure because success had no explicit script exit code. The preflight now
returns zero explicitly; the run above repeated both cases after that fix.

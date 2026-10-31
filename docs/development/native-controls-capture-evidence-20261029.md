# Native controls and capture evidence

This is an internal stabilization pass, not a preview release or audio-quality
acceptance. Product changes are in `9846a4c13234b13842880c8ecda4c498ab240b58`.

## Repairs and automated checks

- Restore normal and pressed button backgrounds/frames using VSTGUI's native
  button renderer. A null gradient bypassed both the fill and frame in that
  renderer. Matrix cells retain their own drawing implementation.
- Hide cells and labels outside the actual matrix page. Hide unnecessary page
  navigation, and dim unavailable inspector actions consistently with their
  input state. Empty and partial pages have portable regression coverage.
- Extend the public WASAPI level probe with significant/nonzero frame counts,
  signal packet counts, first/last significant frame, peak location and maximum
  adjacent-sample step. Frame positions are received-frame offsets, not device
  timestamps or evidence that the stream was continuous.
- Count finite samples separately from NaN/Inf. Exclude unused buffer capacity;
  reset adjacent-sample history across invalid samples. Tests cover bounds,
  cross-packet steps, a single stereo impulse, and subthreshold values.
- Include the probe and its SHA256 in the internal CI lab artifact.

CI runs `40850117056` (push) and `40850126374` (PR) succeeded, including the full
Windows build/test and advisory AddressSanitizer jobs. Windows package run
`40850126434` also succeeded. These do not close the real-audio blockers below.

## Interactive native GUI

The matching preview was run in the dedicated lab's codex desktop session.
Its fresh engine session had four inputs, four outputs and four routes.

- Only the 4x4 matrix appeared; unused page cells/navigation were absent.
- Normal buttons had visible frames/fills; unavailable inspector actions were
  dimmed before selection.
- Maximize retained access to the inspector. Right-click selected the existing
  ASIO Out 1 -> render L route without toggling it.
- Mute changed both the cell to MUTE and the action to Unmute; polling retained
  the change. Unmute restored ON and the Mute action.
- The GUI closed normally through its actual window close button. The remote
  `Process.CloseMainWindow()` helper did not close it; that helper failure must
  not be reported as a successful automated-close test.

This is not complete layout/DPI, keyboard, accessibility or feature-parity
acceptance. Presets, diagnostics and configuration still need a coherent
supported entry point before replacing the existing default panel.

## Actual REAPER -> SAR -> VB-CABLE

The engine and ASIO DLL from the previously validated `dbc5d58` build were kept
constant while the new `9846a4c` probe measured the stream. An isolated REAPER
7.41 configuration selected SAR at 48 kHz / 128 samples, with a generated
stereo 1 kHz, -18 dBFS tone. No system-default endpoint was changed.

Probe SHA256:
`FDC32223FC8A4ACC8F7C869786D0D01AAA767E4EAD6870BDC09CD039BFF4ECB2`.

| Phase | Received frames | Peak | Significant frames | Notes |
| --- | ---: | ---: | ---: | --- |
| Before playback | 138048 | 0 | 0 | REAPER open |
| Actual playback | 143424 | 0.125888824 | 133788 | RMS 0.085984263 |
| First capture after stop | 124896 | 0.0454216003 | 1 | Two nonzero samples, one stereo frame |
| Repeated stopped capture | 141216 | 0 | 0 | REAPER still open; GUI closed |

The post-stop frame was at received offset 1510, packet 3. This confirms a
single-frame event rather than sustained playback in that capture. It does not
identify whether the event originated in SAR, the DAW, Windows conversion or
the virtual cable. The repeated capture differs in both time and GUI state,
so it cannot isolate the GUI as a cause.

**Playback is not accepted as clean.** The maximum adjacent-sample step was
0.235363007 at offset 37310. That is substantially larger than the approximately
0.0165 maximum step of this generated 1 kHz tone at its original sample rate.
The engine also reported 558 producer underflows at one diagnostic snapshot,
despite zero reported xruns/drops and no nonfinite samples. Aggregate counters
alone cannot attribute these events or prove that every underflow was audible.
Timestamped capture and producer timing evidence are still needed.

One earlier playback attempt was blocked by REAPER's "previously failed while
loading project" dialog. Its zero-level captures are excluded from playback
acceptance. After acknowledging the generated test project, actual transport
movement and -18 dBFS DAW meters were observed before the valid capture above.

## Public WASAPI control without SAR

After normal REAPER and engine shutdown, the lab's existing
`wasapi_bridge_probe --run` passed directly between VB-CABLE's public render
and capture endpoints: 142752 frames sent, 180096 captured, finite target
power `1.06242e+17`, exit 0. Three subsequent independent captures each received
144480 frames with zero peak/nonzero frames and no runtime/nonfinite errors.

This old probe has no modern continuity telemetry and closes its render client
before the follow-up captures. It is a useful positive signal/quiet control,
not an equivalent comparison with REAPER holding its ASIO client open, and
does not exonerate or implicate VB-CABLE. An attempted `--stereo` invocation was
unsupported by that installed probe and is explicitly excluded.

## Cleanup and next gate

Only owned temporary ASIO registration was removed. The final audit found zero
REAPER/engine processes, zero owned REAPER tasks and no temporary machine/user
SAR ASIO registration. The native GUI task was removed after normal window
closure. Existing lab media devices remained healthy. Raw evidence is retained
in the ignored stabilization artifacts, not committed user configuration.

Next: capture continuously across play/stop with timestamps and compare the
ASIO producer queue against the outgoing WASAPI signal. Do not hide startup
frames or soften the warning counters merely to make the result green.
Fresh installer/upgrade, recording into a DAW, and complete GUI parity remain
open. No user-facing installer was delivered by this pass.

## Follow-up: continuous capture with the GUI absent

A fresh isolated engine/REAPER session used the same engine, DLL and probe
binaries. No SAR GUI was launched. After REAPER had finished opening its test
project, one 30-second capture was started before a 20-second ReaScript
play/stop cycle. The capture endpoint was not reopened at the stop boundary.
Actual transport movement and -18 dBFS DAW meters were observed.

- Received frames: 1298016; elapsed 30037 ms; packets 2717.
- Peak: 0.125888824; RMS: 0.0747864014; significant frames: 916194.
- First/last significant received-frame offsets: 129828 / 1079728.
- Maximum adjacent-sample step: 0.239673615 at offset 753242, inside the
  significant-signal interval rather than at its start or end.
- Probe runtime errors, nonfinite samples and timeouts: zero.
- The later engine snapshot reported 319 producer underflows, zero drops,
  zero overflows/xruns and callback peak 252.7 us. These are cumulative values,
  not time-correlated attribution of that particular sample step.
- REAPER's final script report was stopped, at playback position
  19.178666666665 seconds. Wall-clock versus audio-clock advancement and the
  received-frame deficit need timestamped investigation; neither a frame
  offset nor the aggregate duration proves a specific timeline gap.

The oversized playback step therefore reproduces without the GUI and without
reopening the capture endpoint during the measurement. This does not yet locate
the fault in the ASIO producer, engine queue, Windows conversion or VB-CABLE.
Do not close the audio-continuity blocker or attribute it solely to GUI load.

An earlier attempt started while REAPER was still initializing and received
only zero samples. It is excluded. The valid attempt above was repeated after
the actual project window was ready. The isolated project was then saved and
closed normally, the temporary ASIO registration withdrawn, and the engine
stopped via `--stop`.

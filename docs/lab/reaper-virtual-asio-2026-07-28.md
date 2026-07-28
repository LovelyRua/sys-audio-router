# REAPER Virtual ASIO Acceptance - 2026-07-28

## Environment

- Windows `10.0.26100.4770`
- REAPER `7.78 x64`
- System Audio Route commit `7d4d21d`
- Virtual ASIO format: 48 kHz, stereo input/output, 128 frames
- Physical render endpoint: `CABLE Input`
- Physical observation endpoint: `CABLE Output`

## Results

The Windows suite passed all 118 CTest targets from a fresh remote source
archive and build directory.

REAPER's Device preferences selected `System Audio Route Virtual ASIO` with two
inputs, two outputs, 48 kHz, and a 128-frame request. The REAPER process loaded
`SystemAudioRouteVirtualASIO.dll` from the tested build. After three seconds the
engine reported one active ASIO producer, 1,247 pushed blocks, 1,247 consumed
blocks, 1,247 mixed blocks, and zero dropped blocks.
The new WinRM acceptance helper repeated the gate from a clean process state
with 1,140 pushed, consumed, and mixed blocks, zero drops, zero xruns, and a
90.3-microsecond peak graph callback, then removed both processes and temporary
Scheduled Tasks.

The preceding bounded host-probe physical loop generated a 440 Hz, -24 dBFS
signal through the same production driver and engine bridge. `CABLE Output`
captured 160,896 frames with peak `0.0630989075`, RMS `0.0327823061`, zero
non-finite samples, zero silent cycles, zero timeout, and zero capture error.
The engine reported 2,334 pushed blocks, 2,333 consumed and mixed blocks, zero
dropped blocks, zero xruns, and an 18.6-microsecond peak graph callback.

## Defect Closed

The first real-machine run exposed a render-only starvation loop when the
external ASIO source was temporarily empty. The runner skipped the WASAPI event
wait, spun on its MMCSS thread, and starved the ASIO callback worker. Render-only
external-input operation now uses the render endpoint as its clock even before
source data arrives, while initial silence priming remains opt-in. Synthetic
coverage and this real-machine acceptance both pass with the fix.

## Remaining DAW Gates

- Run the scripted acceptance against a second target DAW.
- Run two real DAWs concurrently for the planned duration gate.
- Add long-duration per-client cadence and clock-adaptation evidence.

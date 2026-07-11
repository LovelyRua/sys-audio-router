# Current System Architecture

This document summarizes the code that exists now. Strategy documents describe
where the project is going; this file describes the current executable shape.

## Current Stage

The project is still pre-alpha. The portable realtime core, graph execution
prototype, control session shell, Windows WASAPI stream shell, graph runner,
realtime worker, render and duplex loop wrappers, sample conversion helpers,
and smoke-test harness are in place.

The next major milestone is the first measured real-device loop:

```text
WASAPI capture/render stream
  -> WindowsWasapiGraphRunner
  -> Graph::process
  -> WindowsWasapiRealtimeWorker on an MMCSS thread
```

## Portable Core

`core/realtime` contains fixed-size planar float audio buffers and process
context structures. The graph currently consumes and produces
`realtime::AudioBuffer`.

`core/graph` contains:

- `Graph`: linear graph executor with diagnostics updates.
- `Node`: realtime processor interface.
- `GainNode`, `MuteNode`, and passthrough behavior used by smoke tests.
- Lock-free finite float parameter publishing for scalar node controls.
- Graph builder and graph snapshot publishing. Snapshot processing uses an
  allocation-free raw-pointer read on the realtime path; graph ownership stays
  on the publisher/control side, which waits for an audio reader quiescence
  point before releasing the previous snapshot.
- Route matrix core for channel routing and summing experiments.

The graph is still intentionally simple. Branching graph execution, bus summing,
latency compensation, and multi-bus graph execution are future work.

## Control And Diagnostics

`core/control` validates preset documents and control commands before applying
them to non-realtime state. `PresetDocument` can build a route matrix graph for
the currently supported single-matrix preset shape. `ControlSession` now owns the
current preset, the active graph snapshot publisher, virtual endpoint registry,
and next graph version. It handles:

- Loading and saving presets.
- Querying diagnostics, active graph summaries, devices, and full session state.
- Creating and removing virtual endpoint descriptors.
- Applying route, gain, and mute edits.
- Applying batches of preset mutations atomically, with one graph publication on
  success and no state mutation on failure.

The current session state model is still in-process. IPC, persistence, UI
binding, and service hosting are future work.

`core/diagnostics` tracks graph version, processed blocks, callback duration,
peak callback duration, and xrun count. The worker mirrors per-run xrun totals
and last, peak, total, and average callback duration without cross-thread reads
of the mutable engine diagnostics. WASAPI worker summaries now classify
runtime health across stopped, healthy, degraded, and faulted states, including
split capture/render wait timeouts, partial transfers, silent capture, stream
start/stop failures, processing failures, capture data discontinuities, capture
timestamp errors, last transferred frame counts, and last stop wait duration.
WASAPI capture discontinuities also increment the engine xrun counter.
Diagnostics will still need to expand as real loops expose render underrun,
clock drift, and end-to-end latency behavior.

## Platform Layer

`core/platform` contains the platform-facing pieces:

- `AudioDeviceProvider` and descriptor validation.
- `AudioFormat`, including sample rate, channel count, block size, bit depth,
  and native sample format.
- `SampleConverter`, currently covering float32, int16 PCM, packed int24 PCM,
  int24-in-int32 PCM, and int32 PCM interleaved buffers to/from planar float
  buffers.
- `VirtualEndpointRegistry`, the current model shell for future virtual devices.
- Windows WASAPI device enumeration.
- Windows WASAPI stream probing.
- Windows WASAPI stream lifecycle and single-cycle buffer pumping.
- Windows WASAPI graph runner.
- Windows realtime worker shell.
- Windows render-only loop wrapper for the first default-output device path.
- Windows duplex loop wrapper for the first default capture/render path.
- Windows MMCSS realtime thread scope.

## Windows WASAPI Flow

`WindowsWasapiStream` currently supports:

- Default endpoint probing.
- Device-ID endpoint probing and native stream opening for explicit hardware
  selection.
- Default render-endpoint loopback probing and capture stream opening.
- Shared-mode WASAPI initialization.
- Event-driven stream handles.
- `IAudioRenderClient` and `IAudioCaptureClient` ownership.
- `IAudioClock` ownership with allocation-free raw position, frequency, and QPC
  snapshots for future drift and latency analysis.
- Explicit endpoint/loopback stream mode diagnostics.
- Render priming with a silent buffer.
- Start/stop lifecycle.
- Single-cycle `render_once` and `capture_once` calls that can be woken by the
  stream-owned stop event during realtime shutdown.

`WindowsWasapiGraphRunner` orchestrates one processing cycle:

1. Optionally capture from a WASAPI input stream.
2. Run `Graph::process`.
3. Optionally render to a WASAPI output stream.

`WindowsWasapiRealtimeWorker` runs the graph runner in a background thread and
enters MMCSS `Pro Audio` priority through `WindowsRealtimeThreadScope`. It
waits for COM, MMCSS, and stream startup to complete before `start()` reports
success, then publishes worker stats and last errors for non-realtime runtime
summaries. `stop()` requests the graph runner to signal each native WASAPI
stream's stop event before joining the worker thread, so event-driven render and
capture waits wake promptly without being counted as wait timeouts.

`WindowsWasapiRenderLoop` owns a default render stream, graph runner, and
realtime worker. It is the current high-level entry point for the first measured
render-only real-device loop. Its summary includes render stream diagnostics,
worker counters, and runtime health.

`WindowsWasapiDuplexLoop` owns default capture and render streams, a graph
runner, and a realtime worker. It is the current high-level entry point for the
first measured full-duplex real-device loop. Its summary includes capture/render
stream diagnostics, worker counters, and runtime health.

The Windows command-line tools can inspect endpoints and run short real-device
measurements for render-only and full-duplex WASAPI paths. These tools print
runtime health, reason codes, stream diagnostics lines, stream shape,
transferred-frame summaries, stop wait duration, partial/silent transfer
counters, capture discontinuity/timestamp counters, last-cycle flags, worker
counters, and engine diagnostics for lab captures.

## Current Testing Model

The Windows CTest suite currently has 41 smoke targets. Several tests are
synthetic because WinRM sessions may not expose interactive audio endpoints even
when the VM has a desktop audio stack.

Use the WinRM test script for full validation:

```bat
scripts\windows-winrm-test.cmd <host> <user> <password> <slot>
```

Use a unique slot per engineer for concurrent runs, such as `engineer-a` or
`engineer-b`. Keep credentials out of git.

## Known Gaps

- No always-on real-device render/capture loop has been measured yet.
- Loopback capture has a native stream API but not yet a dedicated high-level
  loop wrapper or measurement tool.
- No virtual ASIO driver implementation exists yet.
- No virtual WDM/WASAPI driver implementation exists yet.
- No UI exists yet.
- No plugin hosting exists yet.
- Graph execution is still linear.
- Control sessions are in-process only; no IPC/service boundary exists yet.
- Preset-to-graph build currently supports one route matrix node with matching
  matrix input/output counts.
- Sample conversion does not yet cover unusual byte orders or non-PCM encoded
  formats.
- Drift, underrun, overrun, and end-to-end latency diagnostics are still early.

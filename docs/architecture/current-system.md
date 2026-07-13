# Current System Architecture

This document summarizes the code that exists now. Strategy documents describe
where the project is going; this file describes the current executable shape.

## Current Stage

The project is still pre-alpha. The portable realtime core, graph execution
prototype, control session shell, Windows WASAPI stream shell, graph runner,
realtime worker, render, duplex, and loopback loop wrappers, sample conversion
helpers, and smoke-test harness are in place.

The first measured real-device loops now execute this path:

```text
WASAPI capture/render stream
  -> WindowsWasapiGraphRunner
  -> Graph::process
  -> WindowsWasapiRealtimeWorker on an MMCSS thread
```

Render-only measurements are strict-healthy, while full-duplex measurements
still expose independent-device clocking behavior. Render-master scheduling now
drives a bounded FIFO waterline controller and adaptive capture resampler; the
next backend milestone is long-duration tuning and discontinuity recovery.

## Portable Core

`core/realtime` contains fixed-size planar float audio buffers and process
context structures. The graph currently consumes and produces
`realtime::AudioBuffer`.
`realtime::PlanarAudioFifo` provides fixed-channel, fixed-capacity buffering for
a single-threaded realtime runner. It allocates only during construction;
partial `push` and `pop` calls return the transferred frame count so overflow
and underflow policy remain with the caller. Render paths can use allocation-free
`peek` and `consume` calls when output must be committed in two stages.
Its SPSC queue exposes a destination-based dequeue for realtime consumers, so
successful reads do not construct an optional result object.

`core/graph` contains:

- `Graph`: linear graph executor with diagnostics updates.
- `Node`: realtime processor interface.
- `GainNode`, `MuteNode`, `MeterNode`, and passthrough behavior used by smoke tests.
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

`core/realtime/audio_block_timeline.h` defines the portable clock-boundary data
model. A `ClockDomain` gives an independently paced sample clock a stable ID and
nominal sample rate. An `AudioBlockTimeline` locates one non-empty block by frame
position within that domain and classifies same-domain blocks as overlapping,
contiguous, or separated by a gap. Blocks from different domains are explicitly
not comparable. A future clock-domain bridge must therefore retain source and
destination timelines separately; buffering policy and ASRC remain outside this
model.

`core/realtime/clock_drift_estimator.h` provides the portable, allocation-free
measurement primitive for one clock domain. Given two strictly ordered frame/QPC
samples, it reports the observed sample rate and its error from the domain's
nominal rate in parts per million. Zero elapsed time, frame or QPC rollback and
wraparound, domain mismatch, invalid nominal rates, and non-finite calculations
produce an invalid estimate instead of a partial result.

`FifoWaterlineController` converts bounded FIFO fill error into a slew-limited
parts-per-million correction. `AdaptiveResampler` owns a preinitialized
libsamplerate sinc converter and applies a caller-supplied ratio without
allocating or constructing diagnostics in `process()`. The duplex capture path
now accumulates complete graph blocks from bounded SRC offers, consumes only
reported input frames, and resets/re-primes the bridge after discontinuities.
Its wider duplex correction range covers unusually large independent-clock
error observed in virtualized and commodity Windows audio stacks while the
portable controller keeps conservative defaults.

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
- Windows loopback capture wrapper for routing default system output into the
  graph without feeding it back to the same render endpoint.
- Windows MMCSS realtime thread scope.

## Windows WASAPI Flow

`WindowsWasapiStream` currently supports:

- Default endpoint probing.
- Device-ID endpoint probing and native stream opening for explicit hardware
  selection.
- Default render-endpoint loopback probing and capture stream opening.
- Shared-mode WASAPI initialization.
- Windows Audio Engine sample-rate conversion in shared mode, allowing the
  default 44.1 kHz capture endpoint to run with a 48 kHz render endpoint.
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
render-only real-device loop. The single-ended render and loopback paths use
fixed-capacity realtime FIFOs so device transfer sizes can differ from graph
block sizes without losing frames. Its summary includes render stream
diagnostics, worker counters, and runtime health.

`WindowsWasapiDuplexLoop` owns default capture and render streams, a graph
runner, and a realtime worker. It is the current high-level entry point for the
first measured full-duplex real-device loop. Its summary includes capture/render
stream diagnostics, worker counters, and runtime health. The duplex capture
FIFO reserves two native-buffer spans beyond the graph block so a delayed
capture service cycle can drain queued packets without immediately exhausting
bridge capacity. The adaptive controller still targets one graph block of fill;
the additional capacity is catch-up reserve, not added steady-state latency.

`WindowsWasapiLoopbackLoop` owns a capture-only loopback stream, graph runner,
and realtime worker. It exposes the underlying WASAPI clock snapshot and keeps
the graph output unconnected until a deliberate render or virtual-endpoint
route is selected.

The Windows command-line tools can inspect endpoints and run short real-device
measurements for render-only, full-duplex, and loopback-capture WASAPI paths.
These tools print
runtime health, reason codes, stream diagnostics lines, stream shape,
transferred-frame summaries, stop wait duration, partial/silent transfer
counters, capture discontinuity/timestamp counters, adaptive-capture recovery
state and recovery-silence totals, last-cycle flags, worker counters, and engine
diagnostics for lab captures. Recovery silence remains part of total render FIFO
underflow and is also reported as a labeled subset, so a lab run preserves the
full dropout count while identifying the portion caused by SRC re-priming.

Current real-device evidence includes a strict-healthy 48 kHz render run that
submitted 96,000 frames over two seconds with zero xruns, wait timeouts, or FIFO
faults. A five-second shared-mode duplex run connected the default 44.1 kHz
capture endpoint to the 48 kHz render endpoint and processed approximately
240,000 render-domain frames. That duplex run still reported capture data
discontinuity and render underflow, so it is validation of the SRC-enabled path,
not yet evidence of production stability.

A later 30-second duplex run with a wider controller range completed without
FIFO overflow and kept capture correction between 0 and about -508 ppm, ending
near -454 ppm. It observed six capture discontinuities and fourteen render FIFO
underflow cycles; the recovery-silence counters were added so subsequent runs
can separate SRC re-priming gaps from unrelated render starvation.

On the same test machine, adding the bounded capture drain reserve reduced a
10-second run from four to three capture discontinuities and from nine to five
render FIFO underflow cycles, with zero FIFO overflow. Three additional
five-second runs totalled four discontinuities and eight underflow cycles, also
without overflow. This is repeatable short-run improvement, while long-duration
evidence is still required before treating the tuning as production-stable.

## Current Testing Model

The Windows CTest suite currently has 62 smoke targets. Several tests are
synthetic because WinRM sessions may not expose interactive audio endpoints even
when the VM has a desktop audio stack.

Use the WinRM test script for full validation:

```bat
scripts\windows-winrm-test.cmd <host> <user> <password> <slot>
```

Use a unique slot per engineer for concurrent runs, such as `engineer-a` or
`engineer-b`. Keep credentials out of git.

## Known Gaps

- Full-duplex adaptive capture resampling is wired and bounded, but controller
  tuning and discontinuity recovery still need long-duration real-device
  evidence. Hardware capture discontinuities can still trigger render underflow
  while the bridge resets and re-primes.
- Multi-hour real-device stability has not been demonstrated yet.
- Loopback capture is not yet connected to a selectable render destination or
  virtual endpoint.
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

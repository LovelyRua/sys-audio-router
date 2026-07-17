# Current System Architecture

This document summarizes the code that exists now. Strategy documents describe
where the project is going; this file describes the current executable shape.

## Current Stage

The project is still pre-alpha. The portable realtime core, graph execution
prototype, control session shell, Windows WASAPI stream shell, graph runner,
realtime worker, render, duplex, and loopback loop wrappers, sample conversion
helpers, a first named-pipe engine control service, a bounded control wire
protocol, a mock Virtual ASIO transport, and the smoke-test harness are in place.

The first measured real-device loops now execute this path:

```text
WASAPI capture/render stream
  -> WindowsWasapiGraphRunner
  -> Graph::process
  -> WindowsWasapiRealtimeWorker on an MMCSS thread
```

Render-only measurements are strict-healthy, while full-duplex measurements
still expose independent-device clocking behavior. Render-master scheduling now
drives a bounded FIFO waterline controller and adaptive capture resampler, with
native-clock feed-forward supplying the measured clock-rate difference and the
FIFO controller correcting residual fill error. The feed-forward path has short
real-device evidence only; default-endpoint reopen and pinned-route recovery
through a Windows Audio service outage now have short hardware passes, while
bounded discontinuity recovery, physical pinned-device removal,
render-deadline ordering, and long soaks remain alpha gates.

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

`EngineControlService` now owns a `ControlSession` behind a versioned, bounded
binary protocol. On Windows, `sar_engine_service` hosts that control surface on
a named pipe and `sar_control_cli` can query state, graph, and diagnostics or
apply gain and mute commands. The pipe path is control-plane only; audio data is
not copied through it. Persistence, service installation, concurrent client
policy, UI binding, and ownership of a live WASAPI runtime are still future work.

`MockAsioTransport` is the first engine-side Virtual ASIO transport experiment.
It preallocates fixed-format client-to-engine and engine-to-client block queues,
keeps push/pop allocation-free and lock-free, tracks drops, underruns, sequence
discontinuities, and connection generations, and has a two-thread stress smoke.
It is not an ASIO driver and does not register a DAW-visible device.

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

The duplex loop also samples native capture and render `IAudioClock` positions
on a non-realtime control thread every 500 ms. Relative observed-rate error
becomes a smoothed, slew-limited feed-forward term; the FIFO controller
continues to correct residual fill error. The graph runner combines both terms
and clamps final ASRC correction to +/-2500 ppm. Its realtime path performs only
a lock-free atomic load, while COM calls, sleeping, and filtering remain on the
observer thread. Three consecutive invalid samples disable feed-forward and
restore the previous FIFO-only behavior.

This is the current clock feed-forward implementation, not yet an alpha exit
result. The summary exposes the current feed-forward value and whether it is
valid at the instant of the query, but does not yet count valid, invalid, or
disabled observer samples or time spent at the correction clamp. Those counters
are required to apply the roadmap's long-soak thresholds.

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
- Windows duplex supervisor that rebuilds the complete stream, runner, worker,
  and clock-observer runtime after classified recoverable failures.
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
- Native-rate shared-mode capture opening for duplex streams. The internal
  adaptive resampler applies nominal capture-to-render conversion before its
  bounded clock/FIFO ppm correction, avoiding a second Windows Audio Engine
  conversion stage.
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
bridge capacity. The adaptive controller targets the native-capture frame
equivalent of one graph block of fill; the additional capacity is catch-up
reserve, not added steady-state latency.
Native duplex streams use one coordinated wait over both samples-ready events
and both stop events. The selected auto-reset samples-ready event is latched in
its stream, so the subsequent zero-time capture or render pump does not wait for
the same event twice. Pending readiness also forces the next coordinated wait
to be nonblocking until the corresponding stream has consumed it. Scripted and
synthetic stream implementations retain the existing sequential wait path.
Queued render audio is submitted before a capture burst when both directions
are ready. The refill that follows remains bounded by the configured FIFO target.

`WindowsWasapiDuplexSupervisor` is a control-plane owner above the complete
duplex loop. It classifies failures, synchronously quiesces and destroys the old
runtime, then rebuilds it with 0/500/3000 ms retry delays and a five-second hard
recovery deadline. It is currently driven by explicit control-plane `tick()`
calls. A lock-free `IMMNotificationClient` generation/event source and an
independent capture/render endpoint-selection policy feed the supervisor on the
control thread. Follow-default generation changes settle for 300 ms so paired
capture/render notifications coalesce into one bounded duplex reopen, while
pinned directions ignore unrelated default-device changes. Snapshot consumption
uses a read-reset-read protocol so event-reset races cannot lose a generation.
Only `eConsole` default-role changes are observed because the current stream
probe path resolves that role; multimedia and communications role changes do not
cause unrelated reopen episodes.

The production supervisor factory enumerates devices once per open attempt,
resolves capture and render as one pair, and opens the duplex runtime by those
explicit IDs. Follow-default and pinned selections can be mixed independently.
Each retry repeats resolution; a successful start publishes the actual capture
and render probe IDs, while quiesce and stop clear them.

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
state, startup-, capture-starvation-, and recovery-silence totals, the maximum
silence frames for one recovery episode, last-cycle flags, worker counters, and
engine diagnostics for lab captures. These silence counters remain part of total
render FIFO underflow and are also reported as mutually exclusive labeled
subsets. The duplex acceptance gate fails if any underflow frame is left
unattributed. The supervisor retains the recovery episode maximum across child
runtime replacement. The recovery acceptance script can enforce an explicit
maximum while leaving the gate disabled for legacy logs that do not contain the
field.

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

A repeatable recovery measurement switched the default capture and render
endpoints from the High Definition Audio device to VoiceMeeter and then back.
The 300 ms notification-settle window coalesced each direction pair into one
duplex reopen. The supervisor rebuilt twice and finished with two successful
recoveries, no failed recovery, no notification-reset failure, and no retained
worker error. The maximum reported recovery duration was 590 ms against the
five-second gate, and the final endpoint IDs matched the restored High
Definition Audio endpoints.

A separate pinned-endpoint measurement stopped Windows Audio for two seconds
while the High Definition Audio endpoint IDs remained selected. The original
0/250/1250 ms retry schedule exhausted all attempts before the service returned.
Spreading the same three attempts across 0/500/3000 ms recovered the unchanged
pinned IDs in 3548 ms with one successful recovery, no failed recovery, no
notification reopen, and no retained error. This validates the runtime-failure
path and retry window, but neither experiment proves physical unplug/replug or a
disappearing pinned endpoint.

Coordinating capture and render event waits reduced two subsequent 10-second
runs to two/one capture discontinuities, three/one render FIFO underflow cycles,
and six/eight wait timeouts respectively. Both runs transferred about 482,400
render frames with zero FIFO overflow. Their approximately 1,900 worker cycles
per run match the combined cadence of two roughly 100 Hz device events rather
than an unbounded polling loop.

Hardware-clock feed-forward remained valid during a later 30-second duplex run.
Its final feed-forward was about +125 ppm, while total ASRC correction stayed
between about -589 and +68 ppm. The run transferred roughly 1.44 million frames
with zero FIFO overflow, five capture discontinuities, and three render FIFO
underflow cycles. A preceding 10-second run after feed-forward slew limiting
reported one discontinuity, three underflow cycles, and a total correction
range of about -835 to 0 ppm.

Three consecutive 10-second duplex acceptance runs on the 48 kHz render path
used a 20 ms event-wait timeout and each rendered more than 99% of the
duration-derived target with zero render wait timeout, FIFO overflow, process
error, or unattributed underflow frame. The runs rendered 481,920, 483,360, and
482,400 frames. Their maximum per-discontinuity recovery silence was 2,304
frames. The hardware threshold is 2,594 frames: one 1,058-frame capture target
fill, one 1,056-frame render buffer, and one 480-frame render period so the
completed recovery can be observed at the next discrete callback boundary.
Startup, discontinuity recovery, and normal-state capture starvation remain
visible as separate counters rather than being treated as render deadline
misses.

The WinRM measurement workflow now accepts a pinned capture/render endpoint pair
and writes a local evidence bundle containing the source commit, thresholds,
endpoint IDs, per-attempt command and combined output, and strict soak summary.
Each duplex attempt independently checks configurable render-frame coverage,
wait and process errors, FIFO overflow, exact underflow attribution, and the
per-recovery silence maximum. A three-attempt 5-second validation deliberately
retained a failed third attempt and copied its complete evidence home. A
subsequent pinned 10-second 44.1-to-48 kHz run passed the 99.99% coverage gate,
rendering 482,016 frames against a 480,000-frame target with zero timeout,
overflow, or unattributed underflow and a 2,016-frame recovery maximum against
the 2,594-frame bound. That run used the previous Windows Audio Engine SRC
opening strategy and reported 158 capture discontinuity cycles. A native 48 kHz
capture control run reported one discontinuity cycle, motivating native-rate
capture. The first equivalent 10-second 44.1-to-48 kHz run through the new
internal nominal-rate path reported six discontinuity cycles, zero wait timeout,
zero FIFO overflow, and a 1,344-frame recovery maximum. Three following
10-second attempts totalled 18 discontinuity cycles, or 0.6 per second versus
15.8 per second in the old run, with zero FIFO overflow and a 2,016-frame maximum.
Two attempts passed every soak gate; one reported a single render wait timeout,
so render-deadline stability remains open despite the large discontinuity
reduction.

On 2026-07-15, commit `c2e0029` passed a 60-second backend Alpha candidate
checkpoint on the default CABLE-B 44.1 kHz capture to 48 kHz render pairing.
The run rendered 2,881,632 frames against a 2,880,750 hardware-clock target
(about 100.03% coverage), with zero capture or render wait timeout, FIFO
overflow, processing error, stream start/stop error, or unattributed render
underflow. Two capture discontinuities formed one continuous recovery episode;
the total and maximum per-discontinuity recovery silence were both 960 frames,
below the 2,594-frame bound. Capture clock feed-forward was valid at shutdown.
The worker now keeps the continuous episode count while restarting the bounded
frame measurement at every adapter reset, so closely spaced discontinuities do
not turn a per-discontinuity gate into an aggregate-episode gate. This short
candidate checkpoint does not replace the two eight-hour pairings, 24-hour
soak, or physical unplug/replug evidence required for Backend Alpha exit.

On 2026-07-16, the first eight-hour physical-pairing gate passed on a High
Definition Audio 44.1 kHz capture to 48 kHz render path. It rendered
1,383,145,632 frames against a 1,383,143,040 hardware-clock target, with zero
capture/render wait timeout, FIFO overflow, process error, or stream start/stop
error. All 58,368 render underflow frames were attributed to discontinuity
recovery or capture starvation. The maximum per-discontinuity recovery was
1,824 frames against the 2,594-frame limit. The second distinct eight-hour
pairing, 24-hour soak, and physical unplug/replug evidence remain outstanding.

## Current Testing Model

The Windows CTest suite currently has 87 smoke targets. Several tests are
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
  while the bridge resets and re-primes. Recovery-silence totals and the maximum
  cumulative silence frames for one discontinuity episode are now reported and
  can be thresholded by the recovery acceptance script. Synthetic coverage
  distinguishes a 128-frame episode maximum from 192 aggregate frames. The
  remaining gate is long-duration real-device evidence against the configured
  `target_fill_frames + render_buffer_frames` bound. Native-rate capture reduced
  one 44.1-to-48 kHz path from 15.8 to 0.6 reported discontinuity cycles per
  second across the retained short runs, but one of three attempts still had a
  render wait timeout.
- WASAPI failures preserve native HRESULT/Win32 values through the stream and
  realtime-worker layers, and device invalidation feeds a bounded control-thread
  reopen policy. A lock-free endpoint-notification source and endpoint-selection
  policy now drive supervisor reopen decisions, with generation, reset-failure,
  and notification-reopen counters. The repeatable default-endpoint A-to-B-to-A
  gate passed with two coalesced one-attempt recoveries and a 590 ms maximum. A
  two-second Windows Audio outage also recovered unchanged pinned endpoint IDs
  in 3548 ms within the three-attempt, five-second policy. Physical
  unplug/replug and pinned-endpoint disappearance remain unmeasured.
  `sar_measure_wasapi_recovery` supplies the MTA control loop and machine-readable
  recovery summaries; `--capture-id` and `--render-id` select pinned endpoints
  independently. `windows-wasapi-recovery-acceptance.ps1` enforces final health,
  endpoint identity, successful recovery, reset failure, process exit, and
  recovery-duration gates for retained logs.
- One eight-hour physical, mismatched-rate pairing has passed. A second distinct
  eight-hour pairing and the 24-hour backend alpha soak remain outstanding.
- Loopback capture is not yet connected to a selectable render destination or
  virtual endpoint.
- No virtual ASIO driver implementation exists yet.
- No virtual WDM/WASAPI driver implementation exists yet.
- No UI exists yet.
- No plugin hosting exists yet.
- Graph execution is still linear.
- The first named-pipe control service boundary exists, but it does not yet own
  a live WASAPI runtime, persist sessions, install as a Windows service, or
  define concurrent-client authorization and arbitration.
- Preset-to-graph build currently supports one route matrix node with matching
  matrix input/output counts.
- Sample conversion does not yet cover unusual byte orders or non-PCM encoded
  formats.
- Drift, underrun, overrun, and end-to-end latency diagnostics are still early.

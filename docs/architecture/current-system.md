# Current System Architecture

This document summarizes the code that exists now. Strategy documents describe
where the project is going; this file describes the current executable shape.

## Current Stage

The project is still pre-alpha. The portable realtime core, graph execution
prototype, control session shell, Windows WASAPI stream shell, graph runner,
realtime worker, render, duplex, and loopback loop wrappers, sample conversion
helpers, a first named-pipe engine control service, a bounded control wire
protocol, a mock Virtual ASIO transport, the first Qt Quick control application,
and the smoke-test harness are in place.

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
a named pipe and `sar_control_cli` can query state, the merged virtual and
physical WASAPI device directory, graph, diagnostics, and audio-runtime state;
start or stop an installed runtime; or apply gain and mute
commands. Control wire version 8 carries installed/running/runtime graph-version
state plus the active runtime mode, endpoint selection, and WASAPI runtime
health telemetry in lifecycle and diagnostics responses. A stopped service can
configure default or pinned WASAPI render and
default or pinned-pair duplex runtimes through the same protocol. A running
runtime can be reconfigured transactionally: the candidate must start before it
replaces the old configuration, and a failure restores the previous runtime.
The pipe path is control-plane only; audio data is not copied through it. The
service can now own a live WASAPI render or duplex
runtime, select an explicit capture/render endpoint pair for duplex operation,
start and stop it through an injectable runtime contract, and serve its realtime
diagnostics over the same control protocol. Graph mutations now prepare a new
graph and runtime, stop the previous runtime, start the candidate, refresh active
Virtual ASIO client graphs, and commit only after every step succeeds. A failed
candidate restores the previous running runtime and leaves the preset unchanged.
The Alpha Virtual ASIO DLL and service currently enforce exactly two input and
two output channels. The service-owned render bus also keeps a fixed physical
render quantum for its lifetime, so preset block-size changes are rejected with
a restart-required error instead of committing an unusable format. A stopped
runtime can rebuild against a newer graph through its retained endpoint
configuration. The Qt Quick GUI now binds to control wire v8 from a separate process.
Service installation and concurrent control-client authorization remain future
work.

A physical HDA lifecycle check exercised runtime state, stop, state, start,
diagnostics, and stop through one named-pipe service process. The restarted
44.1-to-48 kHz duplex runtime processed 304 blocks in three seconds with one
startup xrun, zero capture/render overflow, and a 41.1-microsecond peak callback.
An additional pinned-endpoint check stopped graph-v1 duplex, applied a gain
change that published graph v2, and restarted through the control protocol. The
runtime builder preserved both endpoint IDs, rebound to graph v2, and processed
302 blocks in three seconds with zero capture/render overflow and a
57.7-microsecond peak callback.
The wire-v3 configuration acceptance check launched the service with no audio
arguments, configured and started the pinned physical duplex pair, then stopped
and reconfigured the same process for pinned render. Duplex processed 302 blocks
in three seconds with zero overflow and a 59.7-microsecond peak callback; render
processed 216 blocks in two seconds with zero xruns, underflow, or overflow and
a 39.3-microsecond peak callback.

`MockAsioTransport` is the first engine-side Virtual ASIO transport experiment.
It preallocates fixed-format client-to-engine and engine-to-client block queues,
keeps push/pop allocation-free and lock-free, tracks drops, underruns, sequence
discontinuities, and connection generations, and has a two-thread stress smoke.
It is not itself a driver. A separate x64 DLL now supplies the initial official
SDK `IASIO` interface, COM activation, fixed stereo channel and format queries,
double-buffer allocation, and start/stop/dispose lifecycle. Its registration
utility writes matching COM and ASIO discovery entries. A dedicated MMCSS worker
now schedules the negotiated double buffers and exchanges planar float blocks
with the service broker through the existing bounded shared queues. The worker
does not wait for engine output; an empty queue produces deterministic silence.
The COM smoke runs the real DLL against an in-process broker and gain graph,
observes processed audio in a host input buffer, then verifies callback quiescence
before buffer disposal and DLL unload.

The service now also owns a fixed-format `VirtualAsioRenderBus`. Each admitted
ASIO session receives one generation-checked producer slot with a preallocated
bounded SPSC queue. The WASAPI render thread is the sole consumer: it clears its
destination, mixes at most one queued block from each active client, and feeds
the existing engine graph through the generic `RealtimeAudioSource` interface.
Queue-full producers drop without waiting, empty reads produce silence, and
slot attachment and retirement remain on the control side. Client output is
published before its private return graph, so the central physical-output graph
is applied exactly once. The ASIO callback worker now prepares host input before
the callback, reads newly produced host output only after callback return, and
uses QPC absolute deadlines so callback execution time does not accumulate into
clock drift.

`VirtualAsioClientRegistry` defines the control-plane admission policy for
future DAW host connections. The first client establishes the active sample
rate, block size, and input/output channel layout; later clients must match that
clock domain. Client IDs are unique, capacity is bounded, and a monotonically
increasing connection generation prevents a stale disconnect from removing a
new connection that reused the same ID. The active format is released only
after the final client disconnects. Registry operations may allocate and must
never be called from the ASIO audio callback; accepted descriptors are intended
to configure a separate preallocated transport.

`VirtualAsioSharedMemoryHeader` now fixes the pointer-free v1 cross-process ABI
at 256 bytes. It carries protocol major/minor, endian marker, state, feature
bits, process IDs, connection generation, two 128-bit handshake nonces, fixed
format, bounded queue dimensions, and checked 64-bit offsets. Independent input
and output SPSC regions use 128-byte controls and 64-byte-aligned block slots;
layout calculation rejects overflow and mappings above 256 MiB, while consumer
validation recalculates every offset before use. Windows object-name generation
accepts only bounded lowercase ASCII tokens and always produces per-session
`Local\\` names containing the connection generation. A move-only Windows
mapping owner/view now creates or opens the page-file mapping, zeroes new audio
memory before publishing the header, rejects same-name ownership, validates the
actual mapped region, and uses interlocked state publication across views. It
also revalidates the local-session name at the mapping API boundary. Non-owning
input and output queue views now exchange fixed planar-float blocks using only
bounded copies and interlocked positions/counters. Full queues drop, empty
queues return silence, stale generations and malformed slots are consumed
without wedging the ring, and a 20,000-block two-thread smoke covers separate
mapped views. Control-position spans beyond capacity are rejected as corrupt
instead of exposing unpublished audio. Input/output auto-reset events and a
manual-reset shutdown event now provide bounded wakeups over the same object
identity. Mapping and event creation use a non-inheritable explicit DACL that
grants only the current user SID and LocalSystem. A first owning Windows
transport session now combines one mapping, event bundle, independent graph,
preallocated input/output buffers, and a cancellable pump thread. It drains DAW
input blocks, processes them through the session graph, publishes output blocks,
and exposes atomic transport statistics without sharing the graph or mutable
diagnostics with WASAPI. The session also holds a synchronization handle for
the admitted client process and retires itself when that process exits, so a
crashed DAW does not require a successful disconnect request.
An owning transport host now combines the bounded client registry with these
sessions. It enforces one fixed clock domain, assigns connection generations,
generates unpredictable object tokens and server nonces, rolls registry state
back on partial construction failure, rejects stale disconnects, and reaps
sessions whose monitored client process has exited.
A bounded local named-pipe broker now decodes the versioned connect/disconnect
protocol, obtains the caller PID from Windows instead of trusting request data,
builds one graph per admitted client, and returns the randomized shared-memory
and event names. Pipe and transport objects use current-user/System DACLs,
remote pipe clients are rejected, and disconnect ownership is bound to the
original client process.
A DLL-facing broker client owns the inverse path: protocol transactions,
response correlation, shared-header identity and nonce validation, event
opening, queue binding, retryable disconnect, and local handle cleanup. Its
audio block operations remain bounded wrappers over the mapped SPSC queues.

`core/diagnostics` tracks graph version, processed blocks, callback duration,
peak callback duration, xrun count, and aggregate plus per-producer Virtual ASIO
underflow/overflow events. The worker mirrors per-run xrun totals
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
result. The summary now reports ready, invalid, warming-up, and disabled
observer samples. It also reports total correction-clamped cycles plus current
and maximum consecutive clamped cycles and rendered frames. The duplex
acceptance script can require a minimum feed-forward ready ratio and a maximum
consecutive clamped-frame duration. Its ready-ratio denominator includes ready,
invalid, and disabled observations but excludes startup warming-up samples, so
the gate measures usable runtime rather than observer initialization.

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
runtime, then rebuilds it with 0/500/3000 ms fast retry delays. Ordinary
transient failures retain a five-second hard deadline. Explicit device-loss
failures continue with one reopen attempt every five seconds after the fast
window, allowing a service restart or unplugged endpoint to return without
leaving the engine permanently faulted. Standalone users can drive it with explicit control-plane
`tick()` calls; the service-owned duplex adapter now supplies a dedicated 20 ms
control thread and COM endpoint-notification apartment. A lock-free
`IMMNotificationClient` generation/event source and an
independent capture/render endpoint-selection policy feed the supervisor on the
control thread. Notification registration records that control thread and
rejects cross-thread registration or unregistration without releasing the live
COM callback or event. Follow-default generation changes settle for 300 ms so paired
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

A 2026-08-09 render-only follow-default measurement restarted Windows Audio
three seconds into a 25-second run. The first reopen observed no default render
endpoint and entered bounded backoff; the next reopen restored the same endpoint
in 604 ms. The run finished with one successful recovery, zero failed recovery,
zero retained error, and the supervisor still running immediately before the
measurement stopped it. Physical unplug/replug remains the outstanding device-
absence gate.

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

On 2026-08-09, a 30-second CABLE-B 44.1 kHz capture to 48 kHz render run
validated the clock-health counters on real endpoints. It rendered 1,442,496
frames with zero wait timeout, FIFO overflow, processing error, or stream
start/stop error. Feed-forward reported 15 ready, zero invalid, zero disabled,
and 43 initial warming-up observations, for 100% ready coverage after warm-up.
Final capture correction never reached the +/-2500 ppm clamp. One capture
discontinuity produced 480 recovery-silence frames; together with 960 startup
frames, every one of the 1,440 render-underflow frames was attributed. The
strict `--require-healthy` measurement therefore rejected the run as degraded,
while the bounded duplex acceptance gate passed. This is a path check, not a
replacement for the remaining release-qualification evidence.

The same pairing then passed the one-command WinRM soak entry with the 99%
feed-forward-ready and five-second clamp gates enabled. That repeat rendered
1,442,496 frames, reported the same 15 ready and zero invalid/disabled clock
observations, spent zero frames clamped, and kept its 1,920-frame maximum
discontinuity recovery below the 2,594-frame bound.

## Current Testing Model

The Windows CTest suite currently has 124 smoke targets. The named-pipe coverage
includes a full control-wire integration path through `EngineControlService`
for device enumeration, session state, runtime configuration, lifecycle, and
diagnostics. Several tests are
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
  eight-hour pairing and the 24-hour backend alpha soak remain release-
  qualification work; they no longer block feature integration after short
  hardware gates pass.
- Loopback capture is not yet connected to a selectable render destination or
  virtual endpoint.
- The virtual ASIO DLL has a real control/buffer ABI and broker-backed callback
  loop. REAPER 7.78 x64 now enumerates and loads the machine-registered driver
  on the Windows test desktop. The validated session exposes two inputs and two
  outputs at 48 kHz with 128-sample buffers; REAPER reports ASIO active at about
  2.6/2.6 ms, and process-module inspection resolves the loaded DLL to the Alpha
  installation directory. A host probe using the ASIO CLSID-as-IID activation
  convention also completed buffer creation, streaming, and clean shutdown.
  A retained REAPER-to-CABLE acceptance run captured 233,472 frames with a
  0.366211 peak and 0.136627 RMS, with zero timeout, non-finite sample, or
  engine xrun. The broker now publishes the active preset format before buffer
  creation. The driver reports that sample rate and block size as the preferred
  clock domain, while supported power-of-two host block sizes are adapted per
  producer through a preallocated planar FIFO before entering the fixed-quantum
  physical render bus. A 64-frame client can therefore combine into a 128-frame
  engine block, and a 256-frame client can split into two engine blocks without
  realtime allocation. Cross-sample-rate adaptation remains outstanding.
  A repeatable host-probe run now generates a 440 Hz, -24 dBFS signal without
  callback allocation. Over three seconds it observed 1,113 of 1,125 expected
  callbacks; engine diagnostics increased by exactly 1,113 pushed, consumed,
  and mixed blocks and reported the expected 0.0630957 peak with no new drop.
  This run exposed and fixed an implicit dependency on another host raising the
  Windows timer resolution: the driver now prefers a high-resolution waitable
  timer and falls back to the legacy timer only when unavailable.
  A WinRM acceptance helper now repeats the real REAPER load without manual
  process setup. It refuses to take over an existing DAW or engine, registers
  and verifies the tested DLL, launches both processes in the active Explorer
  session, checks the loaded module and non-zero producer/consumer/mixer
  counters with zero drops, and removes its exact processes and Scheduled Tasks.
  The same helper accepts up to eight requested REAPER clients. A two-process
  run loaded the production DLL in both hosts and reported two active producers,
  2,731 pushed blocks, 2,728 consumed blocks, 1,515 physical mix cycles, zero
  drops, zero xruns, and a 257.899-microsecond peak graph callback. A subsequent
  single-client compatibility run also passed.
  The duration gate now measures counter deltas against an 80% nominal cadence
  floor. Its first 60-second two-client run delivered 44,426 pushed, 44,425
  consumed, 22,514 mixed, and 22,346 processed blocks with zero drop, xrun,
  clipping, or non-finite sample and a 1.2674-millisecond peak graph callback.
  Cakewalk by BandLab 29.09 is the second validated DAW. A 30-second
  Cakewalk-plus-REAPER run loaded the same production DLL in both processes,
  retained two active producers, delivered 22,493 pushed, 22,492 consumed,
  11,282 mixed, and 11,269 processed blocks, and reported zero drop or xrun
  with a 745.1-microsecond peak graph callback. The cross-DAW helper preserves
  the already-running first host, owns only the second host task and process,
  and requires the producer count to return to one after disconnect.
  The engine service also reaps stopped transport sessions from its 20 ms
  control loop. A bounded synthetic soak harness can drive up to 32 broker
  clients with mixed block sizes for as long as 24 hours and fails on configured
  callback, queue, timeout, dropout, or sequence-discontinuity thresholds. Its
  three-client 64/128/256 integration smoke is part of the Windows CTest suite.
  A real two-REAPER teardown regression now requires and passed
  the `2->1->0` active-producer sequence, closing the dead-client render-bus
  slot leak exposed by the first interrupted cross-DAW run.
- The first ASIO-to-physical-render bridge is wired for exact-format clients.
  On 2026-07-22, REAPER 7.78 loaded the deployed driver while the service ran a
  pinned 48 kHz hardware render endpoint; after 11,301 graph blocks the runtime
  reported zero xruns, FIFO overflow, or FIFO underflow and a 754-microsecond
  callback peak. Production-bus concurrency and WASAPI source injection are
  covered by dedicated smoke tests. Control-wire v6 now reports producer,
  consumer, queue-waterline, peak, clipping, and non-finite-sample evidence.
  An initial ten-second live delta measured 372.1 ASIO production attempts per
  second but only 105.4 consumed blocks per second on the default shared-mode
  CABLE render period. The render opener now uses `IAudioClient3` to select the
  legal engine period nearest the graph quantum, with a freshly activated legacy
  client fallback. The repeated run consumed 3,665 blocks while 3,664 were
  produced in ten seconds, with zero dropped blocks, xrun, FIFO overflow, or
  FIFO underflow. Captured peak was 0.366211 and RMS was 0.252209. Per-client
  long-duration clock adaptation and engine-period contention remain outstanding.
- No virtual WDM/WASAPI driver implementation exists yet. A five-day ACX 1.1
  versus SysVAD/PortCls decision spike is now required before product driver
  implementation.
- The first Qt Quick GUI exists and controls route state, gain, runtime
  lifecycle, device listing, diagnostics, and preset browsing with atomic
  save/load. Route edits have a bounded undo/redo history that commits only
  after an accepted engine response and resets when a preset is loaded.
  The route matrix now renders only visible cells with fixed headers, two-axis
  scrolling, keyboard navigation, and explicit pending, muted, busy, and offline
  states. Seed-user feedback remains. CMake/CPack now
  produces per-user Windows x64 ZIP and NSIS installer
  payloads, with a launcher that starts the engine and then opens the GUI.
  Its install scripts validate the engine, ASIO, Qt DLL, QML, and platform
  plugin payloads, verify Virtual ASIO registration, preserve and restore an
  existing install on failure, and provide a bounded uninstall path that refuses
  to remove running installed processes. The GUI establishes a process-lifetime
  named instance guard before Qt initialization, and installer acceptance
  relaunches the bootstrap executable while requiring exactly one GUI and one
  engine process. A clean-machine acceptance installed
  the final ZIP, launched its GUI without Qt development paths, verified ASIO
  ownership, rejected a deliberately incomplete runtime before changing the
  target, and removed the installation and registration through its packaged
  uninstaller. The same gate is now repeatable through
  `windows-alpha-package-acceptance.cmd`.
- No plugin hosting exists yet.
- Graph execution is still linear.
- The named-pipe control service can own a WASAPI render or duplex runtime and
  select explicit endpoint IDs. Both modes now drive bounded supervisor
  recovery; follow-default render and duplex selections consume endpoint
  notifications while pinned selections retain their configured IDs. Recovery
  state, episode counts, success/failure counts, duration, notification reopen
  counters, runtime health/reason, wait timeouts, discontinuities, render
  underflow, bounded recovery silence, and sustained correction clamp are
  exposed through control wire v8 for GUI diagnostics. It
  accepts
  runtime state/start/stop commands over the named pipe and lazily rebuilds a
  stopped stale runtime on start. Device-list and session-state requests merge
  the control session's virtual endpoints with a fresh control-thread WASAPI
  enumeration; the merged directory is rejected if IDs collide or descriptors
  are invalid. The control CLI can explicitly save and load validated,
  versioned binary preset files using atomic replacement. With `--session`,
  the engine service also restores and atomically persists the preset, desired
  runtime configuration, and auto-start intent. Corrupt files are preserved
  and missing endpoint failures leave the control pipe online with the desired
  configuration intact. The executable does not yet install as a Windows
  service or define concurrent-client authorization and arbitration.
- Preset-to-graph build currently supports one route matrix node with matching
  matrix input/output counts.
- Sample conversion does not yet cover unusual byte orders or non-PCM encoded
  formats.
- Drift, underrun, overrun, and end-to-end latency diagnostics are still early.

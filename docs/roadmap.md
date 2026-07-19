# Roadmap

This roadmap is optimized for depth and stability, not speed.

## Phase 0: Design and Technical Validation

Purpose: decide the hard platform strategy before building too much around the wrong assumptions.

Deliverables:

- Product principles.
- Realtime engine architecture.
- Routing graph schema draft.
- Windows driver strategy.
- Windows audio option analysis.
- Virtual ASIO strategy.
- Virtual WDM/WASAPI strategy.
- Control plane architecture.
- Performance target draft.
- Driver research notes.
- Phase 0 validation plan.

Exit criteria:

- Clear Windows virtual ASIO strategy.
- Clear Windows virtual WDM/WASAPI strategy.
- Clear split between driver, engine service, UI, and plugin host.
- First coding milestone defined.

## Phase 1: Realtime Core Prototype

Purpose: build the audio engine without depending on real devices.

Status: substantially underway. The portable buffer, process context, linear
graph, graph builder, graph snapshot publisher, route matrix, diagnostics, SPSC
queue, control session shell, and smoke tests exist.

Deliverables:

- Fixed-block graph executor.
- Basic node and edge model.
- Gain, mute, meter, and bus nodes.
- Realtime-safe graph snapshot publishing.
- Preset-to-graph build for the first route-matrix preset shape.
- In-process control session that can publish graph updates.
- Diagnostics counters.
- Offline test harness.

Exit criteria:

- Long synthetic runs without allocation or blocking in the audio path.
- Dynamic graph changes without interruption.
- Repeatable stress test reports.

## Phase 2: Windows Audio Backend v1

Purpose: connect the engine to real Windows audio devices.

Status: underway. WASAPI endpoint enumeration, including service/CLI exposure
of merged physical and virtual device descriptors, default and device-ID
endpoint probing, render loopback probing, shared-mode event-driven stream handles,
single-cycle render/capture pumping, graph runner orchestration, MMCSS scope,
realtime worker, and render, duplex, and capture-only loopback wrappers and
measurement tools exist. Fixed-capacity FIFOs preserve frame counts in the
single-ended paths when device periods and graph blocks differ. Native-rate
capture opening plus internal adaptive resampling now enables a real shared-mode
duplex path from a 44.1 kHz capture endpoint to a 48 kHz render endpoint without
first routing capture through Windows Audio Engine SRC.

The Windows suite currently contains 94 CTest targets. A strict-healthy two-second
render measurement submitted 96,000 frames with zero xruns, wait timeouts, or
FIFO faults. A five-second duplex measurement processed approximately 240,000
render-domain frames, but still exposed capture discontinuity and render
underflow. Render-master scheduling now connects a bounded FIFO waterline
controller to adaptive capture resampling, with bounded SRC offers and
discontinuity reset/re-prime behavior. Native capture and render clock samples
also provide a smoothed, slew-limited feed-forward term while the FIFO
controller corrects residual fill error. Feed-forward remained valid in one
30-second hardware run, but that is path validation rather than an alpha
stability result. Phase 2 is therefore not complete.

The control CLI can now save the active route preset to a bounded, versioned
binary file and load it back through the engine service. Writes use same-folder
atomic replacement so an interrupted save does not publish a partial preset.
The engine host can also own a versioned session file containing the preset,
desired WASAPI runtime configuration, and auto-start intent. Session recovery
keeps the control pipe available when a pinned endpoint is temporarily absent
and never overwrites a corrupt source file.

Deliverables:

- WASAPI input and output backend.
- Hardware device enumeration through the headless service and control CLI.
- Loopback capture prototype.
- Drift and xrun diagnostics.
- Render-master duplex scheduling and FIFO waterline drift control.
- Headless engine service.

Exit criteria:

- Physical input to physical output routing.
- System loopback to engine routing.
- Backend alpha gates below pass on supported Windows hardware.

### Backend Alpha Gates

These gates turn the remaining Phase 2 work into repeatable measurements. A
short hardware run or a synthetic test can validate a path, but neither replaces
the soak gates.

- **Clock feed-forward:** report valid, invalid, and disabled observer samples,
  plus time at the final +/-2500 ppm clamp. After the first valid estimate, each
  duplex soak must keep feed-forward valid for at least 99% of observations and
  must not remain clamped for more than five consecutive seconds. The FIFO term
  remains enabled as residual correction; feed-forward is not treated as a
  replacement for bounded buffering.
- **Bounded discontinuity recovery:** preserve the existing reset/re-prime rule
  and prove, with scripted discontinuities and real-device observations, that no
  pre-discontinuity samples reach the graph. For each discontinuity, graph output
  must resume within `target_fill_frames + render_buffer_frames` of labeled
  recovery silence, with zero capture FIFO overflow. The realtime worker now
  reports a per-event maximum, the supervisor retains it across runtime reopen,
  and the recovery acceptance script can apply an explicit frame threshold.
  Synthetic two-episode coverage proves the maximum is not the aggregate total.
  Consecutive resets retain one continuous episode while restarting the
  per-discontinuity frame bound at each reset.
  Long-duration real-device evidence against the bound still remains.
- **Device invalidation and reopen:** classify
  `AUDCLNT_E_DEVICE_INVALIDATED` separately, leave the realtime worker without a
  blocked wait or join, and reopen on the control side. A disable/enable or
  default-device-change test must resume the selected route within five seconds
  and three open attempts, or terminate with a stable fault reason; it must not
  spin or continue using the invalid stream. The recovery policy, conservative
  exact native HRESULT propagation, lock-free endpoint notifications, endpoint
  selection, notification-to-supervisor reopen decisions, error-code classifier,
  whole-duplex-runtime supervisor, and an interactive recovery measurement CLI
  are implemented. Notifications are filtered to the `eConsole` role used by the
  current probe path. The production factory resolves both directions from one
  enumeration, opens explicit IDs, re-resolves on every retry, and reports the
  IDs that actually started. The repeatable Windows A-to-B-to-A gate switched
  both default directions from the High Definition Audio endpoints to
  VoiceMeeter and back. A 300 ms settle window coalesced the paired direction
  notifications into two one-attempt recoveries; the maximum was 590 ms and the
  run ended with zero failed recoveries, notification-reset failures, or
  retained worker errors. A separate two-second Windows Audio service outage
  exercised the runtime-failure path with pinned High Definition Audio IDs. The
  0/500/3000 ms retry schedule restored those unchanged IDs in 3548 ms with no
  notification reopen or retained error. Physical unplug/replug and
  pinned-endpoint removal evidence still remains.
- **Render deadline ordering:** when capture and render are both ready, service
  render before draining additional capture packets or producing optional graph
  backlog. A deterministic call-order smoke test must prove the ordering. A
  hardware run must report zero render wait timeouts and zero render underflow
  outside explicitly labeled startup, discontinuity-recovery, or capture-source
  starvation silence. Deterministic render-first ordering is implemented. Three
  consecutive 10-second hardware runs passed the duplex acceptance gate with
  zero unattributed underflow and a 2,304-frame maximum recovery episode against
  the 2,594-frame callback-quantized bound. Longer hardware evidence remains
  part of the soak gate below.
- **Long soaks:** first pass an eight-hour duplex run on two distinct physical
  capture/render pairings, including one pairing with different endpoint mix
  rates, then pass one 24-hour duplex run. Each run must complete startup and
  bounded shutdown, process at least 99.99% of the duration-derived render-frame
  target, and report zero stream faults, processing failures, FIFO overflows,
  unlabeled render underflows, and wait timeouts. Every reported discontinuity
  must satisfy the recovery bound above; raw summaries and command lines are
  retained with the test report. A 60-second 44.1-to-48 kHz candidate checkpoint
  on 2026-07-15 passed the 99.99% coverage, zero timeout/error/overflow, exact
  underflow-attribution, and 2,594-frame recovery gates; its maximum recovery
  was 960 frames. The first eight-hour physical, mismatched-rate pairing passed
  on 2026-07-16 with 1,383,145,632 rendered frames against a 1,383,143,040
  hardware-clock target, zero wait timeout/error/overflow, exact attribution of
  58,368 underflow frames, and a 1,824-frame recovery maximum. The second
  distinct eight-hour pairing and the 24-hour gate remain outstanding.

## Phase 3: Virtual ASIO v1

Purpose: implement the project's main differentiator.

Status: engine-side foundations are underway. A fixed-format, preallocated mock
Virtual ASIO transport now provides lock-free client-to-engine and
engine-to-client block queues with generation and discontinuity diagnostics. A
bounded control wire protocol, named-pipe engine control service, and CLI also
exist. The service now owns an injectable audio-runtime lifecycle and can run a
real WASAPI render or duplex loop, including an explicit capture/render endpoint
pair, while exposing live diagnostics. Physical HDA checks processed 80
render-only blocks with zero xruns and 987 supervised duplex blocks across a
44.1-to-48 kHz pair. The final 10-second supervised check reported one startup
xrun, 1,088 render underflow frames, zero capture/render overflow, and a
279.6-microsecond peak callback. FIFO diagnostics are mirrored into atomic
worker snapshots so service queries do not race the realtime diagnostics writer.
Control wire version 3 and `sar_control_cli` now expose runtime state, start,
stop, and WASAPI runtime configuration without restarting the service process.
Render mode accepts default or pinned output selection; duplex accepts default
or paired pinned capture/render selection. A physical named-pipe
stop/start check then processed 304 duplex blocks in three seconds with zero
capture/render overflow and a 41.1-microsecond peak callback. A DAW-visible
ASIO driver is not implemented or registered yet.

A bounded control-plane client registry now admits multiple DAWs into one fixed
session clock domain. It rejects duplicate IDs, capacity overflow, mismatched
sample rates, block sizes, or channel layouts, and uses connection generations
to reject stale disconnects. It is intentionally separate from the realtime
transport and does not yet provide cross-process shared memory or an ASIO ABI.

Stopped Windows runtimes now retain a backend builder. After a graph mutation,
the next runtime-start command rebuilds the render or duplex runtime against the
new graph while preserving pinned endpoint IDs; failed rebuilds leave the prior
stopped runtime installed and return a deterministic control error.
The physical pinned-endpoint acceptance path rebuilt graph-v1 duplex against a
gain-edited graph v2, then processed 302 blocks in three seconds with zero
capture/render overflow and a 57.7-microsecond peak callback.
The no-audio-arguments service acceptance check then configured pinned duplex
and pinned render entirely over wire v3. Duplex processed 302 blocks in three
seconds with zero overflow; render processed 216 blocks in two seconds with zero
xruns, underflow, or overflow.

Deliverables:

- Virtual ASIO driver prototype.
- ASIO channel mapping into the routing graph.
- Multi-client behavior research and implementation.
- DAW bridge templates.

Exit criteria:

- At least two target DAWs can detect and use the virtual ASIO driver.
- DAW-to-DAW routing works under controlled conditions.
- Multiple DAWs can connect without normal single-device contention.

## Phase 4: Virtual WDM/WASAPI Devices

Purpose: support ordinary Windows applications.

Deliverables:

- Multiple virtual playback endpoints.
- Multiple virtual capture endpoints.
- Device naming and persistence.
- OBS, Discord, browser, and game compatibility tests.

Exit criteria:

- Normal apps can send audio into the engine.
- Engine can expose virtual microphones/capture devices.
- ASIO and WDM/WASAPI paths can coexist.

## Phase 5: Matrix and Mixer UI

Purpose: make the engine usable without hiding the system model.

Status: not started as a UI, but several backend pieces now exist: preset
validation, route matrix graph building, active graph summaries, virtual
endpoint listing, and full session-state responses.

Deliverables:

- Matrix routing view.
- Mixer view.
- Device view.
- Preset/session manager.
- Diagnostics panel.
- Undo/redo for control operations.

Exit criteria:

- Users can create, inspect, save, and reload practical routing sessions.
- Large route sets remain responsive.
- Diagnostics are visible enough to support bug reports.

## Phase 6: DAW Bridge Deepening

Purpose: make multi-DAW workflows excellent.

Deliverables:

- DAW-specific presets.
- Per-DAW channel maps.
- Latency measurement and compensation tools.
- Optional MIDI/OSC control research.

Exit criteria:

- Multi-DAW routing is stable enough for real production sessions.
- Latency is visible, understandable, and adjustable.

## Phase 7: Built-In Essential Processing

Purpose: cover routing-system processing needs without becoming a DAW.

Deliverables:

- Gain.
- Pan.
- Polarity invert.
- Delay.
- Safety limiter.
- Basic metering.
- Optional gate/EQ only after the core is stable.

Exit criteria:

- Common monitoring and streaming utility processing works without external plugins.
- All built-in processors are realtime-safe.

## Phase 8: Sandboxed VST3 Host

Purpose: support advanced processing while protecting the main engine.

Deliverables:

- External VST3 host process.
- Crash isolation.
- Plugin latency reporting.
- Plugin preset management.
- Compatibility notes.

Exit criteria:

- Plugin crashes do not terminate the engine.
- Plugin latency is reported and handled.
- Common VST3 plugins are usable under controlled tests.

## Phase 9: Cross-Platform Backends

Purpose: reuse the shared core on other platforms.

Preferred order:

1. Linux with PipeWire/JACK.
2. macOS with CoreAudio/HAL.

Exit criteria:

- Shared graph, preset schema, service API, and UI model remain useful.
- Platform-specific behavior is explicit instead of hidden behind false equivalence.

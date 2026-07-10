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

Status: underway. WASAPI endpoint enumeration, default and device-ID endpoint
probing, render loopback probing, shared-mode event-driven stream handles,
single-cycle render/capture pumping, graph runner orchestration, MMCSS scope,
and a realtime worker shell exist. The next milestone is a measured real-device
loop.

Deliverables:

- WASAPI input and output backend.
- Hardware device enumeration.
- Loopback capture prototype.
- Drift and xrun diagnostics.
- Headless engine service.

Exit criteria:

- Physical input to physical output routing.
- System loopback to engine routing.
- Multi-hour stability run.

## Phase 3: Virtual ASIO v1

Purpose: implement the project's main differentiator.

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

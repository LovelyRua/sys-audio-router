# Routing Graph

The routing graph is the product model. It represents devices, virtual endpoints, DAW bridges, buses, mixer strips, processing nodes, and monitoring paths.

The UI can render the graph as a matrix, patchbay, or mixer, but the underlying state should stay the same.

The first code-level matrix primitive is documented in [Route Matrix Core](route-matrix-core.md).

The first non-realtime graph validation path is documented in [Graph Builder](graph-builder.md).

## Design Goals

- Express complex routing without special cases.
- Support many virtual ASIO and WDM/WASAPI endpoints.
- Allow fast preset loading and session recall.
- Validate graph changes before they reach the audio engine.
- Keep platform-specific device details outside the shared graph schema where possible.

## Product Reference And Interaction Contract

VB-Audio Matrix is the baseline reference for routing semantics: enabled device
slots contribute channel-level sources and destinations to one routing grid,
and every source can feed every destination. System Audio Route may improve the
presentation and workflow, but it must not weaken that patchbay model.

The matrix control path has two classes of change:

- Route point parameters (`connected`, gain, mute, and phase) update the running
  graph without stopping or reopening an audio device. The audible change must
  reach the next practical processing block after the control command arrives.
- Topology and clock changes (device binding, channel count, sample rate, and
  buffer configuration) may rebuild or restart a runtime after validation.

The UI keeps selection separate from mutation. A primary click toggles a route
point; a secondary click selects it for inspection without changing audio.
Gain is expressed in dB, remains adjustable without another toggle gesture, and
is streamed while the control is dragged.

## Alpha Unified Duplex Topology

The Alpha service runs one 4-source by 4-destination graph:

| Graph channels | 0 | 1 | 2 | 3 |
| --- | --- | --- | --- | --- |
| Sources | WASAPI Capture L | WASAPI Capture R | ASIO DAW Out L | ASIO DAW Out R |
| Destinations | WASAPI Render L | WASAPI Render R | ASIO DAW In L | ASIO DAW In R |

The virtual ASIO driver still exposes two inputs and two outputs to each DAW.
Those transport channel counts do not define the size of the global matrix.
DAW output blocks enter the central graph through `VirtualAsioRenderBus`; graph
destinations 2 and 3 return through `VirtualAsioCaptureBus`.

The default preset routes DAW output to physical render and physical capture to
DAW input. Direct hardware monitoring is opt-in so startup cannot unexpectedly
create an acoustic or system feedback path.

The current multi-client Alpha model mixes all active DAW output pairs into one
stereo ASIO source and fans one stereo ASIO destination out to every client.
Per-client matrix lanes are a later topology extension, not an implicit behavior
of the current 4x4 graph.

## Node Categories

### Platform Device Nodes

- Physical WASAPI input.
- Physical WASAPI output.
- ASIO hardware input.
- ASIO hardware output.
- Future CoreAudio and PipeWire devices.

### Virtual Device Nodes

- Virtual ASIO client input.
- Virtual ASIO client output.
- Virtual WDM/WASAPI playback endpoint.
- Virtual WDM/WASAPI capture endpoint.

### Routing Nodes

- Bus.
- Send.
- Return.
- Matrix crosspoint.
- Channel splitter.
- Channel merger.

### Processing Nodes

- Gain.
- Pan.
- Polarity invert.
- Mute.
- Solo.
- Delay.
- Meter.
- Safety limiter.

### External Processing Nodes

- DAW bridge.
- Future sandboxed VST3 plugin.
- Future network or remote audio endpoint.

## Graph Updates

All graph changes follow this flow:

1. UI or control client proposes a change.
2. Service validates device existence, channel counts, and cycle rules.
3. Service builds a new graph snapshot.
4. Engine accepts the snapshot through a realtime-safe publish mechanism.
5. Diagnostics report the active graph version.

No UI interaction should directly mutate the running audio graph.

## Preset Model

A preset should store intent, not fragile runtime handles.

Store:

- Logical device names and stable IDs when available.
- Virtual endpoint definitions.
- Channel maps.
- Routes.
- Bus and mixer state.
- Latency compensation settings.
- UI layout hints.

Do not store:

- Raw OS handles.
- Process IDs.
- Temporary driver instance pointers.

When a preset cannot fully bind to the current machine, the system should load the available parts and report missing bindings clearly.

# Routing Graph

The routing graph is the product model. It represents devices, virtual endpoints, DAW bridges, buses, mixer strips, processing nodes, and monitoring paths.

The UI can render the graph as a matrix, patchbay, or mixer, but the underlying state should stay the same.

The first code-level matrix primitive is documented in [Route Matrix Core](route-matrix-core.md).

## Design Goals

- Express complex routing without special cases.
- Support many virtual ASIO and WDM/WASAPI endpoints.
- Allow fast preset loading and session recall.
- Validate graph changes before they reach the audio engine.
- Keep platform-specific device details outside the shared graph schema where possible.

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

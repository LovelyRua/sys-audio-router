# Phase 0 Validation Plan

Phase 0 exists to reduce the most dangerous unknowns before large implementation work begins.

## Workstream A: Realtime Core Assumptions

Questions:

- Can the graph snapshot model support hot updates cleanly?
- What buffer representation should the engine use?
- What lock-free queues or ring buffers are needed?
- How should diagnostics be collected without affecting callback timing?

Outputs:

- Minimal offline graph executor.
- Synthetic stress harness.
- Draft buffer and node interfaces.

Current seed implementation:

- `core/realtime/audio_buffer.h`
- `core/realtime/process_context.h`
- `core/graph/node.h`
- `core/graph/graph.h`
- `core/graph/graph_snapshot.h`
- `core/realtime/spsc_ring_buffer.h`
- `tests/realtime/realtime_smoke.cpp`
- `tests/realtime/graph_snapshot_smoke.cpp`
- `tests/realtime/route_matrix_smoke.cpp`
- `tests/realtime/diagnostics_smoke.cpp`
- `tests/realtime/spsc_ring_buffer_smoke.cpp`
- `tests/realtime/process_context_smoke.cpp`
- `tests/realtime/spsc_ring_buffer_threaded_smoke.cpp`

## Workstream B: WASAPI Backend

Questions:

- What is the practical scheduling behavior for shared and exclusive mode?
- How much drift appears between common hardware devices?
- How should format negotiation be represented in the shared graph?
- What happens during hot-plug and sleep/wake?

Outputs:

- Device enumeration prototype.
- Physical input-to-output prototype.
- Loopback capture prototype.
- Drift measurement notes.

## Workstream C: Virtual ASIO

Questions:

- What is the minimal ASIO driver shape needed for DAW detection?
- How should ASIO buffers be transported to the engine service?
- What multi-client policy is realistic?
- What DAW compatibility issues appear first?

Outputs:

- ASIO SDK/license notes.
- Mock ASIO transport for engine tests.
- Minimal DAW-visible driver prototype.
- DAW compatibility table.

## Workstream D: Virtual WDM/WASAPI

Questions:

- Which SysVAD path best matches a virtual playback/capture endpoint?
- What driver-to-service transport is realistic?
- How should endpoints behave when the service is stopped?
- What does developer signing require on the test machine?

Outputs:

- SysVAD research notes.
- Minimal endpoint prototype plan.
- Driver installation and rollback notes.
- App compatibility table.

## Workstream E: Product Control Surface

Questions:

- What control API commands are required for the first engine prototype?
- How should presets describe missing devices?
- What diagnostics are needed for actionable bug reports?

Outputs:

- Control API schema draft.
- Preset schema draft.
- Diagnostics report format draft.

## Phase 0 Exit Checklist

- [ ] Realtime graph prototype interface selected.
- [ ] WASAPI physical backend plan selected.
- [ ] Virtual ASIO prototype path selected.
- [ ] Virtual WDM/WASAPI prototype path selected.
- [ ] Driver signing and install approach documented.
- [ ] Test machine baseline recorded.
- [ ] First coding milestone defined.

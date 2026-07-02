# Performance Targets

Performance goals should be measurable. The project should treat audio glitches as first-class bugs, not as vague user reports.

These targets are provisional and should be refined once real driver and backend measurements exist.

## Latency Targets

Initial targets:

- Internal engine processing: less than one audio block of added latency for direct routes.
- Routing graph hot-swap: no audible interruption for simple route changes.
- Virtual ASIO round trip: low enough for DAW monitoring workflows at practical buffer sizes.
- Virtual WDM/WASAPI paths: stable and predictable, even when they cannot match ASIO latency.

## Supported Runtime Modes

Early test matrix:

- 44.1 kHz, 48 kHz, 96 kHz.
- 64, 128, 256, 512 sample buffers.
- Stereo, 8-channel, 16-channel, 32-channel graph configurations.
- Single DAW.
- Two DAWs connected at once.
- DAW plus OBS/Discord/browser.

## Stability Targets

Long-run tests should include:

- 1 hour smoke run.
- 8 hour overnight run.
- 24 hour soak run once the engine is mature.

Each run should capture:

- Xrun count.
- Peak callback time.
- Average callback time.
- Device drift.
- Memory growth.
- Thread priority and scheduling anomalies.
- Graph version changes.

The current core prototype includes a basic xrun detector that compares graph processing time against block duration. Backend-specific underrun and overrun counters are still future work.

## Failure Policy

When something goes wrong, the system should preserve audio where possible and explain the failure.

Examples:

- Missing device: keep the graph loaded with a disconnected placeholder.
- Plugin crash: bypass or disconnect the plugin node, keep the engine alive.
- Buffer underrun: increment diagnostics, surface it clearly, and continue.
- Device format change: renegotiate off the realtime thread.

Silent failure is unacceptable.

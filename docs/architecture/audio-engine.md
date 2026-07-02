# Realtime Audio Engine

The realtime audio engine is the heart of the system. It owns graph execution, audio buffer movement, timing, metering taps, latency accounting, and xrun detection.

The engine must not depend on any desktop UI framework, plugin UI, installer, or platform-specific device API. Platform backends feed audio into the engine and consume audio from it.

## Primary Requirements

- Low and predictable processing latency.
- Stable operation under long sessions.
- Realtime-safe audio callbacks.
- Dynamic graph updates without stopping audio.
- Support for multiple clock domains.
- Accurate diagnostics for performance and failure analysis.

## Realtime Rules

The audio callback must avoid:

- Heap allocation.
- Blocking locks.
- File IO.
- Network IO.
- Logging that can block.
- Waiting on UI, IPC, plugin UI, or driver control threads.
- Calling unbounded third-party code directly.

Graph changes should be built on a non-realtime thread, validated, then atomically published to the audio thread.

## Core Concepts

### Audio Block

The engine processes fixed-size blocks. Platform backends may need adapters when host APIs deliver variable or mismatched buffer sizes.

Each block carries:

- Sample rate.
- Frame count.
- Channel layout.
- Timeline position when available.
- Clock source identity.

The current prototype exposes the first pieces of this through `ProcessContext`:

- Sample rate.
- Frame count.
- Block index.

### Node

A node is a realtime graph processor. Initial node types:

- Hardware input.
- Hardware output.
- Virtual ASIO input.
- Virtual ASIO output.
- Virtual WDM/WASAPI playback input.
- Virtual WDM/WASAPI capture output.
- Bus.
- Gain.
- Mute.
- Solo.
- Meter tap.
- Delay.
- Safety limiter.

### Edge

An edge connects one node output to another node input. Edges can be gain-scaled and channel-mapped.

### Graph Snapshot

The running graph is immutable from the audio thread's point of view. Control threads prepare a new snapshot, validate it, and publish it using a realtime-safe handoff.

## Diagnostics

The engine must expose:

- Current sample rate.
- Current block size.
- Per-callback processing time.
- Peak processing time over a rolling window.
- Xrun count.
- Buffer underrun and overrun count.
- Per-node latency.
- Per-node CPU estimate where practical.
- Clock drift between devices.
- Resampler activity.
- Graph version.

Diagnostics must be designed so collecting them does not destabilize the engine.

## Testing Strategy

The first engine prototype should run without real audio devices.

Required harnesses:

- Offline graph execution tests.
- Long-running silence tests.
- Sine wave routing tests.
- Multi-bus summing tests.
- Dynamic graph hot-swap tests.
- Artificial CPU spike tests.
- Ring buffer underflow/overflow tests.

The first meaningful milestone is not "sound comes out." It is "the engine can run for hours under synthetic stress and explain what happened."

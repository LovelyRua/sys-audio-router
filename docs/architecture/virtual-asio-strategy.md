# Virtual ASIO Strategy

Virtual ASIO is the project's first major technical differentiator. It should be designed as a professional DAW bridge, not as a thin compatibility checkbox.

## Goals

- Provide low-latency virtual ASIO input and output channels.
- Allow more than one DAW to participate in the routing graph.
- Avoid normal hardware device contention.
- Expose stable channel names and layouts.
- Keep ASIO timing and sample position behavior reliable enough for DAW workflows.
- Route ASIO channels to and from the shared realtime engine graph.

## Non-Goals for v1

- Perfect compatibility with every ASIO host.
- Built-in plugin hosting.
- MIDI transport synchronization.
- Network audio.
- Dynamic unlimited channel creation while DAWs are actively streaming.

## Process Model

The ASIO driver should be a thin host-facing layer. The engine service should own routing, mixing, diagnostics, and session policy.

Preferred model:

```text
DAW Host
  <-> Virtual ASIO driver / shim
  <-> shared memory or low-latency IPC transport
  <-> engine service
  <-> realtime graph
```

The transport between ASIO and the engine must be bounded, predictable, and measurable. It must not require the ASIO callback to wait on UI or plugin code.

## Multi-Client Design Problem

ASIO is traditionally one host talking to one driver. Multi-client behavior must be treated as a product feature with explicit policy.

Questions to answer:

- Does each DAW see the same ASIO driver instance or a named virtual device instance?
- Are channel ranges partitioned per client?
- What happens if clients request different sample rates?
- What happens if clients request different buffer sizes?
- Is there a master clock, or does the engine adapt clients into a central clock domain?
- How are late or stalled clients handled?

Initial policy proposal:

- One engine sample rate per active session.
- One engine block size policy per active session.
- Clients that cannot match the active session are rejected or require explicit resampling mode.
- Stalled clients should be disconnected or silenced without stopping the whole engine.

## Compatibility Matrix

Initial hosts:

| Host | Priority | Notes |
| --- | --- | --- |
| REAPER | Highest | Flexible, good for early diagnostics. |
| Cubase | Highest | Important because Steinberg owns ASIO and Cubase is a strict compatibility target. |
| Ableton Live | High | Common production workflow. |
| FL Studio | High | Common multi-app routing use case. |
| Studio One | Medium | Useful additional professional host. |

Each host test should record:

- Host version.
- Windows build.
- Driver build.
- Sample rate.
- Buffer size.
- Channel count.
- Startup behavior.
- Stop/start behavior.
- Device loss behavior.
- Multi-client behavior.

## Prototype Milestones

1. Driver appears in one DAW.
2. DAW can open the driver at one fixed sample rate and buffer size.
3. DAW output reaches the engine test harness.
4. Engine output reaches DAW input.
5. Channel naming and channel count are stable.
6. Two DAWs can connect under the same sample rate and buffer policy.
7. One DAW output routes to another DAW input.
8. Long-running low-buffer test passes.

## Risks

- ASIO SDK licensing and redistribution terms.
- Host-specific assumptions.
- 32-bit host support.
- Buffer-size mismatch.
- Sample-rate mismatch.
- Thread priority inversion.
- Stalled client blocking the graph.
- Installer and registry cleanup.

## Decision Needed

Before implementation, decide whether the first prototype uses:

1. A minimal ASIO driver built from the Steinberg SDK.
2. An existing open-source ASIO-related codebase as a learning reference only.
3. A temporary mock ASIO transport to test engine-side assumptions before driver work.

Option 3 should happen first for engine tests. Option 1 is likely required for real DAW integration.


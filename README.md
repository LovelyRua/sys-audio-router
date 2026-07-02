# System Audio Route

System Audio Route is an experimental professional audio routing platform.

The first target is Windows, with high-performance virtual ASIO, multi-DAW routing, virtual WDM/WASAPI devices, a realtime-safe routing engine, and a responsive matrix/mixer UI.

The long-term architecture is cross-platform-ready, but not cross-platform-first. The shared core should stay portable while the first backend focuses deeply on Windows audio.

## Current Status

The project is in architecture and technical validation.

Start here:

- [Product Principles](docs/architecture/product-principles.md)
- [Roadmap](docs/roadmap.md)
- [Realtime Audio Engine](docs/architecture/audio-engine.md)
- [Routing Graph](docs/architecture/routing-graph.md)
- [Route Matrix Core](docs/architecture/route-matrix-core.md)
- [Realtime Transport](docs/architecture/realtime-transport.md)
- [Windows Driver Strategy](docs/architecture/windows-driver-strategy.md)
- [Windows Audio Options](docs/architecture/windows-audio-options.md)
- [Virtual ASIO Strategy](docs/architecture/virtual-asio-strategy.md)
- [Virtual WDM/WASAPI Strategy](docs/architecture/virtual-wdm-wasapi-strategy.md)
- [Control Plane](docs/architecture/control-plane.md)
- [Performance Targets](docs/architecture/performance-targets.md)
- [Phase 0 Validation Plan](docs/research/phase-0-validation-plan.md)
- [Development](docs/development.md)

Architecture decisions:

- [ADR 0001: Windows-First, Cross-Platform-Ready](docs/adr/0001-windows-first-cross-platform-ready.md)
- [ADR 0002: Keep the Realtime Core Independent](docs/adr/0002-keep-realtime-core-independent.md)
- [ADR 0003: Graph Snapshot Publication](docs/adr/0003-graph-snapshot-publication.md)

## Core Priorities

1. Realtime stability.
2. High-performance virtual ASIO.
3. Multi-DAW bridge workflows.
4. Multiple virtual WDM/WASAPI devices.
5. Observable latency, xrun, drift, and processing diagnostics.
6. Matrix and mixer UI that stays responsive under complex sessions.

## Prototype

The repository now contains a minimal C++20 realtime core skeleton and an offline smoke test target:

- `sar_core`
- `sar_realtime_smoke`
- `sar_graph_snapshot_smoke`
- `sar_route_matrix_smoke`
- `sar_diagnostics_smoke`
- `sar_spsc_ring_buffer_smoke`
- `sar_process_context_smoke`
- `sar_spsc_ring_buffer_threaded_smoke`

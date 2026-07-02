# Development

## Toolchain

The initial core prototype is standard C++20 with CMake.

Expected tools:

- CMake 3.24 or newer.
- A C++20 compiler.
- Ninja or another CMake-supported generator.

On Windows, the preferred compiler is MSVC once the Visual Studio Build Tools are installed. Clang-cl is also acceptable if it proves reliable with the Windows driver and ASIO development workflow.

## Build

Example:

```bat
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The current local Codex environment does not have `cmake`, `cl`, `ninja`, or `g++` available in `PATH`, so the first smoke test has not been compiled locally yet.

Use `scripts\check-toolchain.cmd` to inspect the local Windows toolchain.

## Current Core Prototype

The first prototype contains:

- `AudioBuffer`: fixed channel/frame float buffer.
- `ProcessContext`: per-block timing and format context passed to nodes.
- `Node`: realtime processor interface.
- `GainNode`: minimal processor for smoke testing.
- `Graph`: chain-style graph container with diagnostics update.
- `GraphSnapshotPublisher`: early graph hot-swap prototype.
- `RouteMatrix`: fixed-size channel routing and summing matrix.
- `SpscRingBuffer`: fixed-capacity single-producer single-consumer queue.
- `realtime_smoke`: offline processing smoke test.
- `graph_snapshot_smoke`: offline graph publication smoke test.
- `route_matrix_smoke`: offline matrix routing smoke test.
- `diagnostics_smoke`: diagnostics counter smoke test.
- `spsc_ring_buffer_smoke`: SPSC queue behavior smoke test.
- `process_context_smoke`: node process context smoke test.
- `spsc_ring_buffer_threaded_smoke`: producer/consumer SPSC ordering smoke test.

Known limitation:

- `Graph::process` currently executes a linear chain of nodes. Branching, bus summing, matrix routing, and cycle validation are upcoming work.
- `GraphSnapshotPublisher` currently uses `std::atomic<std::shared_ptr<Graph>>`. This is a prototype mechanism, not yet the final realtime publication strategy.

## Test Targets

Current CTest targets:

- `realtime_smoke`
- `graph_snapshot_smoke`
- `route_matrix_smoke`
- `diagnostics_smoke`
- `spsc_ring_buffer_smoke`
- `process_context_smoke`
- `spsc_ring_buffer_threaded_smoke`

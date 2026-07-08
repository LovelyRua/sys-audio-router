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

When using the shared Windows test machine, pass a unique slot to avoid build
directory collisions with other engineers:

```bat
scripts\windows-winrm-test.cmd <host> <user> <password> engineer-a
```

The current local Codex environment does not have `cmake`, `cl`, `ninja`, or `g++` available in `PATH`, so the first smoke test has not been compiled locally yet.

Use `scripts\check-toolchain.cmd` to inspect the local Windows toolchain.

## Current Prototype

The current prototype contains:

- `AudioBuffer`: fixed channel/frame float buffer.
- `ProcessContext`: per-block timing and format context passed to nodes.
- `Node`: realtime processor interface.
- `GainNode`: minimal processor for smoke testing.
- `Graph`: chain-style graph container with diagnostics update.
- `GraphBuilder`: non-realtime graph validation and construction.
- `GraphSnapshotPublisher`: early graph hot-swap prototype.
- `RouteMatrix`: fixed-size channel routing and summing matrix.
- `SpscRingBuffer`: fixed-capacity single-producer single-consumer queue.
- `AudioDeviceProvider`: platform device descriptor abstraction.
- `SampleConverter`: native interleaved float32/int16/int32 conversion to and
  from planar float buffers.
- `WindowsWasapiDeviceProvider`: active WASAPI endpoint enumeration.
- `WindowsWasapiStream`: WASAPI stream lifecycle, event handle ownership,
  render/capture service ownership, and single-cycle buffer pumping.
- `WindowsWasapiGraphRunner`: one-cycle orchestration around capture, graph
  processing, and render.
- `WindowsWasapiRealtimeWorker`: start/stop worker shell running the graph
  runner on a background MMCSS thread.
- `WindowsWasapiRenderLoop`: high-level render-only wrapper around default
  WASAPI render stream, graph runner, and realtime worker.
- `WindowsWasapiDuplexLoop`: high-level capture/render wrapper around default
  WASAPI streams, graph runner, and realtime worker.
- `ControlSession`: non-realtime control-plane shell for validated command
  application, graph publication, and active graph/session state.
- `sar_list_wasapi_devices`: command-line WASAPI endpoint, default stream probe,
  and loop graph-shape inspection.
- `sar_measure_wasapi_render_loop`: command-line default render loop measurement
  tool for real-device smoke runs, including runtime health, reason codes,
  worker counters, and engine diagnostics.
- `realtime_smoke`: offline processing smoke test.
- `graph_snapshot_smoke`: offline graph publication smoke test.
- `graph_builder_smoke`: graph validation and construction smoke test.
- `route_matrix_smoke`: offline matrix routing smoke test.
- `diagnostics_smoke`: diagnostics counter smoke test.
- `spsc_ring_buffer_smoke`: SPSC queue behavior smoke test.
- `process_context_smoke`: node process context smoke test.
- `xrun_detection_smoke`: forced slow-node xrun detection smoke test.
- `spsc_ring_buffer_threaded_smoke`: producer/consumer SPSC ordering smoke test.
- Windows WASAPI and realtime worker smoke tests when building on Windows.

Known limitation:

- `Graph::process` currently executes a linear chain of nodes. Branching, bus summing, matrix routing, and cycle validation are upcoming work.
- `GraphSnapshotPublisher` currently uses `std::atomic<std::shared_ptr<Graph>>`. This is a prototype mechanism, not yet the final realtime publication strategy.

## Test Targets

Current CTest targets on Windows:

- `realtime_smoke`
- `graph_snapshot_smoke`
- `graph_builder_smoke`
- `route_matrix_smoke`
- `preset_document_smoke`
- `control_command_smoke`
- `control_response_smoke`
- `control_session_smoke`
- `audio_device_smoke`
- `audio_device_registry_smoke`
- `virtual_endpoint_smoke`
- `sample_converter_smoke`
- `windows_wasapi_device_smoke`
- `windows_wasapi_stream_probe_smoke`
- `windows_wasapi_stream_smoke`
- `windows_wasapi_graph_runner_smoke`
- `windows_realtime_thread_smoke`
- `windows_wasapi_realtime_worker_smoke`
- `windows_wasapi_render_loop_smoke`
- `windows_wasapi_duplex_loop_smoke`
- `windows_wasapi_render_loop_measure_help`
- `diagnostics_smoke`
- `spsc_ring_buffer_smoke`
- `process_context_smoke`
- `xrun_detection_smoke`
- `spsc_ring_buffer_threaded_smoke`

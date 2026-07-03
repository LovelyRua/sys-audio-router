# Current System Architecture

This document summarizes the code that exists now. Strategy documents describe
where the project is going; this file describes the current executable shape.

## Current Stage

The project is still pre-alpha. The portable realtime core, graph execution
prototype, Windows WASAPI stream shell, graph runner, realtime worker, render
loop wrapper, sample conversion helpers, and smoke-test harness are in place.

The next major milestone is the first measured real-device loop:

```text
WASAPI capture/render stream
  -> WindowsWasapiGraphRunner
  -> Graph::process
  -> WindowsWasapiRealtimeWorker on an MMCSS thread
```

## Portable Core

`core/realtime` contains fixed-size planar float audio buffers and process
context structures. The graph currently consumes and produces
`realtime::AudioBuffer`.

`core/graph` contains:

- `Graph`: linear graph executor with diagnostics updates.
- `Node`: realtime processor interface.
- `GainNode` and passthrough behavior used by smoke tests.
- Graph builder and graph snapshot prototypes.
- Route matrix core for channel routing and summing experiments.

The graph is still intentionally simple. Branching graph execution, bus summing,
latency compensation, and final graph publication strategy are future work.

## Control And Diagnostics

`core/control` validates preset documents and control commands before applying
them to non-realtime state. This is the beginning of the future engine service
control plane.

`core/diagnostics` tracks graph version, processed blocks, callback duration,
peak callback duration, and xrun count. Diagnostics will need to expand as real
WASAPI loops expose underrun, overrun, drift, and wait-time behavior.

## Platform Layer

`core/platform` contains the platform-facing pieces:

- `AudioDeviceProvider` and descriptor validation.
- `AudioFormat`, including sample rate, channel count, block size, bit depth,
  and native sample format.
- `SampleConverter`, currently covering float32, int16 PCM, and int32 PCM
  interleaved buffers to/from planar float buffers.
- `VirtualEndpointRegistry`, the current model shell for future virtual devices.
- Windows WASAPI device enumeration.
- Windows WASAPI stream probing.
- Windows WASAPI stream lifecycle and single-cycle buffer pumping.
- Windows WASAPI graph runner.
- Windows realtime worker shell.
- Windows render-only loop wrapper for the first default-output device path.
- Windows MMCSS realtime thread scope.

## Windows WASAPI Flow

`WindowsWasapiStream` currently supports:

- Default endpoint probing.
- Shared-mode WASAPI initialization.
- Event-driven stream handles.
- `IAudioRenderClient` and `IAudioCaptureClient` ownership.
- Render priming with a silent buffer.
- Start/stop lifecycle.
- Single-cycle `render_once` and `capture_once` calls.

`WindowsWasapiGraphRunner` orchestrates one processing cycle:

1. Optionally capture from a WASAPI input stream.
2. Run `Graph::process`.
3. Optionally render to a WASAPI output stream.

`WindowsWasapiRealtimeWorker` runs the graph runner in a background thread and
enters MMCSS `Pro Audio` priority through `WindowsRealtimeThreadScope`.

`WindowsWasapiRenderLoop` owns a default render stream, graph runner, and
realtime worker. It is the current high-level entry point for the first measured
render-only real-device loop.

## Current Testing Model

The Windows CTest suite currently has 23 smoke targets. Several tests are
synthetic because WinRM sessions may not expose interactive audio endpoints even
when the VM has a desktop audio stack.

Use the WinRM test script for full validation:

```bat
scripts\windows-winrm-test.cmd <host> <user> <password> <slot>
```

Use a unique slot per engineer for concurrent runs, such as `engineer-a` or
`engineer-b`. Keep credentials out of git.

## Known Gaps

- No always-on real-device render/capture loop has been measured yet.
- No virtual ASIO driver implementation exists yet.
- No virtual WDM/WASAPI driver implementation exists yet.
- No UI exists yet.
- No plugin hosting exists yet.
- Graph execution is still linear.
- Sample conversion does not yet cover packed 24-bit PCM or valid-bits-in-32-bit
  extensible formats.
- Drift, underrun, overrun, and end-to-end latency diagnostics are still early.

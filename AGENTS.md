# Repository Instructions

System Audio Route is a Windows-first professional audio routing platform. The
current codebase is a C++20 technical prototype for the realtime core, WASAPI
backend, graph runner, and realtime worker.

## Shell And Tooling

- On Windows, avoid PowerShell for ordinary shell commands. Prefer `cmd /c`.
- Use CMake 3.24 or newer and an MSVC C++20 toolchain on Windows.
- Do not commit credentials, machine passwords, private tokens, generated build
  directories, or local RDP/WinRM artifacts.
- Keep changes small and testable. This project is optimized for audio stability,
  not rapid feature churn.

## Build And Test

Local build shape:

```bat
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Preferred Windows validation path from the development machine:

```bat
scripts\windows-winrm-test.cmd <host> <user> <password> <slot>
```

Use a unique slot for concurrent test runs, for example `engineer-a`,
`engineer-b`, or `engineer-c`. The slot isolates the remote checkout, build
directory, and bootstrap file.

The current Windows smoke suite has 38 CTest targets. If a change touches
realtime, WASAPI, thread lifecycle, sample conversion, graph execution, or
diagnostics, run the Windows test script before pushing or merging.

## Realtime Safety Rules

- Do not add allocation, locks, file I/O, logging, COM activation, or blocking
  control-plane work inside the audio processing path.
- `Graph::process`, sample conversion, WASAPI buffer pumping, and realtime
  worker loops are latency-sensitive. Treat them as hot-path code.
- Keep ownership and lifetime explicit for COM handles, event handles, threads,
  and graph buffers.
- Prefer deterministic result objects over exceptions for platform/audio runtime
  failures.
- Keep synthetic smoke tests for behavior that cannot be reliably exercised under
  WinRM, especially interactive WASAPI device behavior.

## Current Module Boundaries

- `core/realtime`: portable audio buffers, process context, and realtime helpers.
- `core/graph`: graph execution, nodes, snapshots, and route matrix logic.
- `core/control`: preset and command validation/application.
- `core/diagnostics`: callback, xrun, and processing counters.
- `core/platform`: device abstraction, sample conversion, virtual endpoint model,
  and Windows platform implementations.
- `tools`: command-line diagnostics such as WASAPI endpoint inspection.
- `tests/realtime`: smoke tests for core behavior and Windows platform shells.

## Collaboration Style

- Read the nearby code and docs before changing an area.
- Preserve existing patterns unless a clearer local abstraction is needed.
- Separate risky platform work into small commits.
- If you touch public behavior, update or add a smoke test.
- If you touch architecture direction, update `docs/architecture/current-system.md`
  or `docs/roadmap.md`.

## Highest Priority Work

1. Build the first real WASAPI render/capture loop around
   `WindowsWasapiRealtimeWorker`.
2. Improve diagnostics for underrun, overrun, wait timeout, callback duration,
   and sample conversion failures.
3. Keep the realtime core portable while the first backend focuses on Windows.
4. Defer UI, VST hosting, and virtual driver implementation until the backend
   loop is stable enough to measure.

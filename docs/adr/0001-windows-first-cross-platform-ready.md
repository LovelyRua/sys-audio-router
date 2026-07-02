# ADR 0001: Windows-First, Cross-Platform-Ready

## Status

Accepted.

## Context

The strongest product need is on Windows: high-performance virtual ASIO, DAW-to-DAW routing, and multiple virtual WDM/WASAPI devices. Building and testing every platform from day one would slow the project and dilute the most important technical work.

At the same time, the routing graph, realtime engine, preset model, diagnostics, and control API should not be unnecessarily tied to Windows APIs.

## Decision

Build the first platform implementation for Windows.

Keep the shared core platform-neutral:

- Realtime graph.
- DSP primitives.
- Preset/session schema.
- Diagnostics model.
- Control API.
- Plugin host protocol.

Place platform-specific code under explicit backend modules.

## Consequences

Positive:

- The project can focus deeply on the hardest target platform.
- Future Linux and macOS backends remain possible.
- The shared architecture avoids a full rewrite later.

Negative:

- Some early abstractions may be imperfect until another platform backend exists.
- Windows-specific needs may still influence shared concepts.
- Cross-platform claims must wait for real backend implementations.


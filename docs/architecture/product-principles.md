# Product Principles

This project is a professional system audio routing platform. Its first target is Windows, but its core architecture must stay ready for future platform backends.

## Mission

Build a stable, low-latency audio routing system that can connect DAWs, hardware devices, system audio, virtual ASIO devices, and virtual WDM/WASAPI devices without forcing users into fragile workarounds.

The product should feel closer to audio infrastructure than to a toy mixer. It should be calm, observable, and trustworthy under load.

## Non-Negotiables

1. Realtime stability comes before feature count.
2. Virtual ASIO is a first-class feature, not an afterthought.
3. The audio engine must be usable without the UI.
4. Driver, engine, plugin host, and UI boundaries must stay explicit.
5. Every important runtime behavior must be observable.
6. Presets and routing sessions must be reproducible.
7. Third-party plugins are low-trust code and must not be allowed to take down the main engine.
8. Cross-platform support is planned at the abstraction level, but Windows is the first implementation target.

## Product Shape

The long-term product has these user-facing surfaces:

- A routing matrix for fast signal patching.
- A mixer view for gain, monitoring, metering, and basic bus work.
- A device view for hardware, virtual ASIO, and virtual WDM/WASAPI endpoints.
- A preset/session manager for reusable workflows.
- A diagnostics view for latency, xruns, drift, buffer timing, and processing spikes.

The first usable product should not attempt to be a DAW. It should provide the routing and light processing that a routing system naturally needs, then make DAWs and optional plugin hosts easy to connect.

## Engineering Bias

Prefer small, testable realtime primitives over large feature-rich subsystems.

Prefer user-mode complexity over kernel-mode complexity when the latency and reliability tradeoff allows it.

Prefer explicit latency over hidden latency.

Prefer graph hot-swap and stable session state over restart-heavy configuration changes.

Prefer a boring, durable core over a dramatic UI.


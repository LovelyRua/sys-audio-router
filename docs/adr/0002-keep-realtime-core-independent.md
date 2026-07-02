# ADR 0002: Keep the Realtime Core Independent

## Status

Accepted.

## Context

The project needs a stable low-latency audio engine. UI frameworks, plugin hosts, driver policy, installers, and application control flows can all introduce blocking behavior, large dependencies, or crash risk.

## Decision

The realtime core must be independent from:

- Desktop UI frameworks.
- VST/plugin UI code.
- Installer logic.
- Driver installation logic.
- Network services.
- Blocking control APIs.

The engine accepts validated graph snapshots and provides diagnostics through realtime-safe mechanisms.

## Consequences

Positive:

- The core can be tested headlessly.
- UI crashes do not stop audio.
- Plugin crashes can be isolated later.
- Performance regressions are easier to identify.

Negative:

- More process and API boundaries are required.
- Some features take longer because they must cross explicit interfaces.


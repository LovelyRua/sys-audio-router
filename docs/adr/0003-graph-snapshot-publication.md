# ADR 0003: Graph Snapshot Publication

## Status

Accepted as prototype guidance.

## Context

The engine needs to update routes while audio is running. The audio thread must see a stable graph for each callback. Control threads should build and validate a new graph, then publish it without mutating the active graph in place.

## Decision

Use immutable graph snapshots as the model for route updates.

The current C++ prototype includes `GraphSnapshotPublisher`, implemented with `std::atomic<std::shared_ptr<Graph>>`.

This is acceptable for early architecture and offline smoke tests, but it is not yet accepted as the final realtime handoff mechanism. Its lock-free behavior and destruction timing must be measured on target compilers and platforms.

## Final Mechanism Requirements

The final publication mechanism must guarantee or prove:

- No blocking wait on the audio thread.
- No heap allocation on the audio thread.
- No graph destruction on the audio thread if destruction can do meaningful work.
- A stable graph view for each callback.
- Safe reclamation of old graph snapshots after the audio thread stops using them.

Candidate final mechanisms:

- Atomic pointer plus epoch-based reclamation.
- Read-copy-update style graph handoff.
- Double or triple buffered graph storage with control-thread ownership.
- A measured `atomic<shared_ptr>` path only if it proves acceptable on the supported toolchain.

## Consequences

Positive:

- The product model supports route hot-swap from the start.
- Tests can exercise graph version changes early.
- The UI/control plane stays naturally separated from realtime graph memory.

Negative:

- More infrastructure is required before the core is production-realtime-safe.
- Snapshot memory lifetime must be designed carefully.


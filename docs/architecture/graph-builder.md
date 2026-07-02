# Graph Builder

`GraphBuilder` is the non-realtime construction and validation path for graph snapshots.

The realtime engine should never receive a graph that has not passed validation. UI, service, preset loading, and backend code should build graph candidates off the audio thread, inspect `GraphBuildResult`, then publish only successful graphs.

## Current Implementation

Files:

- `core/graph/graph_builder.h`
- `core/graph/graph_builder.cpp`
- `tests/realtime/graph_builder_smoke.cpp`

Current checks:

- Graph version must be non-zero.
- Channel count must be non-zero.
- Frame count must be non-zero.
- Sample rate must be non-zero.
- Node list must not contain null nodes.
- Node labels must not be empty.
- Node labels must be unique.

Current output:

- Successful builds return a `std::unique_ptr<Graph>`.
- Failed builds return one or more `GraphBuildError` records.
- Successful graphs preserve builder node labels for diagnostics, presets, and future UI routing surfaces.

## Realtime Boundary

`GraphBuilder` is allowed to allocate memory and produce string errors because it is a control-plane tool.

`Graph::process` remains the realtime-facing path and must not depend on `GraphBuilder` internals.

## Future Work

- Split display labels from stable machine IDs.
- Validate graph edges.
- Reject cycles where feedback is not explicitly modeled.
- Validate channel maps.
- Validate route matrix dimensions.
- Emit stable machine-readable error codes for UI and logs.
- Build immutable graph snapshots directly for publication.

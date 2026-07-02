# Route Matrix Core

The route matrix is the low-level model behind a future matrix or patchbay UI.

The first implementation is intentionally simple: a fixed-size input-channel to output-channel gain matrix.

## Current Implementation

Files:

- `core/graph/route_matrix.h`
- `core/graph/route_matrix.cpp`
- `tests/realtime/route_matrix_smoke.cpp`

Capabilities:

- Fixed input and output channel counts.
- Stable input and output endpoint IDs.
- Display labels for input and output endpoints.
- Per-crosspoint gain.
- Multiple inputs summed into one output.
- One input routed to multiple outputs.
- Realtime processing without allocation.

The matrix owns its gain table. Route changes should happen off the realtime path by building a new matrix or graph snapshot, then publishing it.

## Contract

`RouteMatrix::process` must:

- Clear the output buffer first.
- Ignore out-of-range channels safely.
- Avoid heap allocation.
- Avoid blocking.
- Avoid logging.
- Produce deterministic output for a fixed input buffer and gain table.

## Future Work

- Sparse route representation for very large matrices.
- Route validation at the graph/control layer.
- Crosspoint smoothing to avoid clicks during gain changes.
- Matrix serialization in the preset schema.
- UI-level route grouping for multi-channel devices.

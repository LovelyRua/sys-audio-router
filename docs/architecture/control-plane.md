# Control Plane

The control plane connects UI, command-line tools, presets, diagnostics, and the engine service. It must stay separate from the realtime audio path.

## Goals

- Let the UI control routes, devices, sessions, and parameters.
- Let tests and scripts drive the engine without a GUI.
- Provide diagnostics snapshots for bug reports.
- Allow future remote or automation control without redesigning the engine.

## Process Boundary

```text
Desktop UI / CLI / Tests
  <-> Control API
  <-> Engine Service
  <-> Realtime Engine
  <-> Platform Backends
```

The UI never directly mutates realtime graph memory. It sends commands to the service. The service validates commands and publishes new graph snapshots.

## Command Types

Initial command categories:

- List devices.
- Create virtual endpoint.
- Remove virtual endpoint.
- Connect route.
- Disconnect route.
- Set gain.
- Set mute.
- Load preset.
- Save preset.
- Query diagnostics.
- Query active graph.

The first in-repository command model is `ControlCommand`. It is transport-neutral and versioned, with validation before any command may reach the engine service mutation path.

Current validated command data:

- Command schema version.
- Command ID for idempotency and logs.
- Virtual endpoint ID and label.
- Route input/output endpoint IDs.
- Route gain and mute state.
- Load-preset payload through `PresetDocument`.

Responses use `ControlResponse`, which carries:

- Command ID.
- Accepted or rejected status.
- Machine-readable errors.
- Optional diagnostics snapshot.

## Diagnostics API

Diagnostics should be available without opening the full UI.

Minimum diagnostics:

- Engine state.
- Sample rate.
- Block size.
- Active device list.
- Active virtual endpoint list.
- Graph version.
- Xrun counters.
- Peak callback time.
- Recent device errors.
- Recent route changes.

## Serialization

Control messages should use a versioned schema. The exact transport can be chosen later, but the API shape should be testable without desktop UI.

The first in-repository schema layer is `PresetDocument`. It is a C++ control-plane model, not a file format yet. UI, CLI, service code, and future JSON/TOML/binary serializers should all validate through this model before publishing a graph.

Current preset fields:

- Schema version.
- Sample rate.
- Frames per block.
- Node IDs, labels, and type names.
- Route matrix input/output endpoint IDs and labels.
- Route bindings by stable endpoint ID.

Candidate transports:

- Named pipes.
- Local TCP.
- gRPC over local transport.
- Cap'n Proto or FlatBuffers for lower overhead where useful.

The first implementation can be simple if the schema is explicit and versioned.

## Safety Rules

- Control commands must be validated before they affect the engine.
- Invalid preset bindings should not crash the service.
- Diagnostics collection must not block the audio thread.
- UI disconnection must not stop audio.
- Long-running operations must be asynchronous.

# Dynamic Matrix Topology

This document defines the boundary for moving beyond the fixed alpha 2x2 ASIO
and single WASAPI render/capture pair. New device work must use this model
instead of adding offsets to `initial_preset()`.

## Product Rules

- A device definition is persistent configuration; a running stream is not.
- A route addresses stable port IDs, never array indexes or buffer offsets.
- Mute and gain are independent. An unavailable or muted route retains gain.
- The matrix shows active ports by default. Inactive or missing ports may be
  shown read-only so saved routing intent remains inspectable.
- Realtime code consumes an immutable, precompiled topology snapshot. Device
  discovery, allocation, registry access, and graph compilation stay on the
  control plane.

## Control-Plane Model

```text
DeviceDefinition
  id, kind, name, enabled, persistent backend configuration

VirtualAsioDeviceDefinition
  device_id, clsid, registry_name, broker_token
  input_channel_count, output_channel_count

WasapiBindingDefinition
  device_id, endpoint_id, direction, clock_role

PortDescriptor
  id, device_id, direction, channel_index, label
  availability(active | inactive | missing), routable, format

RouteDefinition
  source_port_id, destination_port_id, gain, mute, polarity

RuntimeBinding
  port_id -> realtime bus/channel offset
  stream state, clock domain, measured latency

TopologySnapshot
  generation, devices, ports, routes, runtime bindings
```

Port IDs use an instance identity and channel number, for example
`asio:{device-uuid}:out:3` and `wasapi:{binding-uuid}:capture:1`. Renaming a
device or channel must not change its ID.

## Compilation And Runtime

The control plane validates a candidate topology, expands device definitions
into ports, resolves routes by ID, assigns contiguous realtime bus offsets, and
publishes one immutable snapshot. The realtime graph only sees compiled buffer
slots and scalar parameters. It performs no ID lookup or topology mutation.

Multiple WASAPI streams need separate FIFO, diagnostics, recovery state, and
clock-domain records. One stream is the graph clock master. Every independent
clock enters through a bounded elastic buffer and asynchronous sample-rate
conversion; merely concatenating devices into one callback is not valid.

## Virtual ASIO Instances

One versioned driver implementation may serve many registered ASIO instances.
Each instance has its own CLSID, registry name, broker token, and channel
configuration. `DllGetClassObject` resolves the requested CLSID to an immutable
instance descriptor. Changing channel counts requires all clients of that
instance to close, followed by a new topology generation and registration
update; it is not a live callback-buffer resize.

Installed driver payloads will move to versioned directories. An already loaded
DAW may finish using the old payload while registry ownership moves to the new
version. Cleanup occurs only after no process maps the old DLL.

## Delivery Slices

1. Extend endpoint wire metadata with device ID, backend, direction, channel
   index, and availability; remove QML name inference.
2. Persist device definitions and route intent separately from runtime binding.
3. Remove square/four-channel graph assumptions and compile rectangular buses.
4. Add create, rename, resize, enable, disable, and remove operations for
   virtual ASIO definitions, including per-instance registration.
5. Replace the single WASAPI pair with a binding list and select one master
   clock; add per-stream telemetry and recovery.
6. Add matrix device strips, channel grouping, offline-port visibility, and
   transactional apply/rollback for topology edits.

## Current Implementation Step

The session now persists multiple Virtual ASIO definitions. The engine derives
the aggregate symmetric profile from contiguous `asio-output-*` and
`asio-input-*` matrix port groups, then compiles enabled definitions into
non-overlapping input/output slices. Every instance owns independent realtime
buses, rate matching, transport sessions, and a broker pipe; one DLL resolves
the requested CLSID and broker token at COM activation. `--asio-channels N`
still seeds a new one-instance N x N session, and live topology changes require
an engine restart. GUI create/rename/resize/remove commands and automatic
registration synchronization remain the next control-plane slice.

Each slice requires wire round-trip tests, preset migration tests, graph
compiler tests, and Windows package/host acceptance before the next slice.

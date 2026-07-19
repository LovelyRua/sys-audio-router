# Virtual ASIO Driver ABI and Deployment Boundary

## Status

This document defines the first Windows deployment contract for the System
Audio Route Virtual ASIO driver. It is a design boundary, not evidence that an
ASIO-compatible DLL, cross-process transport, installer, or DAW integration is
already implemented.

The repository must not copy or redistribute Steinberg ASIO SDK source code,
headers, samples, or documentation text. Any implementation of the host-facing
ABI requires a separate legal and technical review. The preferred approach is
an independently authored, minimal compatibility boundary based on behavior
observed from public host/driver contracts and clean-room tests. If that cannot
be established with acceptable licensing confidence, the driver target remains
disabled until an approved SDK acquisition and build process exists outside the
redistributed source tree.

## Architectural Decision

The ASIO DLL is a thin, in-process host adapter. It does not own the routing
graph, presets, hardware WASAPI streams, resampling policy, client arbitration,
or persistent session state. Those responsibilities remain in the engine
service.

```text
32-bit or 64-bit DAW process
  -> matching-bitness Virtual ASIO in-process DLL
  -> authenticated control handshake
  -> per-connection shared-memory audio transport and events
  -> engine service transport adapter
  -> portable realtime graph
  -> Windows WASAPI runtime and/or another virtual client
```

This keeps host crashes and host-specific ABI behavior outside the service and
keeps control-plane work outside the ASIO callback. The DLL may translate ABI
types, copy or deinterleave samples into preallocated slots, publish timing, and
signal bounded transport events. It must not perform heap allocation, registry
access, COM activation, service startup, file I/O, logging, or unbounded waits
from an audio callback.

## DLL and COM Boundary

The driver is a native Windows in-process COM server loaded directly into the
DAW. The first implementation is expected to expose the standard COM loading
surface required by ASIO hosts:

- `DllGetClassObject`
- `DllCanUnloadNow`
- installer-owned registration and unregistration; exported
  `DllRegisterServer` and `DllUnregisterServer` are optional wrappers, not the
  source of installation policy
- one class factory for one stable Virtual ASIO driver CLSID
- one independently implemented host-facing ASIO interface object per COM
  activation

The COM object and class factory must use explicit reference counting and must
not unload while an interface, callback thread, or transport connection is
alive. `DllMain` is limited to loader-safe bookkeeping. It must not connect to
the service, create worker threads, wait on events, or touch COM and registry
APIs.

The DLL boundary ends at a small internal `DriverTransportClient` interface.
Host ABI definitions and calling-convention details stay in the driver target;
shared-memory layout definitions stay in a versioned transport target that can
be tested without loading a DAW. Core graph and service headers must not depend
on ASIO SDK types.

The product registration uses these conceptual entries in both applicable
registry views:

```text
HKLM\Software\ASIO\System Audio Route
  CLSID       = {stable-driver-clsid}
  Description = System Audio Route Virtual ASIO

HKLM\Software\Classes\CLSID\{stable-driver-clsid}
  (Default) = System Audio Route Virtual ASIO
  InprocServer32\(Default) = <bitness-specific absolute DLL path>
  InprocServer32\ThreadingModel = Both
```

Exact values must be validated against REAPER and at least one stricter host
before freezing the installer contract. Registration is machine-wide because
hosts conventionally enumerate ASIO drivers from the machine registry. Runtime
objects and user configuration remain per-user. The installer, not the running
engine or driver DLL, requires elevation.

COM registration does not imply that the engine service is a COM server. The
driver-to-service control channel is a private, versioned IPC protocol. No
service implementation object is exposed to arbitrary COM clients.

## 32-bit and 64-bit Strategy

The supported primary target is native x64 Windows and an x64 driver DLL. A
separate x86 DLL is required for 32-bit DAWs because an in-process COM DLL must
match host bitness. One binary must never be loaded through a thunking proxy,
and a surrogate process is not part of the realtime design.

The two DLLs:

- are built from the same source and expose the same logical driver name and
  CLSID;
- install to separate architecture-qualified paths;
- register `InprocServer32` and the ASIO discovery key in the corresponding
  64-bit and 32-bit registry views;
- speak the same fixed-width, pointer-free shared-memory wire protocol;
- use explicit integer widths and byte offsets rather than native pointers,
  `size_t`, `long`, C++ containers, vtables, or process handles in shared data;
- have independent conformance tests and host-probe evidence.

The service remains x64. An x86 driver connects directly to the x64 service
because the control wire and shared-memory ABI are architecture-neutral. The
shared header includes magic, protocol major/minor, total byte size, endian
marker, connection generation, format, queue dimensions, feature bits, and
state. Every offset and size is range-checked before use.

The first REAPER milestone is x64 only. x86 packaging is not allowed to delay
that probe, but the shared-memory layout must be x86-safe from its first checked
in version. ARM64 and ARM64EC are explicitly deferred.

## Driver, Shared Memory, and Service Lifecycle

### Discovery and connection

1. The DAW activates the driver COM class and performs non-streaming identity,
   channel, clock, sample-rate, and buffer-size queries.
2. Driver initialization opens the per-user engine broker named pipe. This work
   occurs on a non-realtime driver control path.
3. If the engine is absent, the driver returns a bounded, host-safe unavailable
   result. Automatic service launch may be added later through a separate user
   broker, but the DLL never launches a process while under loader lock or from
   an audio callback.
4. The driver sends protocol version, driver build, architecture, host PID,
   client nonce, requested channel layout, sample rate, and block size.
5. The service authenticates the pipe peer, validates the PID and user, applies
   `VirtualAsioClientRegistry` capacity and active-format policy, and assigns a
   monotonically increasing connection generation.
6. The service creates the mapping and synchronization objects with restrictive
   ACLs, initializes every byte and header, then returns names plus a server
   nonce over the authenticated pipe.
7. The driver opens the objects, validates the complete header and nonce, marks
   itself ready, and only then permits streaming startup.

### Streaming

Each connection has two bounded SPSC directions: host output to engine and
engine output to host. Slots contain planar float audio plus fixed metadata such
as sequence, generation, sample position, timestamp, valid frame count, and
flags. Queue capacity and maximum channels/frames are negotiated before mapping
creation and cannot grow while streaming.

The ASIO callback never waits for the engine. On full host-to-engine transport,
it records a dropped block and advances. On empty engine-to-host transport, it
returns deterministic silence and records an underrun. Stale generations are
ignored. Event signaling is an optimization for the receiving non-callback
worker and is not a correctness dependency for queue ownership.

The service owns clock-domain and multi-client policy. The current registry
requires all active clients to match one sample rate, block size, and channel
shape. A future resampling mode must be an explicit protocol capability and
must not silently change this v1 behavior.

### Stop, crash, and restart

1. A normal host stop first disables new callbacks, waits for callbacks already
   entered to leave, marks the connection stopping, and sends a bounded
   disconnect request carrying the connection generation.
2. The service removes the client from graph routing before releasing its
   transport. It keeps the mapping alive until its own handles close; the
   driver closes mapped views and handles without deleting named objects.
3. A host crash is detected by pipe loss and/or the duplicated process-liveness
   handle owned by the service. The service silences and removes that client
   without stopping other clients.
4. A service crash causes driver transport operations to fail closed: DAW input
   receives silence, DAW output is discarded, and callbacks continue with
   bounded work. The driver reports reset/unavailable state on its control path.
5. A restarted service always issues a new generation and new nonce. A driver
   must reconnect and remap; it must never reuse the old mapping based only on
   its name.
6. DLL unload closes all driver-side handles after callbacks and control workers
   are quiescent. No background thread may execute code from an unloaded module.

## Object Naming and Security

Named objects are capabilities, not discovery mechanisms. Their names contain
no raw user name, executable path, preset name, session title, or audio channel
label.

Recommended forms are:

```text
\\.\pipe\LovelyRua.SystemAudioRoute.Engine.v1.<sid-hash>
Local\LovelyRua.SystemAudioRoute.Asio.Map.v1.<sid-hash>.<instance>.<generation>.<nonce>
Local\LovelyRua.SystemAudioRoute.Asio.DataReady.v1.<sid-hash>.<instance>.<generation>.<nonce>
Local\LovelyRua.SystemAudioRoute.Asio.SpaceReady.v1.<sid-hash>.<instance>.<generation>.<nonce>
```

`<sid-hash>` is a stable non-secret digest used only for namespace separation.
`<instance>` and `<nonce>` are cryptographically random values. Names are
bounded ASCII and comparison is ordinal. Use the local session namespace for
v1; `Global\` objects are prohibited unless a later Windows-service topology
proves they are necessary.

The broker pipe and kernel objects receive explicit DACLs granting the current
interactive user, LocalSystem when required, and no broad `Everyone` or
`Authenticated Users` access. The service verifies the connected client's
token and PID before returning object names. Mapping contents are untrusted:
all sizes, offsets, enum values, counters, versions, and arithmetic are checked
before dereference, and malformed peers are disconnected. Handles are created
non-inheritable. Audio memory is zeroed before publication and before reuse
where stale audio could cross client lifetimes.

The protocol major version is incompatible and must reject mismatches. Minor
versions negotiate feature bits and cannot reinterpret existing fields. A
connection generation and both nonces prevent a stale or guessed mapping from
joining a new session.

## Installation and Uninstallation

The installer performs an architecture-aware, transactional deployment:

1. Stop new driver activation and verify that target DLLs can be replaced.
2. Install versioned x64 and optional x86 DLLs with restricted write ACLs.
3. Register each architecture in its matching registry view.
4. Install or update the per-user engine startup mechanism separately from COM
   registration.
5. Run a registration probe using matching-bitness helper processes.
6. Commit installer state only after both requested views pass.

Upgrade must not overwrite a loaded DLL in place. Side-by-side versioned files
and a reboot-required fallback are acceptable; terminating DAWs without consent
is not.

Uninstall removes both ASIO discovery entries, both COM registry views, startup
registration, installed binaries, and installer-owned empty directories. It
requests service shutdown, waits only for a bounded interval, and schedules
loaded binaries for removal after reboot when necessary. Named mappings, events,
and pipes are lifetime objects rather than persistent installation artifacts;
closing all handles removes them. User presets, session files, and evidence logs
are retained by default and removed only through an explicit user-data option.

Uninstall and repair tests must cover partial x86/x64 installation, an absent
service, an active DAW holding the DLL, interrupted installation, stale registry
entries, and repeated uninstall. Cleanup must never enumerate and delete
objects by a broad name prefix.

## First REAPER Detection Milestone

The first host milestone is intentionally narrower than audio streaming. It is
complete only when all of the following are captured on the Windows test
machine:

1. A clean x64 install writes the expected 64-bit ASIO and CLSID entries, with
   no x86 requirement for this milestone.
2. Current x64 REAPER starts without a loader error and lists exactly one
   `System Audio Route Virtual ASIO` device.
3. Selecting the device causes COM activation and one bounded driver
   initialization attempt, proven by an external test counter or ETW/debug
   evidence rather than callback logging.
4. REAPER can query the driver's name, version, fixed channel counts, supported
   48 kHz sample rate, and one fixed buffer size without crashing or hanging.
5. With the engine absent, selection fails or remains unavailable cleanly and
   REAPER stays responsive.
6. With the engine present, the control handshake reaches an `initialized`
   state. Audio transfer is not required yet.
7. REAPER can deselect the driver and exit; reference counts reach zero and the
   DLL unloads with no surviving driver thread or kernel handle.
8. Uninstall removes the device from REAPER after restart and leaves no driver
   or COM registry entry.

The evidence bundle records Windows build, REAPER version, architecture,
installer and driver hashes, source commit, registry exports from both views,
Process Monitor or equivalent load evidence, probe output, and uninstall
verification. A screenshot alone is not acceptance evidence.

## Explicitly Not Implemented

This design does not claim completion of any of the following:

- host-facing ASIO interface declarations or ABI conformance;
- a driver DLL, class factory, exports, signing, installer, or registry writer;
- legal approval for independently authored ASIO compatibility definitions;
- event strategy, explicit object DACLs, or malformed-mapping fuzzing beyond
  the implemented pointer-free layout, Windows mapping owner/view, and bounded
  interlocked SPSC queue checks;
- service-side ASIO transport adapter and graph channel binding;
- ASIO callback scheduling, buffer switching, sample-position, timestamp, reset,
  overload, or latency reporting behavior;
- x86 binaries, ARM64 binaries, or WOW64 installation tests;
- automatic per-user service activation and crash restart;
- sample-rate or block-size conversion between ASIO clients;
- dynamic channel-layout changes while a host is active;
- multi-DAW streaming, DAW-to-DAW routing, or stalled-client eviction;
- code signing, release certificate protection, update rollback, and production
  installer recovery;
- REAPER detection evidence or compatibility results for Cubase, Live, FL
  Studio, or Studio One.

The existing `MockAsioTransport` and `VirtualAsioClientRegistry` validate only
in-process transport behavior and control-plane format arbitration. They are
inputs to this design, not substitutes for any item above.

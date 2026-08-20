# Virtual WASAPI / ACX transport boundary

This directory is a transport-contract spike, not a production virtual audio driver. It does not
contain device creation, INF packaging, signing, power management, APOs, or a complete ACX circuit.

`virtual_wasapi_transport_abi.h` is the shared kernel/user C ABI. All fields use fixed-width integer
types. The mapped section is one 64-byte-aligned header, one ring state, a fixed-stride slot table,
and interleaved audio payloads. Offsets are relative to the header and must be accepted only after
`validate_virtual_wasapi_transport_layout` succeeds against the actual mapped view size. Reserved
fields must remain zero when producing version 1.

## ACX integration points

Following the separation used by Microsoft's ACX AudioCodec sample, create the ACX stream and keep
the mapped transport in the stream context. The ACX stream's `EvtAcxStreamProcessPacket` equivalent
is the realtime data-plane integration point: copy one packet between the ACX packet buffer and the
next ring slot, publish/consume its sequence with acquire/release ordering, and update positions and
counters with interlocked operations. It must not allocate, map memory, wait on a passive queue,
open handles, or issue IOCTLs while processing a packet.

Following SysVAD's split between control handling and audio movement, expose query/attach/detach on
a separate sequential or manual passive-level WDF queue owned by the control device:

- `SAR_VWASAPI_IOCTL_QUERY_CAPABILITIES` returns `SarVirtualWasapiTransportCapabilities`.
- `SAR_VWASAPI_IOCTL_ATTACH_TRANSPORT` validates request sizes and direction, references the caller's
  section handle in the requestor process, maps it with non-executable protection, validates the full
  header and all ranges, then atomically installs one attachment in the stream context.
- `SAR_VWASAPI_IOCTL_DETACH_TRANSPORT` removes the matching attachment, clears the attached flag,
  waits for data-plane rundown outside `ProcessPacket`, unmaps the view, and dereferences the section.

Never trust `section_bytes`, offsets, strides, format fields, attachment IDs, or the user-provided
handle. Reject a second attachment unless replacement has completed. Tie mappings to WDF file-object
cleanup so process termination cannot leave a stale pointer. The IOCTL queue is control-plane only
and must be declared passive; `ProcessPacket` reads a rundown-protected immutable attachment pointer.

## Empty and absent peers

The producer never blocks. If `SAR_VWASAPI_RING_FLAG_RECEIVER_ATTACHED` is clear or the ring is full,
it drops the packet, advances no published producer sequence, and increments `dropped_frames`. A
consumer presented with an absent producer or an empty/malformed slot emits zeroed audio for the ACX
packet, increments `silence_frames` (and `malformed_packets` when applicable), and preserves monotonic
device position. Attach/detach increments `generation`; slots from an older generation are silence.

This policy is deliberately deterministic: loss is observable through counters, while kernel audio
callbacks never wait for a user-mode receiver.

# Native physical ASIO matrix editing

The optional native Audio devices editor configures physical ASIO inside the
existing matrix runtime, alongside WASAPI endpoints. It does not switch to the
legacy standalone PhysicalAsio mode or replace the WASAPI bindings.

## Configuration contract

- Add ASIO creates one capture/playback pair with stable endpoint IDs and a
  shared group ID. The current backend supports one physical driver group.
- The playback member becomes matrix clock master; existing WASAPI endpoint
  IDs, channel slices and native bindings are retained.
- Driver selection updates both group members. Inventory is registry-only;
  selecting a driver does not load its DLL on the UI thread.
- First channel is a zero-based native offset. Count is an explicit contiguous
  slice, separately editable for capture and playback, bounded by 256 channels.
- Sample rate and buffer frames are shared by the whole ASIO group. Draft text
  remains editable when invalid, but Apply is disabled until validation passes.
- Reported directional capabilities constrain channel ranges when available.
  Registry enumeration has no channel capability information: its placeholder
  format is not interpreted as a one-channel hardware limit. The runtime checks
  actual driver support when applying the configuration.
- Remove ASIO group removes both members after confirmation. If a WASAPI render
  remains, the first remaining render becomes master. If none remains, Apply
  stays disabled until a render endpoint is added.
- Applying a configuration involving ASIO requires confirmation because streams
  may reopen and removed channels may prune routes.

Legacy non-matrix configurations are still read-only. New matrix is an explicit
replacement operation. Arbitrary noncontiguous native channel lists and driver
control-panel invocation are not exposed by this editor.

## Verification boundary

The portable model regression checks pair creation and capacity, duplicate-group
rejection, shared driver/timing updates, independent channel slices, invalid
timing text, device disappearance, registry-only enumeration, WASAPI preservation
and clock handoff after group removal. CI must also compile the native window
and run startup/closure checks. Interactive hardware Apply and sound-content
acceptance are separate from these model checks; no new audio reliability result
is implied by this UI implementation.

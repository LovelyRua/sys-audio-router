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

## Interactive acceptance (2026-10-30/31)

VM24 (`SAR-WIN11-LAB`) ran native GUI `4b87c7b` in its interactive codex session
with a private session/log fixture. The service binaries were `d083672`; this
batch changes GUI/model code, not the engine runtime. Registry inspection found
no installed ASIO drivers in that test user's HKCU or native HKLM ASIO inventory.
Consequently no physical-driver Apply or audio-content pass is claimed.

| Flow | Observed result |
| --- | --- |
| Add WASAPI playback, then ASIO | Three draft endpoints appeared; a second ASIO group could not be added |
| Edit capture timing to 96000 Hz / 256 frames, select playback | Both fields matched; ASIO playback was master and the WASAPI endpoint remained present |
| Bind the WASAPI member to existing CABLE In 16ch | Binding remained beside the ASIO pair; Apply stayed disabled for the unbound ASIO driver |
| Remove ASIO group, confirm | Both ASIO endpoints disappeared; the WASAPI binding and 0/2 channel slice remained and became master |
| Apply the remaining WASAPI configuration | Dirty state cleared; independent CLI reported installed, stopped WasapiMatrix runtime |
| Enter channel count 0 | Range feedback appeared and Apply was disabled |
| Reload saved, confirm | Saved channel count 2 returned |
| Start audio, then Stop audio | Both actual states appeared in the GUI; this was an unrouted silent stream, not a sound-content test |
| Add an unbound ASIO draft again | Driver-required feedback and disabled Apply prevented accidental submission |
| Close GUI and stop private service | Both exited normally; temporary tasks and process-ledger entries were cleared |

No host tools, drivers, registrations or boot settings were installed or changed.
The independent read-only review found no concrete correctness bug in group
binding, timing synchronization, range validation or clock handoff. Actual
physical-ASIO enumeration/selection, driver capability rejection and sound tests
still need a test machine with an installed driver and suitable hardware.

Source `4b87c7b` passed
[CI 41075249863](https://github.com/LovelyRua/sys-audio-router/actions/runs/41075249863):
165 CTest cases, five early model/client/service checks, 15 script suites, eight
advisory ASan cases and six-window startup/graceful closure.
[Package 41075262830](https://github.com/LovelyRua/sys-audio-router/actions/runs/41075262830)
also passed with 169 CTest cases. Qt remains the default GUI entry.

# VB-CABLE integration delivery

Self-developed virtual WASAPI driver work is paused by the 2026-10-20 product
decision. Keep its source and evidence; do not spend release work on new kernel
features while ordinary third-party-cable workflows remain incomplete.

## Distribution boundary

The vendor's current distribution policy explicitly permits base VB-CABLE
bundling with origin and donationware attribution and applicable licensing
conditions: https://vb-audio.com/Services/licensing.htm . It explicitly excludes
A/B and C/D from bundles. A personal download or user-provided paid archive does
not authorize uploading those packages to a public repository or CI artifact.
Hi-Fi Cable is not covered by this base-product bundle option.

Only base Pack 45 is fetched by the package workflow. It comes from the official
HTTPS download host, has a pinned SHA-256, and retains the complete original ZIP
and extracted files, including the original end-user license/readme. The SAR
NOTICE and native Audio devices panel identify the vendor and donation model.
No proprietary source or binaries are committed to Git.

## Installer behavior

`SAR_BUNDLE_VBCABLE=ON` requires the native preview package option. The UI opens
the original signed x64 vendor setup after confirmation and signature checking.
It does not supply silent flags, accept terms, bypass security or reboot. UAC
cancellation is distinct from failure; no operation is retried automatically.
Opening setup is not reported as successful device installation. Setup workers
capture no UI objects and do not block the audio thread or window closure.

The driver is independent of SAR's lifecycle. SAR uninstall removes its local
setup copies, not the installed cable or shared Driver Store package. A/B,
Hi-Fi Cable and other existing WASAPI devices continue through the generic
endpoint enumeration/configuration path, not name-based rebinding.

## Verified batch

Revision `cca76d7`, package run `39391417458` (PR merge build
`0444fb83520f3f16fba2723a2eba431eb7fdbf65`):

- 166 package CTest cases passed.
- ZIP and EXE install/update/uninstall acceptance passed.
- Both installs retained the original archive with SHA-256
  `b950e39f01af1d04ea623c8f6d8eb9b6ea5c477c637295fabf20631c85116bfb`.
- Both validated the installed vendor EXE Authenticode signature and readme.
- Native preview launch and shared engine lifetime checks passed.
- Main CI run `39391411059`: 162 CTest cases, 15 script suites, seven advisory
  ASan cases and native window startup/closure passed.

No vendor driver was executed or installed by CI. This round does not establish
actual UAC/setup interaction, reboot recovery, sound quality or continuity.

## Remaining audio qualification

On VM24, preserve existing devices and use only a disposable routing session.
Check the actual device IDs and capabilities, not assumed cable names or stereo
channel counts. Windows CABLE Input is a render destination; CABLE Output is a
capture source. Test each direction with recorded signal evidence before
testing the combined path, to avoid accidentally building a feedback loop.

Required cases: selected channels and sample formats, common rates, sustained
playback/recording, silence transitions, stop/restart, endpoint disappearance,
re-enumeration after reboot, saved-session restoration, and concurrent DAW use.
Measure receiving-end signal continuity and counters separately from listening.
Hi-Fi Cable requires matching input/output rates. Do not call resampled or
mixed audio bit-perfect, and do not generalize base-cable results to A/B or Hi-Fi.

## VM24 interactive verification (2026-10-20)

The tested payload was the ZIP from package run `39391417458`, SHA-256
`d932c1b95baca5aa9c58985a5ed6f42ffc4e965855bc33a8571ce7dcaa4328d3`.
It was extracted into a unique lab directory, not installed over the user's
configuration. VM24 already had a working base VB-CABLE adapter. No driver was
installed, removed or updated; no default endpoint, boot security setting or
saved user session was changed, and no reboot was performed.

### Vendor setup entry

The native Audio devices button opened a confirmation and then the signed
VB-Audio UAC prompt. Cancelling UAC produced the neutral cancellation message
without retrying. On a second attempt, approving elevation opened the original
VB-Audio installer (version 2.1.5.8). It correctly identified the existing driver
and offered Remove Driver. That action was not selected; the vendor window was
closed. Thus setup launch and existing-install recognition passed, but clean
installation, removal and reboot recovery remain untested.

### Actual signal paths

Tests ran in the logged-on interactive desktop, at the endpoints' reported
48 kHz stereo shared format, using the packaged engine/CLI and the measurement
probe from driver repository revision `2af92b6`. An already installed SAR
experimental endpoint pair was only the opposite-side measurement fixture:

- probe -> VB render -> VB capture -> SAR matrix -> fixture render -> probe;
- probe -> fixture render -> fixture capture -> SAR matrix -> VB render -> VB capture -> probe.

Each path used four distinct endpoint IDs so direct cable loopback could not
bypass the matrix. Each ran disabled-before, enabled, disabled-after and
reenabled phases, followed by ten seconds of additional engine observation.

| Isolated run | Signal phases | Processed block delta | Final XRUN | Capture discontinuities | Render underflow |
| --- | --- | --- | --- | --- | --- |
| VB to SAR | 4/4 passed | 10370 | 0 | 0 | 0 frames |
| SAR to VB | 4/4 passed | 10433 | 0 | 0 | 0 frames |

All eight receiving-endpoint steady timeline checks had zero discontinuities,
position gaps and overlaps; each measured 95520-96000 capture frames. Disabled
routes were silent; enabled and reenabled routes had the expected signal.
Final dropped blocks and capture/render overflow counts were zero. Final
callback peaks were 44.8 and 142.7 microseconds respectively. Wait-timeout
counters were nonzero during the source-stopped portions; they are not hidden
or claimed to be zero. Both final runtime health snapshots were healthy.

The first attempt used an outdated probe and failed closed on missing telemetry
fields. It was not counted as sound evidence. A corrected run overlapping the
setup UI test passed signal/timeline checks but recorded one engine-side capture
discontinuity/XRUN in the VB-to-SAR reenabled phase. This remains evidence, not
discarded noise: the isolated repeat did not reproduce it, but its cause is not
established and long-duration/desktop-transition qualification is still open.

All engines stopped gracefully with exit code zero. The GUI and vendor setup
were closed; final audit found zero owned processes and zero owned scheduled
tasks. The original three Media adapters remained present and healthy. Local
raw evidence retains the initial, corrected and isolated results separately.

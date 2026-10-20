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

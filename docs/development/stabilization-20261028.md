# Stabilization gate

Feature expansion and user-facing preview delivery are paused following the
reported REAPER buffer-initialization failure and unusable GUI feedback.
WASAPI probe results are not DAW/Virtual-ASIO end-to-end acceptance.

## Verified code repairs

- `7b9ab41`: remove recursive control-mutex acquisition on invalid ASIO channel
  queries; test negative and upper-bound channels in both directions, then a
  valid query. Synchronize buffer capability reads with initialization.
- `c9b90f4`: explicitly draw matrix cell boundaries and selected/connected/muted
  states; muted routes no longer say ON. Add full-name channel tooltips and an
  axis label. Distinguish command rejection from a healthy status indicator.
  Finite gains at/below -60 dB are no longer mislabeled as silence; invalid
  gain display values have portable regression checks.

CI `40701253047` passed 162 CTest cases, 15 script suites, seven advisory ASan
cases, native model/parser checks and window startup/closure. Package run
`40701262340` passed 166 tests and both installer acceptance paths. These are
internal checks, not approval to deliver the preview to users. Visual changes
still require interactive verification. The reported REAPER failure has not
been reproduced or fixed by these changes.

## Release blockers, in order

1. Reproduce REAPER createBuffers failure with the installed DLL/engine versions,
   requested format, returned ASIO error and broker failure captured. Fix the
   proven failure, not a guessed block-size restriction.
2. Review state ownership between the native panel, Qt panel and persisted
   session. Consolidate the supported entry without silently losing Qt features.
3. Validate routing clicks during polling, gain edits and acknowledgements,
   mute/remove, selection changes and endpoint reconfiguration.
4. Verify complete layouts and long channel names at representative resolutions
   and DPI; review screenshots, not only successful window startup.
5. Run one normal-user flow: install, launch, configure, open REAPER, create
   buffers, pass audio both ways, save, close, restore and update/uninstall.

Keep the existing multi-block-size negotiation contract until its complete
adaptation path is audited. A suspicion about matching engine block sizes was
withdrawn after inspecting registry clock-domain validation; it is not a finding.

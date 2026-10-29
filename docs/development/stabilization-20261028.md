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

1. Validate fresh-install/upgrade driver identity and REAPER initialization.
   The default-pipe mismatch is now reproduced and repaired, with actual DAW
   playback verified; see `reaper-user-pipe-20261029.md`. The complete packaged
   install flow and the nonzero post-stop transient still need acceptance.
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

## October 29 internal pass

- `36a9799` removes the poll-time interaction lockout. A UI-thread-only bounded
  route queue serializes writes, collapses adjacent unsent fader changes, and
  projects unacknowledged edits over readback snapshots. Rejected writes discard
  dependent edits; lost transport clears pending intents rather than replaying
  them after reconnect. Queue saturation reports an error instead of growing
  without bound. No changes were made to realtime callbacks.
- CI `40805704004` and package run `40805709823` passed for this code. The native
  model test now covers stale polls, acknowledgement ordering, fader coalescing,
  rejected dependent edits, rapid reconnect, removed endpoints and queue bounds.
- VM24 was powered on with user authorization and reached at `10.88.0.4`.
  Its codex desktop session was active. Matching GUI/engine binaries were run
  from unique lab directories with a private session file. Connecting a route,
  dragging to -21 dB and muting were observed in the actual native interface;
  subsequent polling retained those values. These checks used an unconfigured
  engine and do not establish audio transport or REAPER acceptance.
- Inspection revealed that the fixed-size windows could strand the inspector
  outside the viewport after a console resolution change. `f17561e` enables
  normal window resizing for all three native panels; scrollable content is
  retained for small displays. This is not a complete responsive-layout redesign.
- The 0 dB button updated its numeric value but left the fader fill stale.
  `549bf78` explicitly invalidates the slider after programmatic updates.
  CI `40811372157` passed for `549bf78`, including both follow-up changes.
  The matching GUI was then exercised on VM24: maximize exposed the inspector
  and page controls; right-click selected without toggling; a drag to -15.1 dB
  followed by the 0 dB command updated both text and fader fill. Unmute changed
  the matrix from MUTE to ON; remove returned the route count from five to four
  and disabled the route inspector controls. Routine polling retained readback.

The lab has REAPER 7.41, but the initial read-only inventory found no matching
SAR ASIO registration. Therefore its current state cannot reproduce the user's
installed-driver failure. Establish a complete matched installation before
claiming a DAW test. The native panel still needs visual/interaction work,
including button affordances and small-window inspector access. No user-facing
package was delivered by this pass.

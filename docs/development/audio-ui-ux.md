# Professional audio UI/UX contract

This is the design and acceptance baseline for System Audio Route's Windows
control panels, especially the native VSTGUI replacement. It describes required
behavior, not a claim that every requirement is implemented. Current gaps remain
in `../product-readiness.md`; native acceptance is in `native-runtime-editor.md`
and `virtual-wasapi-gui-lab.md`.

## Product direction

The primary user patches DAW channels, physical interfaces and virtual endpoints
while listening. Prioritize signal-flow legibility, fast repeated operations,
stable control geometry and trustworthy state. Use a compact, neutral console
with restrained accents. Do not introduce marketing sections, decorative cards,
oversized headings, gradients or animations unrelated to signal or feedback.

Before implementing a surface, describe its user task, authoritative data,
editable draft, destructive actions and failure/recovery behavior. A visual
redesign must not silently change routing semantics or reset existing sessions.

## Visual system

- Reuse named tokens in `app/gui_vstgui/palette.h`; do not scatter new literal
  colors through individual windows. Current tokens are a baseline, not proof
  of sufficient contrast. Review text, focus and control contrast in context.
- Use the system UI typeface with a small, explicit type scale. Labels, values
  and section headings must have distinct roles; telemetry may use tabular
  numerals. Never scale text to viewport width or squeeze it until unreadable.
- Use a 4-unit spacing grid with consistent control heights and alignment.
  Controls must not resize when a value, label, status or hover state changes.
- Color supplements labels and shapes. Green must not ambiguously mean both
  selected and enabled; amber must not ambiguously mean pending and muted.
- Provide a visible focus indicator independent of route state. Disabled text
  must remain readable, and disabled controls must not look actionable.
- Use familiar icons for compact tools with accessible names and tooltips.
  Use the project's available native assets rather than adding a web icon
  dependency. Do not rely on hover to reveal essential status.

## State and interaction contract

| State | Meaning | Required presentation |
| --- | --- | --- |
| Absent route | No connection exists | Neutral cell; distinct from a muted connection |
| Enabled route | Engine confirms an active connection | Persistent connected indicator |
| Disabled route | Connection retained but not passing signal | Muted indicator; retained gain visible in inspector |
| Selected route | Current inspection target | Independent outline/focus, not an audio state |
| Pending command | Request not yet confirmed | Immediate, distinct pending feedback |
| Unavailable endpoint | Binding missing or inactive | Explicit unavailable label; no fake device substitution |
| Failed command | Engine rejected or transport outcome uncertain | Actionable error; actual readback, no automatic mutation retry |

Preserve existing documented matrix gestures unless a deliberate migration is
approved. Selecting a route to inspect it must not itself toggle the route.
Modifier gestures need a discoverable context-menu or keyboard equivalent.
Engine updates must not reset retained gain when enabling/disabling a route.

For pending edits, render local feedback on the next UI update, then distinguish
it from engine-confirmed state. Associate acknowledgements with request IDs and
versions. An old poll response must not undo a newer gesture. Coalesce gain
updates where appropriate and always deliver the final value; do not queue an
unbounded backlog. Measure input-to-feedback and input-to-engine-ack separately;
do not confuse either with audio latency or assert a threshold without evidence.

## Gain and meters

- Dragging gain retains pointer capture and a stable edit target until release.
  Polling cannot reposition the thumb beneath the user's pointer.
- Double-click resets to 0 dB without toggling enable. Keyboard adjustment and
  fine adjustment must be available, with explicit range, units and clamping.
- Silence, negative infinity, clipping and an unavailable meter are different
  states. Never display a fabricated zero or animate without sample evidence.
- Specify meter source, channel, peak/RMS semantics, dBFS range, update rate,
  release ballistics and peak-hold behavior before implementing it. These values
  are a design decision to test, not constants chosen by this document.
- Meter refresh is independent of inventory and slow diagnostic polling. UI
  updates consume bounded snapshots; audio callbacks never allocate, block,
  send control messages or manipulate views for metering.

## Matrix and device editing

- Label axes explicitly as source and destination. Follow the established axis
  orientation consistently across matrix, inspector and documentation.
- Keep channel labels aligned with cells while scrolling/paging; show device
  groups, stable identities and channel counts. Test long and duplicate names.
- A stopped engine and an absent device are not equivalent. Show inactive I/O
  only according to an explicit visibility choice; never fabricate active rows.
- Preserve draft selection, bindings and numeric edits during inventory refresh
  and reconnect. Reloading saved state is a separate confirmed discard action.
- Mark unsupported configurations read-only and explain the constraint. Do not
  erase physical-ASIO fields while editing WASAPI endpoints.
- Topology application, device removal and clock changes must communicate their
  stream-interruption impact before confirmation. Starting audio must not
  implicitly apply an unrelated dirty draft.

## Desktop layout and accessibility

This is not a mobile web UI. Test Windows viewport and DPI constraints instead.
Keep window chrome and close controls reachable. Use constrained layouts and
scrollable content without shrinking controls or hiding the final row. Account
for scrollbar space, long translations, popup placement and multiple monitors.

Every action needs keyboard focus, a readable name and a predictable tab order.
Check keyboard-only operation, focus restoration after dialogs and cancellation.
Do not trap focus in an off-screen control. Native accessibility coverage must
be measured and documented; a tooltip alone does not establish accessibility.

## Verification loop

1. Write the interaction/state contract and choose affected shared tokens.
2. Implement real commands and error states, not static mock data.
3. Add focused model/state/lifetime regression tests for the behavior changed.
4. Build through CI and exercise the actual binary on the designated Windows VM.
5. Capture before/after screenshots, critique alignment, readability, clipping,
   hierarchy and state distinction, then make at least one corrective iteration
   when a defect is found. A clean review does not require arbitrary restyling.
6. Record source SHA, artifact/run, environment, actions, observed results and
   untested cases. A startup smoke is not a user-flow or sound acceptance test.

Minimum coverage for affected surfaces:

| Dimension | Cases |
| --- | --- |
| Viewport | Small 870 x 659 console, 1280 x 720, 1920 x 1080; resize and scroll to last item |
| DPI | 100%, 125%, 150%, 200%; preserve readable controls and reachable chrome |
| Matrix | Empty, stereo, multidevice, large/paged topology; long/duplicate names |
| Gesture | Drag with polling, double-click reset, keyboard/fine gain, route toggle without gain loss |
| Connection | Offline at open, disconnect while editing, reconnect, rejected and uncertain commands |
| Lifecycle | Close during request, reopen, concurrent windows, engine survives panel closure |
| Feedback | Pending versus confirmed, clipping versus unavailable, no stale error after recovery |

Run applicable cases, not unrelated driver mutations. Mark skipped combinations
and their reason explicitly. Do not alter host display settings, install tools,
or weaken VM security merely to claim a check passed. Lab changes remain subject
to the user's authorization and must be scoped and restored as agreed.

## Optional design references

Frontend-design or UI/UX design skills may help critique hierarchy, consistency
and accessibility, but website patterns must be adapted to native audio work.
They are not required to build or contribute. No CLI installer or third-party
skill is implicitly approved by this document. Reuse open-source components only
after checking license compatibility, attribution and runtime suitability; never
copy commercial implementations or assume a manual grants source-code rights.

# Native UI language

The optional native preview supports English and Simplified Chinese. The main
navigation rail exposes System language / English / Chinese (Simplified).
Changes are saved for the next application launch, with an explicit restart
notice. They do not restart the engine or discard an open editor automatically.

## Preference and scope

The preference uses the existing Qt user setting:
`HKCU\Software\System Audio Route\System Audio Route`, value `language`.
Values are `system`, `en`, and `zh_CN`. Reads are bounded and malformed/missing
values fall back to System language. Failed writes are reported to the user.
System language selects Chinese for a Chinese Windows UI language, English
otherwise. Initialization is frozen for the process lifetime.

For isolated QA, `SAR_NATIVE_UI_LANGUAGE=system|en|zh_CN` overrides the preference
in the current process only. It is not persisted. Unknown override values fall
back to the saved preference. Production shortcuts do not set this override.

Translations use exact catalog matches. Dynamic UI fragments are translated
before composition; native device labels, endpoint IDs, preset names and backend
reason codes are not translated or rewritten. Uncatalogued technical error text
and validation text fall back to English. Window titles and the standalone framework menus retain
their existing English identity. Experimental Driver lab localization is not part
of this pass.

## Verification

`native_localization_smoke` checks UTF-8 and wide translations, exact fallback,
embedded NUL handling, enum/menu ordering and frozen startup selection without
writing user preferences. The native startup gate runs all six windows once in
English and once in Chinese, then checks normal process closure. This is not a
full accessibility audit or visual-fit proof; interactive keyboard and layout
acceptance are recorded separately.

## Interactive acceptance (2026-10-31)

VM24 (`SAR-WIN11-LAB`) used a private engine session and interactive native GUI.
Matching GUI/service binaries at `4120367` passed the following manual checks:

| Flow | Observed result |
| --- | --- |
| Launch with process-only Chinese override | Main navigation, header, matrix and inspector rendered Chinese without overlapping text |
| Tab through navigation to language, use Down | English preference saved and a Chinese restart notice appeared; current UI stayed Chinese |
| Save Chinese, close GUI, launch without override | New process read the saved Chinese preference and rendered Chinese |
| Focus language menu initialized at Chinese, use Up | English was selected and saved correctly, proving numeric/display menu state synchronization |
| Add playback, focus device placeholder, use Up | Last actual device selected rather than the first; native device label was unchanged |
| Set channel count 0 | Chinese range feedback appeared and Apply was disabled |
| Add a second virtual ASIO draft, focus its menu, use Up | Selection returned to the original device while both draft entries remained; no topology was applied |
| Open Presets | Existing `guiqa20261029` name was unchanged; labels/buttons fit; an English dynamic count was found and corrected in a follow-up |
| Open Diagnostics, drag scrollbar to bottom | Chinese counters and the final explanatory row were visible; unavailable telemetry remained explicitly unknown, not fabricated zero |

Window captions/framework menus and raw backend technical messages remain English
as described above. These checks cover selected keyboard paths, not complete
screen-reader accessibility. No audio routing, driver installation or sound-content
acceptance was performed in this localization round.

The follow-up GUI at `0a8dab2` was exercised with the existing nonempty preset
list: the count rendered in Chinese and `guiqa20261029` remained unchanged.
The correction reads the displayed list after ownership transfer, rather than
the moved-from response vector. Temporary GUI processes/tasks were cleaned up;
the private service retained PID 5360 across GUI restarts and was then stopped
using its normal stop helper. The VM user's language value was restored to its
original absent state. No existing preset was overwritten or deleted.

One intermediate CI run,
[`41157804979`](https://github.com/LovelyRua/sys-audio-router/actions/runs/41157804979),
passed the English startup check but exceeded the unchanged fifteen-second
deadline for Chinese, without stdout/stderr diagnostics. Its root cause is not
established. Subsequent startup checks must not be treated as a root-cause fix;
`--startup-smoke` now logs initialization and per-window open/close phases to
make a recurrence actionable. This tracing is not enabled in ordinary GUI use.

The final source revision `64f2d6e` passed both language startup/closure gates;
Chinese completed in 7.4 seconds against the unchanged fifteen-second deadline.
The [GUI package run](https://github.com/LovelyRua/sys-audio-router/actions/runs/41158641730)
passed all 170 tests. The corresponding
[full CI run](https://github.com/LovelyRua/sys-audio-router/actions/runs/41158638112)
also records the native startup traces and sanitizer results. These automated
checks supplement, rather than replace, the interactive acceptance above.

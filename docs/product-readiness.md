# Product Readiness

This file tracks what stands between the current alpha and something a normal
user can install and trust. It complements `docs/roadmap.md` (engineering
phases) with product-level gaps found in the September 2026 product review.

Status legend: **Done** (implemented and covered by CI), **Open** (work we can
do in the repository), **External** (needs something outside the repository:
a certificate, hardware, a driver test machine).

## Release and delivery

| ID | Item | Status |
| --- | --- | --- |
| REL-1 | Authenticode signing of installer, EXEs, and driver DLL | External: needs a code-signing certificate; wire `signtool` into the package workflow once available |
| REL-2 | Versioned releases | Done: pushing a `vX.Y.Z` tag that matches `CMakeLists.txt` publishes a prerelease with installer, ZIP, and `SHA256SUMS.txt` |
| REL-3 | Icon, version resource, About/version display | Done: every shipped executable and the driver carry VERSIONINFO; executables carry the icon; the version shows in the control panel |
| REL-4 | Update check | Open |
| LIC-1 | Third-party license texts in the package | Done: libsamplerate and ASIO SDK licenses ship under `licenses/`; `NOTICE.md` lists Qt, libsamplerate, ASIO SDK, and the VC++ runtime |
| FEAT-1 | Virtual WDM/WASAPI endpoints | Open: blocked on the kernel-driver decision spike and a driver test machine |
| QUAL-1 | Release soaks (second 8 h pairing, 24 h, multi-DAW) and more DAWs/interfaces | External: needs hardware time |

## Lifecycle and reliability

| ID | Item | Status |
| --- | --- | --- |
| LIFE-1 | One engine lifecycle | Done: the engine always runs detached and outlives the control panel; closing the window asks to keep or stop it |
| LIFE-2 | Start at login | Done: per-user Run entry launches the engine headless and restores the session. A tray icon is still Open |
| OBS-1 | Logs and crash dumps | Done: rotating `engine.log`, minidumps under `logs\crashdumps`, "Logs" button and error-bar shortcut. The Export button bundles logs, session, dumps, and a system summary into a zip |
| INST-1 | Graceful update/uninstall | Done: installers call `sar_engine_service --stop`, ask the GUI to close, and only then force-kill |
| INST-2 | ASIO registration scope (per-user vs all users, cross-account UAC) | Open |
| INST-3 | Per-user control pipe name | Done: the default pipe name is now `sys-audio-route-control-<SID>` (`core/platform/windows_current_user_sid.cpp`), computed independently by the engine, the control CLI, the bootstrap launcher, and the GUI so two Windows users never contend for one pipe. An explicit `--pipe NAME` still overrides it for tests and lab tooling |
| DRV-1 | Sample-rate negotiation with the DAW | Open: engine-side rate change or conversion needed |
| DRV-2 | Real latency reporting from `getLatencies` | Open: needs a measured round-trip latency model |
| DRV-3 | ASIO Control Panel button opens the app | Done |
| DRV-4 | Block-size/topology change without engine restart | Open |
| ISO-1 | Out-of-process host for vendor ASIO drivers | Open: design decision needed |

## UX

| ID | Item | Status |
| --- | --- | --- |
| UX-1 | Localization | Done: all display strings in `Main.qml` and the GUI's own C++ (`EngineController`, `PresetStore`) use `qsTr()`/`tr()`; ships English and Simplified Chinese (`app/gui/i18n/Sar_zh_CN.ts`), selectable in the sidebar, applied on next launch. Deep engine-side validation error strings (`core/control`) stay English — scoped out, tracked separately |
| UX-2 | Keyboard focus and accessible names | Partly done: focus rings and accessible names on shared controls; matrix cell keyboard navigation is Open |
| UX-3 | Plain-language diagnostics | Done: a summary card states Healthy / Glitches / Fault with advice |
| UX-4 | Error presentation | Done: two-line wrapped error bar with Copy and Open logs |
| UX-5 | Endpoint naming | Withdrawn: the "Endpoint ID" field names the matrix endpoint; native devices are already chosen from a list. A friendlier auto-generated name is a nice-to-have |
| UX-6 | First-run guidance | Partly done: `docs/user-guide.md`; an in-app walkthrough is Open |
| UX-7 | Preset management | Done: overwrite confirmation and delete. Rename and import/export are Open |
| UX-8 | Second launch focuses the existing window | Done |
| UX-9 | Per-channel meters and mixer view | Open |
| UX-10 | Engine-provided endpoint families (replace name matching in `Main.qml`) | Open |
| GUI-1 | Split `Main.qml`; add Qt Quick Test | Open |

## Engineering and process

| ID | Item | Status |
| --- | --- | --- |
| CI-1 | Warnings-as-errors, static analysis | Open |
| CI-2 | Mutation/fuzz coverage of decoders | Done for the control wire protocol, session, and preset codecs (`control_codec_mutation_smoke`) |
| CI-3 | Sanitizer job | See the advisory ASan job in `.github/workflows/ci.yml` |
| SEC-1 | No credentials in docs or arguments | Done for docs and the WinRM wrappers that take a password (`SAR_TEST_PASSWORD`) |
| SEC-2 | Control-client authorization | Open |
| OSS-1 | CONTRIBUTING, SECURITY, issue template | Done |

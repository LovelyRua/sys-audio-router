# Contributing

Thanks for helping. This project optimizes for audio stability, not feature
churn, so small, well-tested changes are the easiest to merge.

## Before you start

- Read `AGENTS.md` (realtime safety rules, module boundaries) and
  `docs/development.md` (build and test).
- Open an issue for anything larger than a bug fix so the approach can be
  agreed first.

## Build and test

```bat
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The GUI additionally needs Qt 6.8 (`-DSAR_BUILD_GUI=ON`). CI builds and
acceptance-tests the installer for every pull request.

## Rules of thumb

- No allocation, locks, file I/O, logging, or COM activation in the audio
  processing path.
- Public behaviour changes need a smoke test.
- Keep diagnostics observable: log to the engine log file, never only to a
  console.
- Use `SAR_TEST_PASSWORD` for the WinRM lab scripts; never commit credentials.
- Match the existing commit style: an imperative summary line, one logical
  change per commit.
- New GUI display text must be wrapped in `qsTr()` (QML) or `tr()` /
  `QCoreApplication::translate("EngineController"/"PresetStore", ...)` (C++),
  and `app/gui/i18n/Sar_zh_CN.ts` needs a matching `<message>` entry (source
  string plus translation) or the string silently falls back to English for
  Chinese users. Values compared against by other code (e.g.
  `engine.wasapiRuntimeHealth`, `engine.runtimeMode`) must stay untranslated
  tokens; translate them for display only, in QML, the way
  `runtimeHealthLabel()`/`runtimeModeLabel()` in `Main.qml` do.

## Pull requests

Describe what changed and how you verified it (which CTest targets, which
hardware). Hardware-dependent claims need evidence from a real device or DAW.

# Engine Session Persistence

`sar_engine_service --session FILE` keeps the engine's desired control state in
a versioned session file. `FILE` is interpreted as a UTF-8 Windows path.

```bat
build\sar_engine_service.exe --session "C:\Users\codex\音频\engine.sar-session"
```

When the file does not exist, the service starts with the built-in preset, no
audio runtime configuration, and `auto_start=false`, then creates the file. A
valid file restores its preset and desired runtime configuration. If
`auto_start` is true, the service also attempts to start that runtime.

Runtime restoration is best effort. Missing endpoints or another WASAPI open or
start failure is reported to stderr while the named pipe remains available. The
loaded desired runtime configuration remains in the persisted session until a
successful `ConfigureAudioRuntime` replaces it. This also means a later preset
change cannot erase a runtime configuration whose initial restoration failed.
Successful configuration persists the service's reconciled preset alongside
the runtime configuration. Successful startup restoration also synchronizes
and saves that preset; an unsuccessful restore preserves the loaded desired
configuration and auto-start intent. Unknown preset ports are not removed by
guessing their names.

Malformed, oversized, unsupported, and unreadable existing files produce a
stable `session_warning` line. The service stays online with the default
session, disables persistence for that invocation, and never replaces the
original file.

Accepted preset mutations and accepted configure, start, and stop runtime
commands trigger persistence. Only explicit start and stop commands change the
auto-start intent. Queries do not write. Virtual ASIO device definitions are
retained when these other fields change. Writes encode to a temporary file in
the same directory and replace the destination with `MoveFileExW`; write
failures are reported to stderr and do not change an already accepted command
into a rejection. Graceful
shutdown makes one final save attempt while preserving the desired `auto_start`
value from the last accepted control command.

`--session` cannot be combined with the legacy `--wasapi-render`,
`--wasapi-duplex`, `--capture-id`, or `--render-id` startup options. `--pipe`
and `--once` remain available.

## Windows acceptance

Run the persistence acceptance against an existing Windows build:

```powershell
scripts\windows-engine-session-acceptance.ps1 `
  -BuildPath build `
  -DiagnosticsDelayMilliseconds 1000
```

The script creates a new session, configures a default WASAPI render runtime,
changes a route gain, starts the runtime, and force-terminates the first service
process. It then starts a second process from the same file and requires the
runtime to be installed, auto-started, and processing audio blocks. It also
configures and replaces matrix endpoints in real engine processes, compares
the saved preset to the live service, and checks stopped and running restores,
rejected configuration, unavailable devices, retained Virtual ASIO definitions,
and preservation of unknown old ports. The default render endpoint must expose
a 48000 Hz mix format. A final service invocation loads a deliberately malformed
session and must keep the
named pipe online without changing the malformed file's SHA-256 hash.

The final `engine_session_acceptance` line is machine readable. A passing run
reports `passed=1`, a positive `restored_processed_blocks` value,
`matrix_persistence_verified=1`, `corrupt_preserved=1`, and
`cleanup_complete=1`. Per-process stdout and stderr logs remain in
`output_directory` for diagnosis.

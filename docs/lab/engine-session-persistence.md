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

Malformed, oversized, unsupported, and unreadable existing files produce a
stable `session_warning` line. The service stays online with the default
session, disables persistence for that invocation, and never replaces the
original file.

Accepted preset mutations and accepted configure, start, and stop runtime
commands trigger persistence. Queries do not write. Virtual endpoints are not
part of `SessionDocument`, so create and remove endpoint commands do not rewrite
the session file. Writes encode to a temporary file in the same directory and
replace the destination with `MoveFileExW`; write failures are reported to
stderr and do not change an already accepted command into a rejection. Graceful
shutdown makes one final save attempt while preserving the desired `auto_start`
value from the last accepted control command.

`--session` cannot be combined with the legacy `--wasapi-render`,
`--wasapi-duplex`, `--capture-id`, or `--render-id` startup options. `--pipe`
and `--once` remain available.

# REAPER user-scoped ASIO broker repair

## Failure and repair

The engine's default control pipe includes the Windows user SID. Its ASIO
brokers append a device token to that full name. The registered ASIO DLL and
the unregistered DLL's fallback still constructed the pre-SID name. A host
could load the DLL, enumerate fallback channels, then time out creating buffers
even with a running engine.

`dbc5d58` centralizes that derivation in
`core/platform/windows_virtual_asio_pipe_name.h` and uses it in the engine,
registered-instance resolution and runtime fallback. Explicit engine pipe names
and the unregistered development DLL's environment override remain available.
No audio callback, sample format or block-size contract was changed.

The COM smoke now discovers a registered instance without an environment
override, then separately creates buffers through the unregistered default
path. SID tests check both default and explicit-control-pipe derivation. CI
`40842965148` passed the full Windows build, CTest, automation-script suites,
native startup check and advisory ASan job. CI lab artifacts now include the
matching ASIO DLL, registration utility and host probe, with SHA256 identities.
Package run `40842971187` also passed after retrying a configure-time DNS failure
downloading the hash-pinned official VB-CABLE archive. That retry did not bypass
the hash check or change product code.

## VM24 controlled comparison

Target: SAR-WIN11-LAB, interactive codex session, REAPER 7.41, existing base
VB-CABLE. The engine used an isolated session/log directory; REAPER used a
separate `-cfgfile`. No existing REAPER or engine was taken over.

The first current-user registration attempt produced **No ASIO drivers**, not
the reported buffer failure. That setup was withdrawn. Subsequent tests used
temporary machine-level registration, matching normal installer discovery.
Registration scope remains a separate product-compatibility item.

The old engine and its VB-CABLE render configuration were held constant while
only the DLL was replaced. The relevant SHA256 identities were:

- Engine: `11FD8E7F9EEA5233DF2A47D7099E303849A2D0DFDA2326F7588B57D7B107CB98`.
- Old DLL: `71F279BF4EFFE970234E6977AAD0E814A63838FE51D60C5F56E35DFC8F1843FC`.
- Repaired DLL: `C5D304C57BEC68D5FE2DA4F0DBEEF655F23F6BD850182F553087DA5273E1AD92`.

Old DLL: REAPER displayed **Error initializing ASIO buffers**, loaded the
expected DLL, and never became an active producer. The host probe returned
`create_buffers result=-1000`, reporting a named-pipe connection timeout.

Repaired DLL: the same probe discovered the engine's preferred 128 frames,
created buffers, delivered 369 callbacks in its one-second test (minimum 300),
and exited successfully. Actual REAPER displayed 48 kHz, 128 samples and 2/2
channels; the engine reported one active producer.

## Actual playback, not just initialization

An isolated one-track REAPER project played a generated 1 kHz stereo tone at
-18 dBFS. Playback used REAPER's ReaScript API, not the ASIO host probe. A
separate interactive WASAPI receiver measured the VB-CABLE capture endpoint:

| Phase | Frames | Peak | RMS | Non-finite / errors |
| --- | ---: | ---: | ---: | ---: |
| REAPER stopped, before playback | 142752 | 0 | 0 | 0 / 0 |
| Actual REAPER playback | 145440 | 0.125888824 | 0.0866553784 | 0 / 0 |
| After stop, REAPER still open | 75648 | 0.0848121643 | 0.000308360983 | 0 / 0 |
| After REAPER quit | 82752 | 0 | 0 | 0 / 0 |

The nonzero post-stop result is retained, not relabeled as silence. Aggregate
level evidence does not locate its waveform or establish its cause. This pass
does **not** certify a click-free stop transition, recording into REAPER,
duplex continuity, multiple DAWs or a full installer/upgrade user flow.

Final engine diagnostics had zero XRUNs, dropped blocks, producer underflows,
producer overflows, clipped samples and non-finite samples. Peak callback time
was 864.4 us. Old failing blank REAPER was aborted only after verifying its PID,
path and private cfgfile; that was not a graceful-close test. Repaired REAPER
saved only the generated test project and exited through its normal quit action.

The engine was stopped with `--stop`. Cleanup audit found zero test audio
processes and owned REAPER tasks, no remaining temporary user/machine ASIO
registration, and the same three healthy pre-existing audio adapters. No host
software, host network settings or kernel drivers were installed or changed.
Raw logs remain in the local ignored stabilization artifacts directory.

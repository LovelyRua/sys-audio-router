# Cakewalk Virtual ASIO Acceptance - 2026-07-29

## Environment

- Windows `10.0.26100.4770`
- Cakewalk by BandLab `29.09.0.098`
- REAPER `7.78 x64`
- System Audio Route commit `0de7716`, plus the cross-DAW acceptance helper
- Virtual ASIO format: 48 kHz, stereo input/output, 128 frames
- Physical render endpoint: `CABLE Input`

## Cakewalk Setup

Cakewalk was installed with the `BandLab.Cakewalk` winget package. In
Preferences, all ReaRoute inputs and outputs were disabled and
`System Audio Route Virtual ASIO Input 1` and
`System Audio Route Virtual ASIO Output 1` were selected. The resulting
`AUD.INI` contained a
`[System Audio Route Virtual ASIO (1 in, 1 out)]` section with both
`MME.DriverMap.UseWaveIn1=1` and `MME.DriverMap.UseWaveOut1=1`.

Cakewalk loaded `SystemAudioRouteVirtualASIO.dll` from the tested build. Creating
an empty project negotiated 48 kHz and started the audio engine. Before the
cross-host interval, diagnostics showed one active producer with continuing
producer, consumer, mixer, and graph callback activity.

The unactivated Cakewalk build does not permit saving the empty project.
Therefore the automated cross-DAW helper deliberately requires the first host
to be open, configured, and actively producing; it does not claim to automate
Cakewalk's license or project UI.

## Simultaneous DAW Result

`windows-winrm-cross-daw-acceptance.cmd` preserved the existing Cakewalk process
and launched REAPER in the same interactive Windows session. It verified that
both processes loaded the exact tested Virtual ASIO DLL and that the engine
reported two active producers.

The 30-second measured interval reported:

- 22,493 pushed blocks against an 18,000-block minimum
- 22,492 consumed blocks
- 11,282 physical mix cycles against a 9,000-cycle minimum
- 11,269 processed graph blocks
- zero dropped blocks
- zero xruns
- a 745.1-microsecond peak graph callback

After measurement, the helper terminated only the REAPER process and its
temporary Scheduled Task. It also requires the active producer count to return
from two to one, catching leaked or delayed client teardown.

## Repeat Command

Start the engine and an active Cakewalk project in the interactive session, then
run:

```bat
scripts\windows-winrm-cross-daw-acceptance.cmd ^
  192.168.123.123 codex <password> ^
  <remote-build-path> Cakewalk ^
  "C:\Program Files\REAPER (x64)\reaper.exe" ^
  cross-daw 30
```

The command refuses to take over an existing second DAW, verifies duration and
cadence thresholds from counter deltas, and cleans up only identities it
created.

## Remaining Gates

- Extend cross-DAW validation to release duration.
- Exercise real projects with non-silent output from both DAWs.
- Add long-duration per-client cadence and clock-adaptation evidence.

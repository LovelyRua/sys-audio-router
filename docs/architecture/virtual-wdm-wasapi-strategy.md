# Virtual WDM/WASAPI Strategy

Virtual WDM/WASAPI endpoints let normal Windows applications use the routing engine. This includes OBS, Discord, browsers, games, media players, and apps that only understand standard Windows audio devices.

## Goals

- Create multiple virtual playback endpoints that send audio into the engine.
- Create multiple virtual capture endpoints that receive audio from the engine.
- Preserve stable endpoint names and identities.
- Support stereo first, then wider channel layouts.
- Survive app restarts and endpoint reopens.
- Report failures clearly.

## Non-Goals for v1

- Replacing the Windows audio policy system.
- Per-application routing without the user selecting a virtual endpoint.
- Complex endpoint DSP in the driver.
- Arbitrary endpoint creation from the realtime audio thread.

## Research Baseline

Use Microsoft's SysVAD sample and Windows audio driver documentation as the starting point for virtual endpoint research.

As of July 2026, standard Windows audio endpoints are a kernel-driver problem.
Microsoft's modern ACX model is implemented on KMDF, while SysVAD is the mature
WDM/PortCls reference. A user-mode-only endpoint is therefore not a viable v1
assumption. Keep policy, routing, persistence, and DSP in the service, but prove
the smallest safe kernel streaming boundary before committing to a product
driver.

Primary references:

- [ACX audio class extensions overview](https://learn.microsoft.com/windows-hardware/drivers/audio/acx-audio-class-extensions-overview)
- [ACX version information](https://learn.microsoft.com/windows-hardware/drivers/audio/acx-version-overview)
- [SysVAD virtual audio device sample](https://learn.microsoft.com/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/)

## Decision Spike

Time box: five engineering days. Throwaway code is expected.

1. Build and test-install the smallest ACX 1.1 stereo render endpoint. Add a
   capture endpoint only after render data reaches a bounded receiver.
2. Keep SysVAD/PortCls as the fallback baseline; do not maintain two production
   prototypes.
3. Move audio to a user-mode receiver through a fixed-capacity, nonblocking
   transport. Measure block size, copy count, loss behavior, and service restart.
4. Record WDK version, supported Windows versions, test-signing workflow,
   production signing requirements, install/uninstall behavior, and reboot needs.
5. End with one decision: ACX, SysVAD/PortCls, or stop Phase 4 with a concrete
   blocker. Do not let an inconclusive spike turn into an open-ended driver fork.

Spike acceptance evidence:

- Endpoint appears in Windows Sound settings with a stable identity.
- A normal application opens it and transfers non-silent stereo samples.
- A user-mode receiver observes those samples without an unbounded wait.
- Stopping the receiver yields deterministic silence or failure, never a hung
  audio engine.
- Uninstall removes the endpoint and package cleanly.

The first investigation should answer:

- What is the minimal virtual playback/capture endpoint shape?
- How does endpoint audio reach user-mode safely?
- What is the best transport boundary between driver and engine service?
- How much endpoint state must live in the driver?
- How are endpoint names and persistent IDs managed?
- What are the signing and installation requirements for a developer build?

## Endpoint Model

Initial endpoint proposal:

- `SAR Playback 1`: app audio into engine, stereo.
- `SAR Playback 2`: app audio into engine, stereo.
- `SAR Capture 1`: engine audio out as virtual microphone, stereo or mono-compatible.
- `SAR Capture 2`: engine audio out as virtual microphone, stereo or mono-compatible.

Later versions can support user-created endpoints and higher channel counts.

## Service Restart Behavior

The driver and endpoint layer must have a defined behavior if the engine service is not running.

Possible policies:

- Endpoint opens but produces silence until the service returns.
- Endpoint open fails with a clear error.
- Driver buffers a tiny amount then drops until service reconnects.

Initial preference: fail or silence predictably; never block indefinitely.

## Test Applications

First compatibility set:

- OBS.
- Discord.
- Chrome or Edge.
- A game with selectable audio output.
- Windows Sound settings.
- Windows Sound Recorder or equivalent capture test.

## Prototype Milestones

1. A virtual playback endpoint appears in Windows Sound settings.
2. A simple app can play audio to it.
3. Audio reaches a user-mode test receiver.
4. Engine can route that audio to a physical output.
5. A virtual capture endpoint appears in Windows Sound settings.
6. A simple recording app can capture engine output.
7. OBS and Discord can use the virtual capture endpoint.
8. Multiple virtual endpoints can exist without identity confusion.

## Risks

- Kernel driver complexity.
- Driver signing.
- Endpoint persistence bugs.
- App-specific format assumptions.
- Sleep/wake failure.
- Service restart edge cases.
- Audio engine and driver version mismatch.

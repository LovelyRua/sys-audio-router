# Windows Audio Options

This document breaks the Windows backend into separate technical paths. The project should evaluate each path independently before committing to driver architecture.

## Backend Categories

| Category | Purpose | First Use |
| --- | --- | --- |
| WASAPI render/capture | Talk to physical Windows audio endpoints | Phase 2 |
| WASAPI loopback | Capture system or endpoint playback | Phase 2 |
| Virtual ASIO | Low-latency DAW bridge and multi-DAW routing | Phase 3 |
| Virtual WDM/WASAPI endpoint | Expose playback/capture devices to normal apps | Phase 4 |
| APO | Optional endpoint processing hook research | Not first implementation |
| Kernel streaming / WaveRT | Driver-level virtual endpoint foundation research | Phase 0/4 |

## Shared Device Model

All backend implementations should report devices through the shared platform model in `core/platform/audio_device.h`.

The model requires:

- Stable device ID.
- Human-readable label.
- Backend kind.
- Direction: input, output, or duplex.
- One or more supported formats.
- Flags for default and virtual devices.

Backend-specific details can be added later, but the control plane should start from this shared descriptor so UI, presets, diagnostics, and tests do not depend on WASAPI, ASIO, or virtual driver internals.

`AudioDeviceRegistry` aggregates one or more backend providers and validates the combined device list. Backend-local enumeration errors and cross-provider ID conflicts must be surfaced before the control plane publishes devices to UI or preset binding code.

## WASAPI Physical Backend

The physical backend should be the first real-device integration because it does not require a custom driver.

The first implementation step is `WindowsWasapiDeviceProvider`, which enumerates active render and capture endpoints through WASAPI and reports them as shared `AudioDeviceDescriptor` values. It does not open streams yet.

Use it to validate:

- Device enumeration.
- Shared versus exclusive mode behavior.
- Format negotiation.
- Buffer scheduling.
- Clock drift detection.
- Endpoint hot-plug.
- Sleep/wake recovery.

This path gives the realtime engine real-world timing pressure before the project enters driver work.

## WASAPI Loopback

Loopback capture is useful for early routing experiments and diagnostics.

It can validate:

- System audio capture.
- Endpoint clock behavior.
- Basic "app audio into engine" workflows.

Loopback is not a substitute for real virtual playback devices. It captures what is already being rendered to an endpoint, while a virtual playback device lets an app intentionally send audio into the engine.

## Virtual ASIO

Virtual ASIO is the first flagship platform feature.

It should be treated separately from WDM/WASAPI virtual devices:

- DAWs discover ASIO through ASIO driver registration and host-specific behavior.
- ASIO buffer negotiation is DAW-driven.
- ASIO channel maps can be wider and more production-oriented than consumer Windows endpoints.
- Multi-client behavior is not guaranteed by the ASIO model and must be designed carefully.

The first ASIO prototype should focus on controlled DAW compatibility, not broad app support.

## Virtual WDM/WASAPI Endpoints

Virtual Windows endpoints are needed for non-DAW applications such as OBS, Discord, browsers, games, and system audio.

The research baseline should include Microsoft's SysVAD sample and the Windows audio driver documentation.

Key questions:

- Can the endpoint be implemented with acceptable driver complexity?
- How many playback and capture endpoints can be created and persisted?
- How are channel count and format changes exposed to apps?
- How does the endpoint transport audio to the user-mode engine service?
- What happens during engine service restart?
- What is the rollback path if driver installation fails?

## APO Research

Audio Processing Objects are not the first implementation path.

They may become useful for endpoint processing or integration with Windows audio policy, but they should not be used to implement the main routing engine. The main graph needs explicit control, many endpoints, DAW routing, diagnostics, and cross-platform-ready semantics.

## Recommended Phase Order

1. WASAPI physical backend.
2. WASAPI loopback capture.
3. Virtual ASIO prototype.
4. Virtual WDM/WASAPI endpoint prototype.
5. Optional APO research only if a concrete use case appears.

This order reduces unknowns before kernel-mode work.

## References

- Microsoft Windows audio driver documentation: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/
- Microsoft SysVAD sample: https://github.com/microsoft/Windows-driver-samples/tree/main/audio/sysvad
- Microsoft WASAPI documentation: https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi
- Steinberg developer portal for ASIO SDK access: https://www.steinberg.net/developers/

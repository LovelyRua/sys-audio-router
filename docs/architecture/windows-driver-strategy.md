# Windows Driver Strategy

Windows is the first implementation target. The project needs deep Windows audio integration, especially virtual ASIO and virtual WDM/WASAPI endpoints.

This document is intentionally conservative. Driver work is expensive to debug, hard to sign, and easy to destabilize.

## Goals

- High-performance virtual ASIO.
- Multiple simultaneous ASIO clients where practical.
- DAW-to-DAW routing without device contention.
- Multiple virtual WDM/WASAPI playback and capture devices.
- Stable operation with OBS, Discord, browsers, games, and common DAWs.
- Minimal kernel-mode complexity.

Detailed option analysis lives in:

- [Windows Audio Options](windows-audio-options.md)
- [Virtual ASIO Strategy](virtual-asio-strategy.md)
- [Virtual WDM/WASAPI Strategy](virtual-wdm-wasapi-strategy.md)

## Process Boundary

The preferred model is:

- Driver layer exposes endpoints and performs the minimum required low-level transport work.
- Engine service owns routing, mixing, diagnostics, and policy.
- UI controls the service through IPC.
- Plugin hosts run outside the engine service.

Kernel-mode code should not own product policy, presets, plugin logic, or routing graph complexity.

## Virtual ASIO

Virtual ASIO is the highest-priority platform feature.

Required research areas:

- ASIO driver registration and discovery.
- Multi-client ASIO behavior.
- Buffer negotiation.
- Sample position and time info.
- Channel naming and dynamic channel maps.
- DAW compatibility quirks.
- 32-bit versus 64-bit host considerations.

Initial target DAWs:

- REAPER.
- Cubase.
- Ableton Live.
- FL Studio.
- Studio One.

The first success criterion is not broad compatibility. It is stable operation with one or two DAWs under controlled buffer sizes, then gradual expansion.

## Virtual WDM/WASAPI

Virtual WDM/WASAPI devices make normal Windows applications usable with the engine.

Required research areas:

- Playback endpoint implementation.
- Capture endpoint implementation.
- Device naming and persistence.
- Format negotiation.
- Exclusive/shared mode behavior.
- System default device interactions.
- App compatibility with OBS, Discord, browsers, and games.

## Risk Areas

- Driver signing and installation.
- Windows version differences.
- Antivirus or anti-cheat suspicion.
- DAW-specific ASIO assumptions.
- Clock drift between hardware devices.
- Sample rate changes while sessions are active.
- Hot-plug and sleep/wake behavior.

Each risk should eventually have a dedicated test plan.

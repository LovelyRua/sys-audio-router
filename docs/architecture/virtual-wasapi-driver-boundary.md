# Virtual WASAPI driver boundary

The SAR engine and UI remain GPLv3. The planned virtual WASAPI driver is an
independent [MS-PL driver project](https://github.com/LovelyRua/sar-virtual-wasapi-driver)
based on the Microsoft SysVAD sample. The sample
cannot be copied into this repository or treated as a GPLv3 component.

The runtime integration uses only public Windows audio device APIs. SAR
enumerates the installed render/capture endpoints, opens them with WASAPI, and
routes audio through its existing graph. Neither project includes the other's
source code, private headers, or shared-memory transport structures. Installer
and binary redistribution terms need a separate review before release.

The driver project's first deliverable is one paired stereo bus, not a full
multi-device product. Upstream SysVAD's synthetic capture tone and optional
file-backed render path are sample behaviors, not audio transport. The first
real validation must prove playback into a virtual endpoint reaches SAR and
SAR output reaches a standard WASAPI capture client with correct data and
timing. Only after that should channel topology and additional buses expand.

The driver lab may temporarily use test signing under its existing safety
procedure. The main repository's CI, installers, and release artifacts must
not claim virtual WASAPI support until the separate driver has passed device
enumeration, transfer, cleanup, and sustained-playback tests.

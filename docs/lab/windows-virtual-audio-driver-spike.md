# Windows Virtual Audio Driver Spike

This log records the time-boxed experiment that decides whether the first
standard Windows endpoint uses ACX or SysVAD/PortCls. It is evidence for an
architecture decision, not a production-driver implementation log.

## Exit Decision

At the end of five engineering days, choose exactly one result:

- `ACX`: continue with one minimal ACX render endpoint.
- `SysVAD/PortCls`: use the older model because ACX cannot satisfy a measured
  requirement or cannot produce a software-only endpoint.
- `Blocked`: stop endpoint implementation and record the concrete blocker.

Do not keep both driver models alive after the spike.

## Test Environment

| Item | Value |
| --- | --- |
| Machine | Windows integration host at `192.168.123.123` |
| Windows build | `10.0.26100.4770` observed before the spike |
| Visual Studio | Visual Studio Community 2022 `17.14.35` |
| WDK package | `Microsoft.WindowsWDK.10.0.26100` |
| WDK version | `10.1.26100.6584` |
| WDK SDK tree | `C:\Program Files (x86)\Windows Kits\10` |
| Microsoft samples | `microsoft/Windows-driver-samples` at `2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89` |
| Sample path | `C:\Users\codex\src\windows-driver-samples-acx` |
| WIL submodule | `3c00e7f1d8cf9930bbb8e5be3ef0df65c84e8928` |
| Test signing | Disabled; do not enable until a sample package builds |

The installed WDK contains the kernel headers, ACX headers, driver MSBuild
targets, `signtool.exe`, and `stampinf.exe`.

## Day 1: Toolchain Baseline

The first build target is Microsoft's unmodified ACX AudioCodec solution:

```bat
scripts\windows-driver-spike-build.cmd C:\Users\codex\src\windows-driver-samples-acx Acx
```

Use target `Probe` to validate the Visual Studio/WDK integration without
building, or target `All` after adding `audio/sysvad` and the WIL submodule to
the sparse checkout. The script does not install a driver, enable test signing,
or reboot the machine.

Initial result: failed with `MSB8020` because the WDK files were installed but
the Visual Studio `WindowsKernelModeDriver10.0` platform-toolset integration
was absent. The corrective action was to install a minimal Visual Studio 2022
Community instance and add `Component.Microsoft.Windows.DriverKit` there,
then rerun the exact command above.

This failure is useful evidence: installing the standalone WDK package does
not by itself establish a reproducible command-line driver build environment.
Visual Studio 2022 Build Tools also does not expose the WDK individual
component. The supported 26100 setup uses a minimal Visual Studio 2022
Community instance with `Component.Microsoft.Windows.DriverKit`.

Use 64-bit MSBuild. WDK 26100 ships the PackageVerifier native dependency in
`build\10.0.26100.0\bin\x64`, not `bin\x86`; 32-bit MSBuild can emit an
`InfVerif.dll` load error while still returning exit code zero. The spike
runner scans MSBuild output for this false-success case.

Final ACX baseline result: pass. Microsoft's unmodified AudioCodec solution
built with `WindowsKernelModeDriver10.0`, passed INF/signability validation,
and produced a test-signed `AudioCodec.sys` and `audiocodec.cat`.

Static endpoint feasibility result: pass. The sample INF is root-enumerated as
`ROOT\AudioCodec` and registers static render, capture, and realtime interfaces
for a speaker and microphone. Its `PrepareHardware` callback ignores both
resource lists, adds static render and capture circuits, and initializes what
the sample calls a virtual streaming engine. No physical codec resource is
required by this sample path.

## Day 1: SysVAD Comparison Baseline

The sparse checkout was expanded without cloning unrelated samples:

```bat
git -C C:\Users\codex\src\windows-driver-samples-acx sparse-checkout add --skip-checks audio/sysvad wil
git -C C:\Users\codex\src\windows-driver-samples-acx submodule update --init wil
scripts\windows-driver-spike-build.cmd C:\Users\codex\src\windows-driver-samples-acx Sysvad
```

The first build produced `TabletAudioSample.sys` but correctly failed the full
solution because the APO projects could not find `atlbase.h`. Adding the VS
2022 v143 ATL and ATL Spectre components resolved all 13 APO errors.

Final SysVAD baseline result: pass. The unmodified solution built the virtual
audio driver, four APO projects, keyword detector adapter, package INF files,
and a test-signed `sysvad.cat`. Signability reported no errors or warnings.

## ACX Versus SysVAD Questions

The AudioCodec sample proves only that the ACX build environment works. It does
not prove that ACX provides the software-only virtual endpoint shape needed by
System Audio Route. After it builds, inspect the sample before installing it:

1. Identify every assumed hardware resource and device interface.
2. Determine whether a root-enumerated software endpoint can create the same
   ACX circuit without simulated hardware.
3. Trace the render stream from the ACX stream callback to a bounded user-mode
   receiver.
4. Count kernel/user copies and define behavior when the receiver disappears.

Build SysVAD only as the comparison baseline. Compare:

| Criterion | ACX | SysVAD/PortCls |
| --- | --- | --- |
| Unmodified sample builds | Pass | Pass |
| Software-only render endpoint | Present in root-enumerated reference sample; runtime proof pending | Present in reference sample; runtime proof pending |
| Bounded user-mode transport | Pending | Pending |
| Service-down behavior | Pending | Pending |
| Minimum driver code and INF surface | Pending | Pending |
| Windows version support | Pending | Pending |
| Install/uninstall cleanliness | Pending | Pending |

## Acceptance Run

Only proceed to test signing after an unmodified Microsoft sample and the
minimal SAR endpoint both build. Before changing boot configuration, take a VM
snapshot and record the rollback command.

The endpoint experiment passes only when all of the following are captured in
one run:

1. The endpoint installs and appears with a stable identity.
2. A normal Windows application opens it and sends a non-silent stereo tone.
3. A bounded user-mode receiver reports frames, peak level, dropped blocks, and
   callback cadence.
4. Stopping the receiver produces deterministic silence or an explicit error.
5. Uninstall removes the endpoint, device node, and driver package cleanly.

Runtime preflight on the test machine found Secure Boot enabled, BitLocker
fully disabled, test signing disabled, and the x64 WDK DevCon tool present.
Before the first install, take a VM snapshot, disable Secure Boot, enable test
signing, and reboot. These boot changes require explicit operator approval.
After uninstall validation, disable test signing and restore Secure Boot.

## Evidence Still Required

- ACX endpoint install and runtime-enumeration result.
- Measured transport copy count and receiver-down behavior.
- Install, application playback, and uninstall transcripts.

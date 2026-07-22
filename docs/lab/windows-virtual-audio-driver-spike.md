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
| Visual Studio | Visual Studio 2022 Build Tools |
| WDK package | `Microsoft.WindowsWDK.10.0.26100` |
| WDK version | `10.1.26100.6584` |
| WDK SDK tree | `C:\Program Files (x86)\Windows Kits\10` |
| Microsoft samples | Sparse checkout of `microsoft/Windows-driver-samples` |
| Sample path | `C:\Users\codex\src\windows-driver-samples-acx` |
| Test signing | Disabled; do not enable until a sample package builds |

The installed WDK contains the kernel headers, ACX headers, driver MSBuild
targets, `signtool.exe`, and `stampinf.exe`.

## Day 1: Toolchain Baseline

The first build target is Microsoft's unmodified ACX AudioCodec solution:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
msbuild "C:\Users\codex\src\windows-driver-samples-acx\audio\Acx\Samples\AudioCodec\Driver\AudioCodec.sln" /m /p:Configuration=Debug /p:Platform=x64 /v:minimal
```

Initial result: failed with `MSB8020` because the WDK files were installed but
the Visual Studio `WindowsKernelModeDriver10.0` platform-toolset integration
was absent. The corrective action is to add
`Component.Microsoft.Windows.DriverKit` to the existing Build Tools instance,
then rerun the exact command above.

This failure is useful evidence: installing the standalone WDK package does
not by itself establish a reproducible command-line driver build environment.

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
| Software-only render endpoint | Pending | Expected from reference sample |
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

## Evidence Still Required

- Visual Studio driver component installation result.
- Unmodified ACX sample build output and sample repository commit.
- Unmodified SysVAD build output and sample repository commit.
- ACX software-only endpoint feasibility result.
- Measured transport copy count and receiver-down behavior.
- Install, application playback, and uninstall transcripts.

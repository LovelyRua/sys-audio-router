# Windows Alpha Package

The first distributable package is a per-user Windows x64 ZIP. It contains the
engine, control CLI, Virtual ASIO driver and registration utility, Qt Quick
control application, required Qt/QML runtime, platform plugins, and MSVC runtime.

Build the package from a Visual Studio developer environment with Qt 6.8
installed at `C:\Qt\6.8.3\msvc2022_64`:

```bat
scripts\windows-alpha-package.cmd
```

Set `SAR_QT_PREFIX` before invoking the helper when Qt is installed elsewhere.
The ZIP is written below `build-alpha`. Extract it, enter the package directory,
and install for the current user:

```bat
install-alpha.cmd
```

The official Microsoft Visual C++ x64 Redistributable is bundled. The installer
checks its machine-wide runtime marker and only runs the redistributable when
the runtime is absent; that first-time prerequisite may request elevation.
The default install directory is
`%LOCALAPPDATA%\Programs\SystemAudioRoute`. Pass `-InstallDirectory <path>` to
either PowerShell script when an isolated test location is required.

Installation validates the complete payload before touching the existing
installation. An existing install is moved to a temporary backup, the new
payload is installed, and the x64 Virtual ASIO registration is verified. A
failure removes the incomplete payload, restores the backup, and re-registers
its driver. Updates and removals require the private installation marker, so a
mistyped path cannot replace or recursively delete an unrelated directory.

Run the installed uninstaller before deleting the application directory:

```bat
uninstall-alpha.cmd
```

Uninstall first moves the complete installation aside, which also detects a
driver still loaded by a DAW before changing the registry. It unregisters the
current-user ASIO entry only when that entry points to this installation. A
failed removal restores both the directory and its registration. Both
operations emit one machine-readable `alpha_install` or `alpha_uninstall`
result line.

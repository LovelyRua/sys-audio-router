# Windows Alpha Packages

The Windows x64 build produces a standard NSIS installer and a portable ZIP.
Both contain the engine, bootstrap launcher, control CLI, Virtual ASIO driver
and registration utility, Qt Quick control application, required Qt/QML
runtime, platform plugins, and MSVC runtime.

Build the package from a Visual Studio developer environment with Qt 6.8
installed at `C:\Qt\6.8.3\msvc2022_64`:

```bat
scripts\windows-alpha-package.cmd
```

The same packages are built without a local Qt SDK by the `Windows GUI Package`
GitHub Actions workflow. Every push to `main` and every manually dispatched
run uploads a 30-day artifact containing the `.exe` installer and ZIP. The
installer is the normal user path: run it, complete setup, and start **System
Audio Route** from the Start menu or desktop shortcut. The launcher starts the
engine service, waits for its control pipe, and then opens the control panel.

Set `SAR_QT_PREFIX` before invoking the helper when Qt is installed elsewhere.
NSIS 3.03 or newer is also required for local packaging. Both packages are
written below `build-alpha/package-output`.

The ZIP remains available for isolated testing. Extract it, enter the package
directory, and install for the current user:

```bat
install-alpha.cmd
```

The official Microsoft Visual C++ x64 Redistributable is bundled. Both install
paths ensure that runtime is present; the standard installer requests elevation
because it installs below Program Files. The ZIP helper only runs the
redistributable when its machine-wide runtime marker is absent.
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
result line. If a rollback step itself fails, the script emits an additional
`rollback=failed` line naming the failed restoration step instead of hiding
that secondary fault behind the original error.

Run the complete package acceptance gate from a clean current-user environment:

```bat
scripts\windows-alpha-package-acceptance.cmd ^
  -PackagePath build-alpha\package-output\SystemAudioRoute-0.1.0-windows-x64.zip ^
  -InstallDirectory "%LOCALAPPDATA%\Programs\SystemAudioRoute-Alpha-Acceptance"
```

Validate the standard installer, including silent install/update, bootstrap
startup, ASIO registration ownership, and uninstall, with:

```bat
scripts\windows-installer-acceptance.cmd ^
  -PackagePath build-alpha\package-output\SystemAudioRoute-0.1.0-windows-x64.exe
```

The acceptance gate refuses an existing install directory or Virtual ASIO
registration. The two gates verify missing-runtime rejection, installation,
the deployed Qt/QML layout, launcher-managed GUI and engine health without Qt
development paths, in-place update, same-prefix process isolation, A/B ASIO
ownership, and complete self-uninstall. They remove only their unique scratch
directories and the installations they created.

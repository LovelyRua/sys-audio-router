# User Guide

System Audio Route (SAR) is alpha software. Read the
[current limits](../README.md#current-limits) before using it on a production
rig.

## Start and stop

Start **System Audio Route** from the Start menu or desktop shortcut. The
launcher starts the audio engine (a background process) and then opens the
control panel.

The engine keeps running when you close the control panel, so your DAWs stay
connected to the Virtual ASIO driver. When you close the window, SAR asks
whether to keep the engine running or stop it. Tick **Remember my choice** to
skip the question next time.

- **Keep running** closes the window only. Start the app again to reopen it;
  a second launch brings the existing window forward.
- **Stop engine** shuts the engine down completely. DAWs lose the driver until
  you start SAR again.

To have the engine start when you sign in, enable **Start engine at login** in
the left panel. It starts headless and restores your last session.

## First run

1. Open **Audio devices** and choose your capture and render endpoints.
2. Apply the runtime, then click **Start engine** in the top bar.
3. Open **Routing matrix** and click a crosspoint to connect a source to a
   destination. Shift+click removes a route; Ctrl+Z / Ctrl+Y undo and redo.
4. Open **Diagnostics**. The summary at the top should read **All clear**
   during steady playback.

## Presets

Type a name and click **Save**. Saving under an existing name asks before
replacing it. **Load** replaces the live matrix; **Delete** removes the saved
file only.

## Using the Virtual ASIO driver in a DAW

Select **System Audio Route Virtual ASIO** as the ASIO device. The DAW must use
the engine's sample rate (48 kHz by default); a different rate is rejected by
the driver. The DAW's **ASIO Control Panel** button opens SAR.

## Troubleshooting

- **Engine offline** in the top bar: the control panel restarts the engine
  automatically. If it stays offline, click **Open logs** in the error bar or
  **Logs** in the left panel.
- **Glitches detected** in Diagnostics: try a larger buffer size and close
  other audio-heavy applications.
- Logs live in `%APPDATA%\System Audio Route\logs\engine.log` (rotated at 4 MB).
  If the engine crashes, a minidump is written to
  `%APPDATA%\System Audio Route\logs\crashdumps`; attach it to your bug report.
- The session (matrix, devices, Virtual ASIO definitions) is stored in
  `%APPDATA%\System Audio Route\engine-session.sarsession`.

## Uninstalling

Use **Apps > Installed apps** or run `uninstall-alpha.cmd` from the portable
package. Uninstalling asks the engine to stop, unregisters the Virtual ASIO
driver, and removes the start-at-login entry. Your session and presets are kept.

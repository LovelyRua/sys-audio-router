@echo off
setlocal EnableExtensions

set "HOST=192.168.123.123"
set "USER=codex"
set "PASSWORD=%SAR_TEST_PASSWORD%"
set "SLOT=%SAR_TEST_SLOT%"
set "PLAYBACK_ID=%SAR_RECOVERY_TARGET_PLAYBACK_ID%"
set "RECORDING_ID=%SAR_RECOVERY_TARGET_RECORDING_ID%"
set "DURATION_MS=%SAR_RECOVERY_DURATION_MS%"
set "EXECUTABLE=%SAR_RECOVERY_EXECUTABLE%"
set "INTERACTIVE_USER=%SAR_RECOVERY_INTERACTIVE_USER%"
set "SWITCH_DELAY_MS=%SAR_RECOVERY_SWITCH_DELAY_MS%"
set "TARGET_HOLD_MS=%SAR_RECOVERY_TARGET_HOLD_MS%"
set "MAX_RECOVERY_MS=%SAR_RECOVERY_MAXIMUM_MS%"

if not "%~1"=="" set "HOST=%~1"
if not "%~2"=="" set "USER=%~2"
if not "%~3"=="" set "PASSWORD=%~3"
if not "%~4"=="" set "SLOT=%~4"
if not "%~5"=="" set "PLAYBACK_ID=%~5"
if not "%~6"=="" set "RECORDING_ID=%~6"
if not "%~7"=="" set "DURATION_MS=%~7"
if not "%~8"=="" set "EXECUTABLE=%~8"

if "%PASSWORD%"=="" goto :usage
if "%PLAYBACK_ID%"=="" goto :usage
if "%RECORDING_ID%"=="" goto :usage

if "%SLOT%"=="" set "SLOT=default-endpoint-recovery"
if "%DURATION_MS%"=="" set "DURATION_MS=12000"
if "%SWITCH_DELAY_MS%"=="" set "SWITCH_DELAY_MS=2500"
if "%TARGET_HOLD_MS%"=="" set "TARGET_HOLD_MS=3000"
if "%MAX_RECOVERY_MS%"=="" set "MAX_RECOVERY_MS=5000"

echo System Audio Route default-endpoint A-to-B-to-A recovery experiment
echo Host: %HOST%
echo Slot: %SLOT%
echo Duration ms: %DURATION_MS%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-default-endpoint-recovery.ps1" -HostName "%HOST%" -UserName "%USER%" -Password "%PASSWORD%" -Slot "%SLOT%" -TargetPlaybackId "%PLAYBACK_ID%" -TargetRecordingId "%RECORDING_ID%" -DurationMs "%DURATION_MS%" -RemoteExecutablePath "%EXECUTABLE%" -InteractiveUser "%INTERACTIVE_USER%" -SwitchDelayMs "%SWITCH_DELAY_MS%" -TargetHoldMs "%TARGET_HOLD_MS%" -MaximumRecoveryDurationMilliseconds "%MAX_RECOVERY_MS%"
exit /b %errorlevel%

:usage
echo Usage: scripts\windows-winrm-default-endpoint-recovery.cmd [host] [user] [password] [slot] [target-playback-id] [target-recording-id] [duration-ms] [remote-executable]
echo.
echo The password may instead be supplied through SAR_TEST_PASSWORD.
echo Target IDs may be supplied through SAR_RECOVERY_TARGET_PLAYBACK_ID and
echo SAR_RECOVERY_TARGET_RECORDING_ID. The script never writes the password to disk.
echo Set SAR_RECOVERY_INTERACTIVE_USER when more than one user has an Explorer session.
exit /b 1

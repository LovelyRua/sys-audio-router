@echo off
setlocal EnableExtensions

set "HOST=192.168.123.123"
set "USER=codex"
set "PASSWORD=%SAR_TEST_PASSWORD%"
set "SLOT=%SAR_TEST_SLOT%"
set "DURATION_MS=%SAR_RECOVERY_DURATION_MS%"
set "CAPTURE_ID=%SAR_RECOVERY_CAPTURE_ID%"
set "RENDER_ID=%SAR_RECOVERY_RENDER_ID%"
set "EXECUTABLE=%SAR_RECOVERY_EXECUTABLE%"
set "INTERACTIVE_USER=%SAR_RECOVERY_INTERACTIVE_USER%"

if not "%~1"=="" set "HOST=%~1"
if not "%~2"=="" set "USER=%~2"
if not "%~3"=="" set "PASSWORD=%~3"
if not "%~4"=="" set "SLOT=%~4"
if not "%~5"=="" set "DURATION_MS=%~5"
if not "%~6"=="" set "CAPTURE_ID=%~6"
if not "%~7"=="" set "RENDER_ID=%~7"
if not "%~8"=="" set "EXECUTABLE=%~8"

if "%PASSWORD%"=="" (
  echo Usage: scripts\windows-winrm-recovery-interactive.cmd [host] [user] [password] [slot] [duration-ms] [capture-id] [render-id] [remote-executable]
  echo.
  echo The password may instead be supplied through SAR_TEST_PASSWORD.
  echo Set SAR_RECOVERY_INTERACTIVE_USER when more than one user has an Explorer session.
  exit /b 1
)

if "%SLOT%"=="" set "SLOT=recovery-interactive"
if "%DURATION_MS%"=="" set "DURATION_MS=30000"

echo System Audio Route interactive WASAPI recovery experiment
echo Host: %HOST%
echo Slot: %SLOT%
echo Duration ms: %DURATION_MS%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-recovery-interactive.ps1" -HostName "%HOST%" -UserName "%USER%" -Password "%PASSWORD%" -Slot "%SLOT%" -DurationMs "%DURATION_MS%" -CaptureId "%CAPTURE_ID%" -RenderId "%RENDER_ID%" -RemoteExecutablePath "%EXECUTABLE%" -InteractiveUser "%INTERACTIVE_USER%"
exit /b %errorlevel%

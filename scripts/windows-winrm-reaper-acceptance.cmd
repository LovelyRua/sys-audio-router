@echo off
setlocal EnableExtensions

if "%~3"=="" (
  echo Usage: scripts\windows-winrm-reaper-acceptance.cmd ^<host^> ^<user^> ^<password^> ^<remote-build-path^> ^<render-endpoint-id^> [slot] [client-count] [duration-seconds]
  exit /b 1
)

if "%~4"=="" (
  echo Remote build path is required.
  exit /b 1
)

if "%~5"=="" (
  echo Render endpoint ID is required.
  exit /b 1
)

set "SLOT=%~6"
if "%SLOT%"=="" set "SLOT=reaper"
set "CLIENT_COUNT=%~7"
if "%CLIENT_COUNT%"=="" set "CLIENT_COUNT=1"
set "DURATION_SECONDS=%~8"
if "%DURATION_SECONDS%"=="" set "DURATION_SECONDS=3"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-reaper-acceptance.ps1" ^
  -HostName "%~1" ^
  -UserName "%~2" ^
  -Password "%~3" ^
  -RemoteBuildPath "%~4" ^
  -RenderDeviceId "%~5" ^
  -Slot "%SLOT%" ^
  -ClientCount "%CLIENT_COUNT%" ^
  -DurationSeconds "%DURATION_SECONDS%"
exit /b %errorlevel%

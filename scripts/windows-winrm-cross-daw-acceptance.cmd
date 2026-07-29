@echo off
setlocal EnableExtensions

if "%~5"=="" (
  echo Usage: scripts\windows-winrm-cross-daw-acceptance.cmd ^<host^> ^<user^> ^<password^> ^<remote-build-path^> ^<first-daw-process-name^> [second-daw-path] [slot] [duration-seconds]
  exit /b 1
)

set "SECOND_DAW_PATH=%~6"
if defined SECOND_DAW_PATH goto second_daw_ready
set "SECOND_DAW_PATH=C:\Program Files\REAPER (x64)\reaper.exe"
:second_daw_ready
set "SLOT=%~7"
if "%SLOT%"=="" set "SLOT=cross-daw"
set "DURATION_SECONDS=%~8"
if "%DURATION_SECONDS%"=="" set "DURATION_SECONDS=30"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-cross-daw-acceptance.ps1" ^
  -HostName "%~1" ^
  -UserName "%~2" ^
  -Password "%~3" ^
  -RemoteBuildPath "%~4" ^
  -FirstDawProcessName "%~5" ^
  -SecondDawPath "%SECOND_DAW_PATH%" ^
  -Slot "%SLOT%" ^
  -DurationSeconds "%DURATION_SECONDS%"
exit /b %errorlevel%

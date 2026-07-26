@echo off
setlocal EnableExtensions

if "%~3"=="" (
  echo Usage: %~nx0 ^<host^> ^<user^> ^<password^> [target] [action] [restart]
  exit /b 2
)

set "TARGET=Sysvad"
set "ACTION=Status"
set "EXTRA="
if not "%~4"=="" set "TARGET=%~4"
if not "%~5"=="" set "ACTION=%~5"
if /I "%~6"=="restart" set "EXTRA=-Restart"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-driver-spike.ps1" -HostName "%~1" -UserName "%~2" -Password "%~3" -Target "%TARGET%" -Action "%ACTION%" %EXTRA%
exit /b %errorlevel%

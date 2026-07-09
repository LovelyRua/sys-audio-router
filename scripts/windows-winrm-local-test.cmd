@echo off
setlocal EnableExtensions

set "HOST=192.168.123.3"
set "USER=codex"
set "PASSWORD="
set "SLOT=%SAR_TEST_SLOT%"

if not "%~1"=="" set "HOST=%~1"
if not "%~2"=="" set "USER=%~2"
if not "%~3"=="" set "PASSWORD=%~3"
if not "%~4"=="" set "SLOT=%~4"

if "%PASSWORD%"=="" (
  echo Usage: scripts\windows-winrm-local-test.cmd [host] [user] [password] [slot]
  echo Example: scripts\windows-winrm-local-test.cmd 192.168.123.3 codex password engineer-a
  echo.
  echo The optional slot isolates the remote checkout, build directory, and upload file.
  echo This script uploads git archive HEAD from the local checkout instead of downloading
  echo source from GitHub.
  exit /b 1
)

if "%SLOT%"=="" set "SLOT=local"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-local-test.ps1" -HostName "%HOST%" -UserName "%USER%" -Password "%PASSWORD%" -Slot "%SLOT%" -RepoRoot "%~dp0.."
exit /b %errorlevel%

@echo off
setlocal EnableExtensions

set "HOST=192.168.123.123"
set "USER=codex"
set "PASSWORD="
set "PACKAGE="
set "SLOT=%SAR_TEST_SLOT%"

if not "%~1"=="" set "PACKAGE=%~1"
if not "%~2"=="" set "HOST=%~2"
if not "%~3"=="" set "USER=%~3"
if not "%~4"=="" set "PASSWORD=%~4"
if not "%~5"=="" set "SLOT=%~5"

if "%PACKAGE%"=="" goto usage
if "%PASSWORD%"=="" goto usage
if "%SLOT%"=="" set "SLOT=engineer-a"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-alpha-package-acceptance.ps1" ^
  -PackagePath "%PACKAGE%" -HostName "%HOST%" -UserName "%USER%" ^
  -Password "%PASSWORD%" -Slot "%SLOT%"
exit /b %errorlevel%

:usage
echo Usage: scripts\windows-winrm-alpha-package-acceptance.cmd package.zip [host] [user] [password] [slot]
echo Example: scripts\windows-winrm-alpha-package-acceptance.cmd package.zip 192.168.123.123 codex password engineer-a
echo.
echo Package acceptance is globally serialized because it temporarily owns the
echo current user's Virtual ASIO registration on the test machine.
exit /b 1

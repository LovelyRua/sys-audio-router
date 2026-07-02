@echo off
setlocal EnableExtensions

set "HOST=192.168.123.3"
set "USER=codex"
set "PASSWORD="

if not "%~1"=="" set "HOST=%~1"
if not "%~2"=="" set "USER=%~2"
if not "%~3"=="" set "PASSWORD=%~3"

if "%PASSWORD%"=="" (
  echo Usage: scripts\windows-winrm-test.cmd [host] [user] [password]
  echo Example: scripts\windows-winrm-test.cmd 192.168.123.3 codex password
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-test.ps1" -HostName "%HOST%" -UserName "%USER%" -Password "%PASSWORD%"
exit /b %errorlevel%

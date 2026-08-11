@echo off
setlocal EnableExtensions

if "%~1"=="" goto usage
if "%~4"=="" goto usage
set "DESTINATION=SystemAudioRoute-latest.exe"
if not "%~5"=="" set "DESTINATION=%~5"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-stage-installer.ps1" ^
  -PackagePath "%~1" -HostName "%~2" -UserName "%~3" -Password "%~4" ^
  -DestinationName "%DESTINATION%"
exit /b %errorlevel%

:usage
echo Usage: scripts\windows-winrm-stage-installer.cmd package.exe host user password [destination.exe]
exit /b 1

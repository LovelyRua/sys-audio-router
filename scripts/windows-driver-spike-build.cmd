@echo off
setlocal EnableExtensions

set "SAMPLES=%USERPROFILE%\src\windows-driver-samples-acx"
set "TARGET=All"
set "CONFIGURATION=Debug"
set "PLATFORM=x64"

if not "%~1"=="" set "SAMPLES=%~1"
if not "%~2"=="" set "TARGET=%~2"
if not "%~3"=="" set "CONFIGURATION=%~3"
if not "%~4"=="" set "PLATFORM=%~4"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-driver-spike-build.ps1" -SamplesRoot "%SAMPLES%" -Target "%TARGET%" -Configuration "%CONFIGURATION%" -Platform "%PLATFORM%"
exit /b %errorlevel%

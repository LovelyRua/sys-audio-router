@echo off
setlocal EnableExtensions

set "SAMPLES=%USERPROFILE%\src\windows-driver-samples-acx"
set "TARGET=Sysvad"
set "ACTION=Status"
set "CONFIGURATION=Debug"
set "PLATFORM=x64"
set "EXTRA="

if not "%~1"=="" set "SAMPLES=%~1"
if not "%~2"=="" set "TARGET=%~2"
if not "%~3"=="" set "ACTION=%~3"
if not "%~4"=="" set "CONFIGURATION=%~4"
if not "%~5"=="" set "PLATFORM=%~5"
if /I "%~6"=="restart" set "EXTRA=-Restart"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-driver-spike-runtime.ps1" -SamplesRoot "%SAMPLES%" -Target "%TARGET%" -Action "%ACTION%" -Configuration "%CONFIGURATION%" -Platform "%PLATFORM%" %EXTRA%
exit /b %errorlevel%

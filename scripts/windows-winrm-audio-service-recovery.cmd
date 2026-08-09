@echo off
setlocal

set "HOST=%~1"
set "USER=%~2"
set "PASSWORD=%~3"
set "SLOT=%~4"
set "DURATION_MS=%~5"
set "EXECUTABLE=%~6"

if "%HOST%"=="" set "HOST=192.168.123.123"
if "%USER%"=="" set "USER=codex"
if "%PASSWORD%"=="" set "PASSWORD=%SAR_TEST_PASSWORD%"
if "%SLOT%"=="" set "SLOT=engineer-a"
if "%DURATION_MS%"=="" set "DURATION_MS=25000"
if "%SAR_AUDIO_SERVICE_RESTART_DELAY_MS%"=="" set "SAR_AUDIO_SERVICE_RESTART_DELAY_MS=3000"
if "%SAR_MAX_RECOVERY_MS%"=="" set "SAR_MAX_RECOVERY_MS=5000"

if "%PASSWORD%"=="" goto :usage

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-audio-service-recovery.ps1" ^
  -HostName "%HOST%" ^
  -UserName "%USER%" ^
  -Password "%PASSWORD%" ^
  -Slot "%SLOT%" ^
  -DurationMs "%DURATION_MS%" ^
  -RestartDelayMs "%SAR_AUDIO_SERVICE_RESTART_DELAY_MS%" ^
  -MaximumRecoveryDurationMilliseconds "%SAR_MAX_RECOVERY_MS%" ^
  -RemoteExecutablePath "%EXECUTABLE%" ^
  -EvidenceDirectory "%SAR_RECOVERY_EVIDENCE_DIR%"
exit /b %errorlevel%

:usage
echo Usage: scripts\windows-winrm-audio-service-recovery.cmd [host] [user] [password] [slot] [duration-ms] [remote-executable]
echo The password may instead be supplied through SAR_TEST_PASSWORD.
exit /b 2


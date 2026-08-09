@echo off
setlocal EnableExtensions

set "HOST=192.168.123.123"
set "USER=codex"
set "PASSWORD="
set "SLOT=%SAR_TEST_SLOT%"
set "CLEANUP=%SAR_SLOT_CLEANUP%"
set "CLEANUP_DRY_RUN=%SAR_SLOT_CLEANUP_DRY_RUN%"
set "RETENTION_DAYS=%SAR_SLOT_RETENTION_DAYS%"
set "RETENTION_COUNT=%SAR_SLOT_RETENTION_COUNT%"
set "CLEANUP_LIMIT=%SAR_SLOT_CLEANUP_LIMIT%"
set "STALE_ACTIVE_HOURS=%SAR_SLOT_STALE_ACTIVE_HOURS%"

if not "%~1"=="" set "HOST=%~1"
if not "%~2"=="" set "USER=%~2"
if not "%~3"=="" set "PASSWORD=%~3"
if not "%~4"=="" set "SLOT=%~4"

if "%PASSWORD%"=="" (
  echo Usage: scripts\windows-winrm-test.cmd [host] [user] [password] [slot]
  echo Example: scripts\windows-winrm-test.cmd 192.168.123.123 codex password engineer-a
  echo.
  echo The optional slot isolates the remote checkout, build directory, and bootstrap file.
  echo Use different slots such as engineer-a, engineer-b, and engineer-c for concurrent runs.
  echo Completed slot cleanup is enabled by default and keeps the newest eight slots.
  exit /b 1
)

if "%CLEANUP%"=="" set "CLEANUP=true"
if "%CLEANUP_DRY_RUN%"=="" set "CLEANUP_DRY_RUN=false"
if "%RETENTION_DAYS%"=="" set "RETENTION_DAYS=14"
if "%RETENTION_COUNT%"=="" set "RETENTION_COUNT=4"
if "%CLEANUP_LIMIT%"=="" set "CLEANUP_LIMIT=4"
if "%STALE_ACTIVE_HOURS%"=="" set "STALE_ACTIVE_HOURS=24"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-test.ps1" -HostName "%HOST%" -UserName "%USER%" -Password "%PASSWORD%" -Slot "%SLOT%" -CleanupCompletedSlotsText "%CLEANUP%" -CleanupDryRunText "%CLEANUP_DRY_RUN%" -RetentionDays "%RETENTION_DAYS%" -RetentionCount "%RETENTION_COUNT%" -CleanupLimit "%CLEANUP_LIMIT%" -StaleActiveHours "%STALE_ACTIVE_HOURS%"
exit /b %errorlevel%

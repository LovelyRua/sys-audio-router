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
  echo Usage: scripts\windows-winrm-local-test.cmd [host] [user] [password] [slot]
  echo Example: scripts\windows-winrm-local-test.cmd 192.168.123.123 codex password engineer-a
  echo.
  echo The optional slot isolates the remote checkout, build directory, and upload file.
  echo This script uploads git archive HEAD from the local checkout instead of downloading
  echo source from GitHub.
  exit /b 1
)

if "%SLOT%"=="" set "SLOT=local"
if "%CLEANUP%"=="" set "CLEANUP=false"
if "%CLEANUP_DRY_RUN%"=="" set "CLEANUP_DRY_RUN=false"
if "%RETENTION_DAYS%"=="" set "RETENTION_DAYS=14"
if "%RETENTION_COUNT%"=="" set "RETENTION_COUNT=8"
if "%CLEANUP_LIMIT%"=="" set "CLEANUP_LIMIT=2"
if "%STALE_ACTIVE_HOURS%"=="" set "STALE_ACTIVE_HOURS=24"

echo System Audio Route local Windows test upload
echo Host: %HOST%
echo Slot: %SLOT%
echo Source: local git archive HEAD
echo Slot cleanup: %CLEANUP%
echo Slot cleanup dry run: %CLEANUP_DRY_RUN%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-local-test.ps1" -HostName "%HOST%" -UserName "%USER%" -Password "%PASSWORD%" -Slot "%SLOT%" -RepoRoot "%~dp0.." -CleanupCompletedSlotsText "%CLEANUP%" -CleanupDryRunText "%CLEANUP_DRY_RUN%" -RetentionDays "%RETENTION_DAYS%" -RetentionCount "%RETENTION_COUNT%" -CleanupLimit "%CLEANUP_LIMIT%" -StaleActiveHours "%STALE_ACTIVE_HOURS%"
exit /b %errorlevel%

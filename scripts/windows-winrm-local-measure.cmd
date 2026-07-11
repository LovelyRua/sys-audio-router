@echo off
setlocal EnableExtensions

set "HOST=192.168.123.3"
set "USER=codex"
set "PASSWORD="
set "SLOT=%SAR_TEST_SLOT%"
set "MODE=%SAR_MEASURE_MODE%"
set "DURATION_MS=%SAR_MEASURE_DURATION_MS%"
set "TIMEOUT_MS=%SAR_MEASURE_TIMEOUT_MS%"
set "REQUIRE_HEALTHY=%SAR_MEASURE_REQUIRE_HEALTHY%"
set "ALLOW_UNAVAILABLE=%SAR_MEASURE_ALLOW_UNAVAILABLE%"
set "ITERATIONS=%SAR_MEASURE_ITERATIONS%"

if not "%~1"=="" set "HOST=%~1"
if not "%~2"=="" set "USER=%~2"
if not "%~3"=="" set "PASSWORD=%~3"
if not "%~4"=="" set "SLOT=%~4"
if not "%~5"=="" set "MODE=%~5"
if not "%~6"=="" set "DURATION_MS=%~6"
if not "%~7"=="" set "TIMEOUT_MS=%~7"
if not "%~8"=="" set "REQUIRE_HEALTHY=%~8"
if not "%~9"=="" set "ALLOW_UNAVAILABLE=%~9"
if not "%~10"=="" set "ITERATIONS=%~10"

if "%PASSWORD%"=="" (
  echo Usage: scripts\windows-winrm-local-measure.cmd [host] [user] [password] [slot] [mode] [duration-ms] [timeout-ms] [require-healthy] [allow-unavailable] [iterations]
  echo Example: scripts\windows-winrm-local-measure.cmd 192.168.123.3 codex password engineer-a render 1000 10 true false
  echo.
  echo mode can be render, duplex, or both. The optional slot isolates the remote checkout,
  echo build directory, and upload file. This script uploads git archive HEAD from the
  echo local checkout instead of downloading source from GitHub.
  exit /b 1
)

if "%SLOT%"=="" set "SLOT=local-measure"
if "%MODE%"=="" set "MODE=render"
if "%DURATION_MS%"=="" set "DURATION_MS=1000"
if "%TIMEOUT_MS%"=="" set "TIMEOUT_MS=10"
if "%REQUIRE_HEALTHY%"=="" set "REQUIRE_HEALTHY=false"
if "%ALLOW_UNAVAILABLE%"=="" set "ALLOW_UNAVAILABLE=false"
if "%ITERATIONS%"=="" set "ITERATIONS=1"

echo System Audio Route local WASAPI measurement
echo Host: %HOST%
echo Slot: %SLOT%
echo Source: local git archive HEAD
echo Mode: %MODE%
echo Duration ms: %DURATION_MS%
echo Timeout ms: %TIMEOUT_MS%
echo Require healthy: %REQUIRE_HEALTHY%
echo Allow unavailable endpoint: %ALLOW_UNAVAILABLE%
echo Iterations: %ITERATIONS%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-local-measure.ps1" -HostName "%HOST%" -UserName "%USER%" -Password "%PASSWORD%" -Slot "%SLOT%" -RepoRoot "%~dp0.." -Mode "%MODE%" -DurationMs "%DURATION_MS%" -TimeoutMs "%TIMEOUT_MS%" -Iterations "%ITERATIONS%" -RequireHealthyText "%REQUIRE_HEALTHY%" -AllowUnavailableText "%ALLOW_UNAVAILABLE%"
exit /b %errorlevel%

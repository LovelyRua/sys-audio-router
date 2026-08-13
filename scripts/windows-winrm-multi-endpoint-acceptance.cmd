@echo off
setlocal EnableExtensions

if "%~4"=="" (
  echo Usage: scripts\windows-winrm-multi-endpoint-acceptance.cmd ^<host^> ^<user^> ^<password^> ^<remote-build-path^> [slot] [capture-a-id] [capture-b-id] [render-id]
  echo Device IDs are optional; when omitted, the acceptance selects two distinct 48 kHz capture endpoints and one 48 kHz render endpoint.
  echo Set SAR_MULTI_ENDPOINT_EVIDENCE_DIR to select the local evidence directory.
  exit /b 1
)

set "SLOT=%~5"
if "%SLOT%"=="" set "SLOT=multi-endpoint"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-winrm-multi-endpoint-acceptance.ps1" ^
  -HostName "%~1" ^
  -UserName "%~2" ^
  -Password "%~3" ^
  -RemoteBuildPath "%~4" ^
  -Slot "%SLOT%" ^
  -InteractiveUser "%SAR_MULTI_ENDPOINT_INTERACTIVE_USER%" ^
  -CaptureDeviceIdA "%~6" ^
  -CaptureDeviceIdB "%~7" ^
  -RenderDeviceId "%~8" ^
  -EvidenceDirectory "%SAR_MULTI_ENDPOINT_EVIDENCE_DIR%"
exit /b %errorlevel%

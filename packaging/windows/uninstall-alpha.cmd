@echo off
setlocal EnableExtensions

if /i "%~1"=="--sar-alpha-relay" goto execute

set "SAR_ALPHA_UNINSTALL_TEMP=%TEMP%\sar-alpha-uninstall-%RANDOM%-%RANDOM%"
set "SAR_ALPHA_UNINSTALL_DEFAULT=%~dp0"
mkdir "%SAR_ALPHA_UNINSTALL_TEMP%" >nul
if errorlevel 1 exit /b %errorlevel%
copy /y "%~dp0uninstall-alpha.ps1" "%SAR_ALPHA_UNINSTALL_TEMP%\uninstall-alpha.ps1" >nul
if errorlevel 1 exit /b %errorlevel%
copy /y "%~f0" "%SAR_ALPHA_UNINSTALL_TEMP%\uninstall-alpha.cmd" >nul
if errorlevel 1 exit /b %errorlevel%
> "%SAR_ALPHA_UNINSTALL_TEMP%\.system-audio-route-uninstall-relay" echo relay

"%SAR_ALPHA_UNINSTALL_TEMP%\uninstall-alpha.cmd" --sar-alpha-relay %*
exit /b %errorlevel%

:execute
shift /1
if not defined SAR_ALPHA_UNINSTALL_TEMP exit /b 1
if not exist "%SAR_ALPHA_UNINSTALL_TEMP%\.system-audio-route-uninstall-relay" exit /b 1
if not "%~1"=="" goto execute_arguments
powershell -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0uninstall-alpha.ps1" ^
  -InstallDirectory "%SAR_ALPHA_UNINSTALL_DEFAULT%"
goto cleanup

:execute_arguments
powershell -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0uninstall-alpha.ps1" %*

:cleanup
set "SAR_ALPHA_UNINSTALL_RESULT=%errorlevel%"
start "" /b powershell.exe -NoProfile -Command ^
  "Start-Sleep -Milliseconds 500; Remove-Item -LiteralPath '%SAR_ALPHA_UNINSTALL_TEMP%' -Recurse -Force"
exit /b %SAR_ALPHA_UNINSTALL_RESULT%

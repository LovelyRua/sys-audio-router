@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "HOST=192.168.123.3"
set "USER=codex"
set "PASSWORD="

if not "%~1"=="" set "HOST=%~1"
if not "%~2"=="" set "USER=%~2"
if not "%~3"=="" set "PASSWORD=%~3"

if "%PASSWORD%"=="" (
  echo Usage: scripts\rdp-trigger-test-machine.cmd [host] [user] [password]
  echo Example: scripts\rdp-trigger-test-machine.cmd 192.168.123.3 codex password
  exit /b 1
)

set "RDP_FILE=%TEMP%\sar-rdp-bootstrap.rdp"
set "REMOTE_COMMAND=cmd.exe /c curl -L https://raw.githubusercontent.com/LovelyRua/sys-audio-router/main/scripts/windows-enable-winrm-and-test.cmd -o %%TEMP%%\sar-enable-winrm-and-test.cmd && %%TEMP%%\sar-enable-winrm-and-test.cmd"

cmdkey /generic:TERMSRV/%HOST% /user:%USER% /pass:%PASSWORD% >nul
if errorlevel 1 (
  echo Failed to store temporary RDP credential.
  exit /b 1
)

> "%RDP_FILE%" echo full address:s:%HOST%
>> "%RDP_FILE%" echo username:s:%USER%
>> "%RDP_FILE%" echo prompt for credentials:i:0
>> "%RDP_FILE%" echo authentication level:i:0
>> "%RDP_FILE%" echo enablecredsspsupport:i:1
>> "%RDP_FILE%" echo screen mode id:i:1
>> "%RDP_FILE%" echo desktopwidth:i:1280
>> "%RDP_FILE%" echo desktopheight:i:720
>> "%RDP_FILE%" echo alternate shell:s:%REMOTE_COMMAND%
>> "%RDP_FILE%" echo shell working directory:s:C:\Windows\System32

echo Launching RDP initial program against %HOST%...
start "" mstsc "%RDP_FILE%"

echo.
echo If the server accepts alternate shell, it will write:
echo   %%USERPROFILE%%\Desktop\sar-rdp-bootstrap.log
echo and WinRM should open on port 5985.

endlocal


@echo off
setlocal

set "HOST=192.168.123.3"
if not "%~1"=="" set "HOST=%~1"

echo Opening RDP session to %HOST%...
mstsc /v:%HOST%

endlocal


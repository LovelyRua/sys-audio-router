@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows-wasapi-lifecycle-soak.ps1" %*
exit /b %errorlevel%

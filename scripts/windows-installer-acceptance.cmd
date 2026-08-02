@echo off
setlocal EnableExtensions
powershell -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0windows-installer-acceptance.ps1" %*
exit /b %errorlevel%

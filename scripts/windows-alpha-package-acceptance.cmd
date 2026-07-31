@echo off
setlocal EnableExtensions
powershell -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0windows-alpha-package-acceptance.ps1" %*
exit /b %errorlevel%

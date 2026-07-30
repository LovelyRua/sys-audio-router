@echo off
setlocal EnableExtensions
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-alpha.ps1" %*
exit /b %errorlevel%

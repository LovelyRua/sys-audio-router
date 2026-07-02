@echo off
setlocal EnableExtensions

set "LOG=%USERPROFILE%\Desktop\sar-rdp-bootstrap.log"
set "BOOTSTRAP=%TEMP%\sar-bootstrap.cmd"

echo System Audio Route RDP bootstrap started at %DATE% %TIME% > "%LOG%"
echo User: %USERNAME% >> "%LOG%"
echo Computer: %COMPUTERNAME% >> "%LOG%"
echo. >> "%LOG%"

echo Enabling WinRM... >> "%LOG%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Service WinRM -StartupType Automatic; Enable-PSRemoting -Force; winrm quickconfig -quiet; if (-not (Get-NetFirewallRule -DisplayName 'SAR WinRM 5985' -ErrorAction SilentlyContinue)) { New-NetFirewallRule -DisplayName 'SAR WinRM 5985' -Direction Inbound -Protocol TCP -LocalPort 5985 -Action Allow | Out-Null }" >> "%LOG%" 2>&1

echo Downloading test bootstrap... >> "%LOG%"
curl -L https://raw.githubusercontent.com/LovelyRua/sys-audio-router/main/scripts/windows-test-bootstrap.cmd -o "%BOOTSTRAP%" >> "%LOG%" 2>&1
if errorlevel 1 (
  echo Failed to download bootstrap. >> "%LOG%"
  exit /b 1
)

echo Running test bootstrap... >> "%LOG%"
set "SAR_ENABLE_WINRM=1"
call "%BOOTSTRAP%" >> "%LOG%" 2>&1
set "RESULT=%ERRORLEVEL%"

echo. >> "%LOG%"
echo Finished at %DATE% %TIME% with exit code %RESULT%. >> "%LOG%"
exit /b %RESULT%

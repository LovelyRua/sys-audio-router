@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "REPO_URL=https://github.com/LovelyRua/sys-audio-router.git"
set "REPO_ZIP_URL=https://github.com/LovelyRua/sys-audio-router/archive/refs/heads/main.zip"
set "REPO_DIR=%USERPROFILE%\src\sys-audio-router"
set "BUILD_DIR=build"

if not "%~1"=="" set "REPO_DIR=%~1"
if not "%~2"=="" set "BUILD_DIR=%~2"
if not "%SAR_BUILD_DIR%"=="" set "BUILD_DIR=%SAR_BUILD_DIR%"

echo System Audio Route Windows test bootstrap
echo Repository: %REPO_URL%
echo Worktree:   %REPO_DIR%
echo Build dir:  %BUILD_DIR%
echo.

call :maybe_enable_winrm

call :ensure_vs_buildtools
call :refresh_common_paths

call :fetch_source
if errorlevel 1 exit /b 1

call :enter_msvc_env
if errorlevel 1 exit /b 1

pushd "%REPO_DIR%"

where ninja >nul 2>nul
if errorlevel 1 (
  cmake -S . -B "%BUILD_DIR%"
) else (
  cmake -S . -B "%BUILD_DIR%" -G Ninja
)
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1

ctest --test-dir "%BUILD_DIR%" --output-on-failure
if errorlevel 1 exit /b 1

popd

echo.
echo Bootstrap, build, and tests completed successfully.
exit /b 0

:ensure_tool
where %~1 >nul 2>nul
if not errorlevel 1 (
  echo [found] %~1
  exit /b 0
)

echo [install] %~2
where winget >nul 2>nul
if errorlevel 1 (
  echo winget is required to install %~2 automatically.
  exit /b 1
)

winget install --id %~2 --exact --silent --accept-package-agreements --accept-source-agreements
exit /b %errorlevel%

:maybe_enable_winrm
if /I not "%SAR_ENABLE_WINRM%"=="1" exit /b 0

echo [winrm] SAR_ENABLE_WINRM=1 requested
net session >nul 2>nul
if errorlevel 1 (
  echo [winrm] Administrator privileges are required; skipping WinRM enable.
  exit /b 0
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Service WinRM -StartupType Automatic; Enable-PSRemoting -Force; winrm quickconfig -quiet; if (-not (Get-NetFirewallRule -DisplayName 'SAR WinRM 5985' -ErrorAction SilentlyContinue)) { New-NetFirewallRule -DisplayName 'SAR WinRM 5985' -Direction Inbound -Protocol TCP -LocalPort 5985 -Action Allow | Out-Null }"
exit /b %errorlevel%

:refresh_common_paths
set "PATH=C:\Tools\cmake-4.4.0-windows-x86_64\bin;%ProgramFiles%\Git\cmd;%ProgramFiles%\CMake\bin;%LocalAppData%\Microsoft\WindowsApps;%PATH%"
call :add_vs_tool_paths
exit /b 0

:add_vs_tool_paths
set "VS_INSTALL="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    set "VS_INSTALL=%%I"
  )
)
if not "%VS_INSTALL%"=="" (
  set "PATH=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
)
exit /b 0

:fetch_source
where git >nul 2>nul
if errorlevel 1 goto fetch_source_zip

if not exist "%REPO_DIR%\.git" (
  if not exist "%REPO_DIR%" mkdir "%REPO_DIR%"
  for %%I in ("%REPO_DIR%") do set "REPO_PARENT=%%~dpI"
  for %%I in ("%REPO_DIR%") do set "REPO_NAME=%%~nxI"
  pushd "!REPO_PARENT!"
  git clone "%REPO_URL%" "!REPO_NAME!"
  if errorlevel 1 exit /b 1
  popd
) else (
  pushd "%REPO_DIR%"
  git fetch origin
  if errorlevel 1 exit /b 1
  git reset --hard origin/main
  if errorlevel 1 exit /b 1
  popd
)
exit /b 0

:fetch_source_zip
echo [fetch] git not found; downloading source zip
where curl >nul 2>nul
if errorlevel 1 (
  echo curl is required to download the source zip when git is unavailable.
  exit /b 1
)
where tar >nul 2>nul
if errorlevel 1 (
  echo tar is required to extract the source zip when git is unavailable.
  exit /b 1
)

set "ZIP_WORK=%TEMP%\sar-source"
set "ZIP_FILE=%TEMP%\sar-main.zip"
if exist "%ZIP_WORK%" rmdir /s /q "%ZIP_WORK%"
mkdir "%ZIP_WORK%"

curl -L "%REPO_ZIP_URL%" -o "%ZIP_FILE%"
if errorlevel 1 exit /b 1

tar -xf "%ZIP_FILE%" -C "%ZIP_WORK%"
if errorlevel 1 exit /b 1

if exist "%REPO_DIR%\.git" (
  echo Existing git worktree found at "%REPO_DIR%"; refusing to replace it with zip source.
  exit /b 1
)
if exist "%REPO_DIR%" rmdir /s /q "%REPO_DIR%"
mkdir "%REPO_DIR%"
xcopy "%ZIP_WORK%\sys-audio-router-main\*" "%REPO_DIR%\" /E /I /Y >nul
if errorlevel 1 exit /b 1
exit /b 0

:ensure_vs_buildtools
where cl >nul 2>nul
if not errorlevel 1 (
  echo [found] cl
  exit /b 0
)

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath >nul 2>nul
  if not errorlevel 1 (
    echo [found] Visual Studio C++ tools
    exit /b 0
  )
)

echo [install] Microsoft.VisualStudio.2022.BuildTools
where winget >nul 2>nul
if not errorlevel 1 (
  winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --silent --accept-package-agreements --accept-source-agreements --override "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended"
  exit /b %errorlevel%
)

echo [install] winget not found; downloading Visual Studio Build Tools directly
where curl >nul 2>nul
if errorlevel 1 (
  echo curl is required to download Visual Studio Build Tools.
  exit /b 1
)
set "VS_BOOTSTRAPPER=%TEMP%\vs_BuildTools.exe"
curl -L "https://aka.ms/vs/17/release/vs_BuildTools.exe" -o "%VS_BOOTSTRAPPER%"
if errorlevel 1 exit /b 1
"%VS_BOOTSTRAPPER%" --wait --quiet --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended
exit /b %errorlevel%

:enter_msvc_env
where cl >nul 2>nul
if not errorlevel 1 (
  exit /b 0
)

if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  echo vswhere.exe was not found after installing Visual Studio Build Tools.
  exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set "VS_INSTALL=%%I"
)

if "%VS_INSTALL%"=="" (
  echo Could not locate Visual Studio C++ tools.
  exit /b 1
)

if not exist "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" (
  echo VsDevCmd.bat not found under "%VS_INSTALL%".
  exit /b 1
)

call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
exit /b %errorlevel%

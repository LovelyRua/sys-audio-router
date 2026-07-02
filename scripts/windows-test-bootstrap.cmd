@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "REPO_URL=https://github.com/LovelyRua/sys-audio-router.git"
set "REPO_DIR=%USERPROFILE%\src\sys-audio-router"
set "BUILD_DIR=build"

if not "%~1"=="" set "REPO_DIR=%~1"

echo System Audio Route Windows test bootstrap
echo Repository: %REPO_URL%
echo Worktree:   %REPO_DIR%
echo.

call :ensure_tool git Git.Git
call :ensure_tool cmake Kitware.CMake
call :ensure_tool ninja Ninja-build.Ninja
call :ensure_vs_buildtools
call :refresh_common_paths

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

:refresh_common_paths
set "PATH=%ProgramFiles%\Git\cmd;%ProgramFiles%\CMake\bin;%LocalAppData%\Microsoft\WindowsApps;%PATH%"
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
if errorlevel 1 (
  echo winget is required to install Visual Studio Build Tools automatically.
  exit /b 1
)

winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --silent --accept-package-agreements --accept-source-agreements --override "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
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

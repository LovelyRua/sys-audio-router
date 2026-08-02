@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "SOURCE_DIR=%%~fI"
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build-alpha"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer vswhere.exe was not found. 1>&2
  exit /b 2
)
for /f "usebackq delims=" %%I in (`
  "%VSWHERE%" -latest -products * ^
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
    -property installationPath
`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL (
  echo Visual Studio C++ build tools were not found. 1>&2
  exit /b 2
)
call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

where cmake >nul 2>nul
if errorlevel 1 if exist "C:\Tools\cmake-4.4.0-windows-x86_64\bin\cmake.exe" (
  set "PATH=C:\Tools\cmake-4.4.0-windows-x86_64\bin;%PATH%"
)
where cmake >nul 2>nul
if errorlevel 1 set "PATH=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found in PATH or the latest Visual Studio installation. 1>&2
  exit /b 2
)

where makensis >nul 2>nul
if errorlevel 1 if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" (
  set "PATH=%ProgramFiles(x86)%\NSIS;%PATH%"
)
where makensis >nul 2>nul
if errorlevel 1 (
  echo NSIS 3.03 or newer was not found. Install NSIS before packaging. 1>&2
  exit /b 2
)

if not defined SAR_QT_PREFIX set "SAR_QT_PREFIX=C:\Qt\6.8.3\msvc2022_64"
if not exist "%SAR_QT_PREFIX%\lib\cmake\Qt6\Qt6Config.cmake" (
  echo Qt 6.8 was not found below SAR_QT_PREFIX="%SAR_QT_PREFIX%". 1>&2
  exit /b 2
)

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 ^
  -DSAR_BUILD_GUI=ON ^
  "-DCMAKE_PREFIX_PATH=%SAR_QT_PREFIX%"
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config Release --target ^
  sar_bootstrap_launcher ^
  sar_engine_service ^
  sar_control_cli ^
  sar_virtual_asio_driver ^
  sar_virtual_asio_register ^
  sar_gui
if errorlevel 1 exit /b %errorlevel%

cpack --config "%BUILD_DIR%\CPackConfig.cmake" -C Release ^
  -B "%BUILD_DIR%\package-output"
exit /b %errorlevel%

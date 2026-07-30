@echo off
setlocal EnableExtensions

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build-alpha"

cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 ^
  -DSAR_BUILD_GUI=OFF
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config Release --target ^
  sar_engine_service ^
  sar_control_cli ^
  sar_virtual_asio_driver ^
  sar_virtual_asio_register
if errorlevel 1 exit /b %errorlevel%

cpack --config "%BUILD_DIR%\CPackConfig.cmake" -C Release
exit /b %errorlevel%

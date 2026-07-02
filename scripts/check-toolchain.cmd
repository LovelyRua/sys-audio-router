@echo off
setlocal

echo System Audio Route toolchain check
echo.

where cmake >nul 2>nul
if errorlevel 1 (
  echo [missing] cmake
) else (
  for /f "tokens=*" %%i in ('cmake --version 2^>nul ^| findstr /n "^"') do (
    echo [found] %%i
    goto :after_cmake
  )
)
:after_cmake

where ninja >nul 2>nul
if errorlevel 1 (
  echo [missing] ninja
) else (
  echo [found] ninja
)

where cl >nul 2>nul
if errorlevel 1 (
  echo [missing] cl
) else (
  echo [found] cl
)

where clang-cl >nul 2>nul
if errorlevel 1 (
  echo [missing] clang-cl
) else (
  echo [found] clang-cl
)

where git >nul 2>nul
if errorlevel 1 (
  echo [missing] git
) else (
  echo [found] git
)

echo.
echo To build once a C++20 toolchain is available:
echo   cmake -S . -B build
echo   cmake --build build
echo   ctest --test-dir build

endlocal


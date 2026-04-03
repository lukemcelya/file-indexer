@echo off
setlocal

set BUILD_DIR=build

if not exist %BUILD_DIR% (
    mkdir %BUILD_DIR%
)

cmake -S . -B %BUILD_DIR%
if errorlevel 1 exit /b 1

cmake --build %BUILD_DIR%
if errorlevel 1 exit /b 1

echo Build complete.
endlocal
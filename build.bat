@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo          Building AnyPerfomans (Release x64)
echo ========================================================

set "CMAKE_EXE=cmake"
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    if exist "G:\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=G:\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else (
        echo [ERROR] CMake not found in PATH or G:\vs.
        exit /b 1
    )
)

echo Using CMake: %CMAKE_EXE%

if not exist build (
    mkdir build
)

echo [1/2] Configuring CMake project...
"%CMAKE_EXE%" -B build -A x64
if %errorlevel% neq 0 (
    echo [ERROR] CMake configure failed.
    exit /b %errorlevel%
)

echo [2/2] Compiling AnyPerfomans (Release)...
"%CMAKE_EXE%" --build build --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    exit /b %errorlevel%
)

echo ========================================================
echo Build Successful! Binary located at:
echo build\Release\AnyPerfomans.exe
echo ========================================================

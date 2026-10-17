@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo        Packaging AnyPerfomans for GitHub Release
echo ========================================================

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_EXE=%ROOT_DIR%\build\Release\AnyPerfomans.exe"
set "DIST_DIR=%ROOT_DIR%\dist"
set "VERSION=v1.0.0"

if not exist "%BUILD_EXE%" (
    echo [INFO] AnyPerfomans.exe not found. Running build.bat first...
    call "%ROOT_DIR%\build.bat"
    if errorlevel 1 (
        echo [ERROR] Build failed. Cannot create release package.
        exit /b 1
    )
)

echo [1/4] Running PowerShell release packager...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%package_release.ps1" -Version "%VERSION%"

if errorlevel 1 (
    echo [ERROR] Packaging failed.
    exit /b 1
)

echo ========================================================
echo Packaging Complete!
echo Packages are located in: %DIST_DIR%
echo ========================================================

param (
    [string]$Version = "v1.0.0"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = (Resolve-Path "$ScriptDir\..").Path
$BuildExe = "$RootDir\build\Release\AnyPerfomans.exe"
$DistDir = "$RootDir\dist"
$StagingDir = "$DistDir\AnyPerfomans-$Version"

Write-Host ">>> Preparing release for AnyPerfomans $Version..." -ForegroundColor Cyan

# 1. Clean and prepare dist directory
if (Test-Path $StagingDir) {
    Remove-Item -Recurse -Force $StagingDir
}
New-Item -ItemType Directory -Force -Path "$StagingDir\reports" | Out-Null

# 2. Copy AnyPerfomans core binaries
Write-Host ">>> Copying executable and configuration..." -ForegroundColor Cyan
Copy-Item -Path $BuildExe -Destination "$StagingDir\AnyPerfomans.exe" -Force

$ConfigSrc = "$RootDir\build\Release\anyperf_config.json"
if (-not (Test-Path $ConfigSrc)) {
    $ConfigSrc = "$RootDir\anyperf_config.json"
}
if (Test-Path $ConfigSrc) {
    Copy-Item -Path $ConfigSrc -Destination "$StagingDir\anyperf_config.json" -Force
}

if (Test-Path "$RootDir\LICENSE") {
    Copy-Item -Path "$RootDir\LICENSE" -Destination "$StagingDir\LICENSE.txt" -Force
}

# 3. Create QuickStart Guide
$QuickStart = @"
================================================================================
                    AnyPerfomans $Version - Quick Start Guide
================================================================================

AnyPerfomans is an ultra-lightweight hardware monitor, overclocking/undervolting
suite, and multi-process telemetry profiler with native RTSS Shared Memory V2.

--------------------------------------------------------------------------------
HOW TO RUN:
--------------------------------------------------------------------------------
1. Run AnyPerfomans.exe as Administrator (Right click -> Run as administrator)
   * Administrator rights are required for low-level GPU/CPU sensors, kernel ETW,
     and hardware clock/voltage regulation.

2. RivaTuner Statistics Server (RTSS):
   * If you have RTSS installed, AnyPerfomans will connect to it automatically.
   * If RTSS is not installed, the bundled 'RTSS' folder contains RTSS.exe.
     You can also click 'Start / Connect RTSS' directly in the SETTINGS tab.

--------------------------------------------------------------------------------
HOTKEYS & SHORTCUTS:
--------------------------------------------------------------------------------
* F11             : Toggle AnyPerfomans Dashboard window
* F12             : Toggle In-Game RTSS OSD Overlay (Show / Hide overlay in games)
* F10             : Start / Stop Benchmark Session Recording
* Ctrl+Shift+1..5 : Quick-switch hardware tuning profiles on the fly in-game!
* Close [X]       : Minimizes silently to Windows System Tray (if tray mode enabled)

--------------------------------------------------------------------------------
SAFETY & SAFE MODE:
--------------------------------------------------------------------------------
* TDR Crash Watchdog: If an unstable overclock or undervolt causes a driver reset
  or reboot during startup, AnyPerfomans will automatically disable
  'Apply at Startup' and engage Safe Mode to prevent boot-loops.

================================================================================
Official Repository: https://github.com/Anyqwer/AnyPerfomans
================================================================================
"@
Set-Content -Path "$StagingDir\README_QUICKSTART.txt" -Value $QuickStart -Encoding UTF8

# 4. Bundle RTSS if available
$RtssInstalledDir = "C:\Program Files (x86)\RivaTuner Statistics Server"
$RtssTargetDir = "$StagingDir\RTSS"

if (Test-Path $RtssInstalledDir) {
    Write-Host ">>> Bundling RTSS from '$RtssInstalledDir'..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $RtssTargetDir | Out-Null
    
    # Copy essential RTSS files & folders
    $RtssItems = Get-ChildItem -Path $RtssInstalledDir | Where-Object { 
        $_.Name -notmatch "^(Uninstall\.exe|Doc|Help)$" 
    }
    foreach ($item in $RtssItems) {
        Copy-Item -Path $item.FullName -Destination $RtssTargetDir -Recurse -Force
    }
    Write-Host ">>> RTSS successfully bundled into release package." -ForegroundColor Green
} else {
    Write-Host ">>> Notice: Local RTSS installation not found in '$RtssInstalledDir'." -ForegroundColor Yellow
}

# Create Official Guru3D Download Shortcut
$UrlContent = @"
[InternetShortcut]
URL=https://www.guru3d.com/download/rtss-rivatuner-statistics-server-download/
"@
Set-Content -Path "$StagingDir\Download-Latest-RTSS.url" -Value $UrlContent

# 5. Create Standalone ZIP (without bundled RTSS, lightweight ~1 MB)
$StandaloneStaging = "$DistDir\AnyPerfomans-$Version-Standalone"
if (Test-Path $StandaloneStaging) { Remove-Item -Recurse -Force $StandaloneStaging }
New-Item -ItemType Directory -Force -Path "$StandaloneStaging\reports" | Out-Null
Copy-Item -Path "$StagingDir\AnyPerfomans.exe" -Destination "$StandaloneStaging\" -Force
if (Test-Path "$StagingDir\anyperf_config.json") {
    Copy-Item -Path "$StagingDir\anyperf_config.json" -Destination "$StandaloneStaging\" -Force
}
Copy-Item -Path "$StagingDir\README_QUICKSTART.txt" -Destination "$StandaloneStaging\" -Force
Copy-Item -Path "$StagingDir\LICENSE.txt" -Destination "$StandaloneStaging\" -Force
Copy-Item -Path "$StagingDir\Download-Latest-RTSS.url" -Destination "$StandaloneStaging\" -Force

$StandaloneZip = "$DistDir\AnyPerfomans-$Version-win64-standalone.zip"
if (Test-Path $StandaloneZip) { Remove-Item -Force $StandaloneZip }
Write-Host ">>> Compressing standalone package to '$StandaloneZip'..." -ForegroundColor Cyan
Compress-Archive -Path "$StandaloneStaging\*" -DestinationPath $StandaloneZip -CompressionLevel Optimal
Remove-Item -Recurse -Force $StandaloneStaging

# 6. Create Full Bundle ZIP (with bundled RTSS)
$FullZip = "$DistDir\AnyPerfomans-$Version-win64-with-RTSS.zip"
if (Test-Path $FullZip) { Remove-Item -Force $FullZip }
Write-Host ">>> Compressing full bundle package to '$FullZip'..." -ForegroundColor Cyan
Compress-Archive -Path "$StagingDir\*" -DestinationPath $FullZip -CompressionLevel Optimal

# 7. Print Checksums and Instructions
Write-Host "`n========================================================" -ForegroundColor Green
Write-Host "         RELEASE PACKAGING SUCCESSFUL!                  " -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green

$HashStandalone = (Get-FileHash -Path $StandaloneZip -Algorithm SHA256).Hash
$SizeStandalone = [math]::Round((Get-Item $StandaloneZip).Length / 1MB, 2)
Write-Host "1. Standalone Package: $StandaloneZip ($SizeStandalone MB)" -ForegroundColor Yellow
Write-Host "   SHA-256: $HashStandalone" -ForegroundColor Gray

if (Test-Path $FullZip) {
    $HashFull = (Get-FileHash -Path $FullZip -Algorithm SHA256).Hash
    $SizeFull = [math]::Round((Get-Item $FullZip).Length / 1MB, 2)
    Write-Host "2. Full Bundle (with RTSS): $FullZip ($SizeFull MB)" -ForegroundColor Yellow
    Write-Host "   SHA-256: $HashFull" -ForegroundColor Gray
}

Write-Host "`nYou are ready to publish this release on GitHub!" -ForegroundColor Green

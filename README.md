# AnyPerfomans ⚡

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![DirectX 11](https://img.shields.io/badge/DirectX-11-green.svg)](https://docs.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey.svg)](https://www.microsoft.com/windows)
[![RTSS V2](https://img.shields.io/badge/RTSS-Shared%20Memory%20V2-orange.svg)](https://www.guru3d.com/files-details/rtss-rivatuner-statistics-server-download.html)
[![Release](https://img.shields.io/badge/Release-v1.0.0-success.svg)](https://github.com/Anyqwer/AnyPerfomans/releases)

**AnyPerfomans** is an ultra-lightweight, high-precision system telemetry monitor, hardware tuner, and multi-process comparative profiler built with **C++20**, **DirectX 11**, **Dear ImGui**, and **ImPlot**.

It delivers the full in-game telemetry and tuning power of **MSI Afterburner** (native **RTSS Shared Memory V2**, custom acoustic fan curves, vendor-safe GPU overclocking, true 3-point undervolting, and TDR crash-safe boot watchdog) paired with a specialized **multi-process attribution engine** that measures the exact resource tax background applications (browsers, Discord, OBS, overlays, launchers) impose on foreground games.

---

## 🌟 Key Features

### 1. Direct RivaTuner (RTSS) Shared Memory V2 Integration
- **Standalone Telemetry Feed**: Feeds telemetry and embedded line graphs directly to `RTSSSharedMemoryV2` without requiring MSI Afterburner.
- **Portable & Installed RTSS Auto-Detection**: Automatically searches for portable bundled `.\RTSS\RTSS.exe` in the application directory before falling back to system registry and `Program Files`.
- **In-Game Framerate Limiter**: Ultra-low latency hardware framerate limiter injected directly into the 3D rendering pipeline (*Unlimited, 60, 120, 144, 165, 240 FPS* + custom numeric cap).
- **Service Lifecycle Control**: Launch, restart, or safely terminate RTSS directly from the AnyPerfomans UI.
- **Clean Exit**: Closes background `RTSS.exe` automatically upon exiting AnyPerfomans to prevent orphaned tasks.
- **Live Overlay Visibility Toggle**: Press **`F12`** (or your custom hotkey) to show or hide the in-game RTSS overlay instantly.

---

### 2. Compact Single-Line Metric Grouping (MSI Afterburner MAX)
Tired of tall, screen-cluttering OSD lists? Enable compact single-line groupings in the **SETTINGS** tab for a razor-sharp, competitive HUD:
* **GPU Line**:
  `GPU : 65 C, Hotspot 78 C, 1750 MHz, 99%, 115 W`
* **VRAM Line**:
  `VRAM : 4850 / 6144 MB, 72 C, 1750 MHz`
* **CPU Line**:
  `CPU : 62 C, 48.5%, 65 W (Game.exe: 41.2%)`
* **RAM / Pagefile Line**:
  `RAM : 14.2 / 31.9 GB, Pagefile 16.5 GB`
* **FPS & Latency Line**:
  `FPS : 144 FPS, 6.9 ms (Avg 142, 1%: 115, 0.1%: 88)` *(with smooth hardware frametime graph underneath)*

---

### 3. CPU Cores Load Breakdown (MSI Afterburner Style)
Comprehensive per-core utilization across all logical processor threads (no arbitrary 8-core cutoff):
* **Column Mode (`в столбик`)**: Outputs each CPU core on its own vertical row (`CPU1: 45%`, `CPU2: 60%`, `CPU3: 18%`...), matching MSI Afterburner.
* **Single-Line Mode (`в 1 строку`)**: Combines all cores into one compact line (`CPU Cores : 45% 60% 18% 80% 35% 90% ...`).
* **Matrix / Row Wrapping**: Wrap cores into neat rows (4, 6, 8, 12, or 16 cores per line) to prevent HUD text from overflowing your display.

---

### 4. GPU Tuning, True Hardware Overclocking & Undervolting
* **Real-Time GPU Core Voltage Telemetry**:
  - Direct hardware polling via official AMD ADL Performance Logging (`ADL_PMLOG_GFX_VOLTAGE = 21`).
  - Reports exact millivolts (**mV**) live in the dashboard, RTSS OSD, DX11 HUD, and curve crosshairs.
* **Interactive MSI Afterburner-Style V/F Curve Editor (`Ctrl + F`)**:
  - Direct 3-State Piecewise V/F Spline for AMD RDNA 1 & 2 (Navi 10 / RX 5600 XT / 5700 XT / 6000 Series).
  - Drag & drop points (**P1**, **P2**, **P3**) directly on the graph with mouse or adjust via precision numerical inputs.
  - Automatic peak voltage flattening (**Flatline past P3**) to guarantee the GPU never pulls excess voltage.
  - Real-time animated **Live Operating Point Crosshair** tracing exact live mV and live MHz while 3D applications or games are running.
  - One-click tuning presets:
    - `Sweet Spot: 1750 MHz @ 940 mV` (Optimal balance for Navi 10 / RX 5600 XT).
    - `Cool & Quiet: 1700 MHz @ 900 mV` (Maximum acoustic silence and efficiency).
    - `Reset to VBIOS Curve`.
* **AMD Overdrive 8 (OD8) Direct Engine**:
  - Directly commits frequency and voltage states to hardware via `ADL2_Overdrive8_Setting_Set` on the fly.
  - Detects factory VBIOS limits and clamps frequency and voltage adjustments to vendor-safe ranges (`1750 .. 1860 MHz` memory).
  - Power Limit headroom control (-50% up to +20%).
* **NVIDIA NVAPI P-States 2.0**: Direct physical clock offsets (`freqDelta_kHz`) for Graphics Core and Memory domains via `NvAPI_GPU_SetPstates20` and native power limit control.
* **TDR Crash Watchdog & Safe Mode (`anyperf_oc_canary.tmp`)**:
  - An intelligent canary marker tracks startup overclocking stability.
  - If an unstable undervolt or clock offset causes a driver crash (TDR) or reboot, AnyPerfomans automatically disables `apply_at_startup`, engages **Safe Mode**, and notifies the user with a warning banner—**completely eliminating Windows boot-loops**.

---

### 5. Acoustic GPU Fan Curve with Temperature Source Selection
* **Three Control Modes**:
  1. *Automatic (VBIOS)*: Factory GPU firmware acoustic curves.
  2. *Manual Fixed %*: Lock fan speed to an exact target percentage (0-100%).
  3. *Custom Fan Curve*: Multi-point interactive temperature-to-speed curve programmed directly into the driver firmware via OD8 feature `0x2000`.
* **Temperature Source Selection**:
  - `GPU Core (Edge)`
  - `GPU Hotspot (Junction)` *(Highly recommended for AMD RDNA cards where hotspot delta runs +20–30°C higher than edge!)*
  - `VRAM Memory Temperature`
* **Zero-RPM Mode**: Fully stops fans below 45°C for silent idle operation.
* **Hardware Hysteresis (1-8°C)**: Prevents annoying rapid spin-up/spin-down cycles during fluctuating gaming loads.

---

### 6. Background System Tray & Non-Intrusive Autostart
* **Minimize to Tray on Close `[X]`**: Runs silently in the background feeding RTSS with < 0.1% CPU overhead.
* **Live Temperature on Tray Icon**: Dynamically renders real-time numeric temperature digits (e.g. `62`) directly onto the taskbar notification icon.
* **Context Menu**: Right-click tray icon to switch profiles, toggle Mini HUD, or exit safely.
* **UAC-Free Windows Autostart**: Automated setup via Windows Task Scheduler (`schtasks.exe` with `/RL HIGHEST`) ensuring the app starts at logon with full administrative privileges without annoying UAC prompts.

---

### 7. Multi-Process Comparative Profiling & Background App Tax
* **Simultaneous Process Profiling**: Monitor any number of active processes side by side ($N \ge 2$).
* **Microsecond CPU Cycles**: Reads exact hardware instruction cycles via `QueryProcessCycleTime`, providing true execution attribution.
* **True Memory Footprint**: Tracks **Private Commit Charge** (actual committed memory exclusive to the process) and **Physical Working Set** via Windows `PSAPI`.
* **Background Tax Summary**: Computes real-time percentage overhead and cumulative RAM footprint across all secondary monitored applications (Discord, Chrome, OBS, Telegram).

---

### 8. Interactive Reports & Instant Discord / Telegram Sharing
* **Single-File HTML Dashboard**: Generates self-contained interactive reports in `reports/` with Chart.js zoomable frametime curves, KPI cards, and process tables.
* **1-Click Share Button**: Built-in `📸 Copy Card for Discord/TG` captures the benchmark result card directly to the Windows clipboard for instant pasting (`Ctrl+V`) into Telegram or Discord.

---

## ⌨️ Controls & Shortcuts

| Hotkey / Shortcut | Action | Description |
| :--- | :--- | :--- |
| **`Ctrl + F`** | **Toggle V/F Curve Editor** | Opens dedicated MSI Afterburner-style interactive voltage-frequency graph |
| **`F11`** | **Toggle AnyPerfomans Dashboard** | Shows or hides the desktop application window without disrupting games |
| **`F12`** | **Toggle RTSS In-Game OSD** | Instantly hides or shows the overlay inside 3D games (like MSI Afterburner) |
| **`F10`** | **Toggle Benchmark Run** | Starts or stops comparative impact recording session |
| **`Ctrl + Shift + 1..5`** | **Quick Profile Switcher** | Instantly switches hardware OC/UV profiles, fan curves, and FPS caps in-game |
| **`Close [X]`** | **Minimize to Tray** | Minimizes silently to system tray (if enabled) |

---

## 📦 Download & Installation

### Option A: Pre-Built Release (Recommended)
Download the latest release from the [GitHub Releases](https://github.com/Anyqwer/AnyPerfomans/releases) page:

1. **`AnyPerfomans-v1.0.0-win64-with-RTSS.zip`** (All-in-One Bundle):
   - Includes `AnyPerfomans.exe`, default configuration, and bundled **RivaTuner Statistics Server (RTSS)** in the `RTSS/` subfolder.
   - Extract anywhere and run `AnyPerfomans.exe` as Administrator.
2. **`AnyPerfomans-v1.0.0-win64-standalone.zip`** (Lightweight ~1 MB):
   - For users who already have RTSS installed on their PC.

> [!IMPORTANT]
> Run `AnyPerfomans.exe` as **Administrator** (`Run as administrator`). Administrative privileges are required for low-level GPU/CPU driver access (ADL / NVAPI), kernel ETW tracking, and hardware clock/voltage regulation.

---

### Option B: Build From Source

#### Prerequisites
- **Operating System**: Windows 10 / Windows 11 (64-bit)
- **Compiler**: Visual Studio 2022 (MSVC v143+) with C++ Desktop Development workload
- **Build System**: CMake 3.24+ (included with Visual Studio)

#### Build Instructions
```cmd
git clone https://github.com/Anyqwer/AnyPerfomans.git
cd AnyPerfomans
build.bat
```
The compiled binary will be placed in `build\Release\AnyPerfomans.exe`.

#### Create Release Package
To create a clean release bundle with zip archives and SHA-256 checksums:
```cmd
scripts\package_release.bat
```
Archives will be generated in the `dist/` directory.

---

## 🛡️ Anti-Cheat & System Safety

* **Zero DLL Injection**: AnyPerfomans does **not** inject DLLs, install API detours, or modify game memory addresses.
* **Kernel ETW Tracking**: Frame presentation is tracked cleanly at kernel level via Event Tracing for Windows (`Microsoft-Windows-D3D9`, `DXGI`).
* **Standard Shared Memory**: Telemetry is passed to RivaTuner via standard memory mapped files (`RTSSSharedMemoryV2`), ensuring 100% compatibility and safety with anti-cheat software (Vanguard, EAC, BattlEye, VAC).

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).

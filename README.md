# AnyPerfomans ⚡

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![DirectX 11](https://img.shields.io/badge/DirectX-11-green.svg)](https://docs.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey.svg)](https://www.microsoft.com/windows)
[![RTSS V2](https://img.shields.io/badge/RTSS-Shared%20Memory%20V2-orange.svg)](https://www.guru3d.com/files-details/rtss-rivatuner-statistics-server-download.html)

**AnyPerfomans** is an ultra-lightweight, high-precision system telemetry monitor, hardware tuner, and multi-process comparative profiler built with **C++20**, **DirectX 11**, **Dear ImGui**, and **ImPlot**.

It combines the full telemetry and tuning power of **MSI Afterburner** (native **RTSS Shared Memory V2**, custom fan curves, vendor-safe GPU overclocking and true undervolting) with a specialized **multi-process attribution engine** that measures the exact resource tax background applications (browsers, Discord, OBS, overlays, launchers) impose on foreground games.

---

## 🌟 Key Features

### 1. Direct RivaTuner (RTSS) Shared Memory V2 Integration
- **Standalone Telemetry Feed**: Connects directly to `RTSSSharedMemoryV2` without requiring MSI Afterburner.
- **Hardware Line Graphs in OSD**: Native Afterburner-style line graphs embedded into games for Frametime, GPU/CPU Load, Clocks, and Temperatures with customizable width (-24 to -48 chars) and height (-2 to -5 lines).
- **In-Game Framerate Limiter**: Ultra-low latency hardware framerate limiter injected directly into the 3D rendering pipeline (Presets: *Unlimited, 60, 120, 144, 165, 240 FPS* + custom numeric cap).
- **Service Lifecycle Control**: Launch, restart, or safely terminate RTSS directly from the AnyPerfomans UI.
- **Auto-Close RTSS**: Automatically terminate `RTSS.exe` when AnyPerfomans exits to ensure zero orphaned background tasks.
- **Duplicate-Free Pipeline**: Smart slot management (`szOSD` / `szOSDEx`) preventing duplicate text or ghost overlays.

### 2. Deep Hardware Telemetry (AMD RDNA, NVIDIA GeForce & Intel)
- **AMD ADL PMLog Integration**: High-speed driver telemetry for modern AMD Radeon graphics cards (RDNA 1, RDNA 2, RDNA 3 / RX 5000, 6000, 7000 series):
  - GPU Core Clock (MHz) & Memory Clock (MHz)
  - Core Edge Temperature (°C), **Junction Hotspot Temperature** (°C), and **VRAM Temperature** (°C)
  - Core & Memory Controller Utilization (%)
  - Board / ASIC Power Consumption (Watts)
  - Fan Speed (RPM & %)
- **NVIDIA NVML & NVAPI**: Native queries for core/memory clocks, temperatures (edge & hotspot), memory utilization, and board power draw.
- **DirectX 1.4 Instant VRAM**: Zero-overhead video memory tracking via `IDXGIAdapter3::QueryVideoMemoryInfo` in < 0.001 ms, replacing heavy legacy PDH counters.
- **CPU Hardware Telemetry**: CPU Package Temperature (°C), Package Power Draw (Watts), Logical Core Count, Per-Core Load Breakdown, and Max Loaded Core Indicator with automatic thermal fallback.

### 3. GPU Tuning, True Hardware Overclocking & Undervolting
- **AMD Overdrive 8 (OD8) Direct Engine**: Native driver-level hardware control for RDNA 1 & RDNA 2 (RX 5000 / RX 6000 series, including RX 5600 XT):
  - **Real 3-Point V/F Curve Undervolting**: Directly adjusts the top operating state voltage (e.g. 1008 mV down to 950 mV) via `ADL2_Overdrive8_Setting_Set`, reducing temperatures and power draw while preserving stability.
  - **Dynamic Peak Frequency & Safe Clamping**: Adjusts core boost clock and clamps memory clock within safe VBIOS limits (`1750 .. 1860 MHz` for RX 5600 XT GDDR6).
  - **Power Limit Range**: Fine control from -50% up to +20% board power headroom.
- **NVIDIA NVAPI P-States 2.0**: Direct physical clock offsets (`freqDelta_kHz`) for Graphics Core and Memory domains via `NvAPI_GPU_SetPstates20` and native power limit control.
- **Dedicated Hardware Telemetry & Control Worker**: Telemetry sampling, RTSS OSD updates, and fan curve regulation run on an independent 100ms background thread, completely immune to Win32 modal loops, window dragging, or GUI stalls.
- **VBIOS Emergency Safety Fallback**: Automatic CRT exit handler (`std::atexit`) instantly restores factory VBIOS fan control in case of application termination or crash.
- **5 Hardware Tuning Profiles & Autostart**: Save, switch, and apply overclock/undervolt settings and fan curves with optional Windows boot autostart.

### 4. Acoustic GPU Fan Curve Controller
- **Three Operation Modes**:
  1. *Automatic (VBIOS)*: Handed off entirely to factory GPU firmware.
  2. *Manual Fixed %*: Lock fan speed to an exact target percentage (0-100%).
  3. *Custom Fan Curve*: Multi-point interactive temperature-to-speed curve.
- **Zero-RPM Mode**: Support for stopping fans completely below 45°C for silent idle operation.
- **Hardware Hysteresis (1-8°C)**: Prevents annoying rapid spin-up/spin-down cycles during fluctuating gaming loads.

### 5. Reorderable OSD Pipeline & Full Customization
- **Reorderable Sensor Table**: Move telemetry rows up and down with `[^]` and `[v]` buttons to customize your exact OSD layout.
- **Custom Sensor Labels**: Rename any metric to your preference (e.g., `"RX 5600 XT"`, `"Ryzen 5600"`, `"VRAM"`).
- **RGB Color Palette**: Individual hex color pickers for every sensor row, value readouts, stutter alerts, and bottleneck tags.
- **Full Percentiles**: Average FPS, 1% Low, 0.1% Low, 0.01% Low, Min/Max FPS, and Frametime (ms).
- **Pacing & Stutter Detector**: Microsecond frame jitter detection calculating stutter counts and frame pacing standard deviation.

### 6. Live Hardware Bottleneck Analyzer
- Evaluates real-time utilization balance across hardware components and highlights the active limiter in the dashboard and OSD:
  - `[GPU ENGINE LIMIT]` (GPU load > 95%)
  - `[CPU SINGLE-THREAD LIMIT]` (Single core saturated > 90% while GPU is starved)
  - `[CPU MULTI-THREAD LIMIT]` (Aggregate CPU load > 88%)
  - `[SYSTEM RAM LIMIT]` (Physical memory usage > 90%)

### 7. Background System Tray & Non-Intrusive Autostart
- **Minimize to Tray on Close [X]**: Keep AnyPerfomans active in the background feeding RTSS with near-zero overhead.
- **Live Temperature on Tray Icon**: Dynamically renders real-time numeric temperature digits (e.g., `62`) directly onto the taskbar notification icon.
- **Context Menu**: Right-click tray menu for quick profile switching, Mini HUD toggle, and safe shutdown.
- **UAC-Free Windows Autostart**: Automated setup via Windows Task Scheduler (`schtasks.exe` with `/RL HIGHEST`) ensuring the app starts at logon with full administrative privileges without annoying UAC prompts.

### 8. Multi-Process Comparative Telemetry
- **Simultaneous Process Profiling**: Monitor any number of active processes side by side ($N \ge 2$).
- **High-Precision CPU Cycles**: Reads exact hardware instruction cycles via `QueryProcessCycleTime`, providing microsecond-level execution attribution.
- **True Memory Footprint**: Tracks **Private Commit Charge** (actual committed memory exclusive to the process) and **Physical Working Set** via Windows `PSAPI`.
- **Background Tax Summary**: Computes real-time percentage overhead and cumulative RAM footprint across all secondary monitored applications.

### 9. Interactive Browser Reports & Instant Discord / TG Sharing
- **Single-File HTML Dashboard**: Generates self-contained interactive reports in `reports/` with Chart.js zoomable frametime curves, KPI cards, and process tables.
- **CSV Data Log**: Exports raw frametimes and telemetry data for deep spreadsheet analysis.
- **1-Click Share Button**: Built-in `📸 Copy Card for Discord/TG` captures the benchmark result card directly to the Windows clipboard for instant pasting (`Ctrl+V`) into Telegram or Discord.

### 10. ASUS ROG Red & Black Liquid Glass Aesthetic
- Deep Obsidian abyss backgrounds, tinted liquid glass child containers, and radiant crimson edge glow accents.
- **High-DPI Dynamic Scaling**: Automatic per-monitor DPI detection (`LOGPIXELSX`) scaling all UI padding, frames, and widgets smoothly across 1080p, 1440p, and 4K displays.
- **TrueType Typography & Cyrillic Support**: High-definition subpixel rasterization with **Segoe UI** and **Cascadia Mono** / **Consolas**, featuring full Unicode Latin and Cyrillic character sets.

---

## 🏗 Architecture Overview

```text
AnyPerfomans/
├── CMakeLists.txt              # CMake build configuration (C++20, DX11, WinAPI)
├── build.bat                   # Automated one-click build script (MSVC x64)
├── include/anyperf/
│   ├── types.hpp               # Core telemetry models, sensor definitions & metrics
│   ├── utils.hpp               # String encoding (UTF-8/Wide), timing & helper utilities
│   ├── ring_buffer.hpp         # High-throughput ring buffer for timeseries plotting
│   ├── process_monitor.hpp     # Multi-process telemetry engine (cycles, memory, handles)
│   ├── thread_profiler.hpp     # Per-thread cycle tracking & NTDLL module mapping
│   ├── cpu_monitor.hpp         # CPU package telemetry, WMI ACPI & thermal fallback
│   ├── gpu_monitor.hpp         # GPU telemetry (AMD ADL PMLog, NVIDIA NVML, DXGI VRAM)
│   ├── hardware_controller.hpp # GPU fan curves, clock offsets, undervolting & NVAPI/ADL
│   ├── rtss_bridge.hpp         # RivaTuner Shared Memory V2, line graphs & framerate limiter
│   ├── bottleneck_analyzer.hpp # Hardware saturation detector (GPU/CPU/RAM limit badges)
│   ├── tray_manager.hpp        # Shell_NotifyIconW tray mode, live icon temp digits & autostart
│   ├── config_manager.hpp      # JSON profile persistence, 5 tuning slots & OSD sensor order
│   ├── report_generator.hpp    # Interactive HTML benchmark exporter & CSV data logger
│   ├── dx11_backend.hpp        # DirectX 11 device, swapchain, fonts & window management
│   └── dashboard_view.hpp      # ImGui & ImPlot UI tabs, tuning sliders & HUD rendering
└── src/
    ├── core/                   # Telemetry collection, hardware control & background services
    ├── render/                 # DirectX 11 backend, shaders & font rasterizers
    ├── ui/                     # Interactive dashboard views, tuning sliders & HUD rendering
    └── main.cpp                # Application entry point, IPC message pump & telemetry loop
```

---

## 🚀 Building & Running

### Prerequisites
- **Operating System**: Windows 10 / Windows 11 (64-bit)
- **Compiler**: Visual Studio 2022 or newer with C++ Desktop Development (MSVC v143+)
- **Build System**: CMake 3.24+ (included with Visual Studio)

### Build Instructions

1. Clone the repository:
   ```cmd
   git clone https://github.com/Anyqwer/AnyPerfomans.git
   cd AnyPerfomans
   ```

2. Run the automated build script:
   ```cmd
   build.bat
   ```

3. Launch AnyPerfomans:
   ```cmd
   build\Release\AnyPerfomans.exe
   ```

---

## ⌨️ Controls & Shortcuts

| Key / Control | Action |
| :--- | :--- |
| **`F11`** | Smart Overlay Switch: Toggles between **Desktop Dashboard** and **Standalone Mini HUD** (minimizes cleanly if RTSS is active) |
| **`F10`** | Start / Stop Comparative Impact Benchmark session |
| **Close Button `[X]`** | Minimizes silently to the Windows System Tray (if tray mode is enabled) |
| **Tray Icon** | Displays live GPU or CPU temperature digits; Right-click for quick profile switcher and exit |
| **Process Selector** | Add/remove active background applications to the comparative telemetry queue |
| **Search Filter** | Instantly find running processes by executable name or PID |
| **Sensors `[^]` / `[v]`** | Reorder OSD telemetry rows in Settings |
| **Graph Checkbox** | Toggle RTSS hardware line graph for any individual sensor |

---

## 🛡️ Anti-Cheat & System Safety

- **Zero DLL Injection**: AnyPerfomans does **not** inject DLLs, install API detours, or modify game memory addresses.
- **Kernel ETW Tracking**: Frame presentation is tracked cleanly at kernel level via Event Tracing for Windows (`Microsoft-Windows-D3D9`, `DXGI`).
- **Standard Shared Memory**: Telemetry is passed to RivaTuner via standard memory mapped files (`RTSSSharedMemoryV2`), ensuring 100% compatibility and safety with anti-cheat software (Vanguard, EAC, BattlEye, VAC).

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).

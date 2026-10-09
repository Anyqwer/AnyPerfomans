# AnyPerfomans ⚡

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![DirectX 11](https://img.shields.io/badge/DirectX-11-green.svg)](https://docs.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey.svg)](https://www.microsoft.com/windows)

**AnyPerfomans** is an ultra-lightweight, high-precision system telemetry monitor and multi-process performance profiler built with **C++20**, **DirectX 11**, **Dear ImGui**, and **ImPlot**.

Unlike conventional monitors that merely report aggregate system-wide CPU utilization, AnyPerfomans isolates and attributes performance overhead across **two or more concurrent processes** in real time. It enables engineers, power users, and gamers to measure the exact performance tax that background applications (such as browsers, communication clients, recording tools, background overlays, or custom utilities) impose on resource-sensitive foreground tasks.

---

## 🌟 Key Features

### 1. Multi-Process Comparative Telemetry
- **Simultaneous Tracking**: Monitor any number of active processes side by side ($N \ge 2$).
- **High-Precision CPU Cycles**: Reads exact hardware instruction cycles via `QueryProcessCycleTime`, providing microsecond-level execution attribution rather than coarse periodic sampling.
- **True Memory Footprint**: Tracks **Private Commit Charge** (actual committed memory exclusive to the process) and **Physical Working Set** via Windows `PSAPI`, filtering out shared system DLL noise.
- **Overhead Attribution**: Computes real-time percentage overhead and cumulative impact across all monitored secondary applications.

### 2. Deep Thread Inspector
- Enumerate and inspect every active thread inside any selected process.
- Profiles per-thread CPU cycle consumption with dynamic sorting.
- Resolves thread start addresses using native Windows NT APIs (`NtQueryInformationThread`) and maps them to loaded modules (`.dll` / `.exe`) to pinpoint which library or subsystem is generating load.

### 3. Hardware-Accelerated Interactive Visualizations
- Real-time timeseries rendering powered by **ImPlot** and **DirectX 11**.
- Multi-series CPU utilization and memory footprint graphs with distinct color assignments.
- Frametime variance and jitter timeline with target latency thresholds.

### 4. Non-Invasive Architecture (Zero-Hook)
- **Zero Injection**: Does not inject DLLs, install API detours, or modify target process memory spaces.
- Transparently queries standard WinAPI diagnostic interfaces (`PROCESS_QUERY_LIMITED_INFORMATION`), ensuring total compatibility and no interference with protected software or anti-cheat runtimes.

### 5. Hybrid Display Modes
- **Interactive Dashboard**: Full desktop workspace with searchable process selectors, thread tables, and multi-graph diagnostics.
- **Mini HUD Overlay (`F11`)**: Seamless, borderless, click-through topmost overlay (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST`) for in-game and fullscreen application monitoring.

### 6. Minimal Footprint
- Native self-contained executable (~630 KB).
- Extremely low memory consumption (< 25 MB RAM).
- Near-zero CPU overhead (< 0.1% CPU).

---

## 🏗 Architecture Overview

```text
AnyPerfomans/
├── CMakeLists.txt              # CMake build configuration (C++20, DX11, WinAPI)
├── build.bat                   # Automated one-click build script (MSVC x64)
├── include/anyperf/
│   ├── types.hpp               # Core telemetry models & multi-target definitions
│   ├── ring_buffer.hpp         # High-throughput ring buffer for timeseries plotting
│   ├── process_monitor.hpp     # Multi-process telemetry engine (cycles, memory, handles)
│   ├── thread_profiler.hpp     # Per-thread cycle tracking & NTDLL module mapping
│   ├── dx11_backend.hpp        # DirectX 11 device, swapchain, and transparent overlay manager
│   └── dashboard_view.hpp      # ImGui & ImPlot UI, multi-process controls, HUD
└── src/
    ├── core/                   # Telemetry collection and thread profiling implementation
    ├── render/                 # DirectX 11 backend and window management
    ├── ui/                     # Interactive dashboard views and HUD rendering
    └── main.cpp                # Application entry point and telemetry dispatch loop
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
   git clone https://github.com/your-username/AnyPerfomans.git
   cd AnyPerfomans
   ```

2. Run the automated build script:
   ```cmd
   build.bat
   ```

3. Launch the profiler:
   ```cmd
   build\Release\AnyPerfomans.exe
   ```

---

## ⌨️ Controls & Shortcuts

| Key / Control | Action |
| :--- | :--- |
| **`F11`** | Toggle between **Full Desktop Dashboard** and **Click-Through Mini HUD** |
| **Process Selector** | Add/remove any active process to the multi-target monitoring queue |
| **Search Filter** | Instantly find running processes by executable name or PID |
| **Start Benchmark** | Record a controlled multi-process performance test session |

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).

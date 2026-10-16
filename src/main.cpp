#include "anyperf/dx11_backend.hpp"
#include "anyperf/process_monitor.hpp"
#include "anyperf/thread_profiler.hpp"
#include "anyperf/etw_tracker.hpp"
#include "anyperf/gpu_monitor.hpp"
#include "anyperf/cpu_monitor.hpp"
#include "anyperf/tray_manager.hpp"
#include "anyperf/config_manager.hpp"
#include "anyperf/dashboard_view.hpp"
#include "anyperf/rtss_bridge.hpp"
#include <chrono>
#include <thread>
#include <deque>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <shellapi.h>

// Official Unwinder export symbol to instruct RTSS to completely bypass
// and never hook or inject into AnyPerfomans
extern "C" {
    __declspec(dllexport) DWORD RTSSHooksCompatibility = 0x00000000;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    anyperf::ConfigManager config_mgr;
    anyperf::Dx11Backend backend;
    if (!backend.init(1320, 840, L"AnyPerfomans - ROG Liquid Glass Edition")) {
        return 1;
    }

    anyperf::ProcessMonitor proc_mon;
    anyperf::ThreadProfiler thread_prof;
    anyperf::GpuMonitor gpu_mon;
    anyperf::CpuMonitor cpu_mon;
    anyperf::TrayManager tray_manager;
    anyperf::EtwTracker etw_tracker;
    anyperf::DashboardView dashboard_view;
    anyperf::RTSSBridge rtss_bridge;

    // Initialize System Tray Integration
    tray_manager.init(backend.get_hwnd());
    backend.set_tray_manager(&tray_manager);
    backend.set_minimize_to_tray_on_close(config_mgr.get_hud().minimize_to_tray_on_close);

    // Check if launched with --minimized flag or start_minimized_to_tray is enabled
    bool start_minimized = config_mgr.get_hud().start_minimized_to_tray ||
                           (wcsstr(GetCommandLineW(), L"--minimized") != nullptr);
    if (start_minimized) {
        backend.set_overlay_mode(anyperf::OverlayMode::Hidden);
    }

    DWORD active_etw_pid = 0;

    auto last_telemetry_time = std::chrono::steady_clock::now();
    auto last_gpu_time = std::chrono::steady_clock::now();
    auto last_cpu_time = std::chrono::steady_clock::now();
    auto last_tray_time = std::chrono::steady_clock::now();
    auto last_thread_time = std::chrono::steady_clock::now();
    auto last_rtss_time = std::chrono::steady_clock::now();

    bool running = true;
    while (running) {
        if (!backend.process_messages() || backend.has_quit_requested()) {
            running = false;
            break;
        }

        // Handle Restart requested from Tray context menu
        if (backend.has_restart_requested()) {
            wchar_t exe_path[MAX_PATH];
            GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            ShellExecuteW(nullptr, L"open", exe_path, nullptr, nullptr, SW_SHOWNORMAL);
            running = false;
            break;
        }

        // Keep backend minimize_to_tray setting in sync with live settings
        backend.set_minimize_to_tray_on_close(config_mgr.get_hud().minimize_to_tray_on_close);

        // Configurable Hotkey: Toggle Overlay (Default: F11)
        UINT toggle_key = config_mgr.get_hotkeys().toggle_overlay_key;
        if (toggle_key != 0 && (GetAsyncKeyState(toggle_key) & 1)) {
            auto current_mode = backend.get_overlay_mode();
            anyperf::OverlayMode new_mode;
            if (current_mode == anyperf::OverlayMode::Dashboard) {
                // If output to RTSS is active, pressing F11 hides AnyPerfomans without opening duplicate DX11 overlay
                if (config_mgr.get_hud().output_to_dx11_overlay && !config_mgr.get_hud().output_to_rtss) {
                    new_mode = anyperf::OverlayMode::MiniHud;
                } else {
                    new_mode = anyperf::OverlayMode::Hidden;
                }
            } else {
                new_mode = anyperf::OverlayMode::Dashboard;
            }
            backend.set_overlay_mode(new_mode);
        }

        auto now = std::chrono::steady_clock::now();

        // 1. Manage ETW target PID according to selected primary process
        const auto* primary = proc_mon.get_primary_target();
        DWORD primary_pid = (primary && primary->is_alive) ? primary->pid : 0;
        if (primary_pid != active_etw_pid) {
            active_etw_pid = primary_pid;
            if (active_etw_pid != 0) {
                etw_tracker.start(active_etw_pid);
            } else {
                etw_tracker.stop();
            }
        }

        // 2. Sample GPU Telemetry (every 250ms)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gpu_time).count() >= 250) {
            gpu_mon.update();
            last_gpu_time = now;
        }

        // 3. Sample CPU Telemetry & Per-Core Load (every 250ms)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cpu_time).count() >= 250) {
            cpu_mon.update();
            last_cpu_time = now;
        }

        // 4. Calculate frame stats (Order of precedence: Kernel ETW Present -> RTSS Shared Memory -> Idle)
        anyperf::FrameStats display_frame_stats{};
        anyperf::FrameStats rtss_stats{};

        if (etw_tracker.is_active() && etw_tracker.get_frame_stats().total_frames > 0) {
            display_frame_stats = etw_tracker.get_frame_stats();
        } else if (rtss_bridge.get_rtss_frame_stats(rtss_stats)) {
            display_frame_stats = rtss_stats;
        } else {
            // Idle state: no 3D application is currently presenting
            display_frame_stats.current_fps = 0.0;
            display_frame_stats.frametime_ms = 0.0;
        }

        // Configurable Hotkey: Toggle Benchmark Session (Default: F10)
        UINT bench_key = config_mgr.get_hotkeys().toggle_benchmark_key;
        if (bench_key != 0 && (GetAsyncKeyState(bench_key) & 1)) {
            dashboard_view.toggle_benchmark(proc_mon, gpu_mon.get_metrics(), display_frame_stats);
        }

        // 5. Sample high-precision multi-process telemetry (every 150ms)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_telemetry_time).count() >= 150) {
            proc_mon.update();
            dashboard_view.push_metrics_sample(
                proc_mon.get_monitored_targets(),
                display_frame_stats,
                gpu_mon.get_metrics()
            );
            last_telemetry_time = now;
        }

        // 6. Sample thread breakdown for the active primary process (every 500ms to keep overhead < 0.1%)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_thread_time).count() >= 500) {
            if (primary_pid != 0) {
                thread_prof.update(primary_pid);
            }
            last_thread_time = now;
        }

        // 7. Update RTSS Shared Memory V2 On-Screen Display (every 100ms)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rtss_time).count() >= 100) {
            rtss_bridge.update_osd(
                proc_mon,
                gpu_mon.get_metrics(),
                cpu_mon.get_metrics(),
                display_frame_stats,
                config_mgr,
                dashboard_view.is_benchmarking(),
                dashboard_view.get_benchmark_elapsed_sec()
            );
            last_rtss_time = now;
        }

        // 8. Update System Tray Icon & Tooltip (every 500ms)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tray_time).count() >= 500) {
            const auto& hud_cfg = config_mgr.get_hud();
            int temp = hud_cfg.tray_temp_is_gpu ? gpu_mon.get_metrics().temperature_c : cpu_mon.get_metrics().package_temp_c;
            std::wstring tooltip = L"AnyPerfomans\n" + 
                (hud_cfg.tray_temp_is_gpu 
                    ? L"GPU Temp: " + std::to_wstring(temp) + L"°C" 
                    : L"CPU Temp: " + (temp > 0 ? std::to_wstring(temp) + L"°C" : L"N/A"));
            tray_manager.update_icon(temp, hud_cfg.tray_temp_is_gpu, hud_cfg.show_tray_temperature, tooltip);
            last_tray_time = now;
        }

        // 9. Render UI frame
        backend.begin_frame();

        auto current_mode = backend.get_overlay_mode();
        dashboard_view.render(
            proc_mon,
            thread_prof,
            etw_tracker,
            gpu_mon,
            cpu_mon,
            config_mgr,
            rtss_bridge,
            backend,
            display_frame_stats,
            current_mode
        );

        if (current_mode != backend.get_overlay_mode()) {
            backend.set_overlay_mode(current_mode);
        }

        backend.end_frame();

        if (current_mode == anyperf::OverlayMode::Hidden) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    // Graceful cleanup on shutdown
    tray_manager.shutdown();
    rtss_bridge.clear_osd();
    rtss_bridge.disconnect();
    if (config_mgr.get_hud().close_rtss_on_exit || rtss_bridge.was_launched_by_us()) {
        anyperf::RTSSBridge::kill_rtss();
    }
    etw_tracker.stop();
    backend.shutdown();
    return 0;
}

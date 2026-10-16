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
#include "anyperf/hardware_controller.hpp"
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
    if (!backend.init(1320, 840, L"AnyPerfomans - Liquid Glass Edition")) {
        return 1;
    }

    anyperf::ProcessMonitor proc_mon;
    anyperf::ThreadProfiler thread_prof;
    anyperf::GpuMonitor gpu_mon;
    anyperf::CpuMonitor cpu_mon;
    anyperf::HardwareController hw_ctrl;
    hw_ctrl.init();

    anyperf::TrayManager tray_manager;
    anyperf::EtwTracker etw_tracker;
    anyperf::DashboardView dashboard_view;
    anyperf::RTSSBridge rtss_bridge;

    // Apply Overclock at startup if enabled in active profile
    if (config_mgr.get_oc().enabled && config_mgr.get_oc().apply_at_startup) {
        hw_ctrl.apply_overclock(config_mgr.get_oc());
    }

    // Apply RTSS framerate limit if configured
    if (config_mgr.get_framerate_limit() > 0) {
        anyperf::RTSSBridge::set_framerate_limit(config_mgr.get_framerate_limit());
    }

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

    // Initialize ETW Kernel Tracker once (monitors Present events in background)
    etw_tracker.start(0);

    DWORD active_etw_pid = 0;
    std::mutex telemetry_mutex;
    std::atomic<bool> telemetry_running{true};
    anyperf::FrameStats display_frame_stats{};

    // Emergency crash/exit fallback to restore factory fan curve
    static anyperf::HardwareController* s_hw_ctrl = &hw_ctrl;
    std::atexit([]() {
        if (s_hw_ctrl) {
            s_hw_ctrl->restore_auto_fans();
        }
    });

    // Independent Hardware Telemetry & Fan Control Worker Thread
    // Ensures telemetry, RTSS OSD and fan curves tick strictly regardless of window dragging or modal loops
    std::thread hardware_worker([&]() {
        auto last_telemetry_time = std::chrono::steady_clock::now();
        auto last_gpu_time = std::chrono::steady_clock::now();
        auto last_cpu_time = std::chrono::steady_clock::now();
        auto last_fan_time = std::chrono::steady_clock::now();
        auto last_tray_time = std::chrono::steady_clock::now();
        auto last_thread_time = std::chrono::steady_clock::now();
        auto last_rtss_time = std::chrono::steady_clock::now();

        while (telemetry_running.load()) {
            auto now = std::chrono::steady_clock::now();

            // 1. Manage ETW target PID
            DWORD primary_pid = 0;
            {
                std::lock_guard<std::mutex> lock(telemetry_mutex);
                const auto* primary = proc_mon.get_primary_target();
                primary_pid = (primary && primary->is_alive) ? primary->pid : 0;
            }
            if (primary_pid != active_etw_pid) {
                active_etw_pid = primary_pid;
                etw_tracker.set_target_pid(active_etw_pid);
            }

            // 2. Sample GPU Telemetry (every 250ms)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gpu_time).count() >= 250) {
                {
                    std::lock_guard<std::mutex> lock(telemetry_mutex);
                    gpu_mon.update();
                }
                last_gpu_time = now;
            }

            // 3. Sample CPU Telemetry & Per-Core Load (every 250ms)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cpu_time).count() >= 250) {
                {
                    std::lock_guard<std::mutex> lock(telemetry_mutex);
                    cpu_mon.update();
                }
                last_cpu_time = now;
            }

            // 4. Sample multi-process telemetry (every 150ms)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_telemetry_time).count() >= 150) {
                {
                    std::lock_guard<std::mutex> lock(telemetry_mutex);
                    proc_mon.update();
                }
                last_telemetry_time = now;
            }

            // 5. Sample thread breakdown for the active primary process (every 500ms)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_thread_time).count() >= 500) {
                if (primary_pid != 0) {
                    std::lock_guard<std::mutex> lock(telemetry_mutex);
                    thread_prof.update(primary_pid);
                }
                last_thread_time = now;
            }

            // 6. Calculate frame stats (Kernel ETW Present -> RTSS Shared Memory -> Idle)
            anyperf::FrameStats cur_stats{};
            anyperf::FrameStats rtss_stats{};
            if (etw_tracker.is_active() && etw_tracker.get_frame_stats().total_frames > 0) {
                cur_stats = etw_tracker.get_frame_stats();
            } else if (rtss_bridge.get_rtss_frame_stats(rtss_stats)) {
                cur_stats = rtss_stats;
            } else {
                cur_stats.current_fps = 0.0;
                cur_stats.frametime_ms = 0.0;
            }
            {
                std::lock_guard<std::mutex> lock(telemetry_mutex);
                display_frame_stats = cur_stats;
            }

            // 7. Update RTSS Shared Memory V2 On-Screen Display (every 100ms)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rtss_time).count() >= 100) {
                std::lock_guard<std::mutex> lock(telemetry_mutex);
                rtss_bridge.update_osd(
                    proc_mon,
                    gpu_mon.get_metrics(),
                    cpu_mon.get_metrics(),
                    cur_stats,
                    config_mgr,
                    dashboard_view.is_benchmarking(),
                    dashboard_view.get_benchmark_elapsed_sec()
                );
                dashboard_view.push_metrics_sample(
                    proc_mon.get_monitored_targets(),
                    cur_stats,
                    gpu_mon.get_metrics()
                );
                last_rtss_time = now;
            }

            // 8. Update System Tray Icon & Tooltip (every 500ms)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tray_time).count() >= 500) {
                const auto& hud_cfg = config_mgr.get_hud();
                int temp = 0;
                {
                    std::lock_guard<std::mutex> lock(telemetry_mutex);
                    temp = hud_cfg.tray_temp_is_gpu ? gpu_mon.get_metrics().temperature_c : cpu_mon.get_metrics().package_temp_c;
                }
                std::wstring tooltip = L"AnyPerfomans\n" + 
                    (hud_cfg.tray_temp_is_gpu 
                        ? L"GPU Temp: " + (temp > 0 ? std::to_wstring(temp) + L"°C" : L"N/A")
                        : L"CPU Temp: " + (temp > 0 ? std::to_wstring(temp) + L"°C" : L"N/A"));
                tray_manager.update_icon(temp, hud_cfg.tray_temp_is_gpu, hud_cfg.show_tray_temperature, tooltip);
                last_tray_time = now;
            }

            // 9. Evaluate Hardware Fan Curve (every 500ms)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fan_time).count() >= 500) {
                int gpu_temp = 0;
                {
                    std::lock_guard<std::mutex> lock(telemetry_mutex);
                    gpu_temp = gpu_mon.get_metrics().temperature_c;
                }
                hw_ctrl.update_fan_curve(config_mgr.get_fan(), gpu_temp);
                last_fan_time = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    });

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

        // Configurable Hotkey: Toggle Benchmark Session (Default: F10)
        UINT bench_key = config_mgr.get_hotkeys().toggle_benchmark_key;
        if (bench_key != 0 && (GetAsyncKeyState(bench_key) & 1)) {
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            dashboard_view.toggle_benchmark(proc_mon, gpu_mon.get_metrics(), display_frame_stats);
        }

        // Render UI frame (Skipped completely when minimized to tray to achieve 0.0% CPU and 0 MB GPU overhead)
        auto current_mode = backend.get_overlay_mode();
        if (current_mode == anyperf::OverlayMode::Hidden) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } else {
            backend.begin_frame();

            {
                std::lock_guard<std::mutex> lock(telemetry_mutex);
                dashboard_view.render(
                    proc_mon,
                    thread_prof,
                    etw_tracker,
                    gpu_mon,
                    cpu_mon,
                    hw_ctrl,
                    config_mgr,
                    rtss_bridge,
                    backend,
                    display_frame_stats,
                    current_mode
                );
            }

            if (current_mode != backend.get_overlay_mode()) {
                backend.set_overlay_mode(current_mode);
            }

            backend.end_frame();
        }
    }

    // Stop worker thread before shutdown
    telemetry_running = false;
    if (hardware_worker.joinable()) {
        hardware_worker.join();
    }

    // Graceful cleanup on shutdown
    hw_ctrl.shutdown();
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

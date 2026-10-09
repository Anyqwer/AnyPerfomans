#include "anyperf/dx11_backend.hpp"
#include "anyperf/process_monitor.hpp"
#include "anyperf/thread_profiler.hpp"
#include "anyperf/etw_tracker.hpp"
#include "anyperf/gpu_monitor.hpp"
#include "anyperf/config_manager.hpp"
#include "anyperf/dashboard_view.hpp"
#include <chrono>
#include <thread>
#include <deque>
#include <numeric>
#include <algorithm>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    anyperf::ConfigManager config_mgr;
    anyperf::Dx11Backend backend;
    if (!backend.init(1320, 840, L"AnyPerfomans - ROG Liquid Glass Edition")) {
        return 1;
    }

    anyperf::ProcessMonitor proc_mon;
    anyperf::ThreadProfiler thread_prof;
    anyperf::GpuMonitor gpu_mon;
    anyperf::EtwTracker etw_tracker;
    anyperf::DashboardView dashboard_view;

    DWORD active_etw_pid = 0;

    auto last_telemetry_time = std::chrono::steady_clock::now();
    auto last_gpu_time = std::chrono::steady_clock::now();
    auto last_thread_time = std::chrono::steady_clock::now();
    auto last_frame_time = std::chrono::steady_clock::now();

    anyperf::FrameStats local_frame_stats{};
    std::deque<double> recent_frametimes; // Rolling window of last 200 frames

    bool running = true;
    while (running) {
        if (!backend.process_messages()) {
            running = false;
            break;
        }

        // Configurable Hotkey: Toggle Overlay (Default: F11)
        UINT toggle_key = config_mgr.get_hotkeys().toggle_overlay_key;
        if (toggle_key != 0 && (GetAsyncKeyState(toggle_key) & 1)) {
            auto current_mode = backend.get_overlay_mode();
            auto new_mode = (current_mode == anyperf::OverlayMode::Dashboard)
                ? anyperf::OverlayMode::MiniHud
                : anyperf::OverlayMode::Dashboard;
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

        // 3. Calculate frame stats (Prefer Kernel ETW Present if active, otherwise local fallback)
        anyperf::FrameStats display_frame_stats{};
        if (etw_tracker.is_active() && etw_tracker.get_frame_stats().total_frames > 0) {
            display_frame_stats = etw_tracker.get_frame_stats();
        } else {
            double delta_ms = std::chrono::duration<double, std::milli>(now - last_frame_time).count();
            if (delta_ms > 0.1 && delta_ms < 1000.0) {
                local_frame_stats.frametime_ms = delta_ms;
                local_frame_stats.current_fps = 1000.0 / delta_ms;
                local_frame_stats.total_frames++;

                recent_frametimes.push_back(delta_ms);
                if (recent_frametimes.size() > 200) {
                    recent_frametimes.pop_front();
                }

                double sum = std::accumulate(recent_frametimes.begin(), recent_frametimes.end(), 0.0);
                local_frame_stats.avg_fps = 1000.0 / (sum / recent_frametimes.size());

                std::vector<double> sorted_ft(recent_frametimes.begin(), recent_frametimes.end());
                std::sort(sorted_ft.begin(), sorted_ft.end());
                size_t idx_1pct = static_cast<size_t>(sorted_ft.size() * 0.99);
                if (idx_1pct < sorted_ft.size()) {
                    local_frame_stats.one_percent_low = 1000.0 / sorted_ft[idx_1pct];
                }
            }
            display_frame_stats = local_frame_stats;
        }
        last_frame_time = now;

        // 4. Sample high-precision multi-process telemetry (every 150ms)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_telemetry_time).count() >= 150) {
            proc_mon.update();
            dashboard_view.push_metrics_sample(
                proc_mon.get_monitored_targets(),
                display_frame_stats,
                gpu_mon.get_metrics()
            );
            last_telemetry_time = now;
        }

        // 5. Sample thread breakdown for the active primary process (every 500ms to keep overhead < 0.1%)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_thread_time).count() >= 500) {
            if (primary_pid != 0) {
                thread_prof.update(primary_pid);
            }
            last_thread_time = now;
        }

        // 6. Render UI frame
        backend.begin_frame();

        auto current_mode = backend.get_overlay_mode();
        dashboard_view.render(
            proc_mon,
            thread_prof,
            etw_tracker,
            gpu_mon,
            config_mgr,
            backend,
            display_frame_stats,
            current_mode
        );

        if (current_mode != backend.get_overlay_mode()) {
            backend.set_overlay_mode(current_mode);
        }

        backend.end_frame();
    }

    etw_tracker.stop();
    backend.shutdown();
    return 0;
}

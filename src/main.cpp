#include "anyperf/dx11_backend.hpp"
#include "anyperf/process_monitor.hpp"
#include "anyperf/thread_profiler.hpp"
#include "anyperf/dashboard_view.hpp"
#include <chrono>
#include <thread>
#include <deque>
#include <numeric>
#include <algorithm>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    anyperf::Dx11Backend backend;
    if (!backend.init(1320, 840, L"AnyPerfomans - Precision Game & Process Profiler")) {
        return 1;
    }

    anyperf::ProcessMonitor proc_mon;
    anyperf::ThreadProfiler thread_prof;
    anyperf::DashboardView dashboard_view;

    // Try detecting CS2 automatically at startup
    proc_mon.auto_detect_game();

    auto last_telemetry_time = std::chrono::steady_clock::now();
    auto last_thread_time = std::chrono::steady_clock::now();
    auto last_frame_time = std::chrono::steady_clock::now();

    anyperf::FrameStats frame_stats{};
    std::deque<double> recent_frametimes; // rolling window of last 200 frames

    bool running = true;
    while (running) {
        if (!backend.process_messages()) {
            running = false;
            break;
        }

        // Toggle HUD on F11 press
        if (GetAsyncKeyState(VK_F11) & 1) {
            auto current_mode = backend.get_overlay_mode();
            auto new_mode = (current_mode == anyperf::OverlayMode::Dashboard)
                ? anyperf::OverlayMode::MiniHud
                : anyperf::OverlayMode::Dashboard;
            backend.set_overlay_mode(new_mode);
        }

        auto now = std::chrono::steady_clock::now();

        // 1. Calculate rendering & frame timing stats
        double delta_ms = std::chrono::duration<double, std::milli>(now - last_frame_time).count();
        last_frame_time = now;
        if (delta_ms > 0.1 && delta_ms < 1000.0) {
            frame_stats.frametime_ms = delta_ms;
            frame_stats.current_fps = 1000.0 / delta_ms;
            frame_stats.total_frames++;

            recent_frametimes.push_back(delta_ms);
            if (recent_frametimes.size() > 200) {
                recent_frametimes.pop_front();
            }

            // Calculate rolling average and 1% low
            double sum = std::accumulate(recent_frametimes.begin(), recent_frametimes.end(), 0.0);
            frame_stats.avg_fps = 1000.0 / (sum / recent_frametimes.size());

            std::vector<double> sorted_ft(recent_frametimes.begin(), recent_frametimes.end());
            std::sort(sorted_ft.begin(), sorted_ft.end());
            // 99th percentile frametime is 1% low FPS
            size_t idx_1pct = static_cast<size_t>(sorted_ft.size() * 0.99);
            if (idx_1pct < sorted_ft.size()) {
                frame_stats.one_percent_low = 1000.0 / sorted_ft[idx_1pct];
            }
        }

        // 2. Sample high-precision process telemetry (every 150ms)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_telemetry_time).count() >= 150) {
            proc_mon.update();
            dashboard_view.push_metrics_sample(
                proc_mon.get_game_metrics(),
                proc_mon.get_aux_metrics(),
                frame_stats
            );
            last_telemetry_time = now;
        }

        // 3. Sample thread breakdown (every 500ms to keep overhead < 0.1%)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_thread_time).count() >= 500) {
            if (proc_mon.get_game_pid() != 0) {
                thread_prof.update(proc_mon.get_game_pid());
            }
            last_thread_time = now;
        }

        // 4. Render UI
        backend.begin_frame();

        auto current_mode = backend.get_overlay_mode();
        dashboard_view.render(proc_mon, thread_prof, frame_stats, current_mode);

        if (current_mode != backend.get_overlay_mode()) {
            backend.set_overlay_mode(current_mode);
        }

        backend.end_frame();
    }

    backend.shutdown();
    return 0;
}
